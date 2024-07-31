#include <sys/queue.h>
#include "shaper.h"

#define OUT_ARG 
#define MAX_REF_PROFILE 8

#define STUB_APP_STATE_HOLD_PACKET 0x04

#define STUB_MAAT_SHAPING_RULE_TABLE_ID 0
#define STUB_MAAT_SHAPING_PROFILE_TABLE_ID 1

#define STUB_TIME_INC_FOR_PACKET 1000000
#define STUB_TIME_INC_FOR_HMGET 10000000

#define STUB_TEST_VSYS_ID 2333

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


void stub_set_token_bucket_avl_per_sec(int profile_id, unsigned int tokens, unsigned char direction, enum shaping_profile_limit_direction limit_direction);
void stub_refresh_token_bucket(int profile_id);
void stub_set_profile_type(int profile_id, enum shaping_profile_type type);
void stub_set_profile_limit_direction(int profile_id, enum shaping_profile_limit_direction limit_direction);
void stub_set_async_token_get_times(int profile_id, int times);

void stub_set_matched_shaping_rules(int rule_num, long long *rule_id, const int *priority, const int *profile_num, int profile_id[][MAX_REF_PROFILE]);
void stub_set_shaping_rule_dscp_value(int rule_id, int dscp_value);
void stub_set_shaping_rule_fair_factor(int rule_id, int fair_factor);
void stub_clear_matched_shaping_rules();

void stub_send_packet(struct stub_packet *packet);
struct stub_pkt_queue* stub_get_tx_queue();

int stub_AQM_drop_packet(int queue_len, unsigned long long income_time);

void stub_curr_time_ns_inc(unsigned long long time_ns);
void stub_curr_time_s_inc(int time_s);
unsigned long long stub_curr_time_ns_get();

void stub_init();
void dummy_swarmkv_init();

/*******************temporary for test******************************/
void stub_shaper_stat_send(int thread_seq);

void polling_entry(struct shaper *sp, struct shaping_stat *stat, struct shaping_thread_ctx *ctx);
/*******************temporary for test******************************/
