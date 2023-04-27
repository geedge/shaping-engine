#include <netinet/in.h>
#include "uthash.h"
#include <fieldstat.h>

enum shaping_stat_tags_index {
    TAG_VSYS_ID_IDX = 0,
    TAG_RULE_ID_IDX,
    TAG_PROFILE_ID_IDX,
    TAG_PRIORITY_IDX,
    TAG_PROFILE_TYPE_IDX,
    TAG_IDX_MAX
};

enum shaping_stat_column_index {
    QUEUEING_SESSIONS_IDX = 0,
    IN_MAX_LATENCY_IDX,
    IN_QUEUE_LEN_IDX,
    OUT_MAX_LATENCY_IDX,
    OUT_QUEUE_LEN_IDX,
    IN_PKTS_IDX,
    IN_BYTES_IDX,
    IN_DROP_PKTS_IDX,
    OUT_PKTS_IDX,
    OUT_BYTES_IDX,
    OUT_DROP_PKTS_IDX,
    STAT_COLUNM_IDX_MAX
};

struct shaping_stat {
    struct fieldstat_dynamic_instance *instance;
    int table_id;
    unsigned int column_ids[STAT_COLUNM_IDX_MAX];
};

void shaper_stat_destroy(struct shaping_stat *stat);
struct shaping_stat* shaper_stat_init(int thread_num);
void shaper_stat_tags_init();

void shaper_stat_queueing_pkt_inc(struct shaping_stat *stat, int vsys_id, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id);
void shaper_stat_queueing_pkt_dec(struct shaping_stat *stat, int vsys_id, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id);
void shaper_stat_forward_inc(struct shaping_stat *stat, int vsys_id, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id);
void shaper_stat_forward_all_rule_inc(struct shaping_stat *stat, struct shaping_flow *sf, unsigned char direction, int pkt_len, int thread_id);
void shaper_stat_drop_inc(struct shaping_stat *stat, int vsys_id, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int thread_id);
void shaper_stat_queueing_session_inc(struct shaping_stat *stat, int vsys_id, int rule_id, int profile_id, int priority, int profile_type, int thread_id);
void shaper_stat_queueing_session_dec(struct shaping_stat *stat, int vsys_id, int rule_id, int profile_id, int priority, int profile_type, int thread_id);
void shaper_stat_max_latency_update(struct shaping_stat *stat, int vsys_id, int rule_id, int profile_id, int priority, unsigned char direction, unsigned long long latency, int profile_type, int thread_id);