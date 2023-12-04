#include <cstring>
#include <gtest/gtest.h>

#include "shaper.h"
#include "stub.h"
#include "shaper_maat.h"

TEST(shaping_rule, parse)
{
	const char *data = "182\t\
                        2\t\
                        32\t\
                        0\t\
                        1\t\
                        {}\t\
                        {\"vsys_id\":2333,\"priority\":1,\"fair_factor\":10,\"profile_chain\":[1,2,3]}\t\
                        0\t\
                        1\t\
                        }";

    struct shaping_rule *s_rule = NULL;
    struct shaping_rule *s_rule_dup = NULL;

    shaper_rule_ex_new("TRAFFIC_SHAPING_COMPILE", 0, NULL, data, (void**)&s_rule, 0, NULL);
    EXPECT_EQ(s_rule->vsys_id, 2333);
    EXPECT_EQ(s_rule->id, 182);
    EXPECT_EQ(s_rule->primary_pf_id, 1);
    EXPECT_EQ(s_rule->borrow_pf_num, 2);
    EXPECT_EQ(s_rule->borrow_pf_id_array[0], 2);
    EXPECT_EQ(s_rule->borrow_pf_id_array[1], 3);
    EXPECT_EQ(s_rule->priority, 1);
    EXPECT_EQ(s_rule->ref_cnt, 1);

    shaper_rule_ex_dup(0, (void**)&s_rule_dup, (void**)&s_rule, 0, NULL);
    EXPECT_EQ(s_rule, s_rule_dup);
    EXPECT_EQ(s_rule->ref_cnt, 2);

    shaper_rule_ex_free(0, (void**)&s_rule, 0, NULL);
    shaper_rule_ex_free(0, (void**)&s_rule, 0, NULL);
    EXPECT_TRUE(s_rule == NULL);
}

TEST(shaping_profile, parse)
{   const char *data = "1\t\
                        fair_share\t\
                        max_min_host_fairness\t\
                        [{\"direction\":\"incoming\",\"bandwidth\":1024},{\"direction\":\"outgoing\",\"bandwidth\":2048}]\t\
                        {\"enabled\":1,\"algorithm\":\"codel\"}\t\
                        null\t\
                        {}\t\
                        1";
    struct shaping_profile *s_pf = NULL;
    struct shaping_profile *s_pf_dup = NULL;

    shaper_profile_ex_new("TRAFFIC_SHAPING_PROFILE", 0, NULL, data, (void**)&s_pf, 0, NULL);
    EXPECT_EQ(s_pf->id, 1);
    EXPECT_EQ(s_pf->in_limit_bandwidth, 1024);
    EXPECT_EQ(s_pf->out_limit_bandwidth, 2048);
    EXPECT_EQ(s_pf->type, PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS);
    EXPECT_EQ(s_pf->ref_cnt, 1);

    shaper_profile_ex_dup(0, (void**)&s_pf_dup, (void**)&s_pf, 0, NULL);
    EXPECT_EQ(s_pf, s_pf_dup);
    EXPECT_EQ(s_pf->ref_cnt, 2);

    shaper_profile_ex_free(0, (void**)&s_pf, 0, NULL);
    shaper_profile_ex_free(0, (void**)&s_pf, 0, NULL);
    EXPECT_TRUE(s_pf == NULL);
}

TEST(shaping_flow, update_rule)
{
    struct shaping_thread_ctx ctx;
    struct shaping_flow sf;
    struct shaping_rule_info *rule_info;
    long long rule_ids[] = {1, 2, 3};
    int prioritys[] = {1, 2, 3};
    int profile_nums[] = {1, 2, 3};
    int profile_ids[][MAX_REF_PROFILE] = {{1}, {2, 3}, {4, 5, 6}};

    stub_init();

    stub_set_matched_shaping_rules(3, rule_ids, prioritys, profile_nums, profile_ids);

    ctx.maat_info = (struct shaping_maat_info *)calloc(1, sizeof(struct shaping_maat_info));
    ctx.maat_info->rule_table_id = STUB_MAAT_SHAPING_RULE_TABLE_ID;
    ctx.maat_info->profile_table_id = STUB_MAAT_SHAPING_PROFILE_TABLE_ID;

    memset(&sf, 0, sizeof(sf));
    sf.priority = SHAPING_PRIORITY_NUM_MAX;
    shaper_rules_update(&ctx, &sf, rule_ids, 3);

    EXPECT_EQ(sf.rule_num, 3);

    rule_info = &sf.matched_rule_infos[0];
    EXPECT_EQ(rule_info->id, 1);
    EXPECT_EQ(rule_info->primary.id, 1);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    rule_info = &sf.matched_rule_infos[1];
    EXPECT_EQ(rule_info->id, 2);
    EXPECT_EQ(rule_info->primary.id, 2);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 1);
    EXPECT_EQ(rule_info->borrowing[0].id, 3);
    EXPECT_EQ(rule_info->borrowing[0].priority, 2);

    rule_info = &sf.matched_rule_infos[2];
    EXPECT_EQ(rule_info->id, 3);
    EXPECT_EQ(rule_info->primary.id, 4);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 2);
    EXPECT_EQ(rule_info->borrowing[0].id, 5);
    EXPECT_EQ(rule_info->borrowing[0].priority, 2);
    EXPECT_EQ(rule_info->borrowing[1].id, 6);
    EXPECT_EQ(rule_info->borrowing[1].priority, 3);

    free(ctx.maat_info);
}

TEST(shaping_flow, update_rule_after_priority_confirmed)
{
    struct shaping_thread_ctx ctx;
    struct shaping_flow sf;
    struct shaping_rule_info *rule_info;
    long long rule_ids[] = {1, 2, 3};
    int prioritys[] = {1, 2, 3};
    int profile_nums[] = {1, 2, 3};
    int profile_ids[][MAX_REF_PROFILE] = {{1}, {2, 3}, {4, 5, 6}};

    stub_init();

    stub_set_matched_shaping_rules(3, rule_ids, prioritys, profile_nums, profile_ids);

    ctx.maat_info = (struct shaping_maat_info *)calloc(1, sizeof(struct shaping_maat_info));
    ctx.maat_info->rule_table_id = STUB_MAAT_SHAPING_RULE_TABLE_ID;
    ctx.maat_info->profile_table_id = STUB_MAAT_SHAPING_PROFILE_TABLE_ID;

    memset(&sf, 0, sizeof(sf));
    sf.priority = SHAPING_PRIORITY_NUM_MAX;
    long long first_rule_ids[] = {2, 3};
    shaper_rules_update(&ctx, &sf, first_rule_ids, 2);

    sf.processed_pkts = CONFIRM_PRIORITY_PKTS + 1;
    long long after_confirm_priority_rule_ids[] = {1};
    shaper_rules_update(&ctx, &sf, after_confirm_priority_rule_ids, 1);

    EXPECT_EQ(sf.rule_num, 3);
    EXPECT_EQ(sf.priority, 2);

    rule_info = &sf.matched_rule_infos[0];
    EXPECT_EQ(rule_info->id, 2);
    EXPECT_EQ(rule_info->primary.id, 2);
    EXPECT_EQ(rule_info->primary.priority, 2);
    EXPECT_EQ(rule_info->borrowing_num, 1);
    EXPECT_EQ(rule_info->borrowing[0].id, 3);
    EXPECT_EQ(rule_info->borrowing[0].priority, 3);

    rule_info = &sf.matched_rule_infos[1];
    EXPECT_EQ(rule_info->id, 3);
    EXPECT_EQ(rule_info->primary.id, 4);
    EXPECT_EQ(rule_info->primary.priority, 2);
    EXPECT_EQ(rule_info->borrowing_num, 2);
    EXPECT_EQ(rule_info->borrowing[0].id, 5);
    EXPECT_EQ(rule_info->borrowing[0].priority, 3);
    EXPECT_EQ(rule_info->borrowing[1].id, 6);
    EXPECT_EQ(rule_info->borrowing[1].priority, 4);

    rule_info = &sf.matched_rule_infos[2];
    EXPECT_EQ(rule_info->id, 1);
    EXPECT_EQ(rule_info->primary.id, 1);
    EXPECT_EQ(rule_info->primary.priority, 2);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    free(ctx.maat_info);
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}