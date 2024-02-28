#pragma once
#include <time.h>

#define BLUE_PROBABILITY_MAX 100
#define BLUE_INCREMENT 10
#define BLUE_DECREMENT 1
#define BLUE_FREEZE_TIME 2 //unit:s
#define BLUE_QUEUE_LEN_MAX 100

#define CODEL_MAX_LATENCY 5 //unit:ms
#define CODEL_DROP_INTERVAL 100 //unit:ms
enum shaper_aqm_type {
    AQM_TYPE_NONE = 0,
    AQM_TYPE_BLUE,
    AQM_TYPE_CODEL,
    AQM_TYPE_MAX
};

struct shaper_aqm_blue_para {
    time_t update_time;
    int probability;
};

enum CoDel_state {
    CODEL_STATE_NORMAL = 0,
    CODEL_STATE_DROPPING_TIMER,
    CODEL_STATE_DROPPING_PHASE,
};

struct shaper_aqm_codel_para {
    unsigned long long start_drop_time_ms;
    unsigned long long next_drop_time_ms;
    enum CoDel_state state;
    unsigned int drop_count;
};

int shaper_aqm_need_drop(struct shaping_profile_info *profile, struct shaping_packet_wrapper *pkt_wrapper, struct timespec *curr_time, unsigned long long latency_us);
int shaper_aqm_blue_need_drop(int profile_id, struct shaper_aqm_blue_para *para, int curr_queue_len);
int shaper_aqm_codel_need_drop(int profile_id, struct shaper_aqm_codel_para *para, unsigned long long curr_time_ms, unsigned long long latency_ms);