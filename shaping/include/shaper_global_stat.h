#include <fieldstat.h>

enum shaping_global_stat_dir {
    SHAPING_GLOBAL_STAT_RX = 0,
    SHAPING_GLOBAL_STAT_TX
};

enum shaping_global_stat_column_index {
    CURR_SESSION_NUM_IDX = 0,
    QUEUEING_PKTS_IDX,
    QUEUEING_BYTES_IDX,

    CTRL_ERR_IDX,
    CTRL_OPENING_IDX,
    CTRL_ACTIVE_IDX,
    CTRL_CLOSE_IDX,
    CTRL_ACTIVE_CLOSE_IDX,
    CTRL_RESETALL_IDX,
    SESSION_LOG_SEND_IDX,

    ASYNC_INVOKE_IDX,
    ASYNC_CALLBACK_IDX,
    ASYNC_TCONSUME_FAILED,
    ASYNC_HINCRBY_FAILED,
    ASYNC_HMGET_FAILED,

    RX_PKTS_IDX,
    RX_BYTES_IDX,
    TX_PKTS_IDX,
    TX_BYTES_IDX,
    DROP_PKTS_IDX,
    DROP_BYTES_IDX,

    HIT_POLICY_RX_PKTS_IDX,
    HIT_POLICY_RX_BYTES_IDX,
    HIT_POLICY_TX_PKTS_IDX,
    HIT_POLICY_TX_BYTES_IDX,
    HIT_POLICY_DROP_PKTS_IDX,
    HIT_POLICY_DROP_BYTES_IDX,

    GLOBAL_STAT_COLUNM_IDX_MAX
};

struct shaping_global_stat_traffic_data {
    long long rx_pkts;
    long long rx_bytes;
    long long tx_pkts;
    long long tx_bytes;
    long long drop_pkts;
    long long drop_bytes;
};

struct shaping_global_stat_data {
    long long curr_session_num;
    long long queueing_pkts;
    long long queueing_bytes;
    long long ctrl_error;
    long long ctrl_opening;
    long long ctrl_active;
    long long ctrl_close;
    long long ctrl_active_close;
    long long ctrl_resetall;
    long long session_log_send;
    long long async_invoke;
    long long async_callback;
    long long async_tconsume_failed;
    long long async_hincrby_failed;
    long long async_hmget_failed;
    struct shaping_global_stat_traffic_data all_traffic;
    struct shaping_global_stat_traffic_data hit_policy_traffic;
};

struct shaping_global_stat {
    struct fieldstat_instance *instance;
    int column_ids[GLOBAL_STAT_COLUNM_IDX_MAX];
    struct shaping_global_stat_data local_stat_data;
    int output_interval_s;
};

struct shaping_global_stat* shaper_global_stat_init();
void shaper_global_stat_destroy(struct shaping_global_stat *stat);

void shaper_global_stat_curr_session_inc(struct shaping_global_stat *stat);
void shaper_global_stat_curr_session_dec(struct shaping_global_stat *stat);
void shaper_global_stat_queueing_inc(struct shaping_global_stat *stat, int pkt_len);
void shaper_global_stat_queueing_dec(struct shaping_global_stat *stat, int pkt_len);

void shaper_global_stat_ctrlpkt_err_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_opening_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_active_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_close_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_active_close_inc(struct shaping_global_stat *stat);
void shaper_global_stat_ctrlpkt_resetall_inc(struct shaping_global_stat *stat);
void shaper_global_stat_session_log_send_num_inc(struct shaping_global_stat *stat);

void shaper_global_stat_async_invoke_inc(struct shaping_global_stat *stat);
void shaper_global_stat_async_callback_inc(struct shaping_global_stat *stat);
void shaper_global_stat_async_tconsume_failed_inc(struct shaping_global_stat *stat);
void shaper_global_stat_async_hincrby_failed_inc(struct shaping_global_stat *stat);
void shaper_global_stat_async_hmget_failed_inc(struct shaping_global_stat *stat);

void shaper_global_stat_throughput_inc(struct shaping_global_stat *stat, enum shaping_global_stat_dir dir, int pkt_len);
void shaper_global_stat_drop_inc(struct shaping_global_stat *stat, int pkt_len);

void shaper_global_stat_hit_policy_throughput_inc(struct shaping_global_stat *stat, enum shaping_global_stat_dir dir, int pkt_len);
void shaper_global_stat_hit_policy_drop_inc(struct shaping_global_stat *stat, int pkt_len);

void shaper_global_stat_refresh(struct shaping_global_stat *stat);