#pragma once
#include <sched.h>
#include <sys/queue.h>
#include <marsio.h>
#include "uthash.h"
#include "session_table.h"
#include "utils.h"
#include "shaper_stat.h"
extern "C" {
#include "timeout.h"
}

#define SHAPING_DIR_IN 0x1
#define SHAPING_DIR_OUT 0x2

#define SHAPING_RULE_NUM_MAX 8
#define SHAPING_REF_PROFILE_NUM_MAX 8
#define SHAPING_PRIORITY_NUM_MAX 10
#define SHAPER_FLOW_POP_NUM_MAX 10

#define SESSION_CLOSE 0x1
#define SESSION_UPDATE_PF_PRIO_LEN 0x2

#define CONFIRM_PRIORITY_PKTS 20

#define SHAPING_WROK_THREAD_NUM_MAX 128

#define SHAPING_STAT_REFRESH_INTERVAL_SEC 2
#define SHAPING_STAT_REFRESH_MAX_PER_POLLING 5

#define SHAPING_GLOBAL_CONF_FILE "./conf/shaping.conf"

struct shaping_system_conf {
    unsigned int session_queue_len_max;
    unsigned int priority_queue_len_max;
    int polling_node_num_max[SHAPING_PRIORITY_NUM_MAX];
    int work_thread_num;
    int cpu_affinity_enable;
    int firewall_sid;
    cpu_set_t cpu_affinity_mask;
    int check_rule_enable_interval_sec;
};

struct shaping_thread_ctx {
    pthread_t tid;
    int thread_index;
    struct shaping_ctx *ref_ctx;
    struct shaper *sp;
    struct shaping_stat *stat;
    struct shaping_global_stat *global_stat;
    struct shaping_marsio_info *marsio_info;
    struct swarmkv *swarmkv_db;//handle of swarmkv 
    struct shaping_maat_info *maat_info;
    struct session_table *session_table;
    struct timeouts *expires;
    time_t last_update_timeout_sec;
    int session_need_reset;
    struct shaping_system_conf conf;
};

struct shaping_ctx {
    int thread_num;
    struct swarmkv *swarmkv_db;//handle of swarmkv 
    struct shaping_maat_info *maat_info;
    struct shaping_marsio_info *marsio_info;
    struct shaping_stat *stat;
    struct shaping_global_stat *global_stat;
    struct shaping_thread_ctx *thread_ctx;
};

enum shaping_packet_action {
    SHAPING_FORWARD = 0,
    SHAPING_QUEUED,
    SHAPING_DROP
};

enum shaping_profile_type_in_rule {
    PROFILE_IN_RULE_TYPE_PRIMARY = 0,
    PROFILE_IN_RULE_TYPE_BORROW
};

enum shaping_profile_type {
    PROFILE_TYPE_GENERIC,
    PROFILE_TYPE_HOST_FARINESS,
    PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS,
    PROFILE_TYPE_SPLIT_BY_LOCAL_HOST
};

struct shaping_profile_info {
    int id;//profile_id
    enum shaping_profile_type type;
    int priority;
    int in_deposit_token;
    int out_deposit_token;
    unsigned long long enqueue_time_us;//to calculate max latency
    unsigned char is_priority_blocked;
    unsigned char is_invalid;
    struct shaping_stat_for_profile stat;
    struct shaping_profile_hash_node *hash_node;
};

struct shaping_rule_info {
    int vsys_id;
    int id;//rule_id
    int fair_factor;
    struct shaping_profile_info primary;
    struct shaping_profile_info borrowing[SHAPING_REF_PROFILE_NUM_MAX];
    int borrowing_num;
    int is_enabled;
};

struct shaping_packet_wrapper {
    void *pkt_buff;
    unsigned long long income_time_ns;
    unsigned long long enqueue_time_us;//first enqueue time
    unsigned int length;
    unsigned char direction;
    unsigned char tcp_pure_contorl;
    TAILQ_ENTRY(shaping_packet_wrapper) node;
};
TAILQ_HEAD(delay_queue, shaping_packet_wrapper);

struct metadata
{
    uint64_t session_id;
    char *raw_data;
    int raw_len;
    int dir;
    int is_tcp_pure_ctrl;
    int is_ctrl_pkt;
    uint16_t l7_offset;       // only control packet set l7_offset
    struct sids sids;
    struct route_ctx route_ctx;
};

struct shaping_flow {
    struct addr_tuple4 tuple4;
    char *src_ip_str;
    size_t src_ip_str_len;
    struct delay_queue packet_queue;
    struct shaping_rule_info matched_rule_infos[SHAPING_RULE_NUM_MAX];
    int priority;
    int rule_num;
    int anchor;//rule_idx
    int ref_count;
    unsigned int queue_len;
    unsigned int flag;
    struct metadata ctrl_meta;
    unsigned long long processed_pkts;
    struct timespec stat_update_time;
    time_t check_rule_time;
    struct timeout timeout_handle;
    time_t last_update_timeout_sec;
};

struct shaper_flow_instance {
    struct shaping_flow *sf;
    int priority;
};

struct shaper;//instance of shaping, thread unsafe

struct shaping_flow* shaping_flow_new(struct shaping_thread_ctx *ctx);
void shaping_flow_free(struct shaping_thread_ctx *ctx, struct shaping_flow *sf);
struct shaper* shaper_new(unsigned int priority_queue_len_max);
void shaper_free(struct shaper *sp);

bool shaper_queue_empty(struct shaping_flow *sf);
void shaper_packet_dequeue(struct shaping_flow *sf);
struct shaping_packet_wrapper* shaper_first_pkt_get(struct shaping_flow *sf);
void shaper_queue_clear(struct shaping_flow *sf, struct shaping_thread_ctx *ctx);
/*return value: 0 for success, -1 for failed*/
int shaper_flow_push(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, unsigned long long enqueue_time);
/*return num of sf_ins*/
void shaper_flow_pop(struct shaping_thread_ctx *ctx, struct shaping_flow *sf);
int shaper_flow_in_order_get(struct shaper *sp, struct shaper_flow_instance sf_ins[], int priority, int max_sf_num);

//enum shaping_packet_action shaper_pkt_action_decide(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int priority, int sf_in_queue);

int shaper_global_conf_init(struct shaping_system_conf *conf);

void shaper_packet_recv_and_process(struct shaping_thread_ctx *ctx);
void shaping_packet_process(struct shaping_thread_ctx *ctx, marsio_buff_t *rx_buff, struct metadata *meta, struct shaping_flow *sf);

struct shaping_ctx *shaping_engine_init();
void shaping_engine_destroy(struct shaping_ctx *ctx);
