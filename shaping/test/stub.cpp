#include <MESA/stream.h>
#include <MESA/maat.h>

#include <cstdio>
#include <cstring>
#include <marsio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "shaper.h"
#include "shaper_stat.h"
#include "stub.h"
#include "shaper_maat.h"
#include "log.h"

#define MAX_STUB_TEST_SESSION_NUM 2
#define MAX_STUB_RULE_NUM 8
#define MAX_STUB_PROFILE_NUM 8

struct stub_matched_rules {
    struct shaping_rule rules[MAX_STUB_RULE_NUM];
    int rule_num;
};


struct stub_pkt_queue tx_queue;
struct stub_matched_rules matched_rules;
struct shaping_profile pf_array[MAX_STUB_PROFILE_NUM];

void stub_set_profile_type(int profile_id, enum shaping_profile_type type)
{
    pf_array[profile_id].type = type;
    return;
}

void stub_set_profile_limit_direction(int profile_id, enum shaping_profile_limit_direction limit_direction)
{
    pf_array[profile_id].limit_direction = limit_direction;
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

void stub_set_shaping_rule_dscp_value(int rule_id, int dscp_value)
{
    matched_rules.rules[rule_id].dscp_enable = 1;
    matched_rules.rules[rule_id].dscp_value = dscp_value;
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

void stub_init()
{
    LOG_INIT("./conf/zlog.conf");

    TAILQ_INIT(&tx_queue);
    memset(&matched_rules, 0, sizeof(struct stub_matched_rules));
    
    return;
}


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

int maat_options_set_logger(struct maat_options *opts, const char *log_path, enum log_level level)
{
    return 0;
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
        return &matched_rules.rules[rule_id];
    } else {
        profile_id = atoi(key);
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