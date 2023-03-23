#include <netinet/in.h>
#include "uthash.h"
#include <MESA/fieldstat.h>

#define SHAPER_STAT_COLUMN_NUM_MAX  20
struct shaping_stat_data_dir {
    unsigned long long tx_pkts;
    unsigned long long tx_bytes;
    unsigned long long drop_pkts;
    unsigned long long max_latency;
    long long queue_len;//num of queueing pkts
};

struct shaping_stat_data_key {
    int rule_id;
    int profile_id;
    int priority;
    int profile_type;
};

struct shaping_stat_data {
    struct shaping_stat_data_key key;
    int queueing_session_num;
    int split_num;
    struct shaping_stat_data_dir incoming;
    struct shaping_stat_data_dir outgoing;
    UT_hash_handle hh;
};

#if 0
struct shaping_stat {
    int sock_fd;
    struct sockaddr_in sock_addr;
    struct timespec update_time;
    struct shaping_stat_data *stat_hashtbl;
};
#endif

enum shaping_stat_tags_index {
    TAG_RULE_ID_IDX = 0,
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
struct shaping_stat* shaper_stat_new(int thread_num);

void shaper_stat_queueing_pkt_inc(struct shaping_stat *stat, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id);
void shaper_stat_queueing_pkt_dec(struct shaping_stat *stat, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id);
void shaper_stat_forward_inc(struct shaping_stat *stat, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id);
void shaper_stat_forward_all_rule_inc(struct shaping_stat *stat, struct shaping_flow *sf, unsigned char direction, int pkt_len, int thread_id);
void shaper_stat_drop_inc(struct shaping_stat *stat, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int thread_id);
void shaper_stat_queueing_session_inc(struct shaping_stat *stat, int rule_id, int profile_id, int priority, int profile_type, int thread_id);
void shaper_stat_queueing_session_dec(struct shaping_stat *stat, int rule_id, int profile_id, int priority, int profile_type, int thread_id);
void shaper_stat_max_latency_update(struct shaping_stat *stat, int rule_id, int profile_id, int priority, unsigned char direction, unsigned long long latency, int profile_type, int thread_id);