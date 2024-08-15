#include <cstring>
#include <stdlib.h>

#include <MESA/MESA_prof_load.h>

#include "log.h"
#include "utils.h"
#include "shaper.h"
#include "shaper_global_stat.h"

struct shping_global_stat_conf
{
    int self_test;
};

thread_local unsigned long long g_queueing_pkts = 0;

static int shaper_global_stat_conf_load(struct shaping_global_stat *stat, struct shping_global_stat_conf *conf)
{
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "GLOBAL_STAT_OUTPUT_INTERVAL_S", &stat->output_interval_s, 1);
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "GLOBAL_STAT_SELF_TEST", &conf->self_test, 0);

    return 0;
}

static void shaper_global_stat_fieldstat_reg(struct shaping_global_stat *stat)
{
    stat->swarmkv_latency_summary_id = fieldstat_easy_register_histogram(stat->instance, "async_delay(us)", 1, 500000, 1);

    stat->column_ids[CURR_SESSION_NUM_IDX] = fieldstat_easy_register_counter(stat->instance, "curr_session_num");
    stat->column_ids[QUEUEING_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "curr_queueing_pkts");
    stat->column_ids[QUEUEING_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "curr_queueing_bytes");

    stat->column_ids[CTRL_ERR_IDX] = fieldstat_easy_register_counter(stat->instance, "ctrl_error");
    stat->column_ids[CTRL_OPENING_IDX] = fieldstat_easy_register_counter(stat->instance, "ctrl_open");
    stat->column_ids[CTRL_ACTIVE_IDX] = fieldstat_easy_register_counter(stat->instance, "ctrl_active");
    stat->column_ids[CTRL_CLOSE_IDX] = fieldstat_easy_register_counter(stat->instance, "ctrl_close");
    stat->column_ids[CTRL_ACTIVE_CLOSE_IDX] = fieldstat_easy_register_counter(stat->instance, "ctrl_sf_close");
    stat->column_ids[CTRL_RESETALL_IDX] = fieldstat_easy_register_counter(stat->instance, "ctrl_reset");
    stat->column_ids[SESSION_LOG_SEND_IDX] = fieldstat_easy_register_counter(stat->instance, "sess_log_send");

    stat->column_ids[ASYNC_INVOKE_IDX] = fieldstat_easy_register_counter(stat->instance, "async");
    stat->column_ids[ASYNC_CALLBACK_IDX] = fieldstat_easy_register_counter(stat->instance, "async_cb");

    stat->column_ids[ASYNC_TCONSUME_INVOKE_IDX] = fieldstat_easy_register_counter(stat->instance, "tconsume");
    stat->column_ids[ASYNC_TCONSUME_CALLBACK_IDX] = fieldstat_easy_register_counter(stat->instance, "tconsume_cb");
    stat->column_ids[ASYNC_HINCRBY_INVOKE_IDX] = fieldstat_easy_register_counter(stat->instance, "hincrby");
    stat->column_ids[ASYNC_HINCRBY_CALLBACK_IDX] = fieldstat_easy_register_counter(stat->instance, "hincrby_cb");
    stat->column_ids[ASYNC_HMGET_INVOKE_IDX] = fieldstat_easy_register_counter(stat->instance, "hmget");
    stat->column_ids[ASYNC_HMGET_CALLBACK_IDX] = fieldstat_easy_register_counter(stat->instance, "hmget_cb");

    stat->column_ids[ASYNC_TCONSUME_FAILED] = fieldstat_easy_register_counter(stat->instance, "tconsume_failed");
    stat->column_ids[ASYNC_HINCRBY_FAILED] = fieldstat_easy_register_counter(stat->instance, "hincrby_failed");
    stat->column_ids[ASYNC_HMGET_FAILED] = fieldstat_easy_register_counter(stat->instance, "hmget_failed");

    stat->column_ids[RX_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "all_rx_pkts");
    stat->column_ids[RX_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "all_rx_bytes");
    stat->column_ids[TX_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "all_tx_pkts");
    stat->column_ids[TX_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "all_tx_bytes");
    stat->column_ids[DROP_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "all_drop_pkts");
    stat->column_ids[DROP_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "all_drop_bytes");

    stat->column_ids[HIT_POLICY_RX_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "shape_rx_pkts");
    stat->column_ids[HIT_POLICY_RX_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "shape_rx_bytes");
    stat->column_ids[HIT_POLICY_TX_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "shape_tx_pkts");
    stat->column_ids[HIT_POLICY_TX_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "shape_tx_bytes");
    stat->column_ids[HIT_POLICY_TX_SYN_ACK_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "shape_tx_syn_ack_pkts");
    stat->column_ids[HIT_POLICY_DROP_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "shape_drop_pkts");
    stat->column_ids[HIT_POLICY_DROP_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "shape_drop_bytes");

    return;
}

struct shaping_global_stat* shaper_global_stat_init(int work_thread_num)
{
    struct shaping_global_stat *stat = NULL;
    struct shping_global_stat_conf conf;

    stat = (struct shaping_global_stat*)calloc(1, sizeof(struct shaping_global_stat));

    if (shaper_global_stat_conf_load(stat, &conf) != 0) {
        LOG_ERROR("%s: shaping init metric conf failed", LOG_TAG_STAT);
        goto ERROR;
    }
    
    stat->instance = fieldstat_easy_new(work_thread_num, "shaping_global", NULL, 0);
    if (stat->instance == NULL) {
        LOG_ERROR("%s: shaping global init fieldstat instance failed", LOG_TAG_STAT);
        goto ERROR;
    }

    shaper_global_stat_fieldstat_reg(stat);
    if (!conf.self_test) {
        int ret = fieldstat_easy_enable_auto_output(stat->instance, "./metric/shaping_global_stat.json", stat->output_interval_s);
        if (ret < 0) {
            LOG_ERROR("%s: shaping global enable auto output failed, ret %d", LOG_TAG_STAT, ret);
            goto ERROR;
        }
    }
    
    return stat;

ERROR:
    if (stat) {
        if (stat->instance) {
            fieldstat_easy_free(stat->instance);
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
        fieldstat_easy_free(stat->instance);
    }
    free(stat);

    return;
}

void shaper_global_stat_swarmkv_latency_update(struct shaping_global_stat *stat, long long latency_us, int thread_idx)
{
    fieldstat_easy_histogram_record(stat->instance, thread_idx, stat->swarmkv_latency_summary_id, NULL, 0, latency_us);

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

    g_queueing_pkts++;

    return;
}

void shaper_global_stat_queueing_dec(struct shaping_global_stat_data *thread_global_stat, int pkt_len)
{
    thread_global_stat->queueing_pkts--;
    thread_global_stat->queueing_bytes -= pkt_len;

    g_queueing_pkts--;

    return;
}

long long shaper_global_stat_queueing_pkts_get()
{
    return g_queueing_pkts;
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

void shaper_thread_global_stat_refresh(struct shaping_thread_ctx *ctx)
{
    struct shaping_global_stat_data *stat_data = &ctx->thread_global_stat;
    struct shaping_global_stat *global_stat = ctx->global_stat;

    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[CURR_SESSION_NUM_IDX], NULL, 0, stat_data->curr_session_num);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[QUEUEING_PKTS_IDX], NULL, 0, stat_data->queueing_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[QUEUEING_BYTES_IDX], NULL, 0, stat_data->queueing_bytes);

    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[CTRL_ERR_IDX], NULL, 0, stat_data->ctrl_error);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[CTRL_OPENING_IDX], NULL, 0, stat_data->ctrl_opening);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[CTRL_ACTIVE_IDX], NULL, 0, stat_data->ctrl_active);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[CTRL_CLOSE_IDX], NULL, 0, stat_data->ctrl_close);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[CTRL_ACTIVE_CLOSE_IDX], NULL, 0, stat_data->ctrl_active_close);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[CTRL_RESETALL_IDX], NULL, 0, stat_data->ctrl_resetall);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[SESSION_LOG_SEND_IDX], NULL, 0, stat_data->session_log_send);

    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_INVOKE_IDX], NULL, 0, stat_data->async_invoke);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_CALLBACK_IDX], NULL, 0, stat_data->async_callback);

    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_TCONSUME_INVOKE_IDX], NULL, 0, stat_data->async_tconsume_invoke);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_TCONSUME_CALLBACK_IDX], NULL, 0, stat_data->async_tconsume_callback);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_HINCRBY_INVOKE_IDX], NULL, 0, stat_data->async_hincrby_invoke);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_HINCRBY_CALLBACK_IDX], NULL, 0, stat_data->async_hincrby_callback);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_HMGET_INVOKE_IDX], NULL, 0, stat_data->async_hmget_invoke);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_HMGET_CALLBACK_IDX], NULL, 0, stat_data->async_hmget_callback);

    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_TCONSUME_FAILED], NULL, 0, stat_data->async_tconsume_failed);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_HINCRBY_FAILED], NULL, 0, stat_data->async_hincrby_failed);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[ASYNC_HMGET_FAILED], NULL, 0, stat_data->async_hmget_failed);

    struct shaping_global_stat_traffic_data *all_traffic_data = &stat_data->all_traffic;
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[RX_PKTS_IDX], NULL, 0, all_traffic_data->rx_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[RX_BYTES_IDX], NULL, 0, all_traffic_data->rx_bytes);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[TX_PKTS_IDX], NULL, 0, all_traffic_data->tx_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[TX_BYTES_IDX], NULL, 0, all_traffic_data->tx_bytes);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[DROP_PKTS_IDX], NULL, 0, all_traffic_data->drop_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[DROP_BYTES_IDX], NULL, 0, all_traffic_data->drop_bytes);

    struct shaping_global_stat_traffic_data *hit_policy_traffic_data = &stat_data->hit_policy_traffic;
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[HIT_POLICY_RX_PKTS_IDX], NULL, 0, hit_policy_traffic_data->rx_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[HIT_POLICY_RX_BYTES_IDX], NULL, 0, hit_policy_traffic_data->rx_bytes);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[HIT_POLICY_TX_PKTS_IDX], NULL, 0, hit_policy_traffic_data->tx_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[HIT_POLICY_TX_BYTES_IDX], NULL, 0, hit_policy_traffic_data->tx_bytes);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[HIT_POLICY_TX_SYN_ACK_PKTS_IDX], NULL, 0, hit_policy_traffic_data->tx_syn_ack_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[HIT_POLICY_DROP_PKTS_IDX], NULL, 0, hit_policy_traffic_data->drop_pkts);
    fieldstat_easy_counter_incrby(global_stat->instance, 0, global_stat->column_ids[HIT_POLICY_DROP_BYTES_IDX], NULL, 0, hit_policy_traffic_data->drop_bytes);

    memset(&ctx->thread_global_stat, 0, sizeof(struct shaping_global_stat_data));
}