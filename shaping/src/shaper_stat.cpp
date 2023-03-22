#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <MESA/stream.h>

#include "shaper.h"
#include "shaper_stat.h"


#define SHAPING_STAT_SEND_INTERVAL_SEC 1 //unit: second
#define SHAPING_STAT_SEND_INTERVAL_NS 500000000 //unit: nano second

#define SHAPING_STAT_FORMAT "SHAPING-STAT,rule_id=%d,profile_id=%d,priority=%d,profile_type=%s "\
                            "queueing_sessions=%d,in_rx_pkts=%llu,in_rx_bytes=%llu,"\
                            "in_tx_pkts=%llu,in_tx_bytes=%llu,in_drop_pkts=%llu,in_max_latency_us=%llu,in_queue_len=%lld,"\
                            "out_rx_pkts=%llu,out_rx_bytes=%llu,out_tx_pkts=%llu,out_tx_bytes=%llu,"\
                            "out_drop_pkts=%llu,out_max_latency_us=%llu,out_queue_len=%lld"

static void shaper_stat_counter_clear(struct shaping_stat_data *s)
{
    long long in_queue_len, out_queue_len;
    struct shaping_stat_data_dir *in = &s->incoming;
    struct shaping_stat_data_dir *out = &s->outgoing;

    in_queue_len = in->queue_len;//queue_len is gauge metric, do not clear
    out_queue_len = out->queue_len;

    memset(in, 0, sizeof(struct shaping_stat_data_dir));
    memset(out, 0, sizeof(struct shaping_stat_data_dir));

    in->queue_len = in_queue_len;
    out->queue_len = out_queue_len;

    return;
}

static void shaper_stat_data_send(struct shaping_stat *stat, struct shaping_stat_data *s)
{
    char buf[1024];
    struct shaping_stat_data_dir *in = &s->incoming;
    struct shaping_stat_data_dir *out = &s->outgoing;

    snprintf(buf, sizeof(buf), SHAPING_STAT_FORMAT, s->key.rule_id, s->key.profile_id, s->key.priority,
            s->key.profile_type == SHAPING_PROFILE_TYPE_PRIMARY ? "primary" : "borrow",
            s->queueing_session_num, in->rx_pkts, in->rx_bytes, in->tx_pkts, in->tx_bytes, 
            in->drop_pkts, in->max_latency, in->queue_len, out->rx_pkts, out->rx_bytes, out->tx_pkts, out->tx_bytes, 
            out->drop_pkts, out->max_latency, out->queue_len);
    
    sendto(stat->sock_fd, buf, strlen(buf), 0, (struct sockaddr*)&stat->sock_addr, sizeof(stat->sock_addr));

    shaper_stat_counter_clear(s);
    return;
}

static void shaper_stat_data_send_free(struct shaping_stat *stat, struct shaping_stat_data **stat_hashtbl)
{
    struct shaping_stat_data *s, *tmp = NULL;

    if (!stat || !*stat_hashtbl) {
        return;
    }

    HASH_ITER(hh, *stat_hashtbl, s, tmp) {
        shaper_stat_data_send(stat, s);
        HASH_DEL(*stat_hashtbl, s);
        free(s);
    }

    return;
}

void shaper_stat_send_free(struct shaping_stat *stat)
{
    if (!stat) {
        return;
    }

    if (stat->stat_hashtbl) {
        shaper_stat_data_send_free(stat, &stat->stat_hashtbl);
    }
    free(stat);

    return;
}

void shaper_stat_send(struct shaping_stat *stat, struct shaping_stat_data **stat_hashtbl)
{
    struct shaping_stat_data *s, *tmp = NULL;
    struct timespec curr_time;

    if (!stat || !*stat_hashtbl) {
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    if (curr_time.tv_sec - stat->update_time.tv_sec >= SHAPING_STAT_SEND_INTERVAL_SEC||
        curr_time.tv_nsec - stat->update_time.tv_nsec >= SHAPING_STAT_SEND_INTERVAL_NS) {
        stat->update_time = curr_time;
        HASH_ITER(hh, *stat_hashtbl, s, tmp) {
            shaper_stat_data_send(stat, s);
        }
    }

    return;
}

struct shaping_stat* shaper_stat_new(char *telegraf_ip, short telegraf_port)
{
    struct shaping_stat *stat = NULL;
    struct timespec curr_time;

    clock_gettime(CLOCK_MONOTONIC, &curr_time);

    stat = (struct shaping_stat *)calloc(1, sizeof(struct shaping_stat));

    stat->sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    stat->sock_addr.sin_family = AF_INET;
    stat->sock_addr.sin_port = htons(telegraf_port);
    stat->sock_addr.sin_addr.s_addr = inet_addr(telegraf_ip);
    stat->update_time = curr_time;

    return stat;
}

static struct shaping_stat_data *shaper_stat_ins_get(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, int profile_type)
{
    struct shaping_stat_data *s_stat_data = NULL;
    struct shaping_stat_data_key key;

    memset(&key, 0, sizeof(key));//important for uthash opration
    key.rule_id = rule_id;
    key.profile_id = profile_id;
    key.priority = priority;
    key.profile_type = profile_type;

    HASH_FIND(hh, *stat_hashtbl, &key, sizeof(struct shaping_stat_data_key), s_stat_data);
    if (!s_stat_data) {
        s_stat_data = (struct shaping_stat_data *)calloc(1, sizeof(struct shaping_stat_data));

        memcpy(&s_stat_data->key, &key, sizeof(key));

        HASH_ADD(hh, *stat_hashtbl, key, sizeof(struct shaping_stat_data_key), s_stat_data);
    }

    return s_stat_data;
}

void shaper_stat_drop_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, 
                            int priority, unsigned char direction, int pkt_len)
{
    struct shaping_stat_data *s_stat_data = NULL;

    s_stat_data = shaper_stat_ins_get(stat_hashtbl, rule_id, profile_id, priority, SHAPING_PROFILE_TYPE_PRIMARY);

    if (direction == SHAPING_DIR_IN) {
        s_stat_data->incoming.drop_pkts++;
        s_stat_data->incoming.rx_pkts++;
        s_stat_data->incoming.rx_bytes += pkt_len;
    } else {
        s_stat_data->outgoing.drop_pkts++;
        s_stat_data->outgoing.rx_pkts++;
        s_stat_data->outgoing.rx_bytes += pkt_len;
    }

    return;
}

void shaper_stat_forward_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, 
                            int priority, unsigned char direction, int pkt_len, int profile_type)
{
    struct shaping_stat_data *s_stat_data = NULL;

    s_stat_data = shaper_stat_ins_get(stat_hashtbl, rule_id, profile_id, priority, profile_type);

    if (direction == SHAPING_DIR_IN) {
        s_stat_data->incoming.tx_pkts++;
        s_stat_data->incoming.tx_bytes += pkt_len;
        s_stat_data->incoming.rx_pkts++;
        s_stat_data->incoming.rx_bytes += pkt_len;
    } else {
        s_stat_data->outgoing.tx_pkts++;
        s_stat_data->outgoing.tx_bytes += pkt_len;
        s_stat_data->outgoing.rx_pkts++;
        s_stat_data->outgoing.rx_bytes += pkt_len;
    }

    return;
}

void shaper_stat_forward_all_rule_inc(struct shaping_stat_data **stat_hashtbl, struct shaping_flow *sf, unsigned char direction, int pkt_len)
{
    struct shaping_rule_info *rule;
    int i;

    for (i = 0; i < sf->rule_num; i++) {
        rule = &sf->matched_rule_infos[i];
        shaper_stat_forward_inc(stat_hashtbl, rule->id, rule->primary.id, rule->primary.priority, direction, pkt_len, SHAPING_PROFILE_TYPE_PRIMARY);
    }

    return;
}

void shaper_stat_queueing_session_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, int profile_type)
{
    struct shaping_stat_data *s_stat_data = NULL;

    s_stat_data = shaper_stat_ins_get(stat_hashtbl, rule_id, profile_id, priority, profile_type);

    s_stat_data->queueing_session_num++;
    return;
}

void shaper_stat_queueing_session_dec(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, int priority, int profile_type)
{
    struct shaping_stat_data *s_stat_data = NULL;

    s_stat_data = shaper_stat_ins_get(stat_hashtbl, rule_id, profile_id, priority, profile_type);

    s_stat_data->queueing_session_num--;
    return;
}

void shaper_stat_queueing_pkt_inc(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, 
                                    int priority, unsigned char direction, int pkt_len, int profile_type)
{
    struct shaping_stat_data *s_stat_data = NULL;

    s_stat_data = shaper_stat_ins_get(stat_hashtbl, rule_id, profile_id, priority, profile_type);

    if (direction == SHAPING_DIR_IN) {
        s_stat_data->incoming.rx_pkts++;
        s_stat_data->incoming.rx_bytes += pkt_len;
        s_stat_data->incoming.queue_len++;
    } else {
        s_stat_data->outgoing.rx_pkts++;
        s_stat_data->outgoing.rx_bytes += pkt_len;
        s_stat_data->outgoing.queue_len++;
    }

    return;
}

void shaper_stat_queueing_pkt_dec(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, 
                                    int priority, unsigned char direction, int pkt_len, int profile_type)
{
    struct shaping_stat_data *s_stat_data = NULL;

    s_stat_data = shaper_stat_ins_get(stat_hashtbl, rule_id, profile_id, priority, profile_type);

    if (direction == SHAPING_DIR_IN) {
        s_stat_data->incoming.rx_pkts--;
        s_stat_data->incoming.rx_bytes -= pkt_len;
        s_stat_data->incoming.queue_len--;
    } else {
        s_stat_data->outgoing.rx_pkts--;
        s_stat_data->outgoing.rx_bytes -= pkt_len;
        s_stat_data->outgoing.queue_len--;
    }

    return;
}

void shaper_stat_max_latency_update(struct shaping_stat_data **stat_hashtbl, int rule_id, int profile_id, 
                                    int priority, unsigned char direction, unsigned long long latency, int profile_type)
{
    struct shaping_stat_data *s_stat_data = NULL;

    s_stat_data = shaper_stat_ins_get(stat_hashtbl, rule_id, profile_id, priority, profile_type);

    if (direction == SHAPING_DIR_IN) {
        if (latency > s_stat_data->incoming.max_latency) {
            s_stat_data->incoming.max_latency = latency;
        }
    } else {
        if (latency > s_stat_data->outgoing.max_latency) {
            s_stat_data->outgoing.max_latency = latency;
        }
    }

    return;
}

#if 0
/*********just for self test stub****************/
void stub_shaper_stat_send(int thread_seq)
{
    struct shaping_stat_data *s, *tmp;

    HASH_ITER(hh, g_rt_para.stat[thread_seq]->stat_hashtbl, s, tmp) {
        shaper_stat_data_send(g_rt_para.stat[thread_seq], s);
    }

    return;
}
/************************************************/
#endif