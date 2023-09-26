#include <cstring>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <MESA/MESA_prof_load.h>
#include <MESA/swarmkv.h>
#include <fieldstat.h>

#include "log.h"
#include "utils.h"
#include "shaper.h"
#include "shaper_stat.h"
#include "shaper_global_stat.h"

#define SHAPER_STAT_ROW_NAME "traffic_shaping_rule_hits"

#define SHAPER_STAT_REFRESH_TIME_NS 200000000  //0.2s

struct shaper_stat_conf {
    int enable_backgroud_thread;
    int output_interval_ms;
    char telegraf_ip[16];
    short telegraf_port;
};

thread_local struct fieldstat_tag tags[TAG_IDX_MAX] = 
{
    [TAG_VSYS_ID_IDX] = {.key = "vsys_id", .value_type = 0},
    [TAG_RULE_ID_IDX] = {.key = "rule_id", .value_type = 0},
    [TAG_PROFILE_ID_IDX] = {.key = "profile_id", .value_type = 0},
    [TAG_PRIORITY_IDX] = {.key = "priority", .value_type = 0},
    [TAG_PROFILE_TYPE_IDX] = {.key = "profile_type", .value_type = 2}
};

void shaper_stat_destroy(struct shaping_stat *stat)
{
    if (!stat) {
        return;
    }

    if (stat->instance) {
        fieldstat_dynamic_instance_free(stat->instance);
    }
    free(stat);

    return;
}

static int shaper_stat_conf_load(struct shaper_stat_conf *conf)
{
    memset(conf, 0, sizeof(struct shaper_stat_conf));

    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "LINE_PROTOCOL_SERVER_IP", conf->telegraf_ip, sizeof(conf->telegraf_ip), "127.0.0.1");
    MESA_load_profile_short_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "LINE_PROTOCOL_SERVER_PORT", &conf->telegraf_port, 8200);
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "FIELDSTAT_OUTPUT_INTERVAL_MS", &conf->output_interval_ms, 500);
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "FIELDSTAT_ENABLE_BACKGRUND_THREAD", &conf->enable_backgroud_thread, 1);

    return 0;
}

struct shaping_stat* shaper_stat_init(int thread_num)
{
    struct shaping_stat *stat = NULL;
    int column_num;
    struct shaper_stat_conf conf;
    const char *column_name[] = {"in_max_latency_us", "in_queue_len", "out_max_latency_us", "out_queue_len", //first line is gauge, second line is counter
                                    "in_pkts", "in_bytes", "in_drop_pkts", "out_pkts", "out_bytes", "out_drop_pkts"};
    enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_GAUGE, FIELD_TYPE_COUNTER, FIELD_TYPE_GAUGE, 
                                    FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};

    column_num = sizeof(column_name)/sizeof(column_name[0]);
    if (column_num != STAT_COLUNM_IDX_MAX) {
        LOG_ERROR("%s: shaping init fieldstat failed, column_num %d != index num %d", LOG_TAG_STAT, column_num, STAT_COLUNM_IDX_MAX);
        goto ERROR;
    }

    if (shaper_stat_conf_load(&conf) != 0) {
        LOG_ERROR("%s: shaping init metric conf failed", LOG_TAG_STAT);
        goto ERROR;
    }

    stat = (struct shaping_stat *)calloc(1, sizeof(struct shaping_stat));

    stat->instance = fieldstat_dynamic_instance_new("shaping_engine", thread_num);
    if (stat->instance == NULL) {
        LOG_ERROR("%s: shaping init fieldstat instance failed", LOG_TAG_STAT);
        goto ERROR;
    }

    fieldstat_dynamic_set_output_interval(stat->instance, conf.output_interval_ms);
    fieldstat_dynamic_set_line_protocol_server(stat->instance, conf.telegraf_ip, conf.telegraf_port);
    if (conf.enable_backgroud_thread == 0) {
        fieldstat_dynamic_disable_background_thread(stat->instance);
    }

    stat->table_id = fieldstat_register_dynamic_table(stat->instance, "shaping_metric", column_name, column_type, column_num, stat->column_ids);
    if (stat->table_id < 0) {
        LOG_ERROR("%s: shaping fieldstat register table failed", LOG_TAG_STAT);
        goto ERROR;
    }

    fieldstat_dynamic_instance_start(stat->instance);

    return stat;

ERROR:
    if (stat) {
        if (stat->instance) {
            fieldstat_dynamic_instance_free(stat->instance);
        }
        free(stat);
    }
    return NULL;
}

static void shaper_stat_tags_build(int vsys_id, int rule_id, int profile_id, int priority, int profile_type)
{
    tags[TAG_VSYS_ID_IDX].value_int = vsys_id;

    tags[TAG_RULE_ID_IDX].value_int = rule_id;

    tags[TAG_PROFILE_ID_IDX].value_int = profile_id;

    tags[TAG_PRIORITY_IDX].value_int = priority;

    if (profile_type == PROFILE_IN_RULE_TYPE_PRIMARY) {
        tags[TAG_PROFILE_TYPE_IDX].value_str = "primary";
    } else {
        tags[TAG_PROFILE_TYPE_IDX].value_str = "borrow";
    }

    return;
}

static void shaper_stat_swarmkv_hincrby_cb(const struct swarmkv_reply *reply, void * cb_arg)
{
    struct shaping_global_stat *global_stat = (struct shaping_global_stat *)cb_arg;

    shaper_global_stat_async_callback_inc(global_stat);

    if (reply->type != SWARMKV_REPLY_INTEGER) {
        shaper_global_stat_async_hincrby_failed_inc(global_stat);
    }

    return;
}

static void shaper_stat_profile_metirc_refresh(struct shaping_thread_ctx *ctx, int vsys_id, int thread_id, int rule_id, struct shaping_profile_info *profile, int profile_type, int need_update_guage)
{
    struct shaping_stat_for_profile *profile_stat = &profile->stat;
    struct shaping_stat *stat = ctx->stat;
    unsigned long long old_latency;
    
    shaper_stat_tags_build(vsys_id, rule_id, profile->id, profile->priority, profile_type);
    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_DROP_PKTS_IDX], SHAPER_STAT_ROW_NAME, profile_stat->in.drop_pkts, tags, TAG_IDX_MAX, thread_id);
    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_PKTS_IDX], SHAPER_STAT_ROW_NAME, profile_stat->in.pkts, tags, TAG_IDX_MAX, thread_id);
    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_BYTES_IDX], SHAPER_STAT_ROW_NAME, profile_stat->in.bytes, tags, TAG_IDX_MAX, thread_id);

    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_DROP_PKTS_IDX], SHAPER_STAT_ROW_NAME, profile_stat->out.drop_pkts, tags, TAG_IDX_MAX, thread_id);
    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_PKTS_IDX], SHAPER_STAT_ROW_NAME, profile_stat->out.pkts, tags, TAG_IDX_MAX, thread_id);
    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_BYTES_IDX], SHAPER_STAT_ROW_NAME, profile_stat->out.bytes, tags, TAG_IDX_MAX, thread_id);

    old_latency = fieldstat_dynamic_table_metric_value_get(stat->instance, stat->table_id, stat->column_ids[IN_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, tags, TAG_IDX_MAX, thread_id);
    if (profile_stat->in.max_latency > old_latency) {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, profile_stat->in.max_latency - old_latency, tags, TAG_IDX_MAX, thread_id);
    }

    old_latency = fieldstat_dynamic_table_metric_value_get(stat->instance, stat->table_id, stat->column_ids[OUT_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, tags, TAG_IDX_MAX, thread_id);
    if (profile_stat->out.max_latency > old_latency) {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, profile_stat->out.max_latency - old_latency, tags, TAG_IDX_MAX, thread_id);
    }

    if (need_update_guage) {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_QUEUE_LEN_IDX], SHAPER_STAT_ROW_NAME, profile_stat->in.queue_len, tags, TAG_IDX_MAX, thread_id);
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_QUEUE_LEN_IDX], SHAPER_STAT_ROW_NAME, profile_stat->out.queue_len, tags, TAG_IDX_MAX, thread_id);

        shaper_global_stat_async_invoke_inc(ctx->global_stat);
        swarmkv_async_command(ctx->swarmkv_db, shaper_stat_swarmkv_hincrby_cb, ctx->global_stat, "HINCRBY tsg-shaping-%d priority-%d %lld", profile->id, profile->priority, profile_stat->in.queue_len + profile_stat->out.queue_len);
        memset(profile_stat, 0, sizeof(struct shaping_stat_for_profile));
    } else {
        profile_stat->in.pkts = 0;
        profile_stat->in.bytes = 0;
        profile_stat->in.drop_pkts = 0;
        profile_stat->in.max_latency = 0;

        profile_stat->out.pkts = 0;
        profile_stat->out.bytes = 0;
        profile_stat->out.drop_pkts = 0;
        profile_stat->out.max_latency = 0;
    }

    return;
}

void shaper_stat_refresh(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int thread_id, int force)
{
    struct shaping_rule_info *rule;
    struct timespec curr_time;
    int need_refresh = 0;

    if (force) {
        need_refresh = 1;
    } else {
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        if (curr_time.tv_sec - sf->stat_update_time.tv_sec > 0 || curr_time.tv_nsec - sf->stat_update_time.tv_nsec > SHAPER_STAT_REFRESH_TIME_NS) {
            need_refresh = 1;
            sf->stat_update_time = curr_time;
            memcpy(&sf->stat_update_time, &curr_time, sizeof(struct timespec));
        }
    }

    if (!need_refresh) {
        return;
    }

    int need_update_guage = sf->processed_pkts > CONFIRM_PRIORITY_PKTS ? 1 : 0;

    for (int i = 0; i < sf->rule_num; i++) {
        rule = &sf->matched_rule_infos[i];
        shaper_stat_profile_metirc_refresh(ctx, rule->vsys_id, thread_id, rule->id, &rule->primary, PROFILE_IN_RULE_TYPE_PRIMARY, need_update_guage);

        for (int j = 0; j < rule->borrowing_num; j++) {
            shaper_stat_profile_metirc_refresh(ctx, rule->vsys_id, thread_id, rule->id, &rule->borrowing[j], PROFILE_IN_RULE_TYPE_BORROW, need_update_guage);
        }
    }

    return;
}

void shaper_stat_drop_inc(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int thread_id)
{
    if (direction == SHAPING_DIR_IN) {
        profile_stat->in.drop_pkts++;
    } else {
        profile_stat->out.drop_pkts++;
    }

    return;
}

void shaper_stat_forward_inc(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int pkt_len, int thread_id)
{
    if (direction == SHAPING_DIR_IN) {
        profile_stat->in.pkts++;
        profile_stat->in.bytes += pkt_len;
    } else {
        profile_stat->out.pkts++;
        profile_stat->out.bytes += pkt_len;
    }

    return;
}

void shaper_stat_forward_all_rule_inc(struct shaping_stat *stat, struct shaping_flow *sf, unsigned char direction, int pkt_len, int thread_id)
{
    struct shaping_rule_info *rule;
    int i;

    for (i = 0; i < sf->rule_num; i++) {
        rule = &sf->matched_rule_infos[i];
        shaper_stat_forward_inc(&rule->primary.stat, direction, pkt_len, thread_id);
    }

    return;
}

void shaper_stat_queueing_pkt_inc(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int thread_id)
{
    if (direction == SHAPING_DIR_IN) {
        profile_stat->in.queue_len++;
    } else {
        profile_stat->out.queue_len++;
    }

    return;
}

void shaper_stat_queueing_pkt_dec(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int thread_id)
{
    if (direction == SHAPING_DIR_IN) {
        profile_stat->in.queue_len--;
    } else {
        profile_stat->out.queue_len--;
    }

    return;
}

void shaper_stat_max_latency_update(struct shaping_stat_for_profile *profile_stat, unsigned char direction, unsigned long long latency, int thread_id)
{
    if (direction == SHAPING_DIR_IN) {
        if (profile_stat->in.max_latency < latency) {
            profile_stat->in.max_latency = latency;
        }
    } else {
        if (profile_stat->out.max_latency < latency) {
            profile_stat->out.max_latency = latency;
        }
    }

    return;
}