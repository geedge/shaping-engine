#include <cstring>
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
    const char * quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";
    stat->swarmkv_latency_summary_id = fieldstat_register_summary(stat->instance, "async_delay(us)", NULL, 0, quantiles, 1, 500000, 3, 1);

    stat->column_ids[CURR_SESSION_NUM_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_GAUGE, "curr_session_num", NULL, 0);
    stat->column_ids[QUEUEING_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_GAUGE, "curr_queueing_pkts", NULL, 0);
    stat->column_ids[QUEUEING_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_GAUGE, "curr_queueing_bytes", NULL, 0);

    stat->column_ids[CTRL_ERR_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_error", NULL, 0);
    stat->column_ids[CTRL_OPENING_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_open", NULL, 0);
    stat->column_ids[CTRL_ACTIVE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_active", NULL, 0);
    stat->column_ids[CTRL_CLOSE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_close", NULL, 0);
    stat->column_ids[CTRL_ACTIVE_CLOSE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_sf_close", NULL, 0);
    stat->column_ids[CTRL_RESETALL_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "ctrl_reset", NULL, 0);
    stat->column_ids[SESSION_LOG_SEND_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "sess_log_send", NULL, 0);

    stat->column_ids[ASYNC_INVOKE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "async", NULL, 0);
    stat->column_ids[ASYNC_CALLBACK_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "async_cb", NULL, 0);

    stat->column_ids[ASYNC_TCONSUME_INVOKE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "tconsume", NULL, 0);
    stat->column_ids[ASYNC_TCONSUME_CALLBACK_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "tconsume_cb", NULL, 0);
    stat->column_ids[ASYNC_HINCRBY_INVOKE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "hincrby", NULL, 0);
    stat->column_ids[ASYNC_HINCRBY_CALLBACK_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "hincrby_cb", NULL, 0);
    stat->column_ids[ASYNC_HMGET_INVOKE_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "hmget", NULL, 0);
    stat->column_ids[ASYNC_HMGET_CALLBACK_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "hmget_cb", NULL, 0);

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
    stat->column_ids[HIT_POLICY_TX_SYN_ACK_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_tx_syn_ack_pkts", NULL, 0);
    stat->column_ids[HIT_POLICY_DROP_PKTS_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_drop_pkts", NULL, 0);
    stat->column_ids[HIT_POLICY_DROP_BYTES_IDX] = fieldstat_register(stat->instance, FIELD_TYPE_COUNTER, "shape_drop_bytes", NULL, 0);

    return;
}

struct shaping_global_stat* shaper_global_stat_init(int work_thread_num)
{
    struct shaping_global_stat *stat = NULL;
    struct shaping_global_stat_conf conf;

    stat = (struct shaping_global_stat*)calloc(1, sizeof(struct shaping_global_stat));
    stat->stat_data = (struct shaping_global_stat_data*)calloc(work_thread_num, sizeof(struct shaping_global_stat_data));

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

    if (stat->stat_data) {
        free(stat->stat_data);
    }

    if (stat->instance) {
        fieldstat_instance_free(stat->instance);
    }
    free(stat);

    return;
}

void shaper_global_stat_swarmkv_latency_update(struct shaping_global_stat *stat, long long latency_us)
{
    fieldstat_value_set(stat->instance, stat->swarmkv_latency_summary_id, latency_us);

    return;
}

void shaper_global_stat_curr_session_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->curr_session_num++;

    return;
}

void shaper_global_stat_curr_session_dec(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->curr_session_num--;

    return;
}

void shaper_global_stat_queueing_inc(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    thread_global_stat->queueing_pkts++;
    thread_global_stat->queueing_bytes += pkt_len;

    return;
}

void shaper_global_stat_queueing_dec(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    thread_global_stat->queueing_pkts--;
    thread_global_stat->queueing_bytes -= pkt_len;

    return;
}

long long shaper_global_stat_queueing_pkts_get(struct shaping_global_stat_data *thread_global_stat)
{
    return thread_global_stat->queueing_pkts;
}

void shaper_global_stat_ctrlpkt_err_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->ctrl_error++;

    return;
}

void shaper_global_stat_ctrlpkt_opening_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->ctrl_opening++;

    return;
}

void shaper_global_stat_ctrlpkt_active_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->ctrl_active++;

    return;
}

void shaper_global_stat_ctrlpkt_close_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->ctrl_close++;

    return;
}

void shaper_global_stat_ctrlpkt_active_close_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->ctrl_active_close++;

    return;
}

void shaper_global_stat_ctrlpkt_resetall_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->ctrl_resetall++;

    return;
}

void shaper_global_stat_session_log_send_num_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->session_log_send++;

    return;
}

void shaper_global_stat_async_invoke_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_invoke++;

    return;
}

void shaper_global_stat_async_callback_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_callback++;

    return;
}

void sheper_global_stat_tconsume_invoke_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_tconsume_invoke++;

    return;
}

void shaper_global_stat_tconsume_callback_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_tconsume_callback++;

    return;
}

void shaper_global_stat_hincrby_invoke_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_hincrby_invoke++;

    return;
}

void shaper_global_stat_hincrby_callback_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_hincrby_callback++;

    return;
}

void shaper_global_stat_hmget_invoke_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_hmget_invoke++;

    return;
}

void shaper_global_stat_hmget_callback_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_hmget_callback++;

    return;
}

void shaper_global_stat_async_tconsume_failed_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_tconsume_failed++;

    return;
}

void shaper_global_stat_async_hincrby_failed_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_hincrby_failed++;

    return;
}

void shaper_global_stat_async_hmget_failed_inc(struct shaping_global_stat_data *thread_global_stat)
{
    thread_global_stat->async_hmget_failed++;

    return;
}

void shaper_global_stat_throughput_rx_inc(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &thread_global_stat->all_traffic;
    data->rx_pkts++;
    data->rx_bytes += pkt_len;
}

void shaper_global_stat_throughput_tx_inc(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &thread_global_stat->all_traffic;
    data->tx_pkts++;
    data->tx_bytes += pkt_len;
}

void shaper_global_stat_drop_inc(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{   
    struct shaping_global_stat_traffic_data *data = &thread_global_stat->all_traffic;
    data->drop_pkts++;
    data->drop_bytes += pkt_len;

    return;
}

void shaper_global_stat_hit_policy_throughput_rx_inc(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &thread_global_stat->hit_policy_traffic;
    data->rx_pkts++;
    data->rx_bytes += pkt_len;
}

void shaper_global_stat_hit_policy_throughput_tx_inc(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &thread_global_stat->hit_policy_traffic;
    data->tx_pkts++;
    data->tx_bytes += pkt_len;
}

void shaper_global_stat_hit_policy_throughput_tx_syn_ack_inc(struct shaping_global_stat_data *thread_global_stat)
{
    struct shaping_global_stat_traffic_data *data = &thread_global_stat->hit_policy_traffic;
    data->tx_syn_ack_pkts++;
}

void shaper_global_stat_hit_policy_drop_inc(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    struct shaping_global_stat_traffic_data *data = &thread_global_stat->hit_policy_traffic;
    data->drop_pkts++;
    data->drop_bytes += pkt_len;

    return;
}

void shaper_global_stat_refresh(struct shaping_ctx *ctx)
{
    static struct shaping_global_stat_data sum;
    struct shaping_global_stat *global_stat = ctx->global_stat;
    struct shaping_global_stat_data *stat_data = global_stat->stat_data;

    for (int i = 0; i < ctx->thread_num; i++) {
        memcpy(&stat_data[i], &ctx->thread_ctx[i].thread_global_stat, sizeof(struct shaping_global_stat_data));
    }

    memset(&sum, 0, sizeof(struct shaping_global_stat_data));
    for (int i = 0; i < ctx->thread_num; i++) {
        sum.curr_session_num += stat_data[i].curr_session_num;
        sum.queueing_pkts += stat_data[i].queueing_pkts;
        sum.queueing_bytes += stat_data[i].queueing_bytes;

        sum.ctrl_error += stat_data[i].ctrl_error;
        sum.ctrl_opening += stat_data[i].ctrl_opening;
        sum.ctrl_active += stat_data[i].ctrl_active;
        sum.ctrl_close += stat_data[i].ctrl_close;
        sum.ctrl_active_close += stat_data[i].ctrl_active_close;
        sum.ctrl_resetall += stat_data[i].ctrl_resetall;
        sum.session_log_send += stat_data[i].session_log_send;

        sum.async_invoke += stat_data[i].async_invoke;
        sum.async_callback += stat_data[i].async_callback;

        sum.async_tconsume_invoke += stat_data[i].async_tconsume_invoke;
        sum.async_tconsume_callback += stat_data[i].async_tconsume_callback;
        sum.async_hincrby_invoke += stat_data[i].async_hincrby_invoke;
        sum.async_hincrby_callback += stat_data[i].async_hincrby_callback;
        sum.async_hmget_invoke += stat_data[i].async_hmget_invoke;
        sum.async_hmget_callback += stat_data[i].async_hmget_callback;

        sum.async_tconsume_failed += stat_data[i].async_tconsume_failed;
        sum.async_hincrby_failed += stat_data[i].async_hincrby_failed;
        sum.async_hmget_failed += stat_data[i].async_hmget_failed;

        sum.all_traffic.rx_pkts += stat_data[i].all_traffic.rx_pkts;
        sum.all_traffic.rx_bytes += stat_data[i].all_traffic.rx_bytes;
        sum.all_traffic.tx_pkts += stat_data[i].all_traffic.tx_pkts;
        sum.all_traffic.tx_bytes += stat_data[i].all_traffic.tx_bytes;
        sum.all_traffic.drop_pkts += stat_data[i].all_traffic.drop_pkts;
        sum.all_traffic.drop_bytes += stat_data[i].all_traffic.drop_bytes;

        sum.hit_policy_traffic.rx_pkts += stat_data[i].hit_policy_traffic.rx_pkts;
        sum.hit_policy_traffic.rx_bytes += stat_data[i].hit_policy_traffic.rx_bytes;
        sum.hit_policy_traffic.tx_pkts += stat_data[i].hit_policy_traffic.tx_pkts;
        sum.hit_policy_traffic.tx_bytes += stat_data[i].hit_policy_traffic.tx_bytes;
        sum.hit_policy_traffic.tx_syn_ack_pkts += stat_data[i].hit_policy_traffic.tx_syn_ack_pkts;
        sum.hit_policy_traffic.drop_pkts += stat_data[i].hit_policy_traffic.drop_pkts;
        sum.hit_policy_traffic.drop_bytes += stat_data[i].hit_policy_traffic.drop_bytes;
    }

    struct shaping_global_stat_traffic_data *all_traffic_data = &sum.all_traffic;
    struct shaping_global_stat_traffic_data *hit_policy_traffic_data = &sum.hit_policy_traffic;

    fieldstat_value_set(global_stat->instance, global_stat->column_ids[CURR_SESSION_NUM_IDX], sum.curr_session_num);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[QUEUEING_PKTS_IDX], sum.queueing_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[QUEUEING_BYTES_IDX], sum.queueing_bytes);

    fieldstat_value_set(global_stat->instance, global_stat->column_ids[CTRL_ERR_IDX], sum.ctrl_error);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[CTRL_OPENING_IDX], sum.ctrl_opening);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[CTRL_ACTIVE_IDX], sum.ctrl_active);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[CTRL_CLOSE_IDX], sum.ctrl_close);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[CTRL_ACTIVE_CLOSE_IDX], sum.ctrl_active_close);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[CTRL_RESETALL_IDX], sum.ctrl_resetall);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[SESSION_LOG_SEND_IDX], sum.session_log_send);

    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_INVOKE_IDX], sum.async_invoke);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_CALLBACK_IDX], sum.async_callback);

    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_TCONSUME_INVOKE_IDX], sum.async_tconsume_invoke);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_TCONSUME_CALLBACK_IDX], sum.async_tconsume_callback);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_HINCRBY_INVOKE_IDX], sum.async_hincrby_invoke);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_HINCRBY_CALLBACK_IDX], sum.async_hincrby_callback);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_HMGET_INVOKE_IDX], sum.async_hmget_invoke);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_HMGET_CALLBACK_IDX], sum.async_hmget_callback);
    
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_TCONSUME_FAILED], sum.async_tconsume_failed);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_HINCRBY_FAILED], sum.async_hincrby_failed);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[ASYNC_HMGET_FAILED], sum.async_hmget_failed);

    fieldstat_value_set(global_stat->instance, global_stat->column_ids[RX_PKTS_IDX], all_traffic_data->rx_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[RX_BYTES_IDX], all_traffic_data->rx_bytes);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[TX_PKTS_IDX], all_traffic_data->tx_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[TX_BYTES_IDX], all_traffic_data->tx_bytes);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[DROP_PKTS_IDX], all_traffic_data->drop_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[DROP_BYTES_IDX], all_traffic_data->drop_bytes);

    fieldstat_value_set(global_stat->instance, global_stat->column_ids[HIT_POLICY_RX_PKTS_IDX], hit_policy_traffic_data->rx_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[HIT_POLICY_RX_BYTES_IDX], hit_policy_traffic_data->rx_bytes);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[HIT_POLICY_TX_PKTS_IDX], hit_policy_traffic_data->tx_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[HIT_POLICY_TX_BYTES_IDX], hit_policy_traffic_data->tx_bytes);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[HIT_POLICY_TX_SYN_ACK_PKTS_IDX], hit_policy_traffic_data->tx_syn_ack_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[HIT_POLICY_DROP_PKTS_IDX], hit_policy_traffic_data->drop_pkts);
    fieldstat_value_set(global_stat->instance, global_stat->column_ids[HIT_POLICY_DROP_BYTES_IDX], hit_policy_traffic_data->drop_bytes);

    fieldstat_passive_output(global_stat->instance);
}