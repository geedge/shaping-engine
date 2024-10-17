#include <sys/queue.h>
#include "shaper.h"
#include "shaper_maat.h"

#define OUT_ARG 
#define MAX_REF_PROFILE 8

#define STUB_APP_STATE_HOLD_PACKET 0x04

#define STUB_TIME_INC_FOR_PACKET 1000000
#define STUB_TIME_INC_FOR_HMGET 10000000

#define STUB_TEST_VSYS_ID 2333

#define AVALIABLE_TOKEN_UNLIMITED -1

struct stub_matched_rule {
    UT_hash_handle hh;
    struct shaping_rule rule;
};

struct stub_avaliable_token {
    int in_limit_bandwidth;
    int out_limit_bandwidth;
    int bidirection_limit_bandwidth;
};

struct stub_shaping_profile {
    struct shaping_profile profile;
    struct stub_avaliable_token avaliable_token;
    UT_hash_handle hh;
};

struct stub_packet {
    unsigned char direction;
    unsigned char pure_control;
    unsigned int length;
    struct shaping_flow *flow;
};

struct stub_packet_node {
    struct stub_packet *raw_packet;
    TAILQ_ENTRY(stub_packet_node) node;
};
TAILQ_HEAD(stub_pkt_queue, stub_packet_node);


void stub_set_token_bucket_avl_per_sec(const char *profile_uuid_str, unsigned int tokens, unsigned char direction, enum shaping_profile_limit_direction limit_direction);
void stub_refresh_token_bucket(const char *profile_uuid_str);
void stub_set_profile_type(const char *profile_uuid_str, enum shaping_profile_type type);
void stub_set_profile_limit_direction(const char *profile_uuid_str, enum shaping_profile_limit_direction limit_direction);

void stub_set_matched_shaping_rules(int rule_num, const char *rule_uuid_str[], const int *priority, const int *profile_num, const char *profile_uuid_str[][MAX_REF_PROFILE]);
void stub_set_shaping_rule_dscp_value(const char *rule_uuid_str, int dscp_value);
void stub_set_shaping_rule_fair_factor(const char *rule_uuid_str, int fair_factor);
void stub_clear_resource();
void stub_swarmkv_clear_resource();

void stub_send_packet(struct stub_packet *packet);
struct stub_pkt_queue* stub_get_tx_queue();

int stub_AQM_drop_packet(int queue_len, unsigned long long income_time);

void stub_curr_time_ns_inc(unsigned long long time_ns);
void stub_curr_time_s_inc(int time_s);
unsigned long long stub_curr_time_ns_get();

void stub_init();

/*******************temporary for test******************************/
void stub_shaper_stat_send(int thread_seq);

void polling_entry(struct shaper *sp, struct shaping_stat *stat, struct shaping_thread_ctx *ctx);
/*******************temporary for test******************************/
