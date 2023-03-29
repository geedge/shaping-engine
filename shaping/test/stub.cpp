#include <MESA/stream.h>
#include <MESA/swarmkv.h>
#include <MESA/maat.h>

#include <marsio.h>
#include <vector>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "stub.h"

using namespace std;

#define DEFAULT_AVALIABLE_TOKEN_PER_SEC -1
#define MAX_STUB_TEST_SESSION_NUM 2
#define MAX_STUB_RULE_NUM 8
#define MAX_STUB_PROFILE_NUM 8

struct shaping_rule {
    int id;
    int priority;
    int primary_pf_id;
    int borrow_pf_id_array[MAX_STUB_PROFILE_NUM];
    int borrow_pf_num;
    int fair_factor;
    int ref_cnt;
};

struct shaping_profile {
    int id;
    int split;
    int host_fairness;
    int in_limit_bandwidth;
    int out_limit_bandwidth;
    int valid;
    int ref_cnt;
};

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

static unsigned long long curr_time = 1;

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
        pf_array[profile_id].in_limit_bandwidth = tokens;
        pf_curr_avl_token[profile_id].in_limit_bandwidth = tokens;
    } else {
        pf_array[profile_id].out_limit_bandwidth = tokens;
        pf_curr_avl_token[profile_id].out_limit_bandwidth = tokens;
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

void swarmkv_async_command(struct swarmkv *db, swarmkv_on_reply_callback_t * cb, void *cb_arg, const char *format, ...)
{
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

void swarmkv_close(struct swarmkv * db)
{
    if (db) {
        free(db);
    }
    return;
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

void *maat_plugin_table_get_ex_data(struct maat *instance, int table_id, const char *key)
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
/***************************************************/