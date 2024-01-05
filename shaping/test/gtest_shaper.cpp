#include <cstring>
#include <ctime>
#include <fieldstat.h>
#include <gtest/gtest.h>
#include <cjson/cJSON.h>
#include <sys/queue.h>

#include "log.h"
#include "shaper.h"
#include "shaper_maat.h"
#include "shaper_stat.h"
#include "shaper_global_stat.h"
#include "shaper_marsio.h"
#include "stub.h"

#define SHAPING_SESSION_QUEUE_LEN 128
#define SHAPING_STAT_FILE_NAME "/tmp/shaping_metrics.json"
#define SHAPING_GLOBAL_STAT_FILE_NAME "shaping_global_metric"
#define FIELDSTAT_AUTO_TIME_MAX 999999000

char profile_type_primary[] = "primary";
char profile_type_borrow[] = "borrow";

char line[4096];

static struct stub_packet* packet_new(unsigned long long income_time, unsigned int length, unsigned char dir)
{
    struct stub_packet *packet;

    packet = (struct stub_packet*)calloc(1, sizeof(struct stub_packet));
    packet->income_time = income_time;
    packet->length = length;
    packet->direction = dir;

    return packet;
}

static struct stub_packet_node* packet_node_new(stub_packet *packet)
{
    struct stub_packet_node *pkt_node;

    pkt_node = (struct stub_packet_node*)calloc(1, sizeof(struct stub_packet_node));
    pkt_node->raw_packet = packet;

    return pkt_node;
}

static void send_packets(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int pkt_num, int pkt_len, unsigned char dir, 
                        struct stub_pkt_queue *expec_tx_queue, int polling_times, int is_tcp_pure_control)
{
    struct stub_packet_node *pkt_node;
    struct stub_packet *packet;
    struct metadata meta;
    unsigned long long time;

    for (int i = 0; i < pkt_num; i++) {
        memset(&meta, 0, sizeof(meta));

        time = stub_curr_time_ns_get();
        packet = packet_new(time, pkt_len, dir);
        if (expec_tx_queue) {
            pkt_node = packet_node_new(packet);
            TAILQ_INSERT_TAIL(expec_tx_queue, pkt_node, node);
        }

        meta.dir = dir;
        meta.raw_len = pkt_len;
        if (is_tcp_pure_control) {
            meta.is_tcp_pure_ctrl = 1;
        }

        shaping_packet_process(ctx, packet, &meta, sf);

        for (int j = 0; j < polling_times; j++) {
            polling_entry(ctx->sp, ctx->stat, ctx);
        }

        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
    }

    return;
}

static int judge_packet_eq(struct stub_pkt_queue *expec_queue, struct stub_pkt_queue *actual_queue, int num)
{
    struct stub_packet_node *expec_pkt_node;
    struct stub_packet_node *actual_pkt_node;

    for (int i = 0; i < num; i++) {
        if(TAILQ_EMPTY(actual_queue)) {
            return -1;
        }
        expec_pkt_node = TAILQ_FIRST(expec_queue);
        actual_pkt_node = TAILQ_FIRST(actual_queue);
        if (expec_pkt_node->raw_packet != actual_pkt_node->raw_packet) {
            return -2;
        }

        TAILQ_REMOVE(expec_queue, expec_pkt_node, node);
        TAILQ_REMOVE(actual_queue, actual_pkt_node, node);
        free(expec_pkt_node->raw_packet);
        free(expec_pkt_node);
        free(actual_pkt_node);
    }
    
    return 0;
}

static void shaping_stat_judge(char *file_line, int rule_id, int profile_id, int priority, 
                            unsigned long long tx_pkts, unsigned long long tx_bytes,
                            unsigned long long drop_pkts, long long queue_len, long long max_latency,
                            unsigned char direction, char profile_type[])
{
    cJSON *json = NULL;
    cJSON *fields_json = NULL;
    cJSON *tags_json = NULL;
    cJSON *tmp_obj = NULL;
    char attr_name[32] = {0};

    json = cJSON_Parse(file_line);
    ASSERT_TRUE(json != NULL);

    tmp_obj = cJSON_GetObjectItem(json, "name");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_STREQ("traffic_shaping_rule_hits", tmp_obj->valuestring);

    /******************parse tags***********************************/
    tags_json = cJSON_GetObjectItem(json, "tags");
    ASSERT_TRUE(tags_json != NULL);

    tmp_obj = cJSON_GetObjectItem(tags_json, "vsys_id");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(atoi(tmp_obj->valuestring), STUB_TEST_VSYS_ID);

    tmp_obj = cJSON_GetObjectItem(tags_json, "rule_id");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(rule_id, atoi(tmp_obj->valuestring));

    tmp_obj = cJSON_GetObjectItem(tags_json, "profile_id");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(profile_id, atoi(tmp_obj->valuestring));

    tmp_obj = cJSON_GetObjectItem(tags_json, "priority");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(priority, atoi(tmp_obj->valuestring));

    tmp_obj = cJSON_GetObjectItem(tags_json, "profile_type");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_STREQ(tmp_obj->valuestring, profile_type);

    /******************parse fields**********************************/
    fields_json = cJSON_GetObjectItem(json, "fields");
    ASSERT_TRUE(fields_json != NULL);

    snprintf(attr_name, sizeof(attr_name), "%s_pkts", direction == SHAPING_DIR_OUT ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(fields_json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(tx_pkts, tmp_obj->valueint);

    snprintf(attr_name, sizeof(attr_name), "%s_bytes", direction == SHAPING_DIR_OUT ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(fields_json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(tx_bytes, tmp_obj->valueint);

    snprintf(attr_name, sizeof(attr_name), "%s_drop_pkts", direction == SHAPING_DIR_OUT ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(fields_json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(drop_pkts, tmp_obj->valueint);

    if (max_latency != -1) {
        snprintf(attr_name, sizeof(attr_name), "%s_max_latency_us", direction == SHAPING_DIR_OUT ? "out" : "in");
        tmp_obj = cJSON_GetObjectItem(fields_json, attr_name);
        ASSERT_TRUE(tmp_obj != NULL);
        EXPECT_EQ(max_latency, tmp_obj->valueint);
    }

    snprintf(attr_name, sizeof(attr_name), "%s_queue_len", direction == SHAPING_DIR_OUT ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(fields_json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(queue_len, tmp_obj->valueint);

    cJSON_Delete(json);

    return;
}

static int shaping_global_stat_field_get(cJSON *metrics, const char *field_name)
{
    int metrics_size = cJSON_GetArraySize(metrics);

    for (int i = 0; i < metrics_size; i++) {
        cJSON *tmp = cJSON_GetArrayItem(metrics, i);
        char *column_name = cJSON_GetObjectItem(tmp, "name")->valuestring;
        if (strcmp(column_name, field_name) == 0) {
            return cJSON_GetObjectItem(tmp, "diff")->valueint;
        }
    }

    return -1;
}

static void shaping_global_stat_judge(char *file_line, int tx_pkts, int tx_bytes, int drop_pkts, int drop_bytes, int queueing_pkts, int queueing_bytes)
{
    cJSON *metrics = NULL;
    cJSON *json = cJSON_Parse(file_line);

    metrics = cJSON_GetObjectItem(json, "metrics");

    EXPECT_EQ(tx_pkts, shaping_global_stat_field_get(metrics, "all_tx_pkts"));
    EXPECT_EQ(tx_bytes, shaping_global_stat_field_get(metrics, "all_tx_bytes"));
    EXPECT_EQ(tx_pkts, shaping_global_stat_field_get(metrics, "shape_tx_pkts"));
    EXPECT_EQ(tx_bytes, shaping_global_stat_field_get(metrics, "shape_tx_bytes"));
    EXPECT_EQ(drop_pkts, shaping_global_stat_field_get(metrics, "all_drop_pkts"));
    EXPECT_EQ(drop_bytes, shaping_global_stat_field_get(metrics, "all_drop_bytes"));
    EXPECT_EQ(drop_pkts, shaping_global_stat_field_get(metrics, "shape_drop_pkts"));
    EXPECT_EQ(drop_bytes, shaping_global_stat_field_get(metrics, "shape_drop_bytes"));
    EXPECT_EQ(queueing_pkts, shaping_global_stat_field_get(metrics, "curr_queueing_pkts"));
    EXPECT_EQ(queueing_bytes, shaping_global_stat_field_get(metrics, "curr_queueing_bytes"));

    cJSON_Delete(json);

    return;
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(single_session, udp_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);


    /**********send packets*********************/
    send_packets(&ctx->thread_ctx[0], sf, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    /*******************************************/

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy
    shaper_global_stat_refresh(ctx);

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    //judge shaping metric
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 0, 0, 170000, SHAPING_DIR_OUT, profile_type_primary);//max latency is last 10 pkts
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);

    //judge shaping global metric
    stat_file = fopen(SHAPING_GLOBAL_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_global_stat_judge(line, 100, 10000, 0, 0, 0, 0);
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(max_min_host_fairness_profile, udp_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    stub_set_profile_type(0, PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);


    /**********send packets*********************/
    send_packets(&ctx->thread_ctx[0], sf, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    /*******************************************/

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy
    shaper_global_stat_refresh(ctx);

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    //judge shaping metric
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 0, 0, 170000, SHAPING_DIR_OUT, profile_type_primary);//max latency is last 10 pkts
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);

    //judge shaping global metric
    stat_file = fopen(SHAPING_GLOBAL_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_global_stat_judge(line, 100, 10000, 0, 0, 0, 0);
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(single_session, tcp_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue expec_pure_ctl_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    TAILQ_INIT(&expec_tx_queue);
    TAILQ_INIT(&expec_pure_ctl_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 20, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf, 10, 100, SHAPING_DIR_OUT, &expec_pure_ctl_tx_queue, 1, 1);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    //10 pure ctrl pkts
    ASSERT_EQ(0, judge_packet_eq(&expec_pure_ctl_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));


    /***********send stat data here********************/
    shaper_stat_refresh(&ctx->thread_ctx[0], sf, ctx->thread_ctx[0].thread_index, 1);
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy
    sleep(2);//wait telegraf generate metric


    stub_refresh_token_bucket(0);
    for (int i = 0; i < 10; i++) {
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
    }
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));//pure ctrl pkts force consume 1000 tokens, current token: -1000--->0, so no pkt can be sent

    stub_refresh_token_bucket(0);
    for (int i = 0; i < 10; i++) {//10 pkts which is not pure control
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
    }
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();


    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 20, 2000, 0, 10, 0, SHAPING_DIR_OUT, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 10, 1000, 0, 0, 30000, SHAPING_DIR_OUT, profile_type_primary);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      profile: limit 1000
                direction OUT*/
TEST(single_session, udp_diff_direction)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 10, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf, 10, 100, SHAPING_DIR_IN, &expec_tx_queue, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf, 10, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf, 10, 100, SHAPING_DIR_IN, &expec_tx_queue, 1, 0);


    //first 10 out packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    //10 in pcakets without consume token
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));

    stub_refresh_token_bucket(0);
    for (int i = 0; i < 20; i++) {
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
    }
    //10 out packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    //10 in packtets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();


    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 20, 2000, 0, 0, 20000, SHAPING_DIR_OUT, profile_type_primary);

    shaping_stat_judge(line, 0, 0, 1, 20, 2000, 0, 0, 20000, SHAPING_DIR_IN, profile_type_primary);
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1, rule2, rule3
  rule1:
      profile1: limit 1200
  rule2:
      profile2: limit 1100
  rule3:
      profile3: limit 1000*/
TEST(single_session, udp_multi_rules)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0, 1, 2};
    int priority[] = {1, 2, 3};
    int profile_num[] = {1, 1, 1};
    int profile_id[][MAX_REF_PROFILE] = {{0}, {1}, {2}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(3, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 3000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(1, 2000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(2, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 3);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 2, 0);


    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        stub_refresh_token_bucket(1);
        stub_refresh_token_bucket(2);
        for (int i = 0; i < 60; i++) {
            //there are 3 rules, send one packet need 3 polling process, so 10 packets need 30 polling
            //even though invoke polling more than 30 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 0
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 0, 0, 506000, SHAPING_DIR_OUT, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1 
    shaping_stat_judge(line, 1, 1, 1, 100, 10000, 0, 0, 1000, SHAPING_DIR_OUT, profile_type_primary);//latency of every queued pkt is 1

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2
    shaping_stat_judge(line, 2, 2, 1, 100, 10000, 0, 0, 90000, SHAPING_DIR_OUT, profile_type_primary);//max latency is first queued pkt

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      profile1: limit 0
      profile2: limit 1000*/
TEST(single_session, udp_borrow)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {1};
    int priority[] = {1};
    int profile_num[] = {2};
    int profile_id[][MAX_REF_PROFILE] = {{1, 2}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(1, 0, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(2, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);


    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(2);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1, primary
    shaping_stat_judge(line, 1, 1, 1, 0, 0, 0, 0, 170000, SHAPING_DIR_OUT, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2, borrow
    shaping_stat_judge(line, 1, 2, 2, 100, 10000, 0, 0, 0, SHAPING_DIR_OUT, profile_type_borrow);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      priority: 9
      profile1: limit 0
      profile2: limit 0
      profile3: limit 1000*/
TEST(single_session, udp_borrow_same_priority_9)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {1};
    int priority[] = {9};
    int profile_num[] = {3};
    int profile_id[][MAX_REF_PROFILE] = {{1, 2, 3}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(1, 0, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(2, 0, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(3, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);


    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(3);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1, primary
    shaping_stat_judge(line, 1, 1, 9, 0, 0, 0, 0, 170000, SHAPING_DIR_OUT, profile_type_primary);

#if 0 //fieldstat don't output a row when all values is zero
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2, borrow
    shaping_stat_judge(line, 1, 2, 9, 0, 0, 0, 0, 0, SHAPING_DIR_OUT, profile_type_borrow);
#endif

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 3, borrow
    shaping_stat_judge(line, 1, 3, 9, 100, 10000, 0, 0, 0, SHAPING_DIR_OUT, profile_type_borrow);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1, session2 match rule2
  rule1:
      priority:2
      profile1: limit 1000
  rule2:
      priority:1
      profile2: limit 1000*/
TEST(two_session_diff_priority, udp_in_order)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_ids[] = {0, 1};
    long long rule_id1[] = {0};
    long long rule_id2[] = {1};
    int profile_nums[] = {1, 1};
    int prioritys[] = {2, 1};
    int profile_ids[][MAX_REF_PROFILE] = {{0}, {1}};
    
    stub_init();
    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(2, rule_ids, prioritys, profile_nums, profile_ids);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(1, 1000, SHAPING_DIR_OUT);

    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id1, 1);
    shaper_rules_update(&ctx->thread_ctx[0], sf2, rule_id2, 1);
    

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf1, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue1, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf2, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue2, 1, 0);


    //first 10 packets for stream1
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));

    //first 10 packets for stream2
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));

    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue2)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        stub_refresh_token_bucket(1);
        for (int i = 0; i < 10; i++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));//stream2 priority 1, first
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        stub_refresh_token_bucket(1);
        for (int i = 0; i < 10; i++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));//stream1 priority 2
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[0], sf2);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 0
    shaping_stat_judge(line, 0, 0, 2, 100, 10000, 0, 0, 280000, SHAPING_DIR_OUT, profile_type_primary);//max latency is every queued pkts

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1
    shaping_stat_judge(line, 1, 1, 1, 100, 10000, 0, 0, 90000, SHAPING_DIR_OUT, profile_type_primary);//max latency is every queued pkts

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1,rule2,rule4; session2 match rule3
  rule1:
      priority:1
      profile1: limit 1000
  rule2:
      priority:2
      profile2: limit 1000
  rule3:
      priority:3
      profile3: limit 1000
  rule4:
      priority:4
      profile4: limit 1000*/
TEST(two_session_diff_priority, udp_in_order_multi_rule)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_ids[] = {1, 2, 3, 4};
    long long rule_id1[] = {1, 2, 4};
    long long rule_id2[] = {3};
    int profile_nums[] = {1, 1, 1, 1};
    int prioritys[] = {1, 2, 3, 4};
    int profile_ids[][MAX_REF_PROFILE] = {{1}, {2}, {3}, {4}};


    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(4, rule_ids, prioritys, profile_nums, profile_ids);

    stub_set_token_bucket_avl_per_sec(1, 1000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(2, 1000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(3, 1000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(4, 1000, SHAPING_DIR_OUT);

    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id1, 3);
    shaper_rules_update(&ctx->thread_ctx[0], sf2, rule_id2, 1);
    

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf1, 20, 100, SHAPING_DIR_OUT, &expec_tx_queue1, 2, 0);
    send_packets(&ctx->thread_ctx[0], sf2, 20, 100, SHAPING_DIR_OUT, &expec_tx_queue2, 1, 0);


    //first 10 packets for stream1
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));

    //first 10 packets for stream2
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));

    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    
    while (!TAILQ_EMPTY(&expec_tx_queue1)) {//stream1, priority 1, last 10 packets
        stub_refresh_token_bucket(1);
        stub_refresh_token_bucket(2);
        stub_refresh_token_bucket(3);
        stub_refresh_token_bucket(4);
        for (int i = 0; i < 30; i++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(1);
        stub_refresh_token_bucket(2);
        stub_refresh_token_bucket(3);
        stub_refresh_token_bucket(4);
        for (int i = 0; i < 10; i++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));//stream2 priority 3
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[0], sf2);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 1, 1, 1, 20, 2000, 0, 0, 48000, SHAPING_DIR_OUT, profile_type_primary);//profile_id 1, max latency is last pkt

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 2, 2, 1, 20, 2000, 0, 0, 1000, SHAPING_DIR_OUT, profile_type_primary);//profile_id 2, evevy queued pkt's latency is 1

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 4, 4, 1, 20, 2000, 0, 0, 1000, SHAPING_DIR_OUT, profile_type_primary);//profile_id 4, max latency is first queued pkt

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 3, 3, 3, 20, 2000, 0, 0, 40000, SHAPING_DIR_OUT, profile_type_primary);//profile_id 3, every queued pkt's latency is 40

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      priority:1
      profile1: limit 1000, first 20 pkts async, then sync
*/
TEST(single_session_async, udp_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};


    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);
    
    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    stub_set_async_token_get_times(0, 20);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);
    

    /*******packets, OP_STATE_DATA***********/
    send_packets(&ctx->thread_ctx[0], sf, 20, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);

    sleep(1);//wait stub async thread complete

    send_packets(&ctx->thread_ctx[0], sf, 80, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);


    //first 10 packets, got token
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 0, 0, 170000, SHAPING_DIR_OUT, profile_type_primary);//max latency is last 10 pkts
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      priority:1
      profile1: limit 1000, first 20 pkts async, then sync
*/
TEST(single_session_async, udp_close_before_async_exec)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    stub_set_async_token_get_times(0, 20);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);
    

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 10, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));//async callback haven't been called, no token, no packet be sent
    sf->flag |= SESSION_CLOSE;// receive close ctrlbuf

    stub_set_async_token_get_times(0, 0);//refresh async count, async thread will be executed
    sleep(1);//ensure async thread exec complete

    for (int i = 0; i < 10; i++) {
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
    }

    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    
    fieldstat_global_disable_prometheus_endpoint();
    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();
}

/*session1 match rule1; session2 match rule2
  rule1:
      priority:1
      primary profile_a: (priority 1)
      borrow profile_b: (priority 2)
  rule2:
      priority:1
      primary profile_b: (priority 1)

profile_a: limit 0;  profile_b:limit 1000
*/
TEST(two_session_diff_priority_same_profile, udp_borrow_in_order)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_ids[] = {1, 2};
    long long rule_id1[] = {1};
    long long rule_id2[] = {2};
    int profile_nums[] = {2, 1};
    int prioritys[] = {1, 1};
    int profile_ids[][MAX_REF_PROFILE] = {{1, 2}, {2}};


    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf2 != NULL);


    stub_set_matched_shaping_rules(2, rule_ids, prioritys, profile_nums, profile_ids);

    stub_set_token_bucket_avl_per_sec(1, 0, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(2, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id1, 1);
    shaper_rules_update(&ctx->thread_ctx[0], sf2, rule_id2, 1);


    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf1, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue1, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf2, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue2, 1, 0);
    

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(2);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }
    shaper_stat_refresh(&ctx->thread_ctx[0], sf2, 0, 1);//refresh stat, to ensure priority queue_len in swarmkv is correct

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {//last 90 delay packets
        stub_refresh_token_bucket(2);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[0], sf2);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1, primary
    shaping_stat_judge(line, 1, 1, 1, 0, 0, 0, 0, 470000, SHAPING_DIR_OUT, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2, borrow
    shaping_stat_judge(line, 1, 2, 2, 100, 10000, 0, 0, 0, SHAPING_DIR_OUT, profile_type_borrow);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2, primary
    shaping_stat_judge(line, 2, 2, 1, 100, 10000, 0, 0, 190000, SHAPING_DIR_OUT, profile_type_primary);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1; session2 match rule1
  rule1:
      priority:1
      primary profile_a:

profile_a: limit 1000
*/
TEST(two_session_same_rule, udp_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_id[] = {1};
    int profile_num[] = {1};
    int priority[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{1}};
    

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);

    stub_set_token_bucket_avl_per_sec(1, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();

    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id, 1);
    shaper_rules_update(&ctx->thread_ctx[0], sf2, rule_id, 1);

    /*******packets, OP_STATE_DATA***********/
    send_packets(&ctx->thread_ctx[0], sf1, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf2, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 190 delay packets
        stub_refresh_token_bucket(1);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[0], sf2);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 1, 1, 1, 200, 20000, 0, 0, 370000, SHAPING_DIR_OUT, profile_type_primary);
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1; session2 match rule2
  rule1:
      priority:1
      primary profile_a: (priority 1)
  rule2:
      priority:2
      primary profile_a: (priority 2)

profile_a: limit 1000
*/
TEST(two_session_diff_priority_same_profile, two_thread_udp_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_ids[] = {1, 2};
    long long rule_id1[] = {1};
    long long rule_id2[] = {2};
    int profile_nums[] = {1, 1};
    int prioritys[] = {1, 2};
    int profile_id[][MAX_REF_PROFILE] = {{0}, {0}};
    

    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[1]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(2, rule_ids, prioritys, profile_nums, profile_id);

    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id1, 1);
    shaper_rules_update(&ctx->thread_ctx[1], sf2, rule_id2, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf1, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue1, 1, 0);
    send_packets(&ctx->thread_ctx[1], sf2, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue2, 1, 0);
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    shaper_stat_refresh(&ctx->thread_ctx[0], sf1, 0, 1);//刷新线程0中的优先级队列长度到swarmkv中
    stub_curr_time_s_inc(1);//inc time to refresh hmget interval
    for (int i = 0; i < 10; i++) {//线程1中的session优先级为2，被线程0中优先级为1的session阻断
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[1].sp, ctx->thread_ctx[1].stat, &ctx->thread_ctx[1]);
        ASSERT_EQ(-1, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 1));//优先级低，不能发出报文
    }

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 1));//sf1 priority 1
    }

    shaper_stat_refresh(&ctx->thread_ctx[0], sf1, 0, 1);//刷新线程0中的优先级队列长度到swarmkv中
    stub_curr_time_s_inc(1);//inc time to refresh hmget interval
    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[1].sp, ctx->thread_ctx[1].stat, &ctx->thread_ctx[1]);

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 1));//sf2 priority 2
    }

    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[1], sf2);
    fieldstat_global_disable_prometheus_endpoint();
    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();
}

/*session1 match rule1; session2 match rule2
  rule1:
      priority:1
      primary profile_a: (priority 1)
  rule2:
      priority:2
      primary profile_a: (priority 2)

profile_a: limit 1000
*/
TEST(two_session_diff_priority_same_profile, session_timer_test)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_ids[] = {1, 2};
    long long rule_id1[] = {1};
    long long rule_id2[] = {2};
    int profile_nums[] = {1, 1};
    int prioritys[] = {1, 2};
    int profile_id[][MAX_REF_PROFILE] = {{0}, {0}};
    

    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[1]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(2, rule_ids, prioritys, profile_nums, profile_id);

    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id1, 1);
    shaper_rules_update(&ctx->thread_ctx[1], sf2, rule_id2, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf1, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue1, 1, 0);
    send_packets(&ctx->thread_ctx[1], sf2, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue2, 1, 0);
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    sleep(3);//wait session timer to expire, to refresh priority queue_len to swarmkv
    for (int i = 0; i < 500; i++) {
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);//inc time to refresh stat in timer
    }
    polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);//timer triggered in polling
    polling_entry(ctx->thread_ctx[1].sp, ctx->thread_ctx[1].stat, &ctx->thread_ctx[1]);

    stub_curr_time_s_inc(1);//inc time to refresh hmget interval
    for (int i = 0; i < 10; i++) {//线程1中的session优先级为2，被线程0中优先级为1的session阻断
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[1].sp, ctx->thread_ctx[1].stat, &ctx->thread_ctx[1]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);

        ASSERT_EQ(-1, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 1));//优先级低，不能发出报文
    }

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 1));//sf1 priority 1
    }

    sleep(3);//wait session timer to expire, to refresh priority queue_len to swarmkv
    for (int i = 0; i < 500; i++) {
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);//inc time to refresh stat in timer
    }
    polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);//timer triggered in polling
    polling_entry(ctx->thread_ctx[1].sp, ctx->thread_ctx[1].stat, &ctx->thread_ctx[1]);

    stub_curr_time_s_inc(1);//inc time to refresh hmget interval
    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[1].sp, ctx->thread_ctx[1].stat, &ctx->thread_ctx[1]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 1));//sf2 priority 2
    }

    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[1], sf2);
    fieldstat_global_disable_prometheus_endpoint();
    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();
}

/*session1 match rule1 & rule2; session2 match rule3
  rule1:
      priority:1
      primary profile_a: (priority 1)
  rule2:
      priority:2
      primary profile_b: (priority 2)
  rule3:
      priority:3
      primary profile_a: (priority 3)

profile_a(id 0): limit 3000
profile_b(id 1): limit 1000
*/
TEST(two_sessions, priority_non_block)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_ids[] = {1, 2, 3};
    long long rule_id1[] = {1, 2};
    long long rule_id2[] = {3};
    int profile_nums[] = {1, 1, 1};
    int prioritys[] = {1, 2, 3};
    int profile_id[][MAX_REF_PROFILE] = {{0}, {1}, {0}};
    

    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[1]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(3, rule_ids, prioritys, profile_nums, profile_id);

    stub_set_token_bucket_avl_per_sec(0, 3000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(1, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id1, 2);
    shaper_rules_update(&ctx->thread_ctx[1], sf2, rule_id2, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf1, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue1, 2, 0);//sf1 blocked by rule2(profile id 1), while rule3(profile id 0) still has 1000 token
    send_packets(&ctx->thread_ctx[1], sf2, 10, 100, SHAPING_DIR_OUT, &expec_tx_queue2, 1, 0);
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));//sf1 should send 10 pkts
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));//sf2 should send 10 pkts cause rule3(profile id 0) has 1000 token
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {
        stub_refresh_token_bucket(0);
        stub_refresh_token_bucket(1);
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);//tow rules per pkt need two polling

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 1));//sf1 remaining 90 pkts
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[1], sf2);
    fieldstat_global_disable_prometheus_endpoint();
    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();
}

/*session1 match rule1; session2 match rule2
  rule1:
      priority:1
      primary profile_a: (priority 1)
  rule2:
      priority:2
      primary profile_a: (priority 2)
      borrow  profile_b: (priority 3)

profile_a(id 0): limit 1000
profile_b(id 1): limit 1000
*/
TEST(two_sessions, priority_block_borrow)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    long long rule_ids[] = {1, 2};
    long long rule_id1[] = {1};
    long long rule_id2[] = {2};
    int profile_nums[] = {1, 2};
    int prioritys[] = {1, 2};
    int profile_id[][MAX_REF_PROFILE] = {{0}, {0, 1}};
    

    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[1]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(2, rule_ids, prioritys, profile_nums, profile_id);

    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(1, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf1, rule_id1, 1);
    shaper_rules_update(&ctx->thread_ctx[1], sf2, rule_id2, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf1, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue1, 1, 0);
    send_packets(&ctx->thread_ctx[1], sf2, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue2, 1, 0);
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));

    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(1);
        polling_entry(ctx->thread_ctx[1].sp, ctx->thread_ctx[1].stat, &ctx->thread_ctx[1]);//primary profile blocked by priority, send by borrow profile

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 1));
    }

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 1));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[1], sf2);
    fieldstat_global_disable_prometheus_endpoint();
    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(statistics, udp_drop_pkt)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);
    
    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, SHAPING_SESSION_QUEUE_LEN + 10, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf, 100, 100, SHAPING_DIR_OUT, NULL, 1, 0);//these 100 pkts will be dropped

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 1));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy
    shaper_global_stat_refresh(ctx);

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    FILE *stat_file;

    //judge shaping metric
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, SHAPING_SESSION_QUEUE_LEN+10, (SHAPING_SESSION_QUEUE_LEN+10)*100, 100, 0, 228000, SHAPING_DIR_OUT, profile_type_primary);//every queued pkt's latency is max
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);

    //judge shaping global metric
    stat_file = fopen(SHAPING_GLOBAL_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_global_stat_judge(line, SHAPING_SESSION_QUEUE_LEN+10, (SHAPING_SESSION_QUEUE_LEN+10)*100, 100, 10000, 0, 0);
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(statistics, udp_queueing_pkt)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    long long rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};
    FILE *stat_file;

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 100, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);


    /***********send stat data here********************/
    shaper_stat_refresh(&ctx->thread_ctx[0], sf, ctx->thread_ctx[0].thread_index, 1);
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy
    shaper_global_stat_refresh(ctx);
    sleep(2);//wait telegraf generate metric

    /*******judge global metric********/
    stat_file = fopen(SHAPING_GLOBAL_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_global_stat_judge(line, 10, 1000, 0, 0, 90, 9000);
    fclose(stat_file);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        stub_curr_time_ns_inc(STUB_TIME_INC_FOR_PACKET);
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 1));
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    fieldstat_global_disable_prometheus_endpoint();

    /***********send stat data here********************/
    fieldstat_dynamic_passive_output(ctx->stat->instance);//send metric manualy
    shaper_global_stat_refresh(ctx);

    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output

    //judge shaping metric
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//stat data first sent
    shaping_stat_judge(line, 0, 0, 1, 10, 1000, 0, 90, 0, SHAPING_DIR_OUT, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//stat data last sent
    shaping_stat_judge(line, 0, 0, 1, 90, 9000, 0, 0, 90000, SHAPING_DIR_OUT, profile_type_primary);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);

    //judge global metric
    stat_file = fopen(SHAPING_GLOBAL_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_global_stat_judge(line, 90, 9000, 0, 0, -90, -9000);
    fclose(stat_file);
}

int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    //testing::GTEST_FLAG(filter) = "two_sessions.priority_block_borrow";
    return RUN_ALL_TESTS();
}