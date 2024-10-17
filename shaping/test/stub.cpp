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
#include "log.h"
#include "uthash.h"

#define MAX_STUB_TEST_SESSION_NUM 2

struct stub_pkt_queue tx_queue;
struct stub_matched_rule *rules_hash = NULL;
struct stub_shaping_profile *profiles_hash = NULL;

void stub_set_profile_type(const char *profile_uuid_str, enum shaping_profile_type type)
{
    uuid_t profile_uuid;
    struct stub_shaping_profile *stub_profile = NULL;

    uuid_parse(profile_uuid_str, profile_uuid);
    HASH_FIND(hh, profiles_hash, profile_uuid, sizeof(uuid_t), stub_profile);
    if (!stub_profile) {
        stub_profile = (struct stub_shaping_profile*)calloc(1, sizeof(struct stub_shaping_profile));
        uuid_copy(stub_profile->profile.uuid, profile_uuid);
        HASH_ADD(hh, profiles_hash, profile.uuid, sizeof(uuid_t), stub_profile);
    }
    
    stub_profile->profile.type = type;

    return;
}

void stub_set_profile_limit_direction(const char *profile_uuid_str, enum shaping_profile_limit_direction limit_direction)
{
    uuid_t profile_uuid;
    struct stub_shaping_profile *stub_profile = NULL;

    uuid_parse(profile_uuid_str, profile_uuid);
    HASH_FIND(hh, profiles_hash, profile_uuid, sizeof(uuid_t), stub_profile);
    if (!stub_profile) {
        stub_profile = (struct stub_shaping_profile*)calloc(1, sizeof(struct stub_shaping_profile));
        uuid_copy(stub_profile->profile.uuid, profile_uuid);
        HASH_ADD(hh, profiles_hash, profile.uuid, sizeof(uuid_t), stub_profile);
    }

    stub_profile->profile.limit_direction = limit_direction;

    return;
}

void stub_set_matched_shaping_rules(int rule_num, const char *rule_uuid_str[], const int *priority, const int *profile_num, const char *profile_uuid_str[][MAX_REF_PROFILE])
{
    int i, j;

    for (i = 0; i < rule_num; i++) {
        struct stub_matched_rule *stub_matched_rule = NULL;
        uuid_t rule_uuid;
        uuid_parse(rule_uuid_str[i], rule_uuid);
        
        HASH_FIND(hh, rules_hash, rule_uuid, sizeof(uuid_t), stub_matched_rule);
        if (stub_matched_rule) {
            continue;
        }

        stub_matched_rule = (struct stub_matched_rule*)calloc(1, sizeof(struct stub_matched_rule));

        stub_matched_rule->rule.vsys_id = STUB_TEST_VSYS_ID;
        uuid_copy(stub_matched_rule->rule.uuid, rule_uuid);
        uuid_parse(profile_uuid_str[i][0], stub_matched_rule->rule.primary_pf_uuid);
        stub_matched_rule->rule.borrow_pf_num = profile_num[i] - 1;
        stub_matched_rule->rule.priority = priority[i];
        for (j = 1; j < profile_num[i]; j++) {
            uuid_parse(profile_uuid_str[i][j], stub_matched_rule->rule.borrow_pf_uuid_array[j - 1]);
        }

        HASH_ADD(hh, rules_hash, rule.uuid, sizeof(uuid_t), stub_matched_rule);
    }

    return;
}

void stub_set_shaping_rule_dscp_value(const char *rule_uuid_str, int dscp_value)
{
    uuid_t rule_uuid;
    struct stub_matched_rule *stub_matched_rule = NULL;

    uuid_parse(rule_uuid_str, rule_uuid);
    HASH_FIND(hh, rules_hash, rule_uuid, sizeof(uuid_t), stub_matched_rule);
    if (stub_matched_rule) {
        stub_matched_rule->rule.dscp_enable = 1;
        stub_matched_rule->rule.dscp_value = dscp_value;
    }

    return;
}

void stub_set_shaping_rule_fair_factor(const char *rule_uuid_str, int fair_factor)
{
    uuid_t rule_uuid;
    struct stub_matched_rule *stub_matched_rule = NULL;

    uuid_parse(rule_uuid_str, rule_uuid);
    HASH_FIND(hh, rules_hash, rule_uuid, sizeof(uuid_t), stub_matched_rule);
    if (stub_matched_rule) {
        stub_matched_rule->rule.fair_factor = fair_factor;
    }
    
    return;
}

void stub_clear_resource()
{
    struct stub_matched_rule *stub_matched_rule, *tmp = NULL;

    HASH_ITER(hh, rules_hash, stub_matched_rule, tmp) {
        HASH_DEL(rules_hash, stub_matched_rule);
        free(stub_matched_rule);
    }

    struct stub_shaping_profile *stub_profile, *tmp_profile = NULL;

    HASH_ITER(hh, profiles_hash, stub_profile, tmp_profile) {
        HASH_DEL(profiles_hash, stub_profile);
        free(stub_profile);
    }

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

void *maat_plugin_table_get_ex_data(struct maat *instance, const char *table_name, const char *key, size_t key_len)
{
    uuid_t rule_uuid;
    uuid_t profile_uuid;

    if (strcmp(table_name, "TRAFFIC_SHAPING_RULE") == 0) {
        struct stub_matched_rule *matched_rule = NULL;
        uuid_parse(key, rule_uuid);
        HASH_FIND(hh, rules_hash, rule_uuid, sizeof(uuid_t), matched_rule);
        return &matched_rule->rule;
    } else {
        struct stub_shaping_profile *stub_profile = NULL;
        uuid_parse(key, profile_uuid);
        HASH_FIND(hh, profiles_hash, profile_uuid, sizeof(uuid_t), stub_profile);
        return &stub_profile->profile;
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