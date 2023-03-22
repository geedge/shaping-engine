#include <MESA/stream.h>
#include <MESA/cJSON.h>
#include <gtest/gtest.h>
#include <vector>
#include <stdlib.h>
#include <time.h>

#include "stub.h"

#define SHAPING_STAT_FILE_NAME "/tmp/shaping_metrics.json"
#define SHAPING_SESSION_QUEUE_LEN 100
#define SHAPING_SESSIONS_LIMIT_PER_AVL 1000

char profile_type_primary[] = "primary";
char profile_type_borrow[] = "borrow";


using namespace std;

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

static void send_packets(struct streaminfo *stream, void **pme, int pkt_num, int pkt_len,
                        unsigned char dir, struct stub_pkt_queue *expec_tx_queue, int polling_times,
                        int is_tcp, int is_pure_control)
{
    struct stub_packet_node *pkt_node;
    struct stub_packet *packet;
    unsigned long long time;
    char ret;

    stream->threadnum = 0;//just 1 thread for test!!!

    for (int i = 0; i < pkt_num; i++) {
        time = stub_curr_time_get();
        packet = packet_new(time, pkt_len, dir);
        if (expec_tx_queue) {
            pkt_node = packet_node_new(packet);
            TAILQ_INSERT_TAIL(expec_tx_queue, pkt_node, node);
        }

        time++;

        stream->routedir = dir;
        stream->hash_index = pkt_len;//just for stub test!!!!! use hash_index to store pkt_len
        if (is_tcp) {
            stream->ptcpdetail->clientpktnum++;
            if (is_pure_control) {
                stream->ptcpdetail->pdata = NULL;
            } else {
                stream->ptcpdetail->pdata = (void*)0x1234;//just for stub test, will not access this pointer
            }
        } else {
            stream->pudpdetail->clientpktnum++;
        }
        
        if (is_tcp) {
            ret = tcp_allpkt_raw_process(stream, NULL, packet, pme);
        } else {
            ret = udp_raw_process(stream, NULL, packet, pme);
        }
        
        if (ret == APP_STATE_GIVEME) {
            if (!packet->detained_flag) {
                stub_send_packet(packet);
            }
        }

        for (int j = 0; j < polling_times; j++) {
            polling_entry(NULL, NULL, 0, NULL);
        }

        stub_curr_time_inc();
    }
}

static void stream_state_change_udp(struct streaminfo *stream, void **pme, unsigned char state)
{
    stream->opstate = state;
    //udp_process(stream, pme, 0, NULL);//just change state, no packet
    if (state == OP_STATE_PENDING) {
        stub_stream_bridge_sync_cb_invoke(stream);
    } else if(state == OP_STATE_CLOSE) {
        stub_stream_bridge_free_cb_invoke(stream);
    }

    udp_raw_process(stream, NULL, NULL, pme);//just change state, no packet

    return;
}

static void stream_state_change_tcp(struct streaminfo *stream, void **pme, unsigned char state)
{
    stream->pktstate = state;
    //tcp_allpkt_process(stream, pme, 0, NULL);//just change state, no packet
    if (state == OP_STATE_PENDING) {
        stub_stream_bridge_sync_cb_invoke(stream);
    } else if(state == OP_STATE_CLOSE) {
        stub_stream_bridge_free_cb_invoke(stream);
    }

    tcp_allpkt_raw_process(stream, NULL, NULL, pme);//just change state, no packet

    return;
}

static void shaping_stat_judge(char *file_line, int rule_id, int profile_id, int priority, 
                            unsigned long long rx_pkts, unsigned long long rx_bytes, 
                            unsigned long long tx_pkts, unsigned long long tx_bytes,
                            unsigned long long drop_pkts, long long queue_len, long long max_latency,
                            int queueing_sessions, unsigned char direction, char profile_type[])
{
    cJSON *json = NULL;
    cJSON *tmp_obj = NULL;
    char attr_name[32] = {0};

    json = cJSON_Parse(file_line);
    ASSERT_TRUE(json != NULL);

    tmp_obj = cJSON_GetObjectItem(json, "rule_id");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(rule_id, atoi(tmp_obj->valuestring));

    tmp_obj = cJSON_GetObjectItem(json, "profile_id");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(profile_id, atoi(tmp_obj->valuestring));

    tmp_obj = cJSON_GetObjectItem(json, "priority");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(priority, atoi(tmp_obj->valuestring));

    tmp_obj = cJSON_GetObjectItem(json, "profile_type");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_STREQ(tmp_obj->valuestring, profile_type);

    tmp_obj = cJSON_GetObjectItem(json, "queueing_sessions");
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(queueing_sessions, tmp_obj->valueint);

    snprintf(attr_name, sizeof(attr_name), "%s_rx_pkts", direction == DIR_ROUTE_UP ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(rx_pkts, tmp_obj->valueint);

    snprintf(attr_name, sizeof(attr_name), "%s_rx_bytes", direction == DIR_ROUTE_UP ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(rx_bytes, tmp_obj->valueint);

    snprintf(attr_name, sizeof(attr_name), "%s_tx_pkts", direction == DIR_ROUTE_UP ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(tx_pkts, tmp_obj->valueint);

    snprintf(attr_name, sizeof(attr_name), "%s_tx_bytes", direction == DIR_ROUTE_UP ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(tx_bytes, tmp_obj->valueint);

    snprintf(attr_name, sizeof(attr_name), "%s_drop_pkts", direction == DIR_ROUTE_UP ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(drop_pkts, tmp_obj->valueint);

    if (max_latency != -1) {
        snprintf(attr_name, sizeof(attr_name), "%s_max_latency_us", direction == DIR_ROUTE_UP ? "out" : "in");
        tmp_obj = cJSON_GetObjectItem(json, attr_name);
        ASSERT_TRUE(tmp_obj != NULL);
        EXPECT_EQ(max_latency, tmp_obj->valueint);
    }

    snprintf(attr_name, sizeof(attr_name), "%s_queue_len", direction == DIR_ROUTE_UP ? "out" : "in");
    tmp_obj = cJSON_GetObjectItem(json, attr_name);
    ASSERT_TRUE(tmp_obj != NULL);
    EXPECT_EQ(queue_len, tmp_obj->valueint);

    cJSON_Delete(json);

    return;
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
    struct streaminfo stream;
    void **pme = (void**)calloc(1, sizeof(void*));
    int rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    memset(&stream, 0, sizeof(struct streaminfo));
    stream.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ASSERT_TRUE(shaping_master_init() >= 0);
    stream.stream_index = 0;
    stub_set_matched_shaping_rules(stream.stream_index, 1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    stub_set_async_token_get_times(0, 20);
    actual_tx_queue = stub_get_tx_queue();
    
    /*******OP_STATE_PENDING***********/
    stream_state_change_udp(&stream, pme, OP_STATE_PENDING);

    /*******packets, OP_STATE_DATA***********/
    stream_state_change_udp(&stream, pme, OP_STATE_DATA);
    send_packets(&stream, pme, 20, 100, DIR_ROUTE_UP, &expec_tx_queue, 1, 0, 0);

    sleep(1);//wait stub async thread complete

    send_packets(&stream, pme, 80, 100, DIR_ROUTE_UP, &expec_tx_queue, 1, 0, 0);

    /*******OP_STATE_CLOSE***********/
    stream_state_change_udp(&stream, pme, OP_STATE_CLOSE);


    //first 20 packets,async get token
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 20));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    stub_refresh_token_bucket(0);
    for (int i = 0; i < 10; i++) {//异步获取token多发送了10个报文，补回token，不应发送报文
        polling_entry(NULL, NULL, 0, NULL);
        stub_curr_time_inc();
    }
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(NULL, NULL, 0, NULL);
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_master_destroy();
    free(pme);
    free(stream.pudpdetail);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 100, 10000, 0, 0, 160, 0, DIR_ROUTE_UP, profile_type_primary);//max latency is last 10 pkts
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
    struct streaminfo stream;
    void **pme = (void**)calloc(1, sizeof(void*));
    int rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    memset(&stream, 0, sizeof(struct streaminfo));
    stream.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ASSERT_TRUE(shaping_master_init() >= 0);
    stream.stream_index = 0;
    stub_set_matched_shaping_rules(stream.stream_index, 1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    stub_set_async_token_get_times(0, 20);
    actual_tx_queue = stub_get_tx_queue();
    
    /*******OP_STATE_PENDING***********/
    stream_state_change_udp(&stream, pme, OP_STATE_PENDING);

    /*******packets, OP_STATE_DATA***********/
    stream_state_change_udp(&stream, pme, OP_STATE_DATA);
    send_packets(&stream, pme, 10, 100, DIR_ROUTE_UP, &expec_tx_queue, 1, 0, 0);

    /*******OP_STATE_CLOSE***********/
    stream_state_change_udp(&stream, pme, OP_STATE_CLOSE);

    stub_set_async_token_get_times(0, 0);//refresh async count, async thread will be executed
    sleep(1);//ensure async thread exec complete

    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));

    shaping_master_destroy();
    free(pme);
    free(stream.pudpdetail);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 10, 1000, 10, 1000, 0, 0, 0, 0, DIR_ROUTE_UP, profile_type_primary);
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
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
    struct streaminfo stream1;
    struct streaminfo stream2;
    void **pme1 = (void**)calloc(1, sizeof(void*));
    void **pme2 = (void**)calloc(1, sizeof(void*));
    int rule_id1[] = {1};
    int rule_id2[] = {2};
    int profile_num1[] = {2};
    int profile_num2[] = {1};
    int priority[] = {1};
    int profile_id1[][MAX_REF_PROFILE] = {{1, 2}};
    int profile_id2[][MAX_REF_PROFILE] = {{2}};

    memset(&stream1, 0, sizeof(struct streaminfo));
    stream1.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));
    memset(&stream2, 0, sizeof(struct streaminfo));
    stream2.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));

    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    ASSERT_TRUE(shaping_master_init() >= 0);
    stub_set_token_bucket_avl_per_sec(1, 0, SHAPING_DIR_OUT);
    stub_set_token_bucket_avl_per_sec(2, 1000, SHAPING_DIR_OUT);

    stream1.stream_index = 0;
    stub_set_matched_shaping_rules(stream1.stream_index, 1, rule_id1, priority, profile_num1, profile_id1);

    stream2.stream_index = 1;
    stub_set_matched_shaping_rules(stream2.stream_index, 1, rule_id2, priority, profile_num2, profile_id2);

    actual_tx_queue = stub_get_tx_queue();

    /*******OP_STATE_PENDING***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_PENDING);
    stream_state_change_udp(&stream2, pme2, OP_STATE_PENDING);

    /*******packets, OP_STATE_DATA***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_DATA);
    stream_state_change_udp(&stream2, pme2, OP_STATE_DATA);
    send_packets(&stream1, pme1, 100, 100, DIR_ROUTE_UP, &expec_tx_queue1, 1, 0, 0);
    send_packets(&stream2, pme2, 100, 100, DIR_ROUTE_UP, &expec_tx_queue2, 1, 0, 0);
    
    /*******OP_STATE_CLOSE***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_CLOSE);
    stream_state_change_udp(&stream2, pme2, OP_STATE_CLOSE);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(2);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(NULL, NULL, 0, NULL);
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {//last 90 delay packets
        stub_refresh_token_bucket(2);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(NULL, NULL, 0, NULL);
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_master_destroy();
    free(pme1);
    free(pme2);
    free(stream1.pudpdetail);
    free(stream2.pudpdetail);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 1, primary
    shaping_stat_judge(line, 1, 1, 1, 0, 0, 0, 0, 0, 0, 470, 0, DIR_ROUTE_UP, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2, borrow
    shaping_stat_judge(line, 1, 2, 2, 100, 10000, 100, 10000, 0, 0, 470, 0, DIR_ROUTE_UP, profile_type_borrow);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//profile_id 2, primary
    shaping_stat_judge(line, 2, 2, 1, 100, 10000, 100, 10000, 0, 0, 190, 0, DIR_ROUTE_UP, profile_type_primary);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}


/*session1 match rule1; session2 match rule2
  rule1:
      priority:1
      primary profile_a:

profile_a: limit 1000
*/
TEST(two_session_same_rule, udp_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct streaminfo stream1;
    struct streaminfo stream2;
    void **pme1 = (void**)calloc(1, sizeof(void*));
    void **pme2 = (void**)calloc(1, sizeof(void*));
    int rule_id[] = {1};
    int profile_num[] = {1};
    int priority[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{1}};

    memset(&stream1, 0, sizeof(struct streaminfo));
    stream1.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));
    memset(&stream2, 0, sizeof(struct streaminfo));
    stream2.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));
    

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ASSERT_TRUE(shaping_master_init() >= 0);
    stub_set_token_bucket_avl_per_sec(1, 1000, SHAPING_DIR_OUT);

    stream1.stream_index = 0;
    stub_set_matched_shaping_rules(stream1.stream_index, 1, rule_id, priority, profile_num, profile_id);

    stream2.stream_index = 1;
    stub_set_matched_shaping_rules(stream2.stream_index, 1, rule_id, priority, profile_num, profile_id);

    actual_tx_queue = stub_get_tx_queue();

    /*******OP_STATE_PENDING***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_PENDING);
    stream_state_change_udp(&stream2, pme2, OP_STATE_PENDING);

    /*******packets, OP_STATE_DATA***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_DATA);
    stream_state_change_udp(&stream2, pme2, OP_STATE_DATA);
    send_packets(&stream1, pme1, 100, 100, DIR_ROUTE_UP, &expec_tx_queue, 1, 0, 0);
    send_packets(&stream2, pme2, 100, 100, DIR_ROUTE_UP, &expec_tx_queue, 1, 0, 0);

    /*******OP_STATE_CLOSE***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_CLOSE);
    stream_state_change_udp(&stream2, pme2, OP_STATE_CLOSE);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 190 delay packets
        stub_refresh_token_bucket(1);
        for (int i = 0; i < 20; i++) {//even though invoke polling more than 10 times, there should be only 10 pkts be sent
            polling_entry(NULL, NULL, 0, NULL);
            stub_curr_time_inc();
        }
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
        ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));
    }

    shaping_master_destroy();
    free(pme1);
    free(pme2);
    free(stream1.pudpdetail);
    free(stream2.pudpdetail);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 1, 1, 1, 200, 20000, 200, 20000, 0, 0, 370, 0, DIR_ROUTE_UP, profile_type_primary);
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
TEST(two_session_diff_priority_same_profile, udp_random_tx_in_order)
{
    struct stub_pkt_queue expec_tx_queue1;
    struct stub_pkt_queue expec_tx_queue2;
    struct stub_pkt_queue *actual_tx_queue;
    struct streaminfo stream1;
    struct streaminfo stream2;
    void **pme1 = (void**)calloc(1, sizeof(void*));
    void **pme2 = (void**)calloc(1, sizeof(void*));
    int rule_id1[] = {1};
    int rule_id2[] = {2};
    int profile_num[] = {1};
    int priority1[] = {1};
    int priority2[] = {2};
    int profile_id[][MAX_REF_PROFILE] = {{0}};
    int stream1_pkt_num = 0;
    int stream2_pkt_num = 0;
    struct timespec curr_time;

    memset(&stream1, 0, sizeof(struct streaminfo));
    stream1.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));
    memset(&stream2, 0, sizeof(struct streaminfo));
    stream2.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));
    

    TAILQ_INIT(&expec_tx_queue1);
    TAILQ_INIT(&expec_tx_queue2);
    stub_init();
    ASSERT_TRUE(shaping_master_init() >= 0);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);

    stream1.stream_index = 0;
    stub_set_matched_shaping_rules(stream1.stream_index, 1, rule_id1, priority1, profile_num, profile_id);

    stream2.stream_index = 1;
    stub_set_matched_shaping_rules(stream2.stream_index, 1, rule_id2, priority2, profile_num, profile_id);

    actual_tx_queue = stub_get_tx_queue();
   
    /*******OP_STATE_PENDING***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_PENDING);
    stream_state_change_udp(&stream2, pme2, OP_STATE_PENDING);

    /*******packets, OP_STATE_DATA***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_DATA);
    stream_state_change_udp(&stream2, pme2, OP_STATE_DATA);

    {//为方便判断统计时的顺序，先给rule1发送一个报文，保证统计发送时rule1顺序在前
        send_packets(&stream1, pme1, 1, 100, DIR_ROUTE_UP, &expec_tx_queue1, 1, 0, 0);
        stream1_pkt_num++;
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 1));
    }

    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    srand(curr_time.tv_sec);
    for (int i = 0; i < 99; i++) {
        if (rand() % 2 == 0) {
            send_packets(&stream1, pme1, 1, 100, DIR_ROUTE_UP, &expec_tx_queue1, 1, 0, 0);
            stream1_pkt_num++;
            if (i < 9) {
                ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 1));
            }
        } else {
            send_packets(&stream2, pme2, 1, 100, DIR_ROUTE_UP, &expec_tx_queue2, 1, 0, 0);
            stream2_pkt_num++;
            if (i < 9) {
                ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 1));
            }
        }
    }

    /*******OP_STATE_CLOSE***********/
    stream_state_change_udp(&stream1, pme1, OP_STATE_CLOSE);
    stream_state_change_udp(&stream2, pme2, OP_STATE_CLOSE);

    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue1)) {
        stub_refresh_token_bucket(0);
        polling_entry(NULL, NULL, 0, NULL);

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue1, actual_tx_queue, 1));//stream1 priority 1
    }

    while (!TAILQ_EMPTY(&expec_tx_queue2)) {
        stub_refresh_token_bucket(0);
        polling_entry(NULL, NULL, 0, NULL);

        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue2, actual_tx_queue, 1));//stream2 priority 2
    }

    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    shaping_master_destroy();
    free(pme1);
    free(pme2);
    free(stream1.pudpdetail);
    free(stream2.pudpdetail);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 1, 0, 1, stream1_pkt_num, stream1_pkt_num*100, 
                    stream1_pkt_num, stream1_pkt_num*100, 0, 0, -1, 0, DIR_ROUTE_UP, profile_type_primary);//can't predict a certain latency cause of random

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 2, 0, 2, stream2_pkt_num, stream2_pkt_num*100, 
                    stream2_pkt_num, stream2_pkt_num*100, 0, 0, -1, 0, DIR_ROUTE_UP, profile_type_primary);//can't predict a certain latency cause of random

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(statistics, udp_drop_pkt)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct streaminfo stream;
    void **pme = (void**)calloc(1, sizeof(void*));
    int rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    memset(&stream, 0, sizeof(struct streaminfo));
    stream.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ASSERT_TRUE(shaping_master_init() >= 0);
    stream.stream_index = 0;
    stub_set_matched_shaping_rules(stream.stream_index, 1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    
    /*******OP_STATE_PENDING***********/
    stream_state_change_udp(&stream, pme, OP_STATE_PENDING);

    /*******packets, OP_STATE_DATA***********/
    stream_state_change_udp(&stream, pme, OP_STATE_DATA);
    send_packets(&stream, pme, SHAPING_SESSION_QUEUE_LEN + 10, 100, DIR_ROUTE_UP, &expec_tx_queue, 1, 0, 0);
    send_packets(&stream, pme, 100, 100, DIR_ROUTE_UP, NULL, 1, 0, 0);//queue len is 100, these 100 pkts will be dropped

    /*******OP_STATE_CLOSE***********/
    stream_state_change_udp(&stream, pme, OP_STATE_CLOSE);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {
        stub_refresh_token_bucket(0);
        polling_entry(NULL, NULL, 0, NULL);
        stub_curr_time_inc();
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 1));
    }

    shaping_master_destroy();
    free(pme);
    free(stream.pudpdetail);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));
    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));
    shaping_stat_judge(line, 0, 0, 1, 210, 21000, 110, 11000, 100, 0, 200, 0, DIR_ROUTE_UP, profile_type_primary);//every queued pkt's latency is 200
    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

/*session1 match rule1
  rule1:
      profile: limit 1000*/
TEST(statistics, udp_queueing_pkt)
{
    struct stub_pkt_queue expec_tx_queue;
    struct stub_pkt_queue *actual_tx_queue;
    struct streaminfo stream;
    void **pme = (void**)calloc(1, sizeof(void*));
    int rule_id[] = {0};
    int priority[] = {1};
    int profile_num[] = {1};
    int profile_id[][MAX_REF_PROFILE] = {{0}};

    memset(&stream, 0, sizeof(struct streaminfo));
    stream.pudpdetail = (struct udpdetail*)calloc(1, sizeof(struct udpdetail));

    TAILQ_INIT(&expec_tx_queue);
    stub_init();
    ASSERT_TRUE(shaping_master_init() >= 0);
    stream.stream_index = 0;
    stub_set_matched_shaping_rules(stream.stream_index, 1, rule_id, priority, profile_num, profile_id);
    stub_set_token_bucket_avl_per_sec(0, 1000, SHAPING_DIR_OUT);
    actual_tx_queue = stub_get_tx_queue();
    
    /*******OP_STATE_PENDING***********/
    stream_state_change_udp(&stream, pme, OP_STATE_PENDING);

    /*******packets, OP_STATE_DATA***********/
    stream_state_change_udp(&stream, pme, OP_STATE_DATA);
    send_packets(&stream, pme, 100, 100, DIR_ROUTE_UP, &expec_tx_queue, 1, 0, 0);

    /***********send stat data here********************/
    stub_shaper_stat_send(0);
    sleep(2);//wait telegraf generate metric

    /*******OP_STATE_CLOSE***********/
    stream_state_change_udp(&stream, pme, OP_STATE_CLOSE);

    //first 10 packets
    ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 10));
    ASSERT_TRUE(TAILQ_EMPTY(actual_tx_queue));

    while (!TAILQ_EMPTY(&expec_tx_queue)) {//last 90 delay packets
        stub_refresh_token_bucket(0);
        polling_entry(NULL, NULL, 0, NULL);
        stub_curr_time_inc();
        ASSERT_EQ(0, judge_packet_eq(&expec_tx_queue, actual_tx_queue, 1));
    }

    shaping_master_destroy();
    free(pme);
    free(stream.pudpdetail);
    stub_clear_matched_shaping_rules();

    /*******test statistics***********/
    sleep(2);//wait telegraf to output
    char line[1024];
    FILE *stat_file;

    stat_file = fopen(SHAPING_STAT_FILE_NAME, "r");
    memset(line, 0, sizeof(line));

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//stat data first sent
    shaping_stat_judge(line, 0, 0, 1, 100, 10000, 10, 1000, 0, 90, 0, 1, DIR_ROUTE_UP, profile_type_primary);

    ASSERT_TRUE(NULL != fgets(line, sizeof(line), stat_file));//stat data last sent
    shaping_stat_judge(line, 0, 0, 1, 0, 0, 90, 9000, 0, 0, 90, 0, DIR_ROUTE_UP, profile_type_primary);

    fclose(stat_file);
    stat_file = fopen(SHAPING_STAT_FILE_NAME, "w");//clear stat file
    fclose(stat_file);
}

int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    //testing::GTEST_FLAG(filter) = "single_session.udp_diff_direction";
    return RUN_ALL_TESTS();
}