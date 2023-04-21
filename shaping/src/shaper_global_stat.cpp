#include <stdlib.h>

#include <MESA/MESA_prof_load.h>
#include <fieldstat.h>

#include "log.h"
#include "utils.h"
#include "shaper.h"
#include "shaper_global_stat.h"

struct shaper_global_stat_conf {
    int enable_backgroud_thread;
    int output_interval_ms;
};

static int shaper_global_stat_conf_load(struct shaper_global_stat_conf *conf)
{
    memset(conf, 0, sizeof(struct shaper_global_stat_conf));

    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "FIELDSTAT_OUTPUT_INTERVAL_MS", &conf->output_interval_ms, 500);
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "FIELDSTAT_ENABLE_BACKGRUND_THREAD", &conf->enable_backgroud_thread, 1);

    return 0;
}

struct shaping_global_stat* shaper_global_stat_init()
{
    struct shaping_global_stat *stat = NULL;
    int column_num;
    struct shaper_global_stat_conf conf;
    const char *column_name[] = {"rx_pkts", "rx_bytes", "tx_pkts", "tx_bytes", "queueing_pkts", 
                                "queueing_bytes", "drop_pkts", "drop_bytes", "hit_policy_pkts", "hit_policy_bytes",
                                "ctrl_pkts", "ctrl_opening_pkts", "ctrl_active_pkts", "ctrl_close_pkts", "ctrl_resetall_pkts", 
                                "curr_session_num", "session_log_send_num"};
    enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_GAUGE, 
                                    FIELD_TYPE_GAUGE, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
                                    FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, 
                                    FIELD_TYPE_GAUGE, FIELD_TYPE_COUNTER};

    column_num = sizeof(column_name)/sizeof(column_name[0]);
    if (column_num != GLOBAL_STAT_COLUNM_IDX_MAX) {
        LOG_ERROR("%s: shaping globat init fieldstat failed, column_num %d != index num %d", LOG_TAG_STAT, column_num, GLOBAL_STAT_COLUNM_IDX_MAX);
        goto ERROR;
    }

    if (shaper_global_stat_conf_load(&conf) != 0) {
        LOG_ERROR("%s: shaping init metric conf failed", LOG_TAG_STAT);
        goto ERROR;
    }

    stat = (struct shaping_global_stat*)calloc(1, sizeof(struct shaping_global_stat));
    stat->instance = fieldstat_instance_new("shaping_global");
    if (stat->instance == NULL) {
        LOG_ERROR("%s: shaping global init fieldstat instance failed", LOG_TAG_STAT);
        goto ERROR;
    }

    stat->table_id = fieldstat_register_table(stat->instance, "shaping_global_metrics", column_name, column_type, column_num);
    if (stat->table_id < 0) {
        LOG_ERROR("%s: shaping global fieldstat register table failed", LOG_TAG_STAT);
        goto ERROR;
    }

    if (fieldstat_register_table_row(stat->instance, stat->table_id, "shaping_global_metric_row", NULL, 0, stat->column_ids) != 0) {
        LOG_ERROR("%s: shaping global fieldstat register table row failed", LOG_TAG_STAT);
        goto ERROR;
    }

    if (conf.enable_backgroud_thread == 0) {
        fieldstat_disable_background_thread(stat->instance);
    }
    fieldstat_set_local_output(stat->instance, "shaping_global_metric", "json");

    if (fieldstat_set_output_interval(stat->instance, conf.output_interval_ms) != 0) {
        LOG_ERROR("%s: shaping global set fieldstat output interval failed", LOG_TAG_STAT);
        goto ERROR;
    }

    if (fieldstat_global_enable_prometheus_endpoint(9007, NULL) != 0) {
        LOG_ERROR("%s: shaping global fieldstat enable prometheus endpoint failed", LOG_TAG_STAT);
        goto ERROR;
    }
    
    if (fieldstat_enable_prometheus_output(stat->instance) != 0) {
        LOG_ERROR("%s: shaping global fieldstat enable prometheus output failed", LOG_TAG_STAT);
        goto ERROR;
    }

    fieldstat_instance_start(stat->instance);
    
    return stat;

ERROR:
    if (stat) {
        if (stat->instance) {
            fieldstat_instance_free(stat->instance);
        }
        free(stat);
    }
    return NULL;
}

void shaper_global_stat_destroy(struct shaping_global_stat *stat)
{
    if (!stat) {
        return;
    }

    if (stat->instance) {
        fieldstat_instance_free(stat->instance);
    }
    free(stat);

    return;
}

void shaper_global_stat_throughput_inc(struct shaping_global_stat *stat, enum shaping_global_stat_dir dir, int pkt_len)
{
    if (dir == SHAPING_GLOBAL_STAT_RX) {
        fieldstat_value_incrby(stat->instance, stat->column_ids[RX_PKTS_IDX], 1);
        fieldstat_value_incrby(stat->instance, stat->column_ids[RX_BYTES_IDX], pkt_len);
    } else {
        fieldstat_value_incrby(stat->instance, stat->column_ids[TX_PKTS_IDX], 1);
        fieldstat_value_incrby(stat->instance, stat->column_ids[TX_BYTES_IDX], pkt_len);
    }

    return;
}

void shaper_global_stat_drop_inc(struct shaping_global_stat *stat, int pkt_len)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[DROP_PKTS_IDX], 1);
    fieldstat_value_incrby(stat->instance, stat->column_ids[DROP_BYTES_IDX], pkt_len);

    return;
}

void shaper_global_stat_queueing_inc(struct shaping_global_stat *stat, int pkt_len)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[QUEUEING_PKTS_IDX], 1);
    fieldstat_value_incrby(stat->instance, stat->column_ids[QUEUEING_BYTES_IDX], pkt_len);

    return;
}

void shaper_global_stat_queueing_dec(struct shaping_global_stat *stat, int pkt_len)
{
    fieldstat_value_decrby(stat->instance, stat->column_ids[QUEUEING_PKTS_IDX], 1);
    fieldstat_value_decrby(stat->instance, stat->column_ids[QUEUEING_BYTES_IDX], pkt_len);

    return;
}

void shaper_global_stat_hit_policy_inc(struct shaping_global_stat *stat, int pkt_len)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[HIT_POLICY_PKTS], 1);
    fieldstat_value_incrby(stat->instance, stat->column_ids[HIT_POLICY_BYTES], pkt_len);

    return;
}

void shaper_global_stat_ctrlpkt_inc(struct shaping_global_stat *stat)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[CTRL_PKTS_IDX], 1);

    return;
}

void shaper_global_stat_ctrlpkt_opening_inc(struct shaping_global_stat *stat)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[CTRL_OPENING_PKTS_IDX], 1);

    return;
}

void shaper_global_stat_ctrlpkt_active_inc(struct shaping_global_stat *stat)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[CTRL_ACTIVE_PKTS_IDX], 1);

    return;
}

void shaper_global_stat_ctrlpkt_close_inc(struct shaping_global_stat *stat)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[CTRL_CLOSE_PKTS_IDX], 1);

    return;
}

void shaper_global_stat_ctrlpkt_resetall_inc(struct shaping_global_stat *stat)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[CTRL_RESETALL_PKTS_IDX], 1);

    return;
}

void shaper_global_stat_curr_session_inc(struct shaping_global_stat *stat)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[CURR_SESSION_NUM_IDX], 1);

    return;
}

void shaper_global_stat_curr_session_dec(struct shaping_global_stat *stat)
{
    fieldstat_value_decrby(stat->instance, stat->column_ids[CURR_SESSION_NUM_IDX], 1);

    return;
}

void shaper_global_stat_session_log_send_num_inc(struct shaping_global_stat *stat)
{
    fieldstat_value_incrby(stat->instance, stat->column_ids[SESSION_LOG_SEND_NUM], 1);

    return;
}