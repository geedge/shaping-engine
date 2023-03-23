#include <MESA/stream.h>
#include <cjson/cJSON.h>
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




int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    //testing::GTEST_FLAG(filter) = "single_session.udp_diff_direction";
    return RUN_ALL_TESTS();
}