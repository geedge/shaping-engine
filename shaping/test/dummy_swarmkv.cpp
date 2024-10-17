#include <stdlib.h>
#include <stdarg.h>
#include <vector>
#include <MESA/swarmkv.h>

#include "shaper.h"
#include "shaper_maat.h"
#include "stub.h"

using namespace std;

struct profile_priority_queue_len {
    uuid_t profile_uuid;
    int priority_queue_len[SHAPING_PRIORITY_NUM_MAX][SHAPING_DIR_MAX];
    UT_hash_handle hh;
};

struct profile_priority_queue_len *profile_priority_queue_len_hash = NULL;
extern struct stub_shaping_profile *profiles_hash;

void stub_set_token_bucket_avl_per_sec(const char *profile_uuid_str, unsigned int tokens, unsigned char direction, enum shaping_profile_limit_direction limit_direction)
{
    uuid_t profile_uuid;
    struct stub_shaping_profile *stub_profile = NULL;
    unsigned token_bits;

    uuid_parse(profile_uuid_str, profile_uuid);
    HASH_FIND(hh, profiles_hash, profile_uuid, sizeof(uuid_t), stub_profile);
    if (!stub_profile) {
        stub_profile = (struct stub_shaping_profile*)calloc(1, sizeof(struct stub_shaping_profile));
        uuid_copy(stub_profile->profile.uuid, profile_uuid);
        HASH_ADD(hh, profiles_hash, profile.uuid, sizeof(uuid_t), stub_profile);
    }

    stub_profile->profile.limit_direction = limit_direction;

    if (tokens == AVALIABLE_TOKEN_UNLIMITED) {
        token_bits = tokens;
    } else {
        token_bits = tokens * 8;
    }

    if (limit_direction == PROFILE_LIMIT_DIRECTION_BIDIRECTION) {
        stub_profile->profile.bidirection_limit_bandwidth = token_bits;
        stub_profile->avaliable_token.bidirection_limit_bandwidth = token_bits;
    } else {
        if (direction == SHAPING_DIR_IN) {
            stub_profile->profile.in_limit_bandwidth = token_bits;
            stub_profile->avaliable_token.in_limit_bandwidth = token_bits;
        } else {
            stub_profile->profile.out_limit_bandwidth = token_bits;
            stub_profile->avaliable_token.out_limit_bandwidth = token_bits;
        }
    }

    return;
}

void stub_refresh_token_bucket(const char *profile_uuid_str)
{
    uuid_t profile_uuid;
    struct stub_shaping_profile *stub_profile = NULL;

    uuid_parse(profile_uuid_str, profile_uuid);
    HASH_FIND(hh, profiles_hash, profile_uuid, sizeof(uuid_t), stub_profile);
    if (!stub_profile) {
        return;
    }

    if (stub_profile->profile.limit_direction == PROFILE_LIMIT_DIRECTION_BIDIRECTION) {
        stub_profile->avaliable_token.bidirection_limit_bandwidth = stub_profile->profile.bidirection_limit_bandwidth;
    } else {
        stub_profile->avaliable_token.in_limit_bandwidth = stub_profile->profile.in_limit_bandwidth;
        stub_profile->avaliable_token.out_limit_bandwidth = stub_profile->profile.out_limit_bandwidth;
    }
    
    return;
}

void stub_swarmkv_clear_resource()
{
    struct profile_priority_queue_len *node, *tmp = NULL;

    HASH_ITER(hh, profile_priority_queue_len_hash, node, tmp) {
        HASH_DEL(profile_priority_queue_len_hash, node);
        free(node);
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
    uuid_t profile_uuid;
    char uuid_str[UUID_STR_LEN] = {0};
    int priority;
    int value;
    char direction[5] = {0};
    enum shaping_packet_dir dir;
    struct swarmkv_reply *reply = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));

    sscanf(cmd_str, "HINCRBY tsg-shaping-%s priority-%d-%s %d", uuid_str, &priority, direction, &value);
    uuid_parse(uuid_str, profile_uuid);
    if (strncmp(direction, "in", 2) == 0) {
        dir = SHAPING_DIR_IN;
    } else {
        dir = SHAPING_DIR_OUT;
    }

    struct profile_priority_queue_len *node = NULL;
    HASH_FIND(hh, profile_priority_queue_len_hash, profile_uuid, sizeof(uuid_t), node);
    if (!node) {
        node = (struct profile_priority_queue_len*)calloc(1, sizeof(struct profile_priority_queue_len));
        uuid_copy(node->profile_uuid, profile_uuid);
        HASH_ADD(hh, profile_priority_queue_len_hash, profile_uuid, sizeof(uuid_t), node);
    }
    node->priority_queue_len[priority][dir] += value;

    reply->type = SWARMKV_REPLY_INTEGER;
    cb(reply, cb_arg);

    free(reply);

    return;
}

static void swarmkv_hmget_cmd_func(char *cmd_str, swarmkv_on_reply_callback_t * cb, void *cb_arg)
{
    uuid_t profile_uuid;
    char uuid_str[UUID_STR_LEN] = {0};
    int priority[10];
    int ret;
    int priority_num;
    char direction[5] = {0};
    enum shaping_packet_dir dir;
    struct swarmkv_reply *reply = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));

    ret = sscanf(cmd_str, "HMGET tsg-shaping-%s priority-%d-%s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s priority-%d-%*s", 
                    uuid_str, &priority[0], direction, &priority[1], &priority[2], &priority[3], &priority[4], &priority[5], &priority[6], &priority[7], &priority[8]);
    priority_num = ret - 1;

    uuid_parse(uuid_str, profile_uuid);
    struct profile_priority_queue_len *node = NULL;
    HASH_FIND(hh, profile_priority_queue_len_hash, profile_uuid, sizeof(uuid_t), node);
    if (!node) {
        node = (struct profile_priority_queue_len*)calloc(1, sizeof(struct profile_priority_queue_len));
        uuid_copy(node->profile_uuid, profile_uuid);
        HASH_ADD(hh, profile_priority_queue_len_hash, profile_uuid, sizeof(uuid_t), node);
    }

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
        sprintf(tmp_str, "%d", node->priority_queue_len[priority[i]][dir]);
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
    struct swarmkv_reply reply;
    char direction[16] = {0};
    char uuid_str[UUID_STR_LEN] = {0};
    uuid_t profile_uuid;
    struct stub_shaping_profile *stub_profile = NULL;

    sscanf(key, "tsg-shaping-%36s-%15s", uuid_str, direction);
    uuid_parse(uuid_str, profile_uuid);
    HASH_FIND(hh, profiles_hash, profile_uuid, sizeof(uuid_t), stub_profile);
    if (!stub_profile) {
        return;
    }

    if (strncmp("bidirectional", direction, sizeof(direction)) == 0) {
        if (stub_profile->avaliable_token.bidirection_limit_bandwidth == AVALIABLE_TOKEN_UNLIMITED) {
            actual_tokens = tokens;
        } else {
            actual_tokens = stub_profile->avaliable_token.bidirection_limit_bandwidth >= tokens ? tokens : 0;
            stub_profile->avaliable_token.bidirection_limit_bandwidth -= actual_tokens;
        }
    } else if (strncmp("incoming", direction, sizeof(direction)) == 0) {
        if (stub_profile->avaliable_token.in_limit_bandwidth == AVALIABLE_TOKEN_UNLIMITED) {
            actual_tokens = tokens;
        } else {
            actual_tokens = stub_profile->avaliable_token.in_limit_bandwidth >= tokens ? tokens : 0;
            stub_profile->avaliable_token.in_limit_bandwidth -= actual_tokens;
        }
    } else {
        if (stub_profile->avaliable_token.out_limit_bandwidth == AVALIABLE_TOKEN_UNLIMITED) {
            actual_tokens = tokens;
        } else {
            actual_tokens = stub_profile->avaliable_token.out_limit_bandwidth >= tokens ? tokens : 0;
            stub_profile->avaliable_token.out_limit_bandwidth -= actual_tokens;
        }
    }

    reply.integer = actual_tokens;
    reply.type = SWARMKV_REPLY_INTEGER;

    cb(&reply, cb_arg);

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