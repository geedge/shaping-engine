#pragma once
#include <time.h>

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

int shaper_aqm_need_drop(struct shaping_profile_info *profile, struct shaping_packet_wrapper *pkt_wrapper);