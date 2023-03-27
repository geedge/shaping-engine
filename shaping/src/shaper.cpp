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

#define NANO_SECONDS_PER_MICRO_SEC 1000
#define MICRO_SECONDS_PER_SEC 1000000
#define NANO_SECONDS_PER_SEC 1000000000

struct shaper {//trees in one thread
    struct avl_tree *priority_trees[SHAPING_PRIORITY_NUM_MAX];//represent 10 avl tree corresponding to 10 priority
};

struct shaping_node {//a session will have 10 nodes, corresponding 10 avl tree
    struct shaping_flow shaping_flow;
    struct avl_node *avl_node[SHAPING_PRIORITY_NUM_MAX];
};

struct shaping_async_cb_arg {
    struct shaping_flow *sf;
    struct shaping_profile_info *s_pf_info;
    unsigned char direction;
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
        free(s_node);
    }

    return;
}

struct shaping_flow* shaping_flow_new()
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

    return &s_node->shaping_flow;

ERROR:
    shaping_node_free(s_node);
    return NULL;
}

void shaping_flow_free(struct shaping_flow *sf)
{
    struct shaping_node *s_node = (struct shaping_node*)sf;

    if (__atomic_sub_fetch(&sf->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
        shaping_node_free(s_node);
    }
    
    return;
}

static int shaper_packet_enqueue(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, void *pkt_buff, struct timespec *income_time, struct metadata *meta)
{
    struct shaping_packet_wrapper *s_pkt = NULL;

    if (sf->queue_len == ctx->session_queue_len_max) {
        return -1;
    }

    s_pkt = (struct shaping_packet_wrapper*)calloc(1, sizeof(struct shaping_packet_wrapper));
    if (!s_pkt) {
        return -1;
    }

    s_pkt->pkt_buff = pkt_buff;
    s_pkt->direction = meta->dir;
    s_pkt->length = meta->raw_len;
    s_pkt->tcp_pure_contorl = meta->is_tcp_pure_ctrl;
    s_pkt->income_time_ns = income_time->tv_sec * NANO_SECONDS_PER_SEC + income_time->tv_nsec;
    s_pkt->enqueue_time_us = income_time->tv_sec * MICRO_SECONDS_PER_SEC + income_time->tv_nsec / NANO_SECONDS_PER_MICRO_SEC;
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
    struct shaping_stat *stat = ctx->stat;
    struct shaping_rule_info *rule = &sf->matched_rule_infos[0];

    while (!shaper_queue_empty(sf)) {
        pkt_wrapper = shaper_first_pkt_get(sf);

        shaper_stat_queueing_pkt_dec(stat, rule->id, rule->primary.id, rule->primary.priority, 
                                    pkt_wrapper->direction, pkt_wrapper->length, SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);
        shaper_stat_drop_inc(stat, rule->id, rule->primary.id, rule->primary.priority, 
                            pkt_wrapper->direction, pkt_wrapper->length, ctx->thread_index);

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
        shaper_stat_queueing_pkt_inc(ctx->stat, s_rule_info->id, s_rule_info->primary.id, 
                                    priority, pkt_wrapper->direction, pkt_wrapper->length, SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);
        shaper_stat_queueing_session_inc(ctx->stat, s_rule_info->id, s_rule_info->primary.id, priority, SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);
        s_rule_info->primary.enqueue_time_us = enqueue_time;
    }

    if (s_rule_info->borrowing_num == 0) {// no borrow profile
        return ret;
    } else {
        for (i = 0; i < s_rule_info->borrowing_num; i++) {
            priority = s_rule_info->borrowing[i].priority;
            avl_tree_node_key_set(s_node->avl_node[priority], pkt_wrapper->income_time_ns);
            if (0 == avl_tree_node_insert(sp->priority_trees[priority], s_node->avl_node[priority])) {
                ret = 0;
                shaper_stat_queueing_pkt_inc(ctx->stat, s_rule_info->id, s_rule_info->borrowing[i].id, 
                                            priority, pkt_wrapper->direction, pkt_wrapper->length, SHAPING_PROFILE_TYPE_BORROW, ctx->thread_index);
                shaper_stat_queueing_session_inc(ctx->stat, s_rule_info->id, s_rule_info->borrowing[i].id, priority, SHAPING_PROFILE_TYPE_BORROW, ctx->thread_index);
                s_rule_info->borrowing[i].enqueue_time_us = enqueue_time;
            }
        }

        return ret;
    }
}

static unsigned long long shaper_pkt_latency_calculate(struct shaping_profile_info *profile, struct timespec *time)
{
    unsigned long long enqueue_time = profile->enqueue_time_us;
    unsigned long long curr_time = time->tv_sec * MICRO_SECONDS_PER_SEC + time->tv_nsec / NANO_SECONDS_PER_MICRO_SEC;

    assert(curr_time >= enqueue_time);
    return (curr_time - enqueue_time);
}

static void shaping_flow_remove_from_pool(struct shaping_thread_ctx *ctx, struct shaping_flow *sf)
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
        shaper_stat_queueing_pkt_dec(ctx->stat, s_rule_info->id, s_rule_info->primary.id, 
                                    priority, pkt_wrapper->direction, pkt_wrapper->length, SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);
        shaper_stat_queueing_session_dec(ctx->stat, s_rule_info->id, s_rule_info->primary.id, priority, SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);

        latency = shaper_pkt_latency_calculate(&s_rule_info->primary, &curr_time);
        shaper_stat_max_latency_update(ctx->stat, s_rule_info->id, s_rule_info->primary.id, 
                                        priority, pkt_wrapper->direction, latency, SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);
    }
    
    if (s_rule_info->borrowing_num == 0) {
        return;
    }

    for (i = 0; i < s_rule_info->borrowing_num; i++) {
        priority = s_rule_info->borrowing[i].priority;
        if (avl_node_in_tree(s_node->avl_node[priority])) {
            avl_tree_node_remove(sp->priority_trees[priority], s_node->avl_node[priority]);
            shaper_stat_queueing_pkt_dec(ctx->stat, s_rule_info->id, s_rule_info->borrowing[i].id, 
                                        priority, pkt_wrapper->direction, pkt_wrapper->length, SHAPING_PROFILE_TYPE_BORROW, ctx->thread_index);
            shaper_stat_queueing_session_dec(ctx->stat, s_rule_info->id, s_rule_info->borrowing[i].id, priority, SHAPING_PROFILE_TYPE_BORROW, ctx->thread_index);

            latency = shaper_pkt_latency_calculate(&s_rule_info->borrowing[i], &curr_time);
            shaper_stat_max_latency_update(ctx->stat, s_rule_info->id, s_rule_info->borrowing[i].id, 
                                            priority, pkt_wrapper->direction, latency, SHAPING_PROFILE_TYPE_BORROW, ctx->thread_index);
        }
    }

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

void shaper_flow_pop(struct shaping_thread_ctx *ctx, struct shaping_flow *sf)
{
    shaping_flow_remove_from_pool(ctx, sf);

    return;
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

    assert(reply->type == SWARMKV_REPLY_INTEGER);

    if (reply->integer < 0) {//profile not exist
        s_pf_info->is_invalid = 1;
        goto END;
    } else {
        s_pf_info->is_invalid = 0;
    }

    shaper_deposit_token_add(s_pf_info, reply->integer, arg->direction);//deposit tokens to profile

END:
    free(cb_arg);
    __atomic_sub_fetch(&s_pf_info->async_ref_count, 1, __ATOMIC_SEQ_CST);
    shaping_flow_free(sf);//sub ref count and decide if need to free

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

static int shaper_token_get_from_profile(struct swarmkv *db, struct shaping_flow *sf, struct shaping_profile_info *pf_info, int profile_type, int req_token, unsigned char direction)
{
    struct shaping_async_cb_arg *arg;
    char key[32] = {0};

    __atomic_add_fetch(&pf_info->async_ref_count, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&sf->ref_count, 1, __ATOMIC_SEQ_CST);

    snprintf(key, sizeof(key), "tsg-shaping-%d-%s", pf_info->id, direction == SHAPING_DIR_OUT ? "outgoing" : "incoming");
    arg = (struct shaping_async_cb_arg *)calloc(1, sizeof(struct shaping_async_cb_arg));
    arg->s_pf_info = pf_info;
    arg->sf = sf;
    arg->direction = direction;

    swarmkv_tconsume(db, key, strlen(key), req_token, shaper_token_get_cb, arg);
    if (__atomic_load_n(&pf_info->async_ref_count, __ATOMIC_SEQ_CST) != 0) {//has async operation not completed
        shaper_deposit_token_sub(pf_info, req_token, direction);
        return 0;
    }

    if (pf_info->is_invalid) {
        if (profile_type == SHAPING_PROFILE_TYPE_PRIMARY) {//for primary, means this rule don't need get token
            return 0;
        } else {//for borrowing, means this profile has no token to borrow
            return -1;
        }
    }

    if (shaper_deposit_token_is_enough(pf_info, req_token, direction)) {
        shaper_deposit_token_sub(pf_info, req_token, direction);
        return 0;
    }

    return -1;
}

#if 0
int shaper_token_consume(struct shaping_flow *sf, unsigned int req_token, struct shaping_rule_info *s_rule_info)
{
    int i;

    if (SHAPING_SUCCESS == shaping_token_get_from_profile(&s_rule_info->primary, 1, req_token)) {
        return SHAPING_SUCCESS;
    }

    if (s_rule_info->borrowing_num > 0) {
        for (i = 0; i < s_rule_info->borrowing_num; i++) {
            if (SHAPING_SUCCESS == shaping_token_get_from_profile(&s_rule_info->borrowing[i], 0, req_token)) {
                return SHAPING_SUCCESS;
            }
        }
    }

    return SHAPING_FAILED;
}
#endif

static int shaper_token_consume(struct swarmkv *db, struct shaping_flow *sf, int req_token, 
                        struct shaping_profile_info *profile, int profile_type, unsigned char direction)
{
    return shaper_token_get_from_profile(db, sf, profile, profile_type, req_token, direction);
        
}

#if 0
enum shaping_packet_action shaper_pkt_action_decide(struct shaping_flow *sf, struct shaper *sp, void *raw_pkt, 
                                                        unsigned int pkt_len, unsigned char direction, unsigned long long income_time)
{
    int i;
    struct shaping_rule_info *s_rule_info;

    for (i = sf->anchor; i < sf->rule_num; i++) {
        s_rule_info = &sf->matched_rule_infos[i];
        if (-1 == shaper_token_consume(sf, pkt_len, s_rule_info, s_rule_info->primary.priority)) {
            sf->anchor = i;
            if (0 == shaper_flow_push(sf, sp)) {
                shaper_packet_enqueue(sf, raw_pkt, direction, income_time);
                return SHAPING_HOLD;
            } else {
                return SHAPING_DROP;
            }
        }
    }

    return SHAPING_FORWARD;
}
#endif

static struct shaping_profile_info * shaper_profile_get(struct shaping_rule_info *s_rule_info, int priority, int *profile_type)
{
    int i;

    if (s_rule_info->primary.priority == priority) {
        *profile_type = SHAPING_PROFILE_TYPE_PRIMARY;
        return &s_rule_info->primary;
    }

    for (i = 0; i < s_rule_info->borrowing_num; i++) {
        if (s_rule_info->borrowing[i].priority == priority) {
            *profile_type = SHAPING_PROFILE_TYPE_BORROW;
            return &s_rule_info->borrowing[i];
        }
    }

    return NULL;
}

static int shaper_next_anchor_get(struct shaping_flow *sf, unsigned char direction)
{
    int anchor = sf->anchor + 1;

    if (anchor > sf->rule_num - 1) {
        return 0;
    }

    return anchor;
}

enum shaping_packet_action shaper_pkt_action_decide(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int priority, int sf_in_queue)
{
    int profile_type = 0;
    struct shaping_profile_info *profile = NULL;
    struct shaping_rule_info *rule = NULL;
    struct shaping_packet_wrapper *pkt_wrapper = NULL;
    struct timespec curr_time;
    unsigned long long enqueue_time;

    rule = &sf->matched_rule_infos[sf->anchor];
    profile = shaper_profile_get(rule, priority, &profile_type);
    assert(profile != NULL);
    pkt_wrapper = shaper_first_pkt_get(sf);
    assert(pkt_wrapper != NULL);

    if (pkt_wrapper->tcp_pure_contorl) {
        if (sf_in_queue) {
            shaper_flow_pop(ctx, sf);
        }
        shaper_stat_forward_all_rule_inc(ctx->stat, sf, pkt_wrapper->direction, pkt_wrapper->length, ctx->thread_index);
        return SHAPING_FORWARD;
    }

    if (0 == shaper_token_consume(ctx->swarmkv_db, sf, pkt_wrapper->length, profile, profile_type, pkt_wrapper->direction)) {
        shaper_stat_forward_inc(ctx->stat, rule->id, profile->id, profile->priority, 
                                pkt_wrapper->direction, pkt_wrapper->length, profile_type, ctx->thread_index);
        
        if (sf_in_queue) {
            shaper_flow_pop(ctx, sf);
        }

        sf->anchor = shaper_next_anchor_get(sf, pkt_wrapper->direction);
        if (sf->anchor == 0) {//no next rule
            return SHAPING_FORWARD;
        } else {
            clock_gettime(CLOCK_MONOTONIC, &curr_time);
            enqueue_time = curr_time.tv_sec * MICRO_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MICRO_SEC;
            goto FLOW_PUSH;
        }
    } else {
        if (sf_in_queue) {
            return SHAPING_QUEUED;
        } else {
            enqueue_time = pkt_wrapper->enqueue_time_us;
            goto FLOW_PUSH;
        }
    }

FLOW_PUSH:
    if (0 == shaper_flow_push(ctx, sf, enqueue_time)) {
        return SHAPING_QUEUED;
    } else {
        rule = &sf->matched_rule_infos[sf->anchor];
        shaper_stat_drop_inc(ctx->stat, rule->id, rule->primary.id, 
                            rule->primary.priority, pkt_wrapper->direction, pkt_wrapper->length, ctx->thread_index);

        sf->anchor = 0;
        return SHAPING_DROP;
    }
}

static int shaper_polling_first_pkt_token_get(struct shaper *sp, struct shaping_flow *sf, int priority, 
                                        struct shaping_stat *stat, struct shaping_thread_ctx *ctx)
{
    struct shaping_packet_wrapper *pkt_wrapper;
    struct shaping_rule_info *rule = NULL;
    int old_anchor = sf->anchor;
    int shaping_ret;

    pkt_wrapper = shaper_first_pkt_get(sf);
    assert(pkt_wrapper != NULL);

#if 0
    //AQM not implement yet
    if (stub_AQM_drop_packet(sf->queue_len, pkt_wrapper->enqueue_time_us)) {
        rule = &sf->matched_rule_infos[sf->anchor];
        shaper_stat_drop_inc(stat_hashtbl, rule->id, rule->primary.id, rule->primary.priority, pkt_wrapper->direction, pkt_wrapper->length);

        shaping_ret = SHAPING_DROP;
    } else {
        shaping_ret = shaper_pkt_action_decide(g_swarmkv_db, sf, sp, priority, stat_hashtbl, 1);
    }
#endif
    shaping_ret = shaper_pkt_action_decide(ctx, sf, priority, 1);

    switch (shaping_ret) {
        case SHAPING_QUEUED:
            if (old_anchor == sf->anchor) {//didn't get token
                return -1;
            } else {//got token for one rule and waiting get token for next rule
                return 0;
            }
            break;
        case SHAPING_DROP:
            marsio_buff_free(ctx->marsio_info->instance, &pkt_wrapper->pkt_buff, 1, 0, ctx->thread_index);
            shaper_packet_dequeue(sf);
            break;
        case SHAPING_FORWARD:
            marsio_send_burst(ctx->marsio_info->mr_path, ctx->thread_index, &pkt_wrapper->pkt_buff, 1);
            shaper_packet_dequeue(sf);
            break;
        default:
            assert(0);//impossible path
            break;
    }

    if (shaper_queue_empty(sf)) {
        if (sf->flag & STREAM_CLOSE) {
            shaping_flow_free(sf);
        }
        return 0;
    } else {
        pkt_wrapper = shaper_first_pkt_get(sf);
        sf->anchor = 0;

        if (0 == shaper_flow_push(ctx, sf, pkt_wrapper->enqueue_time_us)) {
            /*in pkt process, when queue not empty, 
            new pkt's queueing stat was added to primary profile of first rule.
            while shaper_flow_push() here will add queueing stat to every profile of first rule,
            so need adjust queueing stat here*/
            rule = &sf->matched_rule_infos[sf->anchor];
            shaper_stat_queueing_pkt_dec(stat, rule->id, rule->primary.id, rule->primary.priority,
                                        pkt_wrapper->direction, pkt_wrapper->length, SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);
        } else {
            shaper_queue_clear(sf, ctx);//first packet fail, then every packet will fail
            if (sf->flag & STREAM_CLOSE) {
                shaping_flow_free(sf);
            }
        }
        return 0;
    }
}

void shaping_stream_process(struct shaping_thread_ctx *ctx, marsio_buff_t *rx_buff, struct metadata *meta, struct shaping_flow *sf)
{
    int priority;
    int shaping_ret;
    struct shaping_rule_info *s_rule;
    struct shaping_stat *stat = ctx->stat;
    struct shaping_marsio_info *marsio_info = ctx->marsio_info;
    struct timespec curr_time;

    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    if (!shaper_queue_empty(sf)) {//already have queueing pkt, enqueue directly
        s_rule = &sf->matched_rule_infos[0];
        if (0 == shaper_packet_enqueue(ctx, sf, rx_buff, &curr_time, meta)) {
            shaper_stat_queueing_pkt_inc(stat, s_rule->id, 
                                        s_rule->primary.id, s_rule->primary.priority, meta->dir, meta->raw_len,
                                        SHAPING_PROFILE_TYPE_PRIMARY, ctx->thread_index);
        } else {
            shaper_stat_drop_inc(stat, s_rule->id, s_rule->primary.id, s_rule->primary.priority, meta->dir, meta->raw_len, ctx->thread_index);
            marsio_buff_free(marsio_info->instance, &rx_buff, 1, 0, ctx->thread_index);
        }
        
    } else {
        if (meta->is_tcp_pure_ctrl) {
            marsio_send_burst(marsio_info->mr_path, ctx->thread_index, &rx_buff, 1);
            shaper_stat_forward_all_rule_inc(stat, sf, meta->dir, meta->raw_len, ctx->thread_index);
            goto JUDGE_CLOSE;//for tcp pure control pkt, transmit it directly
        }

        if (0 != shaper_packet_enqueue(ctx, sf, rx_buff, &curr_time, meta)) {
            marsio_buff_free(marsio_info->instance, &rx_buff, 1, 0, ctx->thread_index);
            LOG_ERROR("%s: shaping enqueue packet failed while queue empty for session: %s", LOG_TAG_SHAPING, addr_tuple4_to_str(&sf->tuple4));
            goto JUDGE_CLOSE;
        }

        sf->anchor = 0;

        priority = sf->matched_rule_infos[sf->anchor].primary.priority;
        shaping_ret = shaper_pkt_action_decide(ctx, sf, priority, 0);
        switch (shaping_ret) {
            case SHAPING_QUEUED:
                break;
            case SHAPING_DROP:
                marsio_buff_free(marsio_info->instance, &rx_buff, 1, 0, ctx->thread_index);
                shaper_packet_dequeue(sf);
                break;
            case SHAPING_FORWARD:
                marsio_send_burst(marsio_info->mr_path, ctx->thread_index, &rx_buff, 1);
                shaper_packet_dequeue(sf);
                break;
            default:
                assert(0);
                break;
        }
    }

JUDGE_CLOSE:
    if(sf->flag &= STREAM_CLOSE) {
        if (shaper_queue_empty(sf)) {
            shaping_flow_free(sf);
            LOG_DEBUG("%s: shaping free a shaping_flow for session: %s", LOG_TAG_SHAPING, addr_tuple4_to_str(&sf->tuple4));
        }
    }

    return;
}

void polling_entry(struct shaper *sp, struct shaping_stat *stat, struct shaping_thread_ctx *ctx)
{
    struct shaper_flow_instance sf_ins[SHAPER_FLOW_POP_NUM_MAX];
    int sf_num;
    int ret;

    for (int i = 0; i < SHAPING_PRIORITY_NUM_MAX; i++) {
        sf_num = shaper_flow_in_order_get(sp, sf_ins, i, ctx->polling_node_num_max[i]);
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
            sf = shaper_session_opening(ctx, meta, &ctrl_data, &raw_parser);
            break;
        case SESSION_STATE_ACTIVE:
            sf = shaper_session_active(ctx, meta, &ctrl_data);
            break;
        case SESSION_STATE_CLOSING:
            sf = shaper_session_close(ctx, meta);
            break;
        case SESSION_STATE_RESETALL:
            sf = shaper_session_reset_all(ctx, meta);
            break;
        default:
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
    }

    return sf;
}

void shaper_packet_recv_and_process(struct shaping_thread_ctx *ctx)
{
    marsio_buff_t *rx_buff;
    struct shaping_flow *sf = NULL;
    struct metadata meta;
    int rx_num;

    rx_num = marsio_recv_burst(ctx->marsio_info->mr_dev, ctx->thread_index, &rx_buff, 1);
    if (rx_num <= 0) {
        polling_entry(ctx->sp, ctx->stat, ctx);
        return;
    }

    if (marsio_buff_is_ctrlbuf(rx_buff)) {
        sf = shaper_ctrl_pkt_session_handle(ctx, rx_buff, &meta);
    } else {
        sf = shaper_raw_pkt_session_handle(ctx, rx_buff, &meta);
    }

    if (meta.is_ctrl_pkt || !sf) {//ctrl pkt need send directly
        marsio_send_burst(ctx->marsio_info->mr_path, ctx->thread_index, &rx_buff, 1);
    } else {
        shaping_stream_process(ctx, rx_buff, &meta, sf);
    }
    polling_entry(ctx->sp, ctx->stat, ctx);

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
    for (int i = 0; i < conf->work_thread_num; i++) {
        conf->cpu_affinity_mask |= 1 << cpu_mask[i];
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

        if (ctx->thread_ctx) {
            for (int i = 0; i < ctx->thread_num; i++) {
                shaper_free(ctx->thread_ctx[i].sp);
                session_table_destory(ctx->thread_ctx[i].session_table);
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
    int ret;

    memset(&conf, 0, sizeof(conf));
    ctx = (struct shaping_ctx *)calloc(1, sizeof(struct shaping_ctx));

    if (LOG_INIT("./conf/zlog.conf") == -1)
    {
        goto ERROR;
    }

    ret = shaper_global_conf_init(&conf);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf failed", LOG_TAG_SHAPING);
        goto ERROR;
    }

    /*init swarmkv*/
    ctx->swarmkv_db = shaper_swarmkv_init();
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
    
    ctx->thread_ctx = (struct shaping_thread_ctx *)calloc(conf.work_thread_num, sizeof(struct shaping_thread_ctx));
    ctx->thread_num = conf.work_thread_num;
    for (int i = 0; i < conf.work_thread_num; i++) {
        ctx->thread_ctx[i].thread_index = i;
        ctx->thread_ctx[i].sp = shaper_new(conf.priority_queue_len_max);
        ctx->thread_ctx[i].stat = ctx->stat;
        ctx->thread_ctx[i].session_table = session_table_create();
        ctx->thread_ctx[i].maat_info = ctx->maat_info;
        ctx->thread_ctx[i].marsio_info = ctx->marsio_info;
        ctx->thread_ctx[i].swarmkv_db = ctx->swarmkv_db;
        ctx->thread_ctx[i].ref_ctx = ctx;
        ctx->thread_ctx[i].session_queue_len_max = conf.session_queue_len_max;
        memcpy(ctx->thread_ctx[i].polling_node_num_max, conf.polling_node_num_max, sizeof(conf.polling_node_num_max));
    }

    return ctx;

ERROR:
    shaping_engine_destroy(ctx);
    return NULL;
}