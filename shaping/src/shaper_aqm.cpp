#include <math.h>
#include <time.h>
#include "shaper.h"
#include "shaper_aqm.h"

#define PROBABILITY_MAX 100
#define INCREMENT 10
#define DECREMENT 1
#define FREEZE_TIME 2 //unit:s
#define QUEUE_LEN_MAX 100
static int shaper_aqm_blue_need_drop(struct shaping_packet_wrapper *pkt_wrapper, struct shaper_aqm_blue_para *para, int curr_queue_len)
{
    time_t curr_time;

    if (time(&curr_time) - para->update_time >= FREEZE_TIME) {
        para->update_time = curr_time;
        if (curr_queue_len >= QUEUE_LEN_MAX) {
            para->probability = (para->probability + INCREMENT) > PROBABILITY_MAX ? PROBABILITY_MAX : (para->probability + INCREMENT);
        } else if (curr_queue_len == 0) {
            para->probability = (para->probability - DECREMENT) >= 0 ? (para->probability - DECREMENT) : 0;
        }
    }

    if (rand() % PROBABILITY_MAX < para->probability) {
        return 1;
    }

    return 0;
}


#define CODEL_MAX_LATENCY 5 //unit:ms
#define CODEL_DROP_INTERVAL 100 //unit:ms
static int shaper_aqm_codel_need_drop(struct shaping_packet_wrapper *pkt_wrapper, struct shaper_aqm_codel_para *para, unsigned long long curr_time_ms, unsigned long long latency)
{
    if (latency < CODEL_MAX_LATENCY) {
        para->state = CODEL_STATE_NORMAL;
        return 0;
    } 

    int ret = 0;
    switch (para->state) {
        case CODEL_STATE_NORMAL:
            para->start_drop_time_ms = curr_time_ms + CODEL_DROP_INTERVAL;
            para->state = CODEL_STATE_DROPPING_TIMER;
            break;
        case CODEL_STATE_DROPPING_TIMER:
            if (curr_time_ms >= para->next_drop_time_ms) {
                para->state = CODEL_STATE_DROPPING_PHASE;
                para->drop_count = 1;
                para->next_drop_time_ms = curr_time_ms + CODEL_DROP_INTERVAL / sqrt(para->drop_count);
                ret = 1;
            }
            break;
        case CODEL_STATE_DROPPING_PHASE:
            if (curr_time_ms >= para->next_drop_time_ms) {
                para->drop_count++;
                para->next_drop_time_ms = curr_time_ms + CODEL_DROP_INTERVAL / sqrt(para->drop_count);
                ret = 1;
            }
            break;
        default:
            break;
    }

    return ret;
}


static int shaper_aqm_have_processed(struct shaping_packet_wrapper *pkt_wrapper, int profile_id)
{
    int i = 0;

    for (i = 0; i < SHAPING_REF_PROFILE_NUM_MAX; i++) {
        if (pkt_wrapper->aqm_processed_pf_ids[i] == profile_id) {
            return 1;
        } else if (pkt_wrapper->aqm_processed_pf_ids[i] == 0) {
            break;
        }
    }

    return 0;
}

static void shaper_aqm_mark_processed(struct shaping_packet_wrapper *pkt_wrapper, int profile_id)
{
    int i = 0;

    for (i = 0; i < SHAPING_REF_PROFILE_NUM_MAX; i++) {
        if (pkt_wrapper->aqm_processed_pf_ids[i] == 0) {
            pkt_wrapper->aqm_processed_pf_ids[i] = profile_id;
            break;
        }
    }
}

int shaper_aqm_need_drop(struct shaping_profile_info *profile, struct shaping_packet_wrapper *pkt_wrapper)
{
    int ret = 0;
    
    if (profile->hash_node->aqm_type == AQM_TYPE_NONE) {
        return 0;
    }

    if (shaper_aqm_have_processed(pkt_wrapper, profile->id)) {
        return 0;
    }
    
    switch (profile->hash_node->aqm_type) {
        case AQM_TYPE_BLUE:
            ret = shaper_aqm_blue_need_drop(pkt_wrapper, &profile->hash_node->aqm_blue_para, profile->hash_node->queue_len[profile->priority]);
            break;
        case AQM_TYPE_CODEL:
            break;
        default:
            break;
    }

    shaper_aqm_mark_processed(pkt_wrapper, profile->id);

    return ret;
}