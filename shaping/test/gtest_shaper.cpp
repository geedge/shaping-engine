#include <cstring>
#include <gtest/gtest.h>

#include "shaper.h"
#include "shaper_maat.h"
#include "shaper_marsio.h"
#include "stub.h"

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

        time = stub_curr_time_get();
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

        shaping_stream_process(ctx, packet, &meta, sf);

        for (int j = 0; j < polling_times; j++) {
            polling_entry(ctx->sp, ctx->stat, ctx);
        }

        stub_curr_time_inc();
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
    sf = shaping_flow_new();
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
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(sf);
    shaping_engine_destroy(ctx);
    
    stub_clear_matched_shaping_rules();

#if 0
    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 100, 10000, 0, 0, 170, 0, DIR_ROUTE_UP, profile_type_primary);//max latency is last 10 pkts
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
#endif
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(single_session, tcp_tx_in_order)
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
    sf = shaping_flow_new();
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    shaper_rules_update(&ctx->thread_ctx[0], sf, rule_id, 1);

    /*******send packets***********/
    send_packets(&ctx->thread_ctx[0], sf, 20, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 0);
    send_packets(&ctx->thread_ctx[0], sf, 20, 100, SHAPING_DIR_OUT, &expec_tx_queue, 1, 1);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

#if 0
    /***********send stat data here********************/
    stub_shaper_stat_send(0);
    sleep(2);//wait telegraf generate metric
#endif

    stub_refresh_token_bucket(0);
    for (int i = 0; i < 10; i++) {//10 pkts which is not pure control
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        stub_curr_time_inc();
    }
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//20 pure contorl pkts
        polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 1));
        stub_curr_time_inc();
    }

    shaping_flow_free(sf);
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

#if 0
    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 40, 4000, 10, 1000, 0, 30, 0, 1, DIR_ROUTE_UP, profile_type_primary);//max latency is first queueing pkts

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 0, 0, 30, 3000, 0, 0, 30, 0, DIR_ROUTE_UP, profile_type_primary);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
#endif
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
    sf = shaping_flow_new();
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
        stub_curr_time_inc();
    }
    //10 out packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    //10 in packtets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    shaping_flow_free(sf);
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

#if 0
    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 20, 2000, 20, 2000, 0, 0, 20, 0, DIR_ROUTE_UP, profile_type_primary);//max latency is last 10 pkts
    shaping_stat_judge(line, 0, 0, 1, 20, 2000, 20, 2000, 0, 0, 20, 0, DIR_ROUTE_DOWN, profile_type_primary);
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
#endif
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
    sf = shaping_flow_new();
    ASSERT_TRUE(sf != NULL);

    stub_set_matched_shaping_rules(3, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1200, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(1, 1100, SHAPING_DIR_OUT);
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
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(sf);
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

#if 0
    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 0
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 100, 10000, 0, 0, 506, 0, DIR_ROUTE_UP, profile_type_primary);//max latency is last pkt

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1 
    shaping_stat_judge(line, 1, 1, 2, 100, 10000, 100, 10000, 0, 0, 1, 0, DIR_ROUTE_UP, profile_type_primary);//latency of every queued pkt is 1

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2
    shaping_stat_judge(line, 2, 2, 3, 100, 10000, 100, 10000, 0, 0, 90, 0, DIR_ROUTE_UP, profile_type_primary);//max latency is first queued pkt

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
#endif
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
    sf = shaping_flow_new();
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
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(sf);
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

#if 0
    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1, primary
    shaping_stat_judge(line, 1, 1, 1, 0, 0, 0, 0, 0, 0, 170, 0, DIR_ROUTE_UP, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2, borrow
    shaping_stat_judge(line, 1, 2, 2, 100, 10000, 100, 10000, 0, 0, 170, 0, DIR_ROUTE_UP, profile_type_borrow);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
#endif
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
    sf1 = shaping_flow_new();
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new();
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
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));//stream2 priority 1, first
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        stub_refresh_token_bucket(1);
        for (int i = 0; i < 10; i++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));//stream1 priority 2
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(sf1);
    shaping_flow_free(sf2);
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

#if 0
    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 0
    shaping_stat_judge(line, 0, 0, 2, 100, 10000, 100, 10000, 0, 0, 280, 0, DIR_ROUTE_UP, profile_type_primary);//max latency is every queued pkts

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1
    shaping_stat_judge(line, 1, 1, 1, 100, 10000, 100, 10000, 0, 0, 90, 0, DIR_ROUTE_UP, profile_type_primary);//max latency is every queued pkts

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
#endif
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
    sf1 = shaping_flow_new();
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new();
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

    {//stream1 缓存首报文通过rule1和rule2，进入rule4等待，优先级4
        stub_refresh_token_bucket(1);
        stub_refresh_token_bucket(2);
        stub_refresh_token_bucket(3);
        stub_refresh_token_bucket(4);
        for (int j = 0; j < 2; j++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_inc();
        }
        
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(1);
        stub_refresh_token_bucket(2);
        stub_refresh_token_bucket(3);
        stub_refresh_token_bucket(4);
        for (int i = 0; i < 10; i++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));//stream2 priority 3, first
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }
    
    while (!TAILQ_EMPTY(&expec_tx_queue1)) {//stream1 last 10 packets
        stub_refresh_token_bucket(1);
        stub_refresh_token_bucket(2);
        stub_refresh_token_bucket(3);
        stub_refresh_token_bucket(4);
        for (int i = 0; i < 30; i++) {
            polling_entry(ctx->thread_ctx[0].sp, ctx->thread_ctx[0].stat, &ctx->thread_ctx[0]);
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_flow_free(sf1);
    shaping_flow_free(sf2);
    shaping_engine_destroy(ctx);
    stub_clear_matched_shaping_rules();

#if 0
    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 1, 1, 1, 20, 2000, 20, 2000, 0, 0, 58, 0, DIR_ROUTE_UP, profile_type_primary);//profile_id 1, max latency is last pkt

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 2, 2, 2, 20, 2000, 20, 2000, 0, 0, 1, 0, DIR_ROUTE_UP, profile_type_primary);//profile_id 2, evevy queued pkt's latency is 1

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 4, 4, 4, 20, 2000, 20, 2000, 0, 0, 11, 0, DIR_ROUTE_UP, profile_type_primary);//profile_id 4, max latency is first queued pkt

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 3, 3, 3, 20, 2000, 20, 2000, 0, 0, 12, 0, DIR_ROUTE_UP, profile_type_primary);//profile_id 3, every queued pkt's latency is 12

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
#endif
}

int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    //testing::GTEST_FLAG(filter) = "two_session_diff_priority.udp_in_order";
    return RUN_ALL_TESTS();
}