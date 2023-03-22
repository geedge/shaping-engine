#include <netinet/in.h>
#include "uthash.h"

struct shaping_stat_data_dir {
    unsigned long long rx_pkts;
    unsigned long long rx_bytes;
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

struct shaping_stat {
    int sock_fd;
    struct sockaddr_in sock_addr;
    struct timespec update_time;
    struct shaping_stat_data *stat_hashtbl;
};

void shaper_stat_send_free(struct shaping_stat *stat);
void shaper_stat_send(struct shaping_stat *stat, struct shaping_stat_data **stat_hashtbl);
struct shaping_stat* shaper_stat_new(char *telegraf_ip, short telegraf_port);

void shaper_stat_queueing_pkt_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type);
void shaper_stat_queueing_pkt_dec(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type);
void shaper_stat_forward_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len, int profile_type);
void shaper_stat_forward_all_rule_inc(struct shaping_stat_data **stat_hashtbl, struct shaping_flow *sf, unsigned char direction, int pkt_len);
void shaper_stat_drop_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, unsigned char direction, int pkt_len);
void shaper_stat_queueing_session_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, int profile_type);
void shaper_stat_queueing_session_dec(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, int profile_type);
void shaper_stat_max_latency_update(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, unsigned char direction, unsigned long long latency, int profile_type);