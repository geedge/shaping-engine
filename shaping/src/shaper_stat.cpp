#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <MESA/MESA_prof_load.h>
#include <fieldstat.h>

#include "log.h"
#include "utils.h"
#include "shaper.h"
#include "shaper_stat.h"

#define SHAPER_STAT_ROW_NAME "traffic_shaping_rule_hits"

struct shaper_stat_conf {
    int enable_backgroud_thread;
    int output_interval_ms;
    char telegraf_ip[16];
    short telegraf_port;
};

thread_local struct fieldstat_tag tags[TAG_IDX_MAX];

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

    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "TELEGRAF_IP", conf->telegraf_ip, sizeof(conf->telegraf_ip), "127.0.0.1");
    MESA_load_profile_short_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "TELEGRAF_PORT", &conf->telegraf_port, 6379);
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "FIELDSTAT_OUTPUT_INTERVAL_MS", &conf->output_interval_ms, 500);
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "FIELDSTAT_ENABLE_BACKGRUND_THREAD", &conf->enable_backgroud_thread, 1);

    return 0;
}

struct shaping_stat* shaper_stat_init(int thread_num)
{
    struct shaping_stat *stat = NULL;
    int column_num;
    struct shaper_stat_conf conf;
    const char *column_name[] = {"queueing_sessions", "in_max_latency_us", "in_queue_len", "out_max_latency_us", "out_queue_len", //first line is gauge, second line is counter
                                    "in_pkts", "in_bytes", "in_drop_pkts", "out_pkts", "out_bytes", "out_drop_pkts"};
    enum field_type column_type[] = {FIELD_TYPE_GAUGE, FIELD_TYPE_GAUGE, FIELD_TYPE_GAUGE, FIELD_TYPE_GAUGE, FIELD_TYPE_GAUGE, 
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

    fieldstat_global_enable_prometheus_endpoint(9007, NULL);
    //fieldstat_enable_prometheus_output();

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

void shaper_stat_tags_init()
{
    tags[TAG_RULE_ID_IDX].key = "rule_id";
    tags[TAG_RULE_ID_IDX].value_type = 0;
    tags[TAG_PROFILE_ID_IDX].key = "profile_id";
    tags[TAG_PROFILE_ID_IDX].value_type = 0;
    tags[TAG_PRIORITY_IDX].key = "priority";
    tags[TAG_PRIORITY_IDX].value_type = 0;
    tags[TAG_PROFILE_TYPE_IDX].key = "profile_type";
    tags[TAG_PROFILE_TYPE_IDX].value_type = 2;

    return;
}

static void shaper_stat_tags_build(int rule_id, int profile_id, int priority, int profile_type)
{

    tags[TAG_RULE_ID_IDX].value_int = rule_id;

    tags[TAG_PROFILE_ID_IDX].value_int = profile_id;

    tags[TAG_PRIORITY_IDX].value_int = priority;

    if (profile_type == SHAPING_PROFILE_TYPE_PRIMARY) {
        tags[TAG_PROFILE_TYPE_IDX].value_str = "primary";
    } else {
        tags[TAG_PROFILE_TYPE_IDX].value_str = "borrow";
    }

    return;
}

void shaper_stat_drop_inc(struct shaping_stat *stat, int rule_id, int profile_id, 
                            int priority, unsigned char direction, int pkt_len, int thread_id)
{
    shaper_stat_tags_build(rule_id, profile_id, priority, SHAPING_PROFILE_TYPE_PRIMARY);

    if (direction == SHAPING_DIR_IN) {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_DROP_PKTS_IDX], SHAPER_STAT_ROW_NAME, 1, tags, TAG_IDX_MAX, thread_id);
    } else {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_DROP_PKTS_IDX], SHAPER_STAT_ROW_NAME, 1, tags, TAG_IDX_MAX, thread_id);
    }

    return;
}

void shaper_stat_forward_inc(struct shaping_stat *stat, int rule_id, int profile_id, 
                            int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id)
{
    shaper_stat_tags_build(rule_id, profile_id, priority, profile_type);

    if (direction == SHAPING_DIR_IN) {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_PKTS_IDX], SHAPER_STAT_ROW_NAME, 1, tags, TAG_IDX_MAX, thread_id);
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_BYTES_IDX], SHAPER_STAT_ROW_NAME, pkt_len, tags, TAG_IDX_MAX, thread_id);
    } else {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_PKTS_IDX], SHAPER_STAT_ROW_NAME, 1, tags, TAG_IDX_MAX, thread_id);
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_BYTES_IDX], SHAPER_STAT_ROW_NAME, pkt_len, tags, TAG_IDX_MAX, thread_id);
    }

    return;
}

void shaper_stat_forward_all_rule_inc(struct shaping_stat *stat, struct shaping_flow *sf, unsigned char direction, int pkt_len, int thread_id)
{
    struct shaping_rule_info *rule;
    int i;

    for (i = 0; i < sf->rule_num; i++) {
        rule = &sf->matched_rule_infos[i];
        shaper_stat_forward_inc(stat, rule->id, rule->primary.id, rule->primary.priority, direction, pkt_len, SHAPING_PROFILE_TYPE_PRIMARY, thread_id);
    }

    return;
}

void shaper_stat_queueing_session_inc(struct shaping_stat *stat, int rule_id, int profile_id, int priority, int profile_type, int thread_id)
{
    shaper_stat_tags_build(rule_id, profile_id, priority, profile_type);
    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[QUEUEING_SESSIONS_IDX], SHAPER_STAT_ROW_NAME, 1, tags, TAG_IDX_MAX, thread_id);
    return;
}

void shaper_stat_queueing_session_dec(struct shaping_stat *stat, int rule_id, int profile_id, int priority, int profile_type, int thread_id)
{
    shaper_stat_tags_build(rule_id, profile_id, priority, profile_type);
    fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[QUEUEING_SESSIONS_IDX], SHAPER_STAT_ROW_NAME, -1, tags, TAG_IDX_MAX, thread_id);
    return;
}

void shaper_stat_queueing_pkt_inc(struct shaping_stat *stat, int rule_id, int profile_id, 
                                    int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id)
{
    shaper_stat_tags_build(rule_id, profile_id, priority, profile_type);
    if (direction == SHAPING_DIR_IN) {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_QUEUE_LEN_IDX], SHAPER_STAT_ROW_NAME, 1, tags, TAG_IDX_MAX, thread_id);
    } else {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_QUEUE_LEN_IDX], SHAPER_STAT_ROW_NAME, 1, tags, TAG_IDX_MAX, thread_id);
    }

    return;
}

void shaper_stat_queueing_pkt_dec(struct shaping_stat *stat, int rule_id, int profile_id, 
                                    int priority, unsigned char direction, int pkt_len, int profile_type, int thread_id)
{
    shaper_stat_tags_build(rule_id, profile_id, priority, profile_type);
    if (direction == SHAPING_DIR_IN) {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[IN_QUEUE_LEN_IDX], SHAPER_STAT_ROW_NAME, -1, tags, TAG_IDX_MAX, thread_id);
    } else {
        fieldstat_dynamic_table_metric_value_incrby(stat->instance, stat->table_id, stat->column_ids[OUT_QUEUE_LEN_IDX], SHAPER_STAT_ROW_NAME, -1, tags, TAG_IDX_MAX, thread_id);
    }

    return;
}

void shaper_stat_max_latency_update(struct shaping_stat *stat, int rule_id, int profile_id, 
                                    int priority, unsigned char direction, unsigned long long latency, int profile_type, int thread_id)
{
    unsigned long long old_latency;

    shaper_stat_tags_build(rule_id, profile_id, priority, profile_type);
    if (direction == SHAPING_DIR_IN) {
        old_latency = fieldstat_dynamic_table_metric_value_get(stat->instance, stat->table_id, stat->column_ids[IN_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, tags, TAG_IDX_MAX, thread_id);
        if (latency > old_latency) {
            fieldstat_dynamic_table_metric_value_set(stat->instance, stat->table_id, stat->column_ids[IN_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, latency, tags, TAG_IDX_MAX, thread_id);
        }
    } else {
        old_latency = fieldstat_dynamic_table_metric_value_get(stat->instance, stat->table_id, stat->column_ids[OUT_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, tags, TAG_IDX_MAX, thread_id);
        if (latency > old_latency) {
            fieldstat_dynamic_table_metric_value_set(stat->instance, stat->table_id, stat->column_ids[OUT_MAX_LATENCY_IDX], SHAPER_STAT_ROW_NAME, latency, tags, TAG_IDX_MAX, thread_id);
        }
    }

    return;
}