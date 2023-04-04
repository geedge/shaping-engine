#include <fieldstat.h>

enum shaping_global_stat_dir {
    SHAPING_GLOBAL_STAT_RX,
    SHAPING_GLOBAL_STAT_TX
};

enum shaping_global_stat_column_index {
    RX_PKTS_IDX = 0,
    RX_BYTES_IDX,
    TX_PKTS_IDX,
    TX_BYTES_IDX,
    QUEUEING_PKTS_IDX,
    QUEUEING_BYTES_IDX,
    DROP_PKTS_IDX,
    DROP_BYTES_IDX,
    HIT_POLICY_PKTS,
    HIT_POLICY_BYTES,
    CTRL_PKTS_IDX,
    CTRL_OPENING_PKTS_IDX,
    CTRL_ACTIVE_PKTS_IDX,
    CTRL_CLOSE_PKTS_IDX,
    CTRL_RESETALL_PKTS_IDX,
    CURR_SESSION_NUM_IDX,
    GLOBAL_STAT_COLUNM_IDX_MAX
};

struct shaping_global_stat {
    struct fieldstat_instance *instance;
    int table_id;
    int column_ids[GLOBAL_STAT_COLUNM_IDX_MAX];
};

struct shaping_global_stat* shaper_global_stat_init();
void shaper_global_stat_destroy(struct shaping_global_stat *stat);

void shaper_global_stat_throughput_inc(struct shaping_global_stat *stat, enum shaping_global_stat_dir dir, int pkt_len);
void shaper_global_stat_drop_inc(struct shaping_global_stat *stat, int pkt_len);
void shaper_global_stat_queueing_inc(struct shaping_global_stat *stat, int pkt_len);
void shaper_global_stat_queueing_dec(struct shaping_global_stat *stat, int pkt_len);
void shaper_global_stat_hit_policy_inc(struct shaping_global_stat *stat, int pkt_len);
void shaper_global_stat_ctrlpkt_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_opening_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_active_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_close_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_resetall_inc(struct shaping_global_stat *stat);
void shaper_global_stat_curr_session_inc(struct shaping_global_stat *stat);
void shaper_global_stat_curr_session_dec(struct shaping_global_stat *stat);