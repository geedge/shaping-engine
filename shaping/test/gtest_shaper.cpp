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

int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    //testing::GTEST_FLAG(filter) = "single_session.udp_diff_direction";
    return RUN_ALL_TESTS();
}