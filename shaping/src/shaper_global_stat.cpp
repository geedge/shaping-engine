#include <stdlib.h>

#include <MESA/MESA_prof_load.h>
#include <fieldstat.h>

#include "log.h"
#include "utils.h"
#include "shaper.h"
#include "shaper_global_stat.h"

struct shaping_global_stat_conf {
    int is_self_test;
};

static int shaper_global_stat_conf_load(struct shaping_global_stat *stat, struct shaping_global_stat_conf *conf)
{
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "GLOBAL_STAT_OUTPUT_INTERVAL_S", &stat->output_interval_s, 1);
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "SELF_TEST", &conf->is_self_test, 0);

    return 0;
}

static void shaper_global_stat_fieldstat_reg(struct shaping_global_stat *stat)
{
    stat->column_ids[CURR_SESSION_NUM_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_GAUGE, "curr_session_num", NULL, 0);
    stat->column_ids[QUEUEING_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_GAUGE, "curr_queueing_pkts", NULL, 0);
    stat->column_ids[QUEUEING_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_GAUGE, "curr_queueing_bytes", NULL, 0);

    stat->column_ids[CTRL_ERR_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_error", NULL, 0);
    stat->column_ids[CTRL_OPENING_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_opening", NULL, 0);
    stat->column_ids[CTRL_ACTIVE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_active", NULL, 0);
    stat->column_ids[CTRL_CLOSE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_close", NULL, 0);
    stat->column_ids[CTRL_RESETALL_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_resetall", NULL, 0);
    stat->column_ids[SESSION_LOG_SEND_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "session_log_send", NULL, 0);

    stat->column_ids[ASYNC_INVOKE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "async_invoke", NULL, 0);
    stat->column_ids[ASYNC_CALLBACK_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "async_callback", NULL, 0);
    stat->column_ids[ASYNC_TCONSUME_FAILED] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "tconsume_failed", NULL, 0);
    stat->column_ids[ASYNC_HINCRBY_FAILED] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "hincrby_failed", NULL, 0);
    stat->column_ids[ASYNC_HMGET_FAILED] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "hmget_failed", NULL, 0);

    stat->column_ids[RX_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "all_rx_pkts", NULL, 0);
    stat->column_ids[RX_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "all_rx_bytes", NULL, 0);
    stat->column_ids[TX_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "all_tx_pkts", NULL, 0);
    stat->column_ids[TX_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "all_tx_bytes", NULL, 0);
    stat->column_ids[DROP_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "all_drop_pkts", NULL, 0);
    stat->column_ids[DROP_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "all_drop_bytes", NULL, 0);

    stat->column_ids[HIT_POLICY_RX_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_rx_pkts", NULL, 0);
    stat->column_ids[HIT_POLICY_RX_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_rx_bytes", NULL, 0);
    stat->column_ids[HIT_POLICY_TX_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_tx_pkts", NULL, 0);
    stat->column_ids[HIT_POLICY_TX_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_tx_bytes", NULL, 0);
    stat->column_ids[HIT_POLICY_DROP_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_drop_pkts", NULL, 0);
    stat->column_ids[HIT_POLICY_DROP_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_drop_bytes", NULL, 0);

    return;
}

struct shaping_global_stat* shaper_global_stat_init()
{
    struct shaping_global_stat *stat = NULL;
    struct shaping_global_stat_conf conf;

    stat = (struct shaping_global_stat*)calloc(1, sizeof(struct shaping_global_stat));

    if (shaper_global_stat_conf_load(stat, &conf) != 0) {
        LOG_ERROR("%s: shaping init metric conf failed", LOG_TAG_STAT);
        goto ERROR;
    }
    
    stat->instance = fieldstat_instance_new("shaping_global");
    if (stat->instance == NULL) {
        LOG_ERROR("%s: shaping global init fieldstat instance failed", LOG_TAG_STAT);
        goto ERROR;
    }

    shaper_global_stat_fieldstat_reg(stat);

    fieldstat_disable_background_thread(stat->instance);
    if (conf.is_self_test) {
        fieldstat_set_local_output(stat->instance, "shaping_global_metric", "json");
    } else {
        fieldstat_set_local_output(stat->instance, "shaping_global_metric", "default");
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

void shaper_global_stat_curr_session_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->curr_session_num, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_curr_session_dec(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_sub_fetch(&local_stat_data->curr_session_num, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_queueing_inc(struct shaping_global_stat *stat, int pkt_len)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->queueing_pkts, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&local_stat_data->queueing_bytes, pkt_len, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_queueing_dec(struct shaping_global_stat *stat, int pkt_len)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_sub_fetch(&local_stat_data->queueing_pkts, 1, __ATOMIC_RELAXED);
    __atomic_sub_fetch(&local_stat_data->queueing_bytes, pkt_len, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_ctrlpkt_err_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->ctrl_error, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_ctrlpkt_opening_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->ctrl_opening, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_ctrlpkt_active_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->ctrl_active, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_ctrlpkt_close_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->ctrl_close, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_ctrlpkt_resetall_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->ctrl_resetall, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_session_log_send_num_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->session_log_send, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_async_invoke_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->async_invoke, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_async_callback_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->async_callback, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_async_tconsume_failed_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->async_tconsume_failed, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_async_hincrby_failed_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->async_hincrby_failed, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_async_hmget_failed_inc(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;

    __atomic_add_fetch(&local_stat_data->async_hmget_failed, 1, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_throughput_inc(struct shaping_global_stat *stat, enum shaping_global_stat_dir dir, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &stat->local_stat_data.all_traffic;
    if (dir == SHAPING_GLOBAL_STAT_RX) {
        __atomic_add_fetch(&data->rx_pkts, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&data->rx_bytes, pkt_len, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&data->tx_pkts, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&data->tx_bytes, pkt_len, __ATOMIC_RELAXED);
    }

    return;
}

void shaper_global_stat_drop_inc(struct shaping_global_stat *stat, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &stat->local_stat_data.all_traffic;

    __atomic_add_fetch(&data->drop_pkts, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&data->drop_bytes, pkt_len, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_hit_policy_throughput_inc(struct shaping_global_stat *stat, enum shaping_global_stat_dir dir, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &stat->local_stat_data.hit_policy_traffic;

    if (dir == SHAPING_GLOBAL_STAT_RX) {
        __atomic_add_fetch(&data->rx_pkts, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&data->rx_bytes, pkt_len, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&data->tx_pkts, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&data->tx_bytes, pkt_len, __ATOMIC_RELAXED);
    }

    return;
}

void shaper_global_stat_hit_policy_drop_inc(struct shaping_global_stat *stat, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &stat->local_stat_data.hit_policy_traffic;

    __atomic_add_fetch(&data->drop_pkts, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&data->drop_bytes, pkt_len, __ATOMIC_RELAXED);

    return;
}

void shaper_global_stat_refresh(struct shaping_global_stat *stat)
{
    struct shaping_global_stat_data *local_stat_data = &stat->local_stat_data;
    struct shaping_global_stat_traffic_data *all_traffic_data = &stat->local_stat_data.all_traffic;
    struct shaping_global_stat_traffic_data *hit_policy_traffic_data = &stat->local_stat_data.hit_policy_traffic;

    fieldstat_value_set(stat->instance, stat->column_ids[CURR_SESSION_NUM_IDX], local_stat_data->curr_session_num);
    fieldstat_value_set(stat->instance, stat->column_ids[QUEUEING_PKTS_IDX], local_stat_data->queueing_pkts);
    fieldstat_value_set(stat->instance, stat->column_ids[QUEUEING_BYTES_IDX], local_stat_data->queueing_bytes);

    fieldstat_value_set(stat->instance, stat->column_ids[CTRL_ERR_IDX], local_stat_data->ctrl_error);
    fieldstat_value_set(stat->instance, stat->column_ids[CTRL_OPENING_IDX], local_stat_data->ctrl_opening);
    fieldstat_value_set(stat->instance, stat->column_ids[CTRL_ACTIVE_IDX], local_stat_data->ctrl_active);
    fieldstat_value_set(stat->instance, stat->column_ids[CTRL_CLOSE_IDX], local_stat_data->ctrl_close);
    fieldstat_value_set(stat->instance, stat->column_ids[CTRL_RESETALL_IDX], local_stat_data->ctrl_resetall);
    fieldstat_value_set(stat->instance, stat->column_ids[SESSION_LOG_SEND_IDX], local_stat_data->session_log_send);

    fieldstat_value_set(stat->instance, stat->column_ids[ASYNC_INVOKE_IDX], local_stat_data->async_invoke);
    fieldstat_value_set(stat->instance, stat->column_ids[ASYNC_CALLBACK_IDX], local_stat_data->async_callback);
    fieldstat_value_set(stat->instance, stat->column_ids[ASYNC_TCONSUME_FAILED], local_stat_data->async_tconsume_failed);
    fieldstat_value_set(stat->instance, stat->column_ids[ASYNC_HINCRBY_FAILED], local_stat_data->async_hincrby_failed);
    fieldstat_value_set(stat->instance, stat->column_ids[ASYNC_HMGET_FAILED], local_stat_data->async_hmget_failed);

    fieldstat_value_set(stat->instance, stat->column_ids[RX_PKTS_IDX], all_traffic_data->rx_pkts);
    fieldstat_value_set(stat->instance, stat->column_ids[RX_BYTES_IDX], all_traffic_data->rx_bytes);
    fieldstat_value_set(stat->instance, stat->column_ids[TX_PKTS_IDX], all_traffic_data->tx_pkts);
    fieldstat_value_set(stat->instance, stat->column_ids[TX_BYTES_IDX], all_traffic_data->tx_bytes);
    fieldstat_value_set(stat->instance, stat->column_ids[DROP_PKTS_IDX], all_traffic_data->drop_pkts);
    fieldstat_value_set(stat->instance, stat->column_ids[DROP_BYTES_IDX], all_traffic_data->drop_bytes);

    fieldstat_value_set(stat->instance, stat->column_ids[HIT_POLICY_RX_PKTS_IDX], hit_policy_traffic_data->rx_pkts);
    fieldstat_value_set(stat->instance, stat->column_ids[HIT_POLICY_RX_BYTES_IDX], hit_policy_traffic_data->rx_bytes);
    fieldstat_value_set(stat->instance, stat->column_ids[HIT_POLICY_TX_PKTS_IDX], hit_policy_traffic_data->tx_pkts);
    fieldstat_value_set(stat->instance, stat->column_ids[HIT_POLICY_TX_BYTES_IDX], hit_policy_traffic_data->tx_bytes);
    fieldstat_value_set(stat->instance, stat->column_ids[HIT_POLICY_DROP_PKTS_IDX], hit_policy_traffic_data->drop_pkts);
    fieldstat_value_set(stat->instance, stat->column_ids[HIT_POLICY_DROP_BYTES_IDX], hit_policy_traffic_data->drop_bytes);

    fieldstat_passive_output(stat->instance);
}