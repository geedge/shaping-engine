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

int shaper_aqm_need_drop(struct shaping_profile_info *profile, struct shaping_packet_wrapper *pkt_wrapper)
{
    int ret = 0;
    
    if (profile->hash_node->aqm_type == AQM_TYPE_NONE) {
        return 0;
    }

    //TODO: judge if this packet is aqm processed for this profile
    if (pkt_wrapper->aqm_processed) {
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

    pkt_wrapper->aqm_processed = 1;

    return ret;
}