#include <MESA/swarmkv.h>
#include <marsio.h>
#include <cjson/cJSON.h>
#include <MESA/MESA_prof_load.h>

#include <assert.h>
#include <sys/time.h>
#include <time.h>

extern "C" {
#include "libavl.h"
}
#include "log.h"
#include "session_table.h"
#include "addr_tuple4.h"
#include "raw_packet.h"
#include "shaper.h"
#include "shaper_stat.h"
#include "utils.h"
#include "shaper_marsio.h"
#include "shaper_session.h"
#include "shaper_swarmkv.h"
#include "shaper_maat.h"
#include "shaper_global_stat.h"

#define NANO_SECONDS_PER_MICRO_SEC 1000
#define MICRO_SECONDS_PER_SEC 1000000
#define NANO_SECONDS_PER_SEC 1000000000

#define NANO_SECONDS_PER_MILLI_SEC 1000000
#define MILLI_SECONDS_PER_SEC 1000

#define SHAPING_LATENCY_THRESHOLD 2000000 //2s

#define TOKEN_ENLARGE_TIMES 10
#define TOKEN_GET_FAILED_INTERVAL_MS 1

#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_1 "HMGET tsg-shaping-%d priority-0"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_2 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_1 " priority-1"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_3 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_2 " priority-2"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_4 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_3 " priority-3"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_5 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_4 " priority-4"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_6 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_5 " priority-5"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_7 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_6 " priority-6"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_8 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_7 " priority-7"
#define SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_9 SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_8 " priority-8"

const char *swarmkv_queue_len_get_cmd[] = {"", SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_1, SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_2, SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_3, 
                                        SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_4, SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_5, SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_6,
                                        SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_7, SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_8, SWARMKV_QUEUE_LEN_GET_CMD_PRIORITY_9};

struct shaper {//trees in one thread
    struct avl_tree *priority_trees[SHAPING_PRIORITY_NUM_MAX];//represent 10 avl tree corresponding to 10 priority
};

struct shaping_node {//a session will have 10 nodes, corresponding 10 avl tree
    struct shaping_flow shaping_flow;
    struct avl_node *avl_node[SHAPING_PRIORITY_NUM_MAX];
};

struct shaping_async_cb_arg {
    struct shaping_thread_ctx *ctx;
    struct shaping_flow *sf;
    struct shaping_profile_info *s_pf_info;
    int priority;
    unsigned char direction;
};

struct shaping_profile_container {
    struct shaping_profile_info *pf_info;
    int pf_type;
};

enum shaper_token_get_result {
    SHAPER_TOKEN_GET_FAILED = -1,
    SHAPER_TOKEN_GET_SUCCESS = 0,
    SHAPER_TOKEN_GET_PASS = 1,//don't need to get token, regard as success
};

struct shaper* shaper_new(unsigned int priority_queue_len_max)
{
    struct shaper *sp = NULL;
    int i;

    sp = (struct shaper*)calloc(1, sizeof(struct shaper));
    if (!sp) {
        goto ERROR;
    }


    for (i = 0; i < SHAPING_PRIORITY_NUM_MAX; i++) {
        sp->priority_trees[i] = avl_tree_init(priority_queue_len_max);
        if (!sp->priority_trees[i]) {
            goto ERROR;
        }
    }

	return sp;

ERROR:
    shaper_free(sp);
    return NULL;
}

void shaper_free(struct shaper *sp)
{
    int i;

    if (sp) {
        for (i = 0; i < SHAPING_PRIORITY_NUM_MAX; i++) {
            if (sp->priority_trees[i]) {
                avl_tree_destroy(sp->priority_trees[i]);
            }
        }
        free(sp);
    }

    return;
}

static void shaping_node_free(struct shaping_node *s_node)
{
    int i;

    if (s_node) {
        for (i = 0; i < SHAPING_PRIORITY_NUM_MAX; i++) {
            if (s_node->avl_node[i]) {
                avl_tree_node_free(s_node->avl_node[i]);
            }
        }

        if (s_node->shaping_flow.src_ip_str) {
            free(s_node->shaping_flow.src_ip_str);
        }

        if (s_node->shaping_flow.ctrl_meta.raw_data) {
            free(s_node->shaping_flow.ctrl_meta.raw_data);
        }

        free(s_node);
    }

    return;
}

struct shaping_flow* shaping_flow_new(struct shaping_thread_ctx *ctx)
{
    struct shaping_node *s_node = NULL;
    int i;

    s_node = (struct shaping_node*)calloc(1, sizeof(struct shaping_node));
    if (!s_node) {
        goto ERROR;
    }

    for (i = 0; i < SHAPING_PRIORITY_NUM_MAX; i++) {
        s_node->avl_node[i] = avl_tree_node_new(0, &s_node->shaping_flow, NULL);
        if (!s_node->avl_node[i]) {
            goto ERROR;
        }
    }

    TAILQ_INIT(&s_node->shaping_flow.packet_queue);
    s_node->shaping_flow.ref_count = 1;
    s_node->shaping_flow.priority = SHAPING_PRIORITY_NUM_MAX - 1;
    timeout_init(&s_node->shaping_flow.timeout_handle, TIMEOUT_ABS);
    timeouts_add(ctx->expires, &s_node->shaping_flow.timeout_handle, time(NULL) + SHAPING_STAT_REFRESH_INTERVAL_SEC);

    return &s_node->shaping_flow;

ERROR:
    shaping_node_free(s_node);
    return NULL;
}

void shaping_flow_free(struct shaping_thread_ctx *ctx, struct shaping_flow *sf)
{
    struct shaping_node *s_node = (struct shaping_node*)sf;

    if (__atomic_sub_fetch(&sf->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
        timeouts_del(ctx->expires, &sf->timeout_handle);
        shaper_stat_refresh(ctx, sf, ctx->thread_index, 1);
        shaping_node_free(s_node);
    }
    
    return;
}

static int shaper_packet_enqueue(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, void *pkt_buff, struct metadata *meta)
{
    struct shaping_packet_wrapper *s_pkt = NULL;
    struct timespec curr_time;

    if (sf->queue_len == ctx->conf.session_queue_len_max) {
        return -1;
    }

    s_pkt = (struct shaping_packet_wrapper*)calloc(1, sizeof(struct shaping_packet_wrapper));
    if (!s_pkt) {
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &curr_time);

    s_pkt->pkt_buff = pkt_buff;
    s_pkt->direction = meta->dir;
    s_pkt->length = meta->raw_len;
    s_pkt->tcp_pure_contorl = meta->is_tcp_pure_ctrl;
    s_pkt->income_time_ns = curr_time.tv_sec * NANO_SECONDS_PER_SEC + curr_time.tv_nsec;
    s_pkt->enqueue_time_us = curr_time.tv_sec * MICRO_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MICRO_SEC;
    TAILQ_INSERT_TAIL(&sf->packet_queue, s_pkt, node);
    sf->queue_len++;

    return 0;
}

bool shaper_queue_empty(struct shaping_flow *sf)
{
    return TAILQ_EMPTY(&sf->packet_queue);
}

struct shaping_packet_wrapper* shaper_first_pkt_get(struct shaping_flow *sf)
{
    return TAILQ_FIRST(&sf->packet_queue);
}

void shaper_packet_dequeue(struct shaping_flow *sf)
{
    struct shaping_packet_wrapper *s_pkt;

    s_pkt = TAILQ_FIRST(&sf->packet_queue);
    if (s_pkt) {
        TAILQ_REMOVE(&sf->packet_queue, s_pkt, node);
        sf->queue_len--;
        free(s_pkt);
    }

    return;
}

void shaper_queue_clear(struct shaping_flow *sf, struct shaping_thread_ctx *ctx)
{
    struct shaping_packet_wrapper *pkt_wrapper;
    struct shaping_rule_info *rule = &sf->matched_rule_infos[0];

    while (!shaper_queue_empty(sf)) {
        pkt_wrapper = shaper_first_pkt_get(sf);

        shaper_stat_queueing_pkt_dec(&rule->primary.stat, pkt_wrapper->direction, ctx->thread_index);
        shaper_stat_drop_inc(&rule->primary.stat, pkt_wrapper->length, ctx->thread_index);
        shaper_global_stat_queueing_dec(ctx->global_stat, pkt_wrapper->length);
        shaper_global_stat_drop_inc(ctx->global_stat, pkt_wrapper->length);
        shaper_global_stat_hit_policy_drop_inc(ctx->global_stat, pkt_wrapper->length);

        marsio_buff_free(ctx->marsio_info->instance, &pkt_wrapper->pkt_buff, 1, 0, ctx->thread_index);
        shaper_packet_dequeue(sf);
    }

    return;
}

//return success(0) while any avl tree insert success
int shaper_flow_push(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, unsigned long long enqueue_time)
{
    struct shaping_node *s_node = (struct shaping_node*)sf;
    struct shaping_rule_info *s_rule_info = &sf->matched_rule_infos[sf->anchor];
    struct shaper *sp = ctx->sp;
    struct shaping_packet_wrapper *pkt_wrapper = NULL;
    int priority;
    int ret = -1;
    int i;

    pkt_wrapper = shaper_first_pkt_get(sf);
    assert(pkt_wrapper != NULL);

    priority = s_rule_info->primary.priority;
    avl_tree_node_key_set(s_node->avl_node[priority], pkt_wrapper->income_time_ns);
    if (0 == avl_tree_node_insert(sp->priority_trees[priority], s_node->avl_node[priority])) {
        ret = 0;
    }

    if (s_rule_info->borrowing_num == 0) {// no borrow profile
        goto END;
    }

    for (i = 0; i < s_rule_info->borrowing_num; i++) {
        priority = s_rule_info->borrowing[i].priority;
        avl_tree_node_key_set(s_node->avl_node[priority], pkt_wrapper->income_time_ns);
        if (0 == avl_tree_node_insert(sp->priority_trees[priority], s_node->avl_node[priority])) {
            ret = 0;
            //TODO: calculate queue_len for borrow profile and add judge when refresh stat????
            //shaper_stat_queueing_pkt_inc(&s_rule_info->borrowing[i].stat, pkt_wrapper->direction, ctx->thread_index);
        }
    }

END:
    if (ret == 0) {//all avl tree success
        s_rule_info->primary.enqueue_time_us = enqueue_time;
    }

    return ret;
}

static unsigned long long shaper_pkt_latency_us_calculate(struct shaping_profile_info *profile, struct timespec *time)
{
    unsigned long long enqueue_time = profile->enqueue_time_us;
    unsigned long long curr_time = time->tv_sec * MICRO_SECONDS_PER_SEC + time->tv_nsec / NANO_SECONDS_PER_MICRO_SEC;

    assert(curr_time >= enqueue_time);
    return (curr_time - enqueue_time);
}

void shaper_flow_pop(struct shaping_thread_ctx *ctx, struct shaping_flow *sf)
{
    struct shaping_node *s_node = (struct shaping_node*)sf;
    struct shaping_rule_info *s_rule_info = &sf->matched_rule_infos[sf->anchor];
    struct shaper *sp = ctx->sp;
    struct shaping_packet_wrapper *pkt_wrapper = NULL;
    struct timespec curr_time;
    unsigned long long latency;
    int priority;
    int i;

    pkt_wrapper = shaper_first_pkt_get(sf);
    assert(pkt_wrapper != NULL);

    clock_gettime(CLOCK_MONOTONIC, &curr_time);

    priority = s_rule_info->primary.priority;
    if (avl_node_in_tree(s_node->avl_node[priority])) {
        avl_tree_node_remove(sp->priority_trees[priority], s_node->avl_node[priority]);
    }
    
    if (s_rule_info->borrowing_num == 0) {
        goto END;
    }

    for (i = 0; i < s_rule_info->borrowing_num; i++) {
        priority = s_rule_info->borrowing[i].priority;
        if (avl_node_in_tree(s_node->avl_node[priority])) {
            avl_tree_node_remove(sp->priority_trees[priority], s_node->avl_node[priority]);
            //TODO: calculate queue_len for borrow profile and add judge when refresh stat????
        }
    }

END:
    latency = shaper_pkt_latency_us_calculate(&s_rule_info->primary, &curr_time);
    shaper_stat_max_latency_update(&s_rule_info->primary.stat, pkt_wrapper->direction, latency, ctx->thread_index);
    shaper_stat_queueing_pkt_dec(&s_rule_info->primary.stat, pkt_wrapper->direction, ctx->thread_index);

    return;
}

int shaper_flow_in_order_get(struct shaper *sp, struct shaper_flow_instance sf_ins[], int priority, int max_sf_num)
{
    struct avl_node *avl_node = NULL;
    int count = 0;

    if (max_sf_num <= 0) {
        return 0;
    }

    avl_node = avl_tree_minimum_node_get(sp->priority_trees[priority]);
    while(avl_node) {
        sf_ins[count].sf = (struct shaping_flow*)avl_tree_node_data_get(avl_node);
        sf_ins[count].priority = priority;
        count++;
        if (count == max_sf_num) {
            return count;
        }

        avl_node = avl_tree_next_in_order_node_get(avl_node);
    }


    return count;
}

static void shaper_deposit_token_add(struct shaping_profile_info *pf_info, int req_token, unsigned char direction)
{
    if (direction == SHAPING_DIR_IN) {
        __atomic_add_fetch(&pf_info->in_deposit_token, req_token, __ATOMIC_SEQ_CST);
    } else {
        __atomic_add_fetch(&pf_info->out_deposit_token, req_token, __ATOMIC_SEQ_CST);
    }
}

static void shaper_token_get_cb(const struct swarmkv_reply *reply, void * cb_arg)
{
    struct shaping_async_cb_arg *arg = (struct shaping_async_cb_arg*)cb_arg;
    struct shaping_profile_info *s_pf_info = arg->s_pf_info;
    struct shaping_flow *sf = arg->sf;

    shaper_global_stat_async_callback_inc(arg->ctx->global_stat);

    LOG_INFO("Swarmkv reply type =%d, integer =%llu",reply->type, reply->integer);

    if (reply->type != SWARMKV_REPLY_INTEGER) {
        shaper_global_stat_async_tconsume_failed_inc(arg->ctx->global_stat);
        goto END;
    }

    if (reply->integer < 0) {//profile not exist
        s_pf_info->is_invalid = 1;
        goto END;
    } else {
        s_pf_info->is_invalid = 0;
    }

    if (reply->integer == 0) {//no token
        struct timespec curr_time;
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        s_pf_info->last_failed_get_token_ms = curr_time.tv_sec * MILLI_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MILLI_SEC;
        goto END;
    }

    shaper_deposit_token_add(s_pf_info, reply->integer, arg->direction);//deposit tokens to profile

END:
    shaping_flow_free(arg->ctx, sf);//sub ref count and decide if need to free
    free(cb_arg);
    cb_arg = NULL;

    return;
}

static void shaper_deposit_token_sub(struct shaping_profile_info *pf_info, int req_token, unsigned char direction)
{
    if (direction == SHAPING_DIR_IN) {
        __atomic_sub_fetch(&pf_info->in_deposit_token, req_token, __ATOMIC_SEQ_CST);
    } else {
        __atomic_sub_fetch(&pf_info->out_deposit_token, req_token, __ATOMIC_SEQ_CST);
    }
}

static int shaper_deposit_token_is_enough(struct shaping_profile_info *pf_info, int req_token, unsigned char direction)
{
    if (direction == SHAPING_DIR_IN) {
        if (__atomic_load_n(&pf_info->in_deposit_token, __ATOMIC_SEQ_CST) >= req_token) {
            return 1;
        } else {
            return 0;
        }
    } else {
        if (__atomic_load_n(&pf_info->out_deposit_token, __ATOMIC_SEQ_CST) >= req_token) {
            return 1;
        } else {
            return 0;
        }
    }
}

static int shaper_token_get_from_profile(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, struct shaping_profile_info *pf_info, int profile_type, int req_token_bits, unsigned char direction)
{
    struct shaping_async_cb_arg *arg = NULL;
    char key[32] = {0};

    __atomic_add_fetch(&sf->ref_count, 1, __ATOMIC_SEQ_CST);

    snprintf(key, sizeof(key), "tsg-shaping-%d-%s", pf_info->id, direction == SHAPING_DIR_OUT ? "outgoing" : "incoming");
    arg = (struct shaping_async_cb_arg *)calloc(1, sizeof(struct shaping_async_cb_arg));
    arg->ctx = ctx;
    arg->s_pf_info = pf_info;
    arg->sf = sf;
    arg->direction = direction;

    shaper_global_stat_async_invoke_inc(ctx->global_stat);
    switch (pf_info->type) {
        case PROFILE_TYPE_GENERIC:
            swarmkv_tconsume(ctx->swarmkv_db, key, strlen(key), req_token_bits * TOKEN_ENLARGE_TIMES, shaper_token_get_cb, arg);
            break;
        case PROFILE_TYPE_HOST_FARINESS:
        case PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS:
            swarmkv_ftconsume(ctx->swarmkv_db, key, strlen(key), sf->src_ip_str, sf->src_ip_str_len, sf->matched_rule_infos[sf->anchor].fair_factor, req_token_bits * TOKEN_ENLARGE_TIMES, shaper_token_get_cb, arg);
            break;
        case PROFILE_TYPE_SPLIT_BY_LOCAL_HOST:
            swarmkv_btconsume(ctx->swarmkv_db, key, strlen(key), sf->src_ip_str, sf->src_ip_str_len, req_token_bits * TOKEN_ENLARGE_TIMES, shaper_token_get_cb, arg);
            break;
        default:
            if (arg) {
                free(arg);
            }
            break;
    }

    if (pf_info->is_invalid) {
        if (profile_type == PROFILE_IN_RULE_TYPE_PRIMARY) {//for primary, means this rule don't need get token
            return SHAPER_TOKEN_GET_SUCCESS;
        } else {//for borrowing, means this profile has no token to borrow
            return SHAPER_TOKEN_GET_FAILED;
        }
    }

    if (shaper_deposit_token_is_enough(pf_info, req_token_bits, direction)) {
        shaper_deposit_token_sub(pf_info, req_token_bits, direction);
        return SHAPER_TOKEN_GET_SUCCESS;
    }

    return SHAPER_TOKEN_GET_FAILED;
}

static void shaper_queue_len_get_cb(const struct swarmkv_reply *reply, void * cb_arg)
{
    struct shaping_async_cb_arg *arg = (struct shaping_async_cb_arg *)cb_arg;
    struct shaping_profile_info *s_pf_info = arg->s_pf_info;
    struct shaping_flow *sf = arg->sf;

    shaper_global_stat_async_callback_inc(arg->ctx->global_stat);

    s_pf_info->is_priority_blocked = 0;

    if (!reply || (reply->type != SWARMKV_REPLY_NIL && reply->type != SWARMKV_REPLY_ARRAY)) {
        shaper_global_stat_async_hmget_failed_inc(arg->ctx->global_stat);
        goto END;
    }

    if (reply->type != SWARMKV_REPLY_ARRAY) {
        goto END;
    }

    for (unsigned int i = 0; i < reply->n_element; i++) {
        if (reply->elements[i]->type == SWARMKV_REPLY_STRING) {
            char tmp_str[32] = {0};
            memcpy(tmp_str, reply->elements[i]->str, reply->elements[i]->len);
            if (strtoll(tmp_str, NULL, 10) > 0) {
                s_pf_info->is_priority_blocked = 1;
                break;
            }
        }
    }

END:
    shaping_flow_free(arg->ctx, sf);//sub ref count and decide if need to free
    free(cb_arg);
    cb_arg = NULL;
}

static int shaper_profile_is_priority_blocked(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, struct shaping_profile_info *profile)
{
    struct shaping_async_cb_arg *arg;
    int priority = profile->priority;

    if (priority == 0) {//highest priority, can't be blocked
        return 0;
    }

    arg = (struct shaping_async_cb_arg *)calloc(1, sizeof(struct shaping_async_cb_arg));
    arg->ctx = ctx;
    arg->s_pf_info = profile;
    arg->sf = sf;
    arg->priority = priority;

    __atomic_add_fetch(&sf->ref_count, 1, __ATOMIC_SEQ_CST);

    shaper_global_stat_async_invoke_inc(ctx->global_stat);
    swarmkv_async_command(ctx->swarmkv_db, shaper_queue_len_get_cb, arg, swarmkv_queue_len_get_cmd[priority], profile->id);

    if (profile->is_priority_blocked) {
        return 1;
    } else {
        return 0;
    }
}

static int shaper_token_consume(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int req_token_bytes,
                        struct shaping_profile_info *profile, int profile_type, unsigned char direction)
{
    struct shaping_rule_info *rule = &sf->matched_rule_infos[sf->anchor];
    time_t curr_time = time(NULL);
    
    if (curr_time - sf->check_rule_time >= ctx->conf.check_rule_enable_interval_sec) {
        sf->check_rule_time = curr_time;
        if (shaper_rule_is_enabled(ctx, rule->id) != 1) {
            rule->is_enabled = 0;
            return SHAPER_TOKEN_GET_PASS;//rule is disabled, don't need to get token and forward packet
        } else {
            rule->is_enabled = 1;
        }
    }

    if (rule->is_enabled != 1) {
        return SHAPER_TOKEN_GET_PASS;//rule is disabled, don't need to get token and forward packet
    }

    if (shaper_deposit_token_is_enough(profile, req_token_bytes * 8, direction)) {
        shaper_deposit_token_sub(profile, req_token_bytes * 8, direction);
        return SHAPER_TOKEN_GET_SUCCESS;
    }

    struct timespec curr_timespec;
    clock_gettime(CLOCK_MONOTONIC, &curr_timespec);
    unsigned long long curr_time_ms = curr_timespec.tv_sec * MILLI_SECONDS_PER_SEC + curr_timespec.tv_nsec / NANO_SECONDS_PER_MILLI_SEC;
    if (curr_time_ms - profile->last_failed_get_token_ms < TOKEN_GET_FAILED_INTERVAL_MS) {//if failed to get token in last 1ms, return failed; for swarmkv can't reproduce token in 1ms
        return SHAPER_TOKEN_GET_FAILED;
    }

    if (shaper_profile_is_priority_blocked(ctx, sf, profile)) {
        return SHAPER_TOKEN_GET_FAILED;
    } else {
        int req_token_bits = req_token_bytes * 8;
        return shaper_token_get_from_profile(ctx, sf, profile, profile_type, req_token_bits, direction);
    }
}

int shaper_profile_get(struct shaping_rule_info *s_rule_info, int priority, struct shaping_profile_container pf_container[])
{
    int num = 0;

    if (priority == SHAPING_PRIORITY_NUM_MAX - 1) {//priority 9 allow multi profiles for one priority
        if (s_rule_info->primary.priority == priority) {
            pf_container[num].pf_type = PROFILE_IN_RULE_TYPE_PRIMARY;
            pf_container[num].pf_info = &s_rule_info->primary;
            num++;
        }

        for (int i = 0; i < s_rule_info->borrowing_num; i++) {
            if (s_rule_info->borrowing[i].priority == priority) {
                pf_container[num].pf_type = PROFILE_IN_RULE_TYPE_BORROW;
                pf_container[num].pf_info = &s_rule_info->borrowing[i];
                num++;
            }
        }

        return num;
    } else {
        if (s_rule_info->primary.priority == priority) {
            pf_container[0].pf_type = PROFILE_IN_RULE_TYPE_PRIMARY;
            pf_container[0].pf_info = &s_rule_info->primary;
            return 1;
        }

        for (int i = 0; i < s_rule_info->borrowing_num; i++) {
            if (s_rule_info->borrowing[i].priority == priority) {
                pf_container[0].pf_type = PROFILE_IN_RULE_TYPE_BORROW;
                pf_container[0].pf_info = &s_rule_info->borrowing[i];
                return 1;
            }
        }
    }

    return num;
}

static int shaper_next_anchor_get(struct shaping_flow *sf, unsigned char direction)
{
    int anchor = sf->anchor + 1;

    if (anchor > sf->rule_num - 1) {
        return 0;
    }

    return anchor;
}

static enum shaping_packet_action shaper_pkt_action_decide_queueing(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int priority)
{
    struct shaping_rule_info *rule = NULL;
    struct shaping_profile_info *profile = NULL;
    int profile_type;
    struct shaping_packet_wrapper *pkt_wrapper = NULL;
    struct shaping_profile_container pf_container[SHAPING_PRIORITY_NUM_MAX];
    struct timespec curr_time;
    unsigned long long enqueue_time;
    int get_token_success = 0;
    int profile_num;

    rule = &sf->matched_rule_infos[sf->anchor];
    profile_num = shaper_profile_get(rule, priority, pf_container);
    assert(profile_num > 0);
    pkt_wrapper = shaper_first_pkt_get(sf);
    assert(pkt_wrapper != NULL);

    if (pkt_wrapper->tcp_pure_contorl) {
        shaper_flow_pop(ctx, sf);
        shaper_stat_forward_all_rule_inc(ctx->stat, sf, pkt_wrapper->direction, pkt_wrapper->length, ctx->thread_index);
        return SHAPING_FORWARD;
    }

    if (pf_container[0].pf_type == PROFILE_IN_RULE_TYPE_PRIMARY) {
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        if (shaper_pkt_latency_us_calculate(pf_container[0].pf_info, &curr_time) > SHAPING_LATENCY_THRESHOLD) {
            shaper_flow_pop(ctx, sf);
            goto DROP;
        }
    }
    /*todo: AQM, just for primary profile*/

    for (int i = 0; i < profile_num; i++) {
        profile = pf_container[i].pf_info;
        profile_type = pf_container[i].pf_type;
        int ret = shaper_token_consume(ctx, sf, pkt_wrapper->length, profile, profile_type, pkt_wrapper->direction);
        if (ret >= SHAPER_TOKEN_GET_SUCCESS) {
            if (ret == SHAPER_TOKEN_GET_SUCCESS) {
                shaper_stat_forward_inc(&profile->stat, pkt_wrapper->direction, pkt_wrapper->length, ctx->thread_index);
            }
            get_token_success = 1;
            break;
        }
    }

    if (!get_token_success) {
        return SHAPING_QUEUED;
    }

    shaper_flow_pop(ctx, sf);
    sf->anchor = shaper_next_anchor_get(sf, pkt_wrapper->direction);
    if (sf->anchor == 0) {//no next rule
        return SHAPING_FORWARD;
    }

    //push sf for next rule
    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    enqueue_time = curr_time.tv_sec * MICRO_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MICRO_SEC;
    if (0 == shaper_flow_push(ctx, sf, enqueue_time)) {
        shaper_stat_queueing_pkt_inc(&sf->matched_rule_infos[sf->anchor].primary.stat, pkt_wrapper->direction, ctx->thread_index);
        return SHAPING_QUEUED;
    } else {
        goto DROP;
    }

DROP:
    rule = &sf->matched_rule_infos[sf->anchor];
    shaper_stat_drop_inc(&rule->primary.stat, pkt_wrapper->direction, ctx->thread_index);
    sf->anchor = 0;
    return SHAPING_DROP;
}

static enum shaping_packet_action shaper_pkt_action_decide_no_queue(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, struct metadata *meta, struct shaping_profile_info *profile, marsio_buff_t *rx_buff)
{
    int profile_type = PROFILE_IN_RULE_TYPE_PRIMARY;
    struct timespec curr_time;
    unsigned long long enqueue_time;

    if (meta->is_tcp_pure_ctrl) {
        shaper_stat_forward_all_rule_inc(ctx->stat, sf, meta->dir, meta->raw_len, ctx->thread_index);
        return SHAPING_FORWARD;
    }

    int ret = shaper_token_consume(ctx, sf, meta->raw_len, profile, profile_type, meta->dir);
    if (ret >= SHAPER_TOKEN_GET_SUCCESS) {
        if (ret == SHAPER_TOKEN_GET_SUCCESS) {
            shaper_stat_forward_inc(&profile->stat, meta->dir, meta->raw_len, ctx->thread_index);
        }

        sf->anchor = shaper_next_anchor_get(sf, meta->dir);
        if (sf->anchor == 0) {//no next rule
            return SHAPING_FORWARD;
        } else {
            goto FLOW_PUSH;
        }
    }

FLOW_PUSH:
    if (shaper_packet_enqueue(ctx, sf, rx_buff, meta) != 0) {
        char *addr_str = addr_tuple4_to_str(&sf->tuple4);
        LOG_ERROR("%s: shaping enqueue packet failed while queue empty for session: %s", LOG_TAG_SHAPING, addr_str);
        if (addr_str) {
            free(addr_str);
        }
        goto DROP;
    }

    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    enqueue_time = curr_time.tv_sec * MICRO_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MICRO_SEC;
    if (0 == shaper_flow_push(ctx, sf, enqueue_time)) {
        shaper_stat_queueing_pkt_inc(&sf->matched_rule_infos[sf->anchor].primary.stat, meta->dir, ctx->thread_index);
        return SHAPING_QUEUED;
    } else {
        goto DROP;
    }

DROP:
    shaper_stat_drop_inc(&sf->matched_rule_infos[sf->anchor].primary.stat, meta->dir, ctx->thread_index);
    sf->anchor = 0;
    return SHAPING_DROP;
}

static int shaper_polling_first_pkt_token_get(struct shaper *sp, struct shaping_flow *sf, int priority, 
                                        struct shaping_stat *stat, struct shaping_thread_ctx *ctx)
{
    struct shaping_packet_wrapper *pkt_wrapper;
    int old_anchor = sf->anchor;
    int shaping_ret;

    pkt_wrapper = shaper_first_pkt_get(sf);
    assert(pkt_wrapper != NULL);

    shaping_ret = shaper_pkt_action_decide_queueing(ctx, sf, priority);

    switch (shaping_ret) {
        case SHAPING_QUEUED:
            if (old_anchor == sf->anchor) {//didn't get token
                return -1;
            } else {//got token for one rule and waiting get token for next rule
                return 0;
            }
            break;
        case SHAPING_DROP:
            shaper_global_stat_queueing_dec(ctx->global_stat, pkt_wrapper->length);
            shaper_global_stat_drop_inc(ctx->global_stat, pkt_wrapper->length);
            shaper_global_stat_hit_policy_drop_inc(ctx->global_stat, pkt_wrapper->length);

            marsio_buff_free(ctx->marsio_info->instance, &pkt_wrapper->pkt_buff, 1, 0, ctx->thread_index);
            shaper_packet_dequeue(sf);
            break;
        case SHAPING_FORWARD:
            shaper_global_stat_queueing_dec(ctx->global_stat, pkt_wrapper->length);
            shaper_global_stat_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_TX, pkt_wrapper->length);
            shaper_global_stat_hit_policy_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_TX, pkt_wrapper->length);

            marsio_send_burst(ctx->marsio_info->mr_path, ctx->thread_index, &pkt_wrapper->pkt_buff, 1);
            shaper_packet_dequeue(sf);
            break;
        default:
            assert(0);//impossible path
            break;
    }

    shaper_stat_refresh(ctx, sf, ctx->thread_index, 0);

    if (shaper_queue_empty(sf)) {
        if (sf->flag & SESSION_CLOSE) {
            sf->flag &= (~SESSION_CLOSE);
            shaping_flow_free(ctx, sf);
        }
        return 0;
    } else {
        pkt_wrapper = shaper_first_pkt_get(sf);
        sf->anchor = 0;

        if (0 == shaper_flow_push(ctx, sf, pkt_wrapper->enqueue_time_us)) {
            /*in pkt process, when queue not empty, new pkt's queueing stat was added to primary profile of first rule. 
            so don't need add queueing stat here*/
        } else {
            shaper_queue_clear(sf, ctx);//first packet fail, then every packet will fail
            if (sf->flag & SESSION_CLOSE) {
                sf->flag &= (~SESSION_CLOSE);
                shaping_flow_free(ctx, sf);
            }
        }
        return 0;
    }
}

void shaping_packet_process(struct shaping_thread_ctx *ctx, marsio_buff_t *rx_buff, struct metadata *meta, struct shaping_flow *sf)
{
    int shaping_ret;
    struct shaping_rule_info *s_rule;
    struct shaping_stat *stat = ctx->stat;
    struct shaping_marsio_info *marsio_info = ctx->marsio_info;

    sf->processed_pkts++;
    
    if (meta->is_tcp_pure_ctrl) {
        marsio_send_burst(marsio_info->mr_path, ctx->thread_index, &rx_buff, 1);
        shaper_global_stat_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_TX, meta->raw_len);
        shaper_global_stat_hit_policy_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_TX, meta->raw_len);
        shaper_stat_forward_all_rule_inc(stat, sf, meta->dir, meta->raw_len, ctx->thread_index);
        goto END;//for tcp pure control pkt, transmit it directly
    }

    if (!shaper_queue_empty(sf)) {//already have queueing pkt, enqueue directly
        s_rule = &sf->matched_rule_infos[0];
        if (0 == shaper_packet_enqueue(ctx, sf, rx_buff, meta)) {
            shaper_stat_queueing_pkt_inc(&s_rule->primary.stat, meta->dir, ctx->thread_index);
            shaper_global_stat_queueing_inc(ctx->global_stat, meta->raw_len);
        } else {
            shaper_stat_drop_inc(&s_rule->primary.stat, meta->dir, ctx->thread_index);
            shaper_global_stat_drop_inc(ctx->global_stat, meta->raw_len);
            shaper_global_stat_hit_policy_drop_inc(ctx->global_stat, meta->raw_len);
            marsio_buff_free(marsio_info->instance, &rx_buff, 1, 0, ctx->thread_index);
        }
        
    } else {//no queueing pkt, decide action
        sf->anchor = 0;

        shaping_ret = shaper_pkt_action_decide_no_queue(ctx, sf, meta, &sf->matched_rule_infos[sf->anchor].primary, rx_buff);
        switch (shaping_ret) {
            case SHAPING_QUEUED:
                shaper_global_stat_queueing_inc(ctx->global_stat, meta->raw_len);
                break;
            case SHAPING_DROP:
                marsio_buff_free(marsio_info->instance, &rx_buff, 1, 0, ctx->thread_index);
                shaper_global_stat_drop_inc(ctx->global_stat, meta->raw_len);
                shaper_global_stat_hit_policy_drop_inc(ctx->global_stat, meta->raw_len);
                break;
            case SHAPING_FORWARD:
                marsio_send_burst(marsio_info->mr_path, ctx->thread_index, &rx_buff, 1);
                shaper_global_stat_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_TX, meta->raw_len);
                shaper_global_stat_hit_policy_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_TX, meta->raw_len);
                break;
            default:
                assert(0);
                break;
        }
    }

END:
    shaper_stat_refresh(ctx, sf, ctx->thread_index, 0);
    time_t curr_time = time(NULL);
    if (curr_time > sf->last_update_timeout_sec) {
        timeouts_add(ctx->expires, &sf->timeout_handle, curr_time + SHAPING_STAT_REFRESH_INTERVAL_SEC);
        sf->last_update_timeout_sec = curr_time;
    }

    if(sf->flag & SESSION_CLOSE) {
        if (shaper_queue_empty(sf)) {
            char *addr_str = addr_tuple4_to_str(&sf->tuple4);
            LOG_DEBUG("%s: shaping free a shaping_flow for session: %s", LOG_TAG_SHAPING, addr_str);

            sf->flag &= (~SESSION_CLOSE);
            shaping_flow_free(ctx, sf);

            if (addr_str) {
                free(addr_str);
            }
        }
    }

    return;
}

void polling_entry(struct shaper *sp, struct shaping_stat *stat, struct shaping_thread_ctx *ctx)
{
    swarmkv_caller_loop(ctx->swarmkv_db, SWARMKV_LOOP_NONBLOCK, NULL);

    struct timeout *t = NULL;
    struct shaping_flow *sf = NULL;
    time_t curr_time = time(NULL);
    int cnt = 0;

    if (curr_time > ctx->last_update_timeout_sec) {
        timeouts_update(ctx->expires, curr_time);
        ctx->last_update_timeout_sec = curr_time;
    }
    
    t = timeouts_get(ctx->expires);
    while (t && cnt < SHAPING_STAT_REFRESH_MAX_PER_POLLING) {
        sf = container_of(t, struct shaping_flow, timeout_handle);
        shaper_stat_refresh(ctx, sf, ctx->thread_index, 0);
        timeouts_add(ctx->expires, &sf->timeout_handle, time(NULL) + SHAPING_STAT_REFRESH_INTERVAL_SEC);
        t = timeouts_get(ctx->expires);
        cnt++;
    }

    if (shaper_global_stat_queueing_pkts_get(ctx->global_stat) == 0) {
        return;
    }
    
    struct shaper_flow_instance sf_ins[SHAPER_FLOW_POP_NUM_MAX];
    int sf_num;
    int ret;

    for (int i = 0; i < SHAPING_PRIORITY_NUM_MAX; i++) {
        sf_num = shaper_flow_in_order_get(sp, sf_ins, i, ctx->conf.polling_node_num_max[i]);
        if (sf_num == 0) {
            continue;
        }

        for (int j = 0; j < sf_num; j++) {
            ret = shaper_polling_first_pkt_token_get(sp, sf_ins[j].sf, sf_ins[j].priority, stat, ctx);
            if (ret == 0) {
                return;
            }
        }
    }

    return;
}

static struct shaping_flow* shaper_ctrl_pkt_session_handle(struct shaping_thread_ctx *ctx, marsio_buff_t *rx_buff, struct metadata *meta)
{
    struct ctrl_pkt_data ctrl_data;
    struct shaping_flow *sf = NULL;
    struct raw_pkt_parser raw_parser;

    if (shaper_marsio_pkt_metadata_get(rx_buff, meta, 1, &raw_parser) < 0) {
        LOG_ERROR("%s: shaping marsio get metadata from ctrl pkt failed", LOG_TAG_CTRLPKT);
        //TODO: dump meta data for debug
        return NULL;
    }

    if (shaper_marsio_ctrl_pkt_data_parse(&ctrl_data, meta->raw_data + meta->l7_offset, meta->raw_len - meta->l7_offset) < 0) {
        LOG_ERROR("%s: shaping marsio parse json data from ctrl pkt failed", LOG_TAG_CTRLPKT);
        return NULL;
    }

    if (ctrl_data.session_id != meta->session_id) {
        LOG_ERROR("%s: shaping marsio ctrl data session_id %ld != meta session_id %ld", LOG_TAG_CTRLPKT, ctrl_data.session_id, meta->session_id);
        return NULL;
    }

    switch (ctrl_data.state) {
        case SESSION_STATE_OPENING:
            shaper_global_stat_ctrlpkt_opening_inc(ctx->global_stat);
            //sf = shaper_session_opening(ctx, meta, &ctrl_data, &raw_parser);
            break;
        case SESSION_STATE_ACTIVE:
            shaper_global_stat_ctrlpkt_active_inc(ctx->global_stat);
            sf = shaper_session_active(ctx, meta, &ctrl_data, &raw_parser);
            break;
        case SESSION_STATE_CLOSING:
            shaper_global_stat_ctrlpkt_close_inc(ctx->global_stat);
            sf = shaper_session_close(ctx, meta);
            break;
        case SESSION_STATE_RESETALL:
            shaper_global_stat_ctrlpkt_resetall_inc(ctx->global_stat);
            sf = shaper_session_reset_all(ctx, meta);
            break;
        default:
            shaper_global_stat_ctrlpkt_err_inc(ctx->global_stat);
            assert(0);
    }

    return sf;
}

static struct shaping_flow *shaper_raw_pkt_session_handle(struct shaping_thread_ctx *ctx, marsio_buff_t *rx_buff, struct metadata *meta)
{
    struct shaping_flow *sf = NULL;
    struct session_node *session_node = NULL;
    struct raw_pkt_parser raw_parser;

    if (shaper_marsio_pkt_metadata_get(rx_buff, meta, 0, &raw_parser) < 0) {
        LOG_ERROR("%s: shaping marsio get metadata from raw pkt failed", LOG_TAG_CTRLPKT);
        //TODO: dump meta data for debug
        return NULL;
    }

    session_node = session_table_search_by_id(ctx->session_table, meta->session_id);
    if (session_node) {
        sf = (struct shaping_flow *)session_node->val_data;
        shaper_global_stat_hit_policy_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_RX, meta->raw_len);
    }

    return sf;
}

void shaper_packet_recv_and_process(struct shaping_thread_ctx *ctx)
{
    marsio_buff_t *rx_buff[SHAPER_MARSIO_RX_BRUST_MAX];
    struct shaping_flow *sf = NULL;
    struct metadata meta;
    int rx_num;
    int i;

    rx_num = marsio_recv_burst(ctx->marsio_info->mr_dev, ctx->thread_index, rx_buff, ctx->marsio_info->rx_brust_max);
    if (rx_num <= 0) {
        polling_entry(ctx->sp, ctx->stat, ctx);
        return;
    }

    for (i = 0; i < rx_num; i++) {
        if (marsio_buff_is_ctrlbuf(rx_buff[i])) {
            sf = shaper_ctrl_pkt_session_handle(ctx, rx_buff[i], &meta);
        } else {
            sf = shaper_raw_pkt_session_handle(ctx, rx_buff[i], &meta);
        }
        shaper_global_stat_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_RX, meta.raw_len);

        if (meta.is_ctrl_pkt || !sf || sf->rule_num == 0) {//ctrl pkt need send directly
            marsio_send_burst(ctx->marsio_info->mr_path, ctx->thread_index, &rx_buff[i], 1);
            shaper_global_stat_throughput_inc(ctx->global_stat, SHAPING_GLOBAL_STAT_TX, meta.raw_len);
        } else {
            shaping_packet_process(ctx, rx_buff[i], &meta, sf);
        }
        polling_entry(ctx->sp, ctx->stat, ctx);
    }

    return;
}

int shaper_global_conf_init(struct shaping_system_conf *conf)
{
    int ret;
    int array_num;
    cJSON *json = NULL;
    cJSON *tmp_obj = NULL, *tmp_array_obj = NULL;
    char polling_node_num_max[128] = {0};
    unsigned int cpu_mask[SHAPING_WROK_THREAD_NUM_MAX] = {0};

    ret = MESA_load_profile_int_nodef(SHAPING_GLOBAL_CONF_FILE, "SYSTEM", "WORK_THREAD_NUM", &conf->work_thread_num);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf WORK_THREAD_NUM failed", LOG_TAG_SHAPING);
        return ret;
    }
    conf->work_thread_num = MIN(conf->work_thread_num, SHAPING_WROK_THREAD_NUM_MAX);

    ret = MESA_load_profile_int_nodef(SHAPING_GLOBAL_CONF_FILE, "SYSTEM", "ENABLE_CPU_AFFINITY", &conf->cpu_affinity_enable);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf ENABLE_CPU_AFFINITY failed", LOG_TAG_SHAPING);
        return ret;
    }

    ret = MESA_load_profile_uint_range(SHAPING_GLOBAL_CONF_FILE, "SYSTEM", "CPU_AFFINITY_MASK", SHAPING_WROK_THREAD_NUM_MAX, cpu_mask);
    if (ret < 0 || ret != conf->work_thread_num) {
        LOG_ERROR("%s: shaping init global conf get CPU_AFFINITY_MASK failed or incomplete config", LOG_TAG_SHAPING);
        return -1;
    }
    CPU_ZERO(&conf->cpu_affinity_mask);
    for (int i = 0; i < conf->work_thread_num; i++) {
        int cpu_id = cpu_mask[i];
        CPU_SET(cpu_id, &conf->cpu_affinity_mask);
    }

    ret = MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "SYSTEM", "firewall_sids", &conf->firewall_sid, 1001);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf firewall_sids failed", LOG_TAG_SHAPING);
        return ret;
    }

#if 0 //temporarily not support array config
    array_num = SHAPING_PRIORITY_NUM_MAX;
    ret = MESA_load_profile_int_array(SHAPING_GLOBAL_CONF_FILE, "SHAPING", "POLLING_NODE_NUM_MAX", &array_num, g_sp_conf.polling_node_num_max);
    if (ret < 0) {
        MESA_handle_runtime_log(g_rt_para.log_handle, RLOG_LV_FATAL, SHAPING_LOG_MODULE,
                                "shaping init global conf POLLING_NODE_NUM_MAX failed, ret is %d", ret);
        return ret;
    }
    for (int i = 0; i < array_num; i++) {
        if (g_sp_conf.polling_node_num_max[i] > SHAPER_FLOW_POP_NUM_MAX) {
            MESA_handle_runtime_log(g_rt_para.log_handle, RLOG_LV_FATAL, SHAPING_LOG_MODULE,
                                "shaping init global conf POLLING_NODE_NUM_MAX failed, index %d value %d exceed limit %d", 
                                i, g_sp_conf.polling_node_num_max[i], SHAPER_FLOW_POP_NUM_MAX);
            return -1;
        }
    }
#endif

    /*************parse max process num of each priority avl tree*************/
    ret = MESA_load_profile_string_nodef(SHAPING_GLOBAL_CONF_FILE, "CONFIG", "POLLING_NODE_NUM_MAX", polling_node_num_max, sizeof(polling_node_num_max));
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf POLLING_NODE_NUM_MAX failed", LOG_TAG_SHAPING);
        return ret;
    }

    json = cJSON_Parse(polling_node_num_max);
    if (!json) {
        LOG_ERROR("%s: shaping parse global conf json POLLING_NODE_NUM_MAX failed", LOG_TAG_SHAPING);
        goto ERROR;
    }
    tmp_obj = cJSON_GetObjectItem(json, "polling_node_num_max");
    if (!tmp_obj) {
        LOG_ERROR("%s: shaping init global conf parse json polling_node_num_max failed", LOG_TAG_SHAPING);
        goto ERROR;
    }

    array_num = cJSON_GetArraySize(tmp_obj);
    if (array_num != SHAPING_PRIORITY_NUM_MAX) {
        LOG_ERROR("%s: shaping init global conf POLLING_NODE_NUM_MAX failed, array_num is %d", LOG_TAG_SHAPING, array_num);
        goto ERROR;
    }

    for (int i = 0; i < array_num; i++) {
        tmp_array_obj = cJSON_GetArrayItem(tmp_obj, i);
        if (tmp_array_obj->valueint > SHAPER_FLOW_POP_NUM_MAX) {
            LOG_ERROR("%s: shaping init global conf POLLING_NODE_NUM_MAX failed, value at index %d exceed limit %d", LOG_TAG_SHAPING, i, SHAPER_FLOW_POP_NUM_MAX);
            goto ERROR;
        }
        conf->polling_node_num_max[i] = tmp_array_obj->valueint;
    }

    if (json) {
        cJSON_Delete(json);
    }
    /*************************************************************************/

    MESA_load_profile_uint_def(SHAPING_GLOBAL_CONF_FILE, "CONFIG", "SESSION_QUEUE_LEN_MAX", &conf->session_queue_len_max, 128);
    MESA_load_profile_uint_def(SHAPING_GLOBAL_CONF_FILE, "CONFIG", "PRIORITY_QUEUE_LEN_MAX", &conf->priority_queue_len_max, 1024);

    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "CONFIG", "CHECK_RULE_ENABLE_INTERVAL_SEC", &conf->check_rule_enable_interval_sec, 120);

    return 0;

ERROR:
    if (json) {
        cJSON_Delete(json);
    }
    return -1;
}

void shaping_engine_destroy(struct shaping_ctx *ctx)
{
    LOG_DEBUG("%s: destroy shaping engine", LOG_TAG_SHAPING);

    if (ctx) {
        shaper_swarmkv_destroy(ctx->swarmkv_db);
        shaper_maat_destroy(ctx->maat_info);
        shaper_marsio_destroy(ctx->marsio_info);
        shaper_stat_destroy(ctx->stat);
        shaper_global_stat_destroy(ctx->global_stat);

        if (ctx->thread_ctx) {
            for (int i = 0; i < ctx->thread_num; i++) {
                shaper_free(ctx->thread_ctx[i].sp);
                session_table_destory(ctx->thread_ctx[i].session_table);
                timeouts_close(ctx->thread_ctx[i].expires);
            }
            free(ctx->thread_ctx);
        }
        free(ctx);
    }

    LOG_CLOSE();
    return;
}

struct shaping_ctx *shaping_engine_init()
{
    struct shaping_system_conf conf;
    struct shaping_ctx *ctx = NULL;
    int ret, error;

    memset(&conf, 0, sizeof(conf));
    ctx = (struct shaping_ctx *)calloc(1, sizeof(struct shaping_ctx));

    ret = shaper_global_conf_init(&conf);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf failed", LOG_TAG_SHAPING);
        goto ERROR;
    }

    /*init swarmkv*/
    ctx->swarmkv_db = shaper_swarmkv_init(conf.work_thread_num);
    if (ctx->swarmkv_db == NULL) {
        goto ERROR;
    }

    /*init maat*/
    ctx->maat_info = shaper_maat_init("SHAPING");
    if (ctx->maat_info == NULL) {
        goto ERROR;
    }

    /*init marsio*/
    ctx->marsio_info = shaper_marsio_init(&conf);
    if (ctx->marsio_info == NULL) {
        goto ERROR;
    }

    ctx->stat = shaper_stat_init(conf.work_thread_num);
    if (ctx->stat == NULL) {
        goto ERROR;
    }

    ctx->global_stat = shaper_global_stat_init();
    if (ctx->global_stat == NULL) {
        goto ERROR;
    }
    
    ctx->thread_ctx = (struct shaping_thread_ctx *)calloc(conf.work_thread_num, sizeof(struct shaping_thread_ctx));
    ctx->thread_num = conf.work_thread_num;
    for (int i = 0; i < conf.work_thread_num; i++) {
        ctx->thread_ctx[i].thread_index = i;
        ctx->thread_ctx[i].sp = shaper_new(conf.priority_queue_len_max);
        ctx->thread_ctx[i].stat = ctx->stat;
        ctx->thread_ctx[i].global_stat = ctx->global_stat;
        ctx->thread_ctx[i].session_table = session_table_create();
        ctx->thread_ctx[i].maat_info = ctx->maat_info;
        ctx->thread_ctx[i].marsio_info = ctx->marsio_info;
        ctx->thread_ctx[i].swarmkv_db = ctx->swarmkv_db;
        ctx->thread_ctx[i].expires = timeouts_open(0, &error);
        ctx->thread_ctx[i].ref_ctx = ctx;
        memcpy(&ctx->thread_ctx[i].conf, &conf, sizeof(conf));
    }

    return ctx;

ERROR:
    shaping_engine_destroy(ctx);
    return NULL;
}