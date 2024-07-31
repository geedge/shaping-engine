#pragma once

#include <netinet/in.h>
#include <librdkafka/rdkafka.h>
#include <fieldstat/fieldstat_easy.h>
#include "uthash.h"

enum shaping_packet_dir {
    SHAPING_DIR_IN = 0,
    SHAPING_DIR_OUT,
    SHAPING_DIR_MAX
};

enum shaping_stat_tags_index {
    TAG_VSYS_ID_IDX = 0,
    TAG_RULE_ID_IDX,
    TAG_PROFILE_ID_IDX,
    TAG_PRIORITY_IDX,
    TAG_PROFILE_TYPE_IDX,
    TAG_IDX_MAX
};

enum shaping_stat_column_index {
    IN_QUEUE_LEN_IDX = 0,
    OUT_QUEUE_LEN_IDX,
    IN_PKTS_IDX,
    IN_BYTES_IDX,
    IN_DROP_PKTS_IDX,
    OUT_PKTS_IDX,
    OUT_BYTES_IDX,
    OUT_DROP_PKTS_IDX,
    STAT_COLUNM_IDX_MAX
};

struct shaping_stat_for_profile_dir {
    unsigned long long pkts;
    unsigned long long bytes;
    unsigned long long drop_pkts;
    unsigned long long max_latency;
    long long queue_len;
};
struct shaping_stat_for_profile {
    struct shaping_stat_for_profile_dir in;
    struct shaping_stat_for_profile_dir out;
    long long priority_queue_len[SHAPING_DIR_MAX];
};

struct shaping_stat {
    struct fieldstat_easy *instance;
    rd_kafka_t *kafka_handle; 
    rd_kafka_topic_t *topic_rkt;
    int output_interval_s;
    int latency_histogram_id;
    unsigned int column_ids[STAT_COLUNM_IDX_MAX];
};

void shaper_stat_destroy(struct shaping_stat *stat);
struct shaping_stat* shaper_stat_init(int thread_num);

void shaper_stat_queueing_pkt_inc(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int thread_id);
void shaper_stat_queueing_pkt_dec(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int thread_id);
void shaper_stat_queueing_pkt_inc_for_rule(struct shaping_rule_info *rule, unsigned char direction, int thread_id);
void shaper_stat_queueing_pkt_dec_for_rule(struct shaping_rule_info *rule, unsigned char direction, int thread_id);
void shaper_stat_forward_inc(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int pkt_len, int thread_id);
void shaper_stat_forward_all_rule_inc(struct shaping_stat *stat, struct shaping_flow *sf, unsigned char direction, int pkt_len, int thread_id);
void shaper_stat_drop_inc(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int thread_id);
void shaper_stat_max_latency_update(struct shaping_stat_for_profile *profile_stat, unsigned char direction, unsigned long long latency, int thread_id);

void shaper_stat_refresh(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int force);
void shaper_stat_priority_queue_len_refresh_all(struct shaping_thread_ctx *ctx, struct shaping_profile_hash_node *profile_hash_node);

void shaper_stat_output(struct shaping_stat *stat);