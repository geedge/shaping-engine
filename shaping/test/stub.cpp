#include <MESA/stream.h>
#include <MESA/swarmkv.h>
#include <MESA/maat.h>

#include <cstdio>
#include <marsio.h>
#include <vector>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "stub.h"
#include "shaper_maat.h"

using namespace std;

#define DEFAULT_AVALIABLE_TOKEN_PER_SEC -1
#define MAX_STUB_TEST_SESSION_NUM 2
#define MAX_STUB_RULE_NUM 8
#define MAX_STUB_PROFILE_NUM 8

struct stub_matched_rules {
    struct shaping_rule rules[MAX_STUB_RULE_NUM];
    int rule_num;
};

struct stub_token_thread_arg {
    int profile_id;
    struct swarmkv_reply reply;
    swarmkv_on_reply_callback_t *cb;
    void *cb_arg;
};

struct stub_avaliable_token {
    int in_limit_bandwidth;
    int out_limit_bandwidth;
};

struct stub_pkt_queue tx_queue;

static struct stub_avaliable_token pf_curr_avl_token[MAX_STUB_PROFILE_NUM];
static int pf_async_times[MAX_STUB_PROFILE_NUM];
vector<struct stub_token_thread_arg> pf_async_thread[MAX_STUB_PROFILE_NUM];
struct stub_matched_rules matched_rules;
struct shaping_profile pf_array[MAX_STUB_PROFILE_NUM];
static int profile_priority_len[MAX_STUB_PROFILE_NUM][10] = {{0}};

static unsigned long long curr_time = 2000000000;//2s

void * stub_get_token_thread_func(void *data)
{
    struct stub_token_thread_arg *thread_arg;

    thread_arg = (struct stub_token_thread_arg*)data;

    thread_arg->cb(&thread_arg->reply, thread_arg->cb_arg);

    return NULL;
}

void stub_set_token_bucket_avl_per_sec(int profile_id, unsigned int tokens, unsigned char direction)
{
    if (direction == SHAPING_DIR_IN) {
        pf_array[profile_id].in_limit_bandwidth = tokens * 8;
        pf_curr_avl_token[profile_id].in_limit_bandwidth = tokens * 8;
    } else {
        pf_array[profile_id].out_limit_bandwidth = tokens * 8;
        pf_curr_avl_token[profile_id].out_limit_bandwidth = tokens * 8;
    }

    return;
}

void stub_refresh_token_bucket(int profile_id)
{
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

void stub_set_matched_shaping_rules(int rule_num, long long *rule_id, const int *priority, const int *profile_num, int profile_id[][MAX_REF_PROFILE])
{
    struct shaping_rule *rules;
    int i, j;
    int id;

    rules = matched_rules.rules;

    for (i = 0; i < rule_num; i++) {
        id = rule_id[i];
        assert(id < MAX_STUB_RULE_NUM);

        rules[id].vsys_id = STUB_TEST_VSYS_ID;
        rules[id].id = id;
        rules[id].primary_pf_id = profile_id[i][0];
        rules[id].borrow_pf_num = profile_num[i] - 1;
        rules[id].priority = priority[i];
        for (j = 1; j < profile_num[i]; j++) {
            rules[id].borrow_pf_id_array[j - 1] = profile_id[i][j];
        }
    }

    matched_rules.rule_num = rule_num;

    return;
}

void stub_clear_matched_shaping_rules()
{
    memset(&matched_rules, 0, sizeof(struct stub_matched_rules));

    return;
}

void stub_send_packet(struct stub_packet *packet)
{
    struct stub_packet_node *pkt_node = NULL;

    pkt_node = (struct stub_packet_node*)calloc(1, sizeof(struct stub_packet_node));
    if (!pkt_node) {
        return;
    }

    pkt_node->raw_packet = packet;
    TAILQ_INSERT_TAIL(&tx_queue, pkt_node, node);

    return;
}

struct stub_pkt_queue* stub_get_tx_queue()
{
    return &tx_queue;
}

void stub_curr_time_inc(unsigned long long time_ns)
{
    curr_time += time_ns;

    return;
}

unsigned long long stub_curr_time_get()
{
    return curr_time;
}

void stub_init()
{
    int i;

    TAILQ_INIT(&tx_queue);
    memset(&matched_rules, 0, sizeof(struct stub_matched_rules));
    memset(&pf_array, 0, MAX_STUB_PROFILE_NUM * sizeof(struct shaping_profile));

    for (i = 0; i < MAX_STUB_RULE_NUM; i++) {
        matched_rules.rules[i].ref_cnt = 1;
    }

    for (i = 0; i < MAX_STUB_PROFILE_NUM; i++) {
        pf_curr_avl_token[i].in_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_curr_avl_token[i].out_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_array[i].id = i;
        pf_array[i].in_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_array[i].out_limit_bandwidth = DEFAULT_AVALIABLE_TOKEN_PER_SEC;
        pf_array[i].ref_cnt = 1;
        pf_async_times[i] = 0;
    }
    return;
}

/****************stub of time.h****************/

int clock_gettime (clockid_t __clock_id, struct timespec *__tp)
{
    __tp->tv_sec = 0;
    __tp->tv_nsec = curr_time;

    return 0;
}

/**********************************************/


/**************stub of swarmkv*****************/
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
    struct swarmkv_reply *reply = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));

    sscanf(cmd_str, "HINCRBY tsg-shaping-%d priority-%d %d", &profile_id, &priority, &value);
    profile_priority_len[profile_id][priority] += value;

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
    struct swarmkv_reply *reply = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));

    ret = sscanf(cmd_str, "HMGET tsg-shaping-%d priority-%d priority-%d priority-%d priority-%d priority-%d priority-%d priority-%d priority-%d priority-%d", 
                    &profile_id, &priority[0], &priority[1], &priority[2], &priority[3], &priority[4], &priority[5], &priority[6], &priority[7], &priority[8]);
    priority_num = ret - 1;

    reply->type = SWARMKV_REPLY_ARRAY;
    reply->n_element = priority_num;
    reply->elements = (struct swarmkv_reply**)calloc(priority_num, sizeof(struct swarmkv_reply*));
    for (int i = 0; i < priority_num; i++) {
        reply->elements[i] = (struct swarmkv_reply*)calloc(1, sizeof(struct swarmkv_reply));
        reply->elements[i]->type = SWARMKV_REPLY_STRING;

        char tmp_str[128] = {0};
        sprintf(tmp_str, "%d", profile_priority_len[profile_id][priority[i]]);
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

    if (strncmp("incoming", direction, sizeof(direction)) == 0) {
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

/*************stub of maat*********************/
struct maat_options *maat_options_new(void)
{
    return NULL;
}

int maat_options_set_instance_name(struct maat_options *opts, const char *instance_name)
{
    return 0;
}

int maat_options_set_json_file(struct maat_options *opts, const char *json_filename)
{
    return 0;
}

int maat_options_set_redis(struct maat_options *opts, const char *redis_ip, uint16_t redis_port, int redis_db)
{
    return 0;
}

struct maat *maat_new(struct maat_options *opts, const char *table_info_path)
{
    struct maat *instance = NULL;

    instance = (struct maat *)calloc(1, 1);

    return instance;
}

void maat_options_free(struct maat_options *opts)
{
    return;
}

int maat_get_table_id(struct maat *instance, const char *table_name)
{
    if (strcmp(table_name, "TRAFFIC_SHAPING_COMPILE") == 0) {
        return STUB_MAAT_SHAPING_RULE_TABLE_ID;
    } else {
        return STUB_MAAT_SHAPING_PROFILE_TABLE_ID;
    }
}

int maat_plugin_table_ex_schema_register(struct maat *instance, const char *table_name,
                                         maat_ex_new_func_t *new_func,
                                         maat_ex_free_func_t *free_func,
                                         maat_ex_dup_func_t *dup_func,
                                         long argl, void *argp)
{
    return 0;
}

void maat_free(struct maat *instance)
{
    if (instance) {
        free(instance);
    }
    return;
}

void *maat_plugin_table_get_ex_data(struct maat *instance, int table_id, const char *key, size_t key_len)
{
    int rule_id;
    int profile_id;

    if (table_id == STUB_MAAT_SHAPING_RULE_TABLE_ID) {
        rule_id = *(int*)key;
        matched_rules.rules[rule_id].ref_cnt++;
        return &matched_rules.rules[rule_id];
    } else {
        profile_id = atoi(key);
        pf_array[profile_id].ref_cnt++;
        return &pf_array[profile_id];
    }
}
/**********************************************/

/***************stub of marsio**********************/
struct mr_instance * marsio_create()
{
    struct mr_instance *instance = NULL;

    instance = (struct mr_instance *)calloc(1, 1);

    return instance;
}

int marsio_destory(struct mr_instance * instance)
{
    if (instance) {
        free(instance);
    }
    return 0;
}

struct mr_sendpath * marsio_sendpath_create_by_vdev(struct mr_vdev * dest_device)
{
    struct mr_sendpath *sendpath = NULL;

    sendpath = (struct mr_sendpath*)calloc(1, 1);
    
    return sendpath;
}
void marsio_sendpath_destory(struct mr_sendpath * sendpath)
{
    if (sendpath) {
        free(sendpath);
    }

    return;
}

struct mr_vdev * marsio_open_device(struct mr_instance * instance, const char * devsym, unsigned int nr_rxstream, unsigned int nr_txstream)
{
    struct mr_vdev *vdev = NULL;

    vdev = (struct mr_vdev *)calloc(1, 1);

    return vdev;
}

void marsio_close_device(struct mr_vdev * vdev)
{
    if (vdev) {
        free(vdev);
    }

    return;
}

int marsio_send_burst(struct mr_sendpath * sendpath, queue_id_t qid, marsio_buff_t * mbufs[], int nr_mbufs)
{
    struct stub_packet_node *pkt_node = NULL;

    assert(nr_mbufs == 1);

    pkt_node = (struct stub_packet_node*)calloc(1, sizeof(struct stub_packet_node));
    if (!pkt_node) {
        return 0;
    }

    pkt_node->raw_packet = (struct stub_packet *)mbufs[0];
    TAILQ_INSERT_TAIL(&tx_queue, pkt_node, node);

    return 0;

}

void marsio_buff_set_ctrlbuf(marsio_buff_t * m)
{
    return;
}

void marsio_buff_free(struct mr_instance * instance, marsio_buff_t * marsio_buff[], unsigned int nr_mbufs, int socket_id, int thread_id)
{
    assert(nr_mbufs == 1);
    free(marsio_buff[0]);

    return;
}

int marsio_recv_burst(struct mr_vdev * vdev, queue_id_t qid, marsio_buff_t * mbufs[], int nr_mbufs)
{
    return 0;
}

int marsio_buff_is_ctrlbuf(marsio_buff_t * m)
{
    return 0;
}

int marsio_option_set(struct mr_instance * instance, marsio_opt_type_t opt_type, void * opt, size_t sz_opt)
{
    return 0;
}

int marsio_init(struct mr_instance * instance, const char * appsym)
{
    return 0;
}

int marsio_buff_get_metadata(marsio_buff_t * m, enum mr_buff_metadata_type type, void * data, unsigned int sz_data)
{
    return 0;
}

uint32_t marsio_buff_datalen(marsio_buff_t * m)
{
    return 0;
}

char * marsio_buff_mtod(marsio_buff_t * m)
{
    return NULL;
}

int marsio_buff_malloc_global(struct mr_instance * instance, marsio_buff_t * marsio_buff[], unsigned int nr_mbufs, int socket_id, int thread_id)
{
    return 0;
}

char * marsio_buff_append(marsio_buff_t * m, uint16_t len)
{
    return NULL;
}

int marsio_buff_set_metadata(marsio_buff_t * m, enum mr_buff_metadata_type type, void * data, unsigned int sz_data)
{
    return 0;
}

int marsio_buff_set_sid_list(marsio_buff_t * m, sid_t * slist, uint8_t sz_slist)
{
    return 0;
}
/***************************************************/