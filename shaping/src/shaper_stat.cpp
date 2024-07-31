#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <MESA/MESA_prof_load.h>
#include <MESA/swarmkv.h>

#include "log.h"
#include "utils.h"
#include "shaper.h"
#include "shaper_stat.h"
#include "shaper_global_stat.h"

#define SHAPER_STAT_REFRESH_TIME_US 10000  //10 ms
#define HINCRBY_RETRY_MAX 5

struct shaper_stat_conf {
    char device_group[32];
    char device_id[32];
    char data_center[32];
    char kafka_topic[64];
    char kafka_username[64];
    char kafka_password[64];
    char kafka_brokers[256];
};

thread_local struct field tags[TAG_IDX_MAX] = 
{
    [TAG_VSYS_ID_IDX] = {.key = "vsys_id", .type = FIELD_VALUE_INTEGER},
    [TAG_RULE_ID_IDX] = {.key = "rule_id", .type = FIELD_VALUE_INTEGER},
    [TAG_PROFILE_ID_IDX] = {.key = "profile_id", .type = FIELD_VALUE_INTEGER},
    [TAG_PRIORITY_IDX] = {.key = "priority", .type = FIELD_VALUE_INTEGER},
    [TAG_PROFILE_TYPE_IDX] = {.key = "profile_type", .type = FIELD_VALUE_CSTRING}
};

void shaper_stat_destroy(struct shaping_stat *stat)
{
    if (stat) {
        if (stat->instance) {
            fieldstat_easy_free(stat->instance);
        }
        free(stat);
    }

    return;
}

static void shaper_stat_kafka_init(struct shaping_stat *stat, struct shaper_stat_conf *conf)
{
    char kafka_errstr[1024]={0};

    if (strlen(conf->kafka_topic) == 0 || strlen(conf->kafka_brokers) == 0) {
        LOG_ERROR("%s: kafka topic or brokers is empty", LOG_TAG_STAT);
        return;
    }

    if (strlen(conf->kafka_username) == 0 || strlen(conf->kafka_password) == 0) {
        LOG_ERROR("%s: kafka username or password is empty", LOG_TAG_STAT);
        return;
    }

    rd_kafka_conf_t *rdkafka_conf = rd_kafka_conf_new();

    if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "queue.buffering.max.messages", "1000000", kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set queue.buffering.max.messages failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
    if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "topic.metadata.refresh.interval.ms", "600000", kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set topic.metadata.refresh.interval.ms failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
    if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "socket.keepalive.enable", "true", kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set socket.keepalive.enable failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
    if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "security.protocol", "sasl_plaintext", kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set security.protocol failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
    if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "client.id", conf->kafka_topic, kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set client.id failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
	if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "sasl.mechanisms", "PLAIN", kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set sasl.mechanisms failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
	if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "sasl.username", conf->kafka_username, kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set sasl.username failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
	if (RD_KAFKA_CONF_OK != rd_kafka_conf_set(rdkafka_conf, "sasl.password", conf->kafka_password, kafka_errstr, sizeof(kafka_errstr)))
    {
        LOG_ERROR("%s: kafka producer set sasl.password failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }

    stat->kafka_handle = rd_kafka_new(RD_KAFKA_PRODUCER, rdkafka_conf, kafka_errstr, sizeof(kafka_errstr));
    if (stat->kafka_handle == NULL) {
        LOG_ERROR("%s: kafka producer create failed, err %s", LOG_TAG_STAT, kafka_errstr);
        return;
    }
    if (rd_kafka_brokers_add(stat->kafka_handle, conf->kafka_brokers) <= 0)
    {
        LOG_ERROR("%s: kafka producer add brokers failed", LOG_TAG_STAT);
        return;
    }

    stat->topic_rkt = rd_kafka_topic_new(stat->kafka_handle, conf->kafka_topic, NULL);
    if (stat->topic_rkt == NULL) {
        LOG_ERROR("%s: kafka producer create topic failed", LOG_TAG_STAT);
        return;
    }

    return;
}

static int shaper_stat_conf_load(struct shaping_stat *stat, struct shaper_stat_conf *conf)
{
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "FIELDSTAT_OUTPUT_INTERVAL_S", &stat->output_interval_s, 1);
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "DEVICE_GROUP", conf->device_group, sizeof(conf->device_group), "");
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "DEVICE_ID", conf->device_id, sizeof(conf->device_id), "");
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "DATA_CENTER", conf->data_center, sizeof(conf->data_center), "");

    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "KAFKA_TOPIC", conf->kafka_topic, sizeof(conf->kafka_topic), "");
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "KAFKA_USERNAME", conf->kafka_username, sizeof(conf->kafka_username), "");
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "KAFKA_PASSWORD", conf->kafka_password, sizeof(conf->kafka_password), "");
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "METRIC", "KAFKA_BROKERS", conf->kafka_brokers, sizeof(conf->kafka_brokers), "");

    return 0;
}

struct shaping_stat* shaper_stat_init(int thread_num)
{
    struct field global_tags[5];
    struct shaper_stat_conf conf;
    struct shaping_stat *stat = (struct shaping_stat *)calloc(1, sizeof(struct shaping_stat));

    if (shaper_stat_conf_load(stat, &conf) != 0) {
        LOG_ERROR("%s: shaping init metric conf failed", LOG_TAG_STAT);
        goto ERROR;
    }

    shaper_stat_kafka_init(stat, &conf);

    global_tags[0].key = "app_name";
    global_tags[0].type = FIELD_VALUE_CSTRING;
    global_tags[0].value_str = "shaping_engine";
    
    global_tags[1].key = "device_group";
    global_tags[1].type = FIELD_VALUE_CSTRING;
    global_tags[1].value_str = conf.device_group;

    global_tags[2].key = "device_id";
    global_tags[2].type = FIELD_VALUE_CSTRING;
    global_tags[2].value_str = conf.device_id;

    global_tags[3].key = "data_center";
    global_tags[3].type = FIELD_VALUE_CSTRING;
    global_tags[3].value_str = conf.data_center;

    global_tags[4].key = "table_name";
    global_tags[4].type = FIELD_VALUE_CSTRING;
    global_tags[4].value_str = "shaping_metric";

    stat->instance = fieldstat_easy_new(thread_num, "traffic_shaping_rule_hits", global_tags, 5);
    if (stat->instance == NULL) {
        LOG_ERROR("%s: shaping init fieldstat instance failed", LOG_TAG_STAT);
        goto ERROR;
    }

    stat->latency_histogram_id = fieldstat_easy_register_histogram(stat->instance, "latency_distribution_us", 1, 1000000, 5);
    if (stat->latency_histogram_id < 0) {
        LOG_ERROR("%s: shaping fieldstat register histogram failed", LOG_TAG_STAT);
        goto ERROR;
    }

    stat->column_ids[IN_QUEUE_LEN_IDX] = fieldstat_easy_register_counter(stat->instance, "in_queue_len");
    stat->column_ids[OUT_QUEUE_LEN_IDX] = fieldstat_easy_register_counter(stat->instance, "out_queue_len");
    stat->column_ids[IN_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "in_pkts");
    stat->column_ids[IN_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "in_bytes");
    stat->column_ids[IN_DROP_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "in_drop_pkts");
    stat->column_ids[OUT_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "out_pkts");
    stat->column_ids[OUT_BYTES_IDX] = fieldstat_easy_register_counter(stat->instance, "out_bytes");
    stat->column_ids[OUT_DROP_PKTS_IDX] = fieldstat_easy_register_counter(stat->instance, "out_drop_pkts");

    for (int i = IN_QUEUE_LEN_IDX; i < STAT_COLUNM_IDX_MAX; i++) {
        if (stat->column_ids[i] < 0) {
            LOG_ERROR("%s: shaping fieldstat register column %d failed", LOG_TAG_STAT, i);
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

static void shaper_stat_tags_build(int vsys_id, int rule_id, int profile_id, int priority, int profile_type)
{
    tags[TAG_VSYS_ID_IDX].value_longlong = vsys_id;

    tags[TAG_RULE_ID_IDX].value_longlong = rule_id;

    tags[TAG_PROFILE_ID_IDX].value_longlong = profile_id;

    tags[TAG_PRIORITY_IDX].value_longlong = priority;

    if (profile_type == PROFILE_IN_RULE_TYPE_PRIMARY) {
        tags[TAG_PROFILE_TYPE_IDX].value_str = "primary";
    } else {
        tags[TAG_PROFILE_TYPE_IDX].value_str = "borrow";
    }

    return;
}

static void shaper_stat_swarmkv_hincrby_cb(const struct swarmkv_reply *reply, void * cb_arg)
{
    struct shaping_hincrby_cb_arg *arg = (struct shaping_hincrby_cb_arg *)cb_arg;
    struct shaping_thread_ctx *ctx = arg->ctx;
    struct shaping_global_stat *global_stat = ctx->ref_ctx->global_stat;
    struct timespec curr_time;
    long long curr_time_us;

    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    curr_time_us = curr_time.tv_sec * MICRO_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MICRO_SEC;
    shaper_global_stat_swarmkv_latency_update(global_stat, curr_time_us - arg->start_time_us, ctx->thread_index);

    shaper_global_stat_async_callback_inc(&ctx->thread_global_stat);
    shaper_global_stat_hincrby_callback_inc(&ctx->thread_global_stat);

    if (reply->type != SWARMKV_REPLY_INTEGER) {
        shaper_global_stat_async_hincrby_failed_inc(&ctx->thread_global_stat);

        if (arg->retry_cnt >= HINCRBY_RETRY_MAX) {
            LOG_ERROR("%s: shaping stat hincrby failed after retry %d times for profile id %d priority %d, operate %s queue_len %lld", 
                    LOG_TAG_STAT, arg->retry_cnt, arg->profile_id, arg->priority, arg->dir == SHAPING_DIR_IN ? "in" : "out", arg->queue_len);
            goto END;
        }

        arg->retry_cnt++;
        arg->start_time_us = curr_time_us;
        shaper_global_stat_async_invoke_inc(&ctx->thread_global_stat);//hincrby failed, retry
        shaper_global_stat_hincrby_invoke_inc(&ctx->thread_global_stat);

        LOG_DEBUG("%s: shaping stat hincrby failed, retry for profile id %d priority %d, operate %s queue_len %lld", LOG_TAG_STAT, arg->profile_id, arg->priority, arg->dir == SHAPING_DIR_IN ? "in" : "out", arg->queue_len);

        if (arg->dir == SHAPING_DIR_IN) {
            swarmkv_async_command(ctx->swarmkv_db, shaper_stat_swarmkv_hincrby_cb, arg, "HINCRBY tsg-shaping-%d priority-%d-in %lld", arg->profile_id, arg->priority, arg->queue_len);
        } else {
            swarmkv_async_command(ctx->swarmkv_db, shaper_stat_swarmkv_hincrby_cb, arg, "HINCRBY tsg-shaping-%d priority-%d-out %lld", arg->profile_id, arg->priority, arg->queue_len);
        }
        
        return;
    }

END:
    free(cb_arg);
    return;
}

static void shaper_stat_priority_queue_len_refresh_dir(struct shaping_thread_ctx *ctx, struct shaping_profile_hash_node *profile_hash_node, int priority, enum shaping_packet_dir direction, long long curr_time_us)
{
    if (profile_hash_node->local_queue_len[priority][direction] == 0) {
        return;
    }

    struct shaping_hincrby_cb_arg *arg = (struct shaping_hincrby_cb_arg *)calloc(1, sizeof(struct shaping_hincrby_cb_arg));

    arg->ctx = ctx;
    arg->start_time_us = curr_time_us;
    arg->profile_id = profile_hash_node->id;
    arg->priority = priority;
    arg->dir = direction;
    arg->queue_len = profile_hash_node->local_queue_len[priority][direction];
    shaper_global_stat_async_invoke_inc(&ctx->thread_global_stat);
    shaper_global_stat_hincrby_invoke_inc(&ctx->thread_global_stat);
    if (direction == SHAPING_DIR_IN) {
        swarmkv_async_command(ctx->swarmkv_db, shaper_stat_swarmkv_hincrby_cb, arg, "HINCRBY tsg-shaping-%d priority-%d-in %lld", arg->profile_id, arg->priority, arg->queue_len);
    } else {
        swarmkv_async_command(ctx->swarmkv_db, shaper_stat_swarmkv_hincrby_cb, arg, "HINCRBY tsg-shaping-%d priority-%d-out %lld", arg->profile_id, arg->priority, arg->queue_len);
    }

    profile_hash_node->local_queue_len[priority][direction] = 0;

    return;
}

static void shaper_stat_priority_queue_len_refresh(struct shaping_thread_ctx *ctx, struct shaping_profile_hash_node *profile_hash_node, int priority, long long curr_time_us)
{
    if (curr_time_us - profile_hash_node->local_queue_len_update_time_us[priority] < SHAPER_STAT_REFRESH_TIME_US) {
        return;
    }

    shaper_stat_priority_queue_len_refresh_dir(ctx, profile_hash_node, priority, SHAPING_DIR_IN, curr_time_us);
    shaper_stat_priority_queue_len_refresh_dir(ctx, profile_hash_node, priority, SHAPING_DIR_OUT, curr_time_us);

    profile_hash_node->local_queue_len_update_time_us[priority] = curr_time_us;

    return;
}

void shaper_stat_priority_queue_len_refresh_all(struct shaping_thread_ctx *ctx, struct shaping_profile_hash_node *profile_hash_node)
{
    struct timespec curr_time;
    long long curr_time_us;

    clock_gettime(CLOCK_MONOTONIC_COARSE, &curr_time);
    curr_time_us = curr_time.tv_sec * MICRO_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MICRO_SEC;

    for (int i = 0; i < SHAPING_PRIORITY_NUM_MAX; i++) {
        shaper_stat_priority_queue_len_refresh(ctx, profile_hash_node, i, curr_time_us);
    }

    return;
}

static void shaper_stat_profile_metirc_refresh(struct shaping_thread_ctx *ctx, struct shaping_rule_info *rule, struct shaping_profile_info *profile, int profile_type, int need_refresh_stat, int need_update_guage, long long curr_time_us)
{
    struct shaping_stat_for_profile *profile_stat = &profile->stat;
    struct shaping_stat *stat = ctx->stat;
    int priority = profile->priority;
    int thread_id = ctx->thread_index;

    if (need_update_guage) {
        profile->hash_node->local_queue_len[priority][SHAPING_DIR_IN] += profile_stat->priority_queue_len[SHAPING_DIR_IN];
        profile->hash_node->local_queue_len[priority][SHAPING_DIR_OUT] += profile_stat->priority_queue_len[SHAPING_DIR_OUT];
        profile_stat->priority_queue_len[SHAPING_DIR_IN] = 0;
        profile_stat->priority_queue_len[SHAPING_DIR_OUT] = 0;
        shaper_stat_priority_queue_len_refresh(ctx, profile->hash_node, priority, curr_time_us);
    }

    if (!need_refresh_stat) {
        return;
    }
    
    shaper_stat_tags_build(rule->vsys_id, rule->id, profile->id, priority, profile_type);

    fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[IN_DROP_PKTS_IDX], tags, TAG_IDX_MAX, profile_stat->in.drop_pkts);
    fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[IN_PKTS_IDX], tags, TAG_IDX_MAX, profile_stat->in.pkts);
    fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[IN_BYTES_IDX], tags, TAG_IDX_MAX, profile_stat->in.bytes);

    fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[OUT_DROP_PKTS_IDX], tags, TAG_IDX_MAX, profile_stat->out.drop_pkts);
    fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[OUT_PKTS_IDX], tags, TAG_IDX_MAX, profile_stat->out.pkts);
    fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[OUT_BYTES_IDX], tags, TAG_IDX_MAX, profile_stat->out.bytes);

    fieldstat_easy_histogram_record(stat->instance, thread_id, stat->latency_histogram_id, tags, TAG_IDX_MAX, profile_stat->out.max_latency);

    if (need_update_guage) {
        if (profile_type == PROFILE_IN_RULE_TYPE_PRIMARY) {
            fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[IN_QUEUE_LEN_IDX], tags, TAG_IDX_MAX, profile_stat->in.queue_len);
            fieldstat_easy_counter_incrby(stat->instance, thread_id, stat->column_ids[OUT_QUEUE_LEN_IDX], tags, TAG_IDX_MAX, profile_stat->out.queue_len);
        }

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

void shaper_stat_refresh(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int force)
{
    struct shaping_rule_info *rule;
    struct timespec curr_time;
    int need_refresh = 0;
    long long curr_time_us;

    clock_gettime(CLOCK_MONOTONIC_COARSE, &curr_time);
    curr_time_us = curr_time.tv_sec * MICRO_SECONDS_PER_SEC + curr_time.tv_nsec / NANO_SECONDS_PER_MICRO_SEC;

    if (force) {
        need_refresh = 1;
    } else {
        if (curr_time_us - sf->stat_update_time_us >= SHAPER_STAT_REFRESH_TIME_US) {
            need_refresh = 1;
            sf->stat_update_time_us = curr_time_us;
        }
    }

    int need_update_guage = sf->processed_pkts > CONFIRM_PRIORITY_PKTS ? 1 : 0;

    if (!need_refresh && !need_update_guage) {
        return;
    }

    for (int i = 0; i < sf->rule_num; i++) {
        rule = &sf->matched_rule_infos[i];
        shaper_stat_profile_metirc_refresh(ctx, rule, &rule->primary, PROFILE_IN_RULE_TYPE_PRIMARY, need_refresh, need_update_guage, curr_time_us);

        for (int j = 0; j < rule->borrowing_num; j++) {
            shaper_stat_profile_metirc_refresh(ctx, rule, &rule->borrowing[j], PROFILE_IN_RULE_TYPE_BORROW, need_refresh, need_update_guage, curr_time_us);
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
        if (!rule->is_enabled) {
            continue;
        }
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

    profile_stat->priority_queue_len[direction]++;

    return;
}

void shaper_stat_queueing_pkt_dec(struct shaping_stat_for_profile *profile_stat, unsigned char direction, int thread_id)
{
    if (direction == SHAPING_DIR_IN) {
        profile_stat->in.queue_len--;
    } else {
        profile_stat->out.queue_len--;
    }

    profile_stat->priority_queue_len[direction]--;

    return;
}

void shaper_stat_queueing_pkt_inc_for_rule(struct shaping_rule_info *rule, unsigned char direction, int thread_id)
{
    shaper_stat_queueing_pkt_inc(&rule->primary.stat, direction, thread_id);
    for (int i = 0; i < rule->borrowing_num; i++) {
        shaper_stat_queueing_pkt_inc(&rule->borrowing[i].stat, direction, thread_id);
    }

    return;
}

void shaper_stat_queueing_pkt_dec_for_rule(struct shaping_rule_info *rule, unsigned char direction, int thread_id)
{
    shaper_stat_queueing_pkt_dec(&rule->primary.stat, direction, thread_id);
    for (int i = 0; i < rule->borrowing_num; i++) {
        shaper_stat_queueing_pkt_dec(&rule->borrowing[i].stat, direction, thread_id);
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

void shaper_stat_output(struct shaping_stat *stat)
{
    char **output_buff_array = NULL;
    size_t array_size = 0;
    fieldstat_easy_output_array_and_reset(stat->instance, &output_buff_array, &array_size);

    for (int i = 0; i < array_size; i++) {
        int status=rd_kafka_produce(stat->topic_rkt, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY, output_buff_array[i], strlen(output_buff_array[i]), NULL, 0, NULL);
        if (status < 0) {
            LOG_ERROR("%s:shaper_stat_output, rd_kafka_produce is error of code: %d %s(%s), status: %d",
                                LOG_TAG_STAT,
                                rd_kafka_last_error(),
                                rd_kafka_err2name(rd_kafka_last_error()),
                                rd_kafka_err2str(rd_kafka_last_error()),
                                status);
        }
        free(output_buff_array[i]);
    }

    return;
}