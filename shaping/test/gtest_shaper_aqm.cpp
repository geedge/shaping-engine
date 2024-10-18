#include <gtest/gtest.h>
#include <unistd.h>

#include "shaper.h"
#include "shaper_aqm.h"

TEST(aqm_bule, need_drop)
{
    struct shaper_aqm_blue_para para;
    int drop_cnt = 0;
    uuid_t uuid;

    uuid_parse("00000000-0000-0000-0000-000000000001", uuid);

    para.update_time = 0;
    para.probability = 0;

    for (int i = 0; i < 10000; i++) {
        if (shaper_aqm_blue_need_drop(uuid, &para, BLUE_QUEUE_LEN_MAX + 1)) {
            drop_cnt++;
        }
    }
    
    EXPECT_EQ(para.probability, BLUE_INCREMENT);

    EXPECT_GT(drop_cnt, 900);
    EXPECT_LT(drop_cnt, 1100);

    sleep(3);
    shaper_aqm_blue_need_drop(uuid, &para, 0);
    EXPECT_EQ(para.probability, BLUE_INCREMENT - BLUE_DECREMENT);
}

TEST(aqm_blue, no_drop)
{
    struct shaper_aqm_blue_para para;
    int drop_cnt = 0;
    uuid_t uuid;

    uuid_parse("00000000-0000-0000-0000-000000000001", uuid);

    para.update_time = 0;
    para.probability = 0;

    for (int i = 0; i < 10000; i++) {
        if (shaper_aqm_blue_need_drop(uuid, &para, BLUE_QUEUE_LEN_MAX - 1)) {
            drop_cnt++;
        }
    }
    
    EXPECT_EQ(para.probability, 0);
    EXPECT_EQ(drop_cnt, 0);

    sleep(3);
    shaper_aqm_blue_need_drop(uuid, &para, BLUE_QUEUE_LEN_MAX - 1);
    EXPECT_EQ(para.probability, 0);
}

TEST(aqm_codel, need_drop)
{
    struct shaper_aqm_codel_para para;
    int curr_time_ms = 0;
    uuid_t uuid;

    uuid_parse("00000000-0000-0000-0000-000000000001", uuid);

    memset(&para, 0, sizeof(para));

    shaper_aqm_codel_need_drop(uuid, &para, curr_time_ms, CODEL_MAX_LATENCY + 1);
    EXPECT_EQ(para.state, CODEL_STATE_DROPPING_TIMER);
    EXPECT_EQ(para.start_drop_time_ms, curr_time_ms + CODEL_DROP_INTERVAL);

    curr_time_ms = para.start_drop_time_ms + 1;
    shaper_aqm_codel_need_drop(uuid, &para, curr_time_ms, CODEL_MAX_LATENCY + 1);
    EXPECT_EQ(para.state, CODEL_STATE_DROPPING_PHASE);
    EXPECT_EQ(para.drop_count, 1);
    EXPECT_EQ(para.next_drop_time_ms, int(curr_time_ms + CODEL_DROP_INTERVAL / sqrt(para.drop_count)));

    curr_time_ms = para.next_drop_time_ms + 1;
    shaper_aqm_codel_need_drop(uuid, &para, curr_time_ms, CODEL_MAX_LATENCY + 1);
    EXPECT_EQ(para.state, CODEL_STATE_DROPPING_PHASE);
    EXPECT_EQ(para.drop_count, 2);
    EXPECT_EQ(para.next_drop_time_ms, int(curr_time_ms + CODEL_DROP_INTERVAL / sqrt(para.drop_count)));

    shaper_aqm_codel_need_drop(uuid, &para, curr_time_ms, CODEL_MAX_LATENCY - 1);
    EXPECT_EQ(para.state, CODEL_STATE_NORMAL);
}

TEST(aqm_codel, no_drop)
{
    struct shaper_aqm_codel_para para;
    int curr_time_ms = 0;
    uuid_t uuid;

    uuid_parse("00000000-0000-0000-0000-000000000001", uuid);

    memset(&para, 0, sizeof(para));

    shaper_aqm_codel_need_drop(uuid, &para, curr_time_ms, CODEL_MAX_LATENCY - 1);
    EXPECT_EQ(para.state, CODEL_STATE_NORMAL);
    EXPECT_EQ(para.drop_count, 0);
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}