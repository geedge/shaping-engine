#include <stdlib.h>
#include <stdarg.h>
#include <vector>
#include <MESA/swarmkv.h>

#include "shaper.h"
#include "shaper_maat.h"
#include "stub.h"

using namespace std;

#define MAX_STUB_RULE_NUM 8
#define MAX_STUB_PROFILE_NUM 8

#define DEFAULT_AVALIABLE_TOKEN_PER_SEC -1

struct stub_token_thread_arg {
    int profile_id;
    struct swarmkv_reply reply;
    swarmkv_on_reply_callback_t *cb;
    void *cb_arg;
};

struct stub_avaliable_token {
    int in_limit_bandwidth;
    int out_limit_bandwidth;
    int bidirection_limit_bandwidth;
};

static int profile_priority_len[MAX_STUB_PROFILE_NUM][SHAPING_PRIORITY_NUM_MAX][SHAPING_DIR_MAX];
static struct stub_avaliable_token pf_curr_avl_token[MAX_STUB_PROFILE_NUM];
static int pf_async_times[MAX_STUB_PROFILE_NUM];
vector<struct stub_token_thread_arg> pf_async_thread[MAX_STUB_PROFILE_NUM];
extern struct shaping_profile pf_array[MAX_STUB_PROFILE_NUM];

void dummy_swarmkv_init()
{
    memset(&pf_array, 0, MAX_STUB_PROFILE_NUM * sizeof(struct shaping_profile));
    memset(&profile_priority_len, 0, MAX_STUB_PROFILE_NUM * SHAPING_PRIORITY_NUM_MAX * SHAPING_DIR_MAX * sizeof(int));

    for (int i = 0; i < MAX_STUB_PROFILE_NUM; i++) {
        pf_curr_avl_token[i].in_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_curr_avl_token[i].out_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_curr_avl_token[i].bidirection_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_array[i].id = i;
        pf_array[i].in_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_array[i].out_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_async_times[i] = 0;
        memset(profile_priority_len[i], 0, 10 * sizeof(int)); 
    }
}

void * stub_get_token_thread_func(void *data)
{
    struct stub_token_thread_arg *thread_arg;

    thread_arg = (struct stub_token_thread_arg*)data;

    thread_arg->cb(&thread_arg->reply, thread_arg->cb_arg);

    return NULL;
}

void stub_set_token_bucket_avl_per_sec(int profile_id, unsigned int tokens, unsigned char direction, enum shaping_profile_limit_direction limit_direction)
{
    pf_array[profile_id].limit_direction = limit_direction;

    if (limit_direction == PROFILE_LIMIT_DIRECTION_BIDIRECTION) {
        pf_array[profile_id].bidirection_limit_bandwidth = tokens * 8;
        pf_curr_avl_token[profile_id].bidirection_limit_bandwidth = tokens * 8;
    } else {
        if (direction == SHAPING_DIR_IN) {
            pf_array[profile_id].in_limit_bandwidth = tokens * 8;
            pf_curr_avl_token[profile_id].in_limit_bandwidth = tokens * 8;
        } else {
            pf_array[profile_id].out_limit_bandwidth = tokens * 8;
            pf_curr_avl_token[profile_id].out_limit_bandwidth = tokens * 8;
        }
    }

    return;
}

void stub_refresh_token_bucket(int profile_id)
{
    pf_curr_avl_token[profile_id].bidirection_limit_bandwidth = pf_array[profile_id].bidirection_limit_bandwidth;
    pf_curr_avl_token[profile_id].in_limit_bandwidth = pf_array[profile_id].in_limit_bandwidth;
    pf_curr_avl_token[profile_id].out_limit_bandwidth = pf_array[profile_id].out_limit_bandwidth;
    return;
}

void stub_set_async_token_get_times(int profile_id, int times)
{
    pf_async_times[profile_id] = times;
    
    if (pf_async_times[profile_id] == 0) {
        for (unsigned int i = 0; i < pf_async_thread[profile_id].size(); i++) {
            stub_get_token_thread_func(&pf_async_thread[profile_id][i]);
        }
        pf_async_thread[profile_id].clear();
    }

    return;
}

/**************stub of swarmkv*****************/
void swarmkv_register_thread(struct swarmkv *db)
{
    return;
}

struct swarmkv_options* swarmkv_options_new(void)
{
    return NULL;
}

int swarmkv_options_set_logger(struct swarmkv_options *opts, void *logger)
{
    return 0;
}

int swarmkv_options_set_bind_address(struct swarmkv_options *opts, const char* ip_addr)
{
    return 0;
}

int swarmkv_options_set_cluster_port(struct swarmkv_options *opts, unsigned int cluster_port)
{
    return 0;
}

int swarmkv_options_set_consul_port(struct swarmkv_options *opts, unsigned int consul_port)
{
    return 0;
}

int swarmkv_options_set_consul_host(struct swarmkv_options *opts, const char* ip_addr)
{
    return 0;
}

int swarmkv_options_set_cluster_announce_ip(struct swarmkv_options *opts, const char *ip_addr)
{
    return 0;
}

int swarmkv_options_set_cluster_announce_port(struct swarmkv_options *opts, unsigned int cluster_announce_port)
{
    return 0;
}

int swarmkv_options_set_health_check_port(struct swarmkv_options *opts, unsigned int health_check_port)
{
    return 0;
}

int swarmkv_options_set_health_check_announce_port(struct swarmkv_options *opts, unsigned int health_check_announce_port)
{
    return 0;
}

struct swarmkv *swarmkv_open(struct swarmkv_options *opts, const char * cluster_name, char **err)
{
    struct swarmkv *db;

    db = (struct swarmkv *)calloc(1, 1);

    return db;
}

long long swarmkv_caller_get_pending_commands(struct swarmkv *db)
{
    return 0;
}

int swarmkv_options_set_log_path(struct swarmkv_options *opts, const char *logpath)
{
    return 0;
}

int swarmkv_options_set_log_level(struct swarmkv_options *opts, int loglevel)
{
    return 0;
}

static void swarmkv_hincrby_cmd_func(char *cmd_str, swarmkv_on_reply_callback_t * cb, void *cb_arg)
{
    int profile_id;
    int priority;
    int value;
    char direction[5] = {0};
    enum shaping_packet_dir dir;
    struct swarmkv_reply *reply = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));

    sscanf(cmd_str, "HINCRBY tsg-shaping-%d priority-%d-%s %d", &profile_id, &priority, direction, &value);
    if (strncmp(direction, "in", 2) == 0) {
        dir = SHAPING_DIR_IN;
    } else {
        dir = SHAPING_DIR_OUT;
    }

    profile_priority_len[profile_id][priority][dir] += value;

    reply->type = SWARMKV_REPLY_INTEGER;
    cb(reply, cb_arg);

    free(reply);

    return;
}

static void swarmkv_hmget_cmd_func(char *cmd_str, swarmkv_on_reply_callback_t * cb, void *cb_arg)
{
    int profile_id;
    int priority[10];
    int ret;
    int priority_num;
    char direction[5] = {0};
    enum shaping_packet_dir dir;
    struct swarmkv_reply *reply = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));

    ret = sscanf(cmd_str, "HMGET tsg-shaping-%d priority-%d-%s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s", 
                    &profile_id, &priority[0], direction, &priority[1], &priority[2], &priority[3], &priority[4], &priority[5], &priority[6], &priority[7], &priority[8]);
    priority_num = ret - 1;

    if (strncmp(direction, "in", 2) == 0) {
        dir = SHAPING_DIR_IN;
    } else {
        dir = SHAPING_DIR_OUT;
    }

    reply->type = SWARMKV_REPLY_ARRAY;
    reply->n_element = priority_num;
    reply->elements = (struct swarmkv_reply**)calloc(priority_num, sizeof(struct swarmkv_reply*));
    for (int i = 0; i < priority_num; i++) {
        reply->elements[i] = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));
        reply->elements[i]->type = SWARMKV_REPLY_STRING;

        char tmp_str[128] = {0};
        sprintf(tmp_str, "%d", profile_priority_len[profile_id][priority[i]][dir]);
        reply->elements[i]->str = (char *)calloc(1, strlen(tmp_str));
        memcpy(reply->elements[i]->str, tmp_str, strlen(tmp_str));
        reply->elements[i]->len = strlen(tmp_str);
    }

    cb(reply, cb_arg);

    for(unsigned int i = 0; i < reply->n_element; i++) {
        if (reply->elements[i]) {
            free(reply->elements[i]->str);
            free(reply->elements[i]);
        }
    }
    free(reply->elements);
    free(reply);
    
}

void swarmkv_async_command(struct swarmkv *db, swarmkv_on_reply_callback_t * cb, void *cb_arg, const char *format, ...)
{
    char *cmd_str = NULL;
    char cmd_keyword[32] = {0};

    va_list ap;
    va_start(ap,format);
	vasprintf(&cmd_str, format, ap);
    va_end(ap);

    sscanf(cmd_str, "%31s %*s", cmd_keyword);
    if (strcmp(cmd_keyword, "HINCRBY") == 0) {
        swarmkv_hincrby_cmd_func(cmd_str, cb, cb_arg);
    } else if (strcmp(cmd_keyword, "HMGET") == 0) {
        swarmkv_hmget_cmd_func(cmd_str, cb, cb_arg);
    }

    free(cmd_str);

    return;
}

void swarmkv_tconsume(struct swarmkv * db, const char * key, size_t keylen, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg)
{
    int actual_tokens;
    struct stub_token_thread_arg thread_arg;
    struct swarmkv_reply reply;
    int profile_id;
    char direction[16] = {0};

    sscanf(key, "tsg-shaping-%d-%15s", &profile_id, direction);

    if (strncmp("bidirectional", direction, sizeof(direction)) == 0) {
        if (pf_curr_avl_token[profile_id].bidirection_limit_bandwidth == DEFAULT_AVALIABLE_TOKEN_PER_SEC) {
            actual_tokens = tokens;
        } else {
            actual_tokens = pf_curr_avl_token[profile_id].bidirection_limit_bandwidth >= tokens ? tokens : 0;
            pf_curr_avl_token[profile_id].bidirection_limit_bandwidth -= actual_tokens;
        }
    } else if (strncmp("incoming", direction, sizeof(direction)) == 0) {
        if (pf_curr_avl_token[profile_id].in_limit_bandwidth == DEFAULT_AVALIABLE_TOKEN_PER_SEC) {
            actual_tokens = tokens;
        } else {
            actual_tokens = pf_curr_avl_token[profile_id].in_limit_bandwidth >= tokens ? tokens : 0;
            pf_curr_avl_token[profile_id].in_limit_bandwidth -= actual_tokens;
        }
    } else {
        if (pf_curr_avl_token[profile_id].out_limit_bandwidth == DEFAULT_AVALIABLE_TOKEN_PER_SEC) {
            actual_tokens = tokens;
        } else {
            actual_tokens = pf_curr_avl_token[profile_id].out_limit_bandwidth >= tokens ? tokens : 0;
            pf_curr_avl_token[profile_id].out_limit_bandwidth -= actual_tokens;
        }
    }

    if (pf_async_times[profile_id] == 0) {
        for (unsigned int i = 0; i < pf_async_thread[profile_id].size(); i++) {
            stub_get_token_thread_func(&pf_async_thread[profile_id][i]);
        }
        pf_async_thread[profile_id].clear();
    }

    reply.integer = actual_tokens;
    reply.type = SWARMKV_REPLY_INTEGER;

    if (pf_async_times[profile_id] > 0) {
        pf_async_times[profile_id]--;

        thread_arg.profile_id = profile_id;
        thread_arg.reply = reply;
        thread_arg.cb = cb;
        thread_arg.cb_arg = cb_arg;

        pf_async_thread[profile_id].push_back(thread_arg);

    } else {
        cb(&reply, cb_arg);
    }

    return;
}

void swarmkv_ftconsume(struct swarmkv * db, const char * key, size_t keylen, const char * member, size_t member_len, long long weight, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg)
{
    swarmkv_tconsume(db, key, keylen, tokens, cb, cb_arg);
    return;
}

void swarmkv_btconsume(struct swarmkv * db, const char * key, size_t keylen, const char * member, size_t member_len, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg)
{
    return;
}

void swarmkv_close(struct swarmkv * db)
{
    if (db) {
        free(db);
    }
    return;
}

void swarmkv_caller_loop(struct swarmkv *db, int flags, struct timeval *tv)
{
    return;
}

int swarmkv_options_set_caller_thread_number(struct swarmkv_options *opts, int nr_caller_threads)
{
    return 0;
}

int swarmkv_options_set_worker_thread_number(struct swarmkv_options *opts, int nr_worker_threads)
{
    return 0;
}
/**********************************************/