#include <cstring>
#include <gtest/gtest.h>

#include "shaper.h"
#include "stub.h"
#include "shaper_maat.h"

TEST(shaping_rule, parse)
{
	const char *data = "{\
            \"uuid\": \"00000000-0000-0000-0000-000000000182\",\
            \"service\": 2,\
            \"action\": 32,\
            \"do_blacklist\": 0,\
            \"do_log\": 1,\
            \"effective_rage\": 0,\
            \"action_parameter\": {\
                \"vsys_id\": 2333,\
				\"priority\": 1,\
				\"fair_factor\": 10,\
				\"dscp_marking\": {\
					\"enabled\": 1,\
					\"dscp_type\": \"Assured Forwarding (AF)\",\
					\"dscp_name\": \"af11\",\
					\"dscp_value\": 10\
				},\
				\"profile_chain\": [\
                \"00000000-0000-0000-0000-000000000001\",\
                \"00000000-0000-0000-0000-000000000002\",\
                \"00000000-0000-0000-0000-000000000003\"\
            	]\
			}\
            }";

    struct shaping_rule *s_rule = NULL;
    struct shaping_rule *s_rule_dup = NULL;

    shaper_rule_ex_new("TRAFFIC_SHAPING_RULE", NULL, data, (void**)&s_rule, 0, NULL);
    EXPECT_EQ(s_rule->vsys_id, 2333);
    char uuid_str[UUID_STR_LEN];
    uuid_unparse(s_rule->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000182", uuid_str);
    uuid_unparse(s_rule->primary_pf_uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    EXPECT_EQ(s_rule->borrow_pf_num, 2);
    uuid_unparse(s_rule->borrow_pf_uuid_array[0], uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    uuid_unparse(s_rule->borrow_pf_uuid_array[1], uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    EXPECT_EQ(s_rule->priority, 1);
    EXPECT_EQ(s_rule->dscp_enable, 1);
    EXPECT_EQ(s_rule->dscp_value, 10);

    shaper_rule_ex_dup(0, (void**)&s_rule_dup, (void**)&s_rule, 0, NULL);
    EXPECT_EQ(s_rule, s_rule_dup);

    shaper_rule_ex_free(0, (void**)&s_rule, 0, NULL);
    EXPECT_TRUE(s_rule == NULL);
}

TEST(shaping_profile, parse)
{   
    const char *data = "{\
                    \"uuid\":\"00000000-0000-0000-0000-000000000001\",\
                    \"type\": \"fair_share\",\
                    \"type_argument\": \"max_min_host_fairness\",\
                    \"limits\": [\
                        {\"direction\":\"incoming\",\"bandwidth\":1024},\
                        {\"direction\":\"outcoming\",\"bandwidth\":2048}\
                    ],\
                    \"aqm_options\": {\"algorithm\":\"codel\"},\
                    \"is_valid\":\"yes\"}";
    struct shaping_profile *s_pf = NULL;
    struct shaping_profile *s_pf_dup = NULL;

    shaper_profile_ex_new("TRAFFIC_SHAPING_PROFILE", NULL, data, (void**)&s_pf, 0, NULL);
    char uuid_str[UUID_STR_LEN];
    uuid_unparse(s_pf->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    EXPECT_EQ(s_pf->in_limit_bandwidth, 1024);
    EXPECT_EQ(s_pf->out_limit_bandwidth, 2048);
    EXPECT_EQ(s_pf->type, PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS);
    EXPECT_EQ(s_pf->aqm_type, AQM_TYPE_CODEL);

    shaper_profile_ex_dup(0, (void**)&s_pf_dup, (void**)&s_pf, 0, NULL);
    EXPECT_EQ(s_pf, s_pf_dup);

    shaper_profile_ex_free(0, (void**)&s_pf, 0, NULL);
    EXPECT_TRUE(s_pf == NULL);
}

TEST(shaping_flow, update_rule)
{
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow sf;
    struct shaping_rule_info *rule_info;
    const char *rule_uuid_strs[] = {"00000000-0000-0000-0000-000000000001", "00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003"};
    int prioritys[] = {1, 2, 3};
    int profile_nums[] = {1, 2, 3};
    const char *profile_uuid_strs[][MAX_REF_PROFILE] = {{"00000000-0000-0000-0000-000000000001"}, 
                                                        {"00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003"}, 
                                                        {"00000000-0000-0000-0000-000000000004", "00000000-0000-0000-0000-000000000005", "00000000-0000-0000-0000-000000000006"}};

    stub_init();

    ctx = shaping_engine_init();

    stub_set_matched_shaping_rules(3, rule_uuid_strs, prioritys, profile_nums, profile_uuid_strs);
    stub_set_profile_type("00000000-0000-0000-0000-000000000001", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000002", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000003", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000004", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000005", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000006", PROFILE_TYPE_GENERIC);

    memset(&sf, 0, sizeof(sf));
    sf.priority = SHAPING_PRIORITY_NUM_MAX;
    uuid_t rule_uuids[3];
    uuid_parse("00000000-0000-0000-0000-000000000001", rule_uuids[0]);
    uuid_parse("00000000-0000-0000-0000-000000000002", rule_uuids[1]);
    uuid_parse("00000000-0000-0000-0000-000000000003", rule_uuids[2]);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, rule_uuids, 3);

    EXPECT_EQ(sf.rule_num, 3);

    rule_info = &sf.matched_rule_infos[0];
    char uuid_str[UUID_STR_LEN];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    rule_info = &sf.matched_rule_infos[1];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 1);
    uuid_unparse(rule_info->borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    EXPECT_EQ(rule_info->borrowing[0].priority, 2);

    rule_info = &sf.matched_rule_infos[2];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000004", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 2);
    uuid_unparse(rule_info->borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000005", uuid_str);
    EXPECT_EQ(rule_info->borrowing[0].priority, 2);
    uuid_unparse(rule_info->borrowing[1].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000006", uuid_str);
    EXPECT_EQ(rule_info->borrowing[1].priority, 3);

    shaping_engine_destroy(ctx);
    stub_clear_resource();
}

TEST(shaping_flow, update_rule_dup)
{
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow sf;
    struct shaping_rule_info *rule_info;
    const char *rule_uuid_strs[] = {"00000000-0000-0000-0000-000000000001", "00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003", "00000000-0000-0000-0000-000000000004"};
    int prioritys[] = {1, 2, 3, 4};
    int profile_nums[] = {1, 2, 3, 1};
    const char *profile_uuid_strs[][MAX_REF_PROFILE] = {{"00000000-0000-0000-0000-000000000001"}, 
                                                        {"00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003"}, 
                                                        {"00000000-0000-0000-0000-000000000004", "00000000-0000-0000-0000-000000000005", "00000000-0000-0000-0000-000000000006"}, 
                                                        {"00000000-0000-0000-0000-000000000007"}};

    stub_init();

    ctx = shaping_engine_init();

    stub_set_matched_shaping_rules(4, rule_uuid_strs, prioritys, profile_nums, profile_uuid_strs);
    stub_set_profile_type("00000000-0000-0000-0000-000000000001", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000002", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000003", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000004", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000005", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000006", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000007", PROFILE_TYPE_GENERIC);

    memset(&sf, 0, sizeof(sf));
    sf.priority = SHAPING_PRIORITY_NUM_MAX;

    uuid_t rule_uuid1[3];
    uuid_parse("00000000-0000-0000-0000-000000000001", rule_uuid1[0]);
    uuid_parse("00000000-0000-0000-0000-000000000002", rule_uuid1[1]);
    uuid_parse("00000000-0000-0000-0000-000000000003", rule_uuid1[2]);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, rule_uuid1, 3);

    EXPECT_EQ(sf.rule_num, 3);

    rule_info = &sf.matched_rule_infos[0];
    char uuid_str[UUID_STR_LEN];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    rule_info = &sf.matched_rule_infos[1];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 1);
    uuid_unparse(rule_info->borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    EXPECT_EQ(rule_info->borrowing[0].priority, 2);

    rule_info = &sf.matched_rule_infos[2];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000004", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 2);
    uuid_unparse(rule_info->borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000005", uuid_str);
    EXPECT_EQ(rule_info->borrowing[0].priority, 2);
    uuid_unparse(rule_info->borrowing[1].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000006", uuid_str);
    EXPECT_EQ(rule_info->borrowing[1].priority, 3);

    uuid_t rule_uuid2;
    uuid_parse("00000000-0000-0000-0000-000000000001", rule_uuid2);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, &rule_uuid2, 1);
    EXPECT_EQ(sf.rule_num, 3);

    uuid_t rule_uuid3[3];
    uuid_parse("00000000-0000-0000-0000-000000000002", rule_uuid3[0]);
    uuid_parse("00000000-0000-0000-0000-000000000003", rule_uuid3[1]);
    uuid_parse("00000000-0000-0000-0000-000000000004", rule_uuid3[2]);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, rule_uuid3, 3);
    EXPECT_EQ(sf.rule_num, 4);
    rule_info = &sf.matched_rule_infos[3];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000004", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000007", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);

    shaping_engine_destroy(ctx);
    stub_clear_resource();
}

TEST(shaping_flow, update_rule_after_priority_confirmed)
{
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow sf;
    struct shaping_rule_info *rule_info;
    const char *rule_uuid_strs[] = {"00000000-0000-0000-0000-000000000001", "00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003"};
    int prioritys[] = {1, 2, 3};
    int profile_nums[] = {1, 2, 3};
    const char *profile_uuid_strs[][MAX_REF_PROFILE] = {{"00000000-0000-0000-0000-000000000001"}, 
                                                        {"00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003"}, 
                                                        {"00000000-0000-0000-0000-000000000004", "00000000-0000-0000-0000-000000000005", "00000000-0000-0000-0000-000000000006"}};

    stub_init();

    ctx = shaping_engine_init();

    stub_set_matched_shaping_rules(3, rule_uuid_strs, prioritys, profile_nums, profile_uuid_strs);
    stub_set_profile_type("00000000-0000-0000-0000-000000000001", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000002", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000003", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000004", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000005", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000006", PROFILE_TYPE_GENERIC);

    memset(&sf, 0, sizeof(sf));
    sf.priority = SHAPING_PRIORITY_NUM_MAX;
    uuid_t first_rule_uuids[2];
    uuid_parse("00000000-0000-0000-0000-000000000002", first_rule_uuids[0]);
    uuid_parse("00000000-0000-0000-0000-000000000003", first_rule_uuids[1]);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, first_rule_uuids, 2);

    sf.processed_pkts = CONFIRM_PRIORITY_PKTS + 1;
    uuid_t after_confirm_priority_rule_uuids;
    uuid_parse("00000000-0000-0000-0000-000000000001", after_confirm_priority_rule_uuids);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, &after_confirm_priority_rule_uuids, 1);

    EXPECT_EQ(sf.rule_num, 3);
    EXPECT_EQ(sf.priority, 2);

    rule_info = &sf.matched_rule_infos[0];
    char uuid_str[UUID_STR_LEN];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 2);
    EXPECT_EQ(rule_info->borrowing_num, 1);
    uuid_unparse(rule_info->borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    EXPECT_EQ(rule_info->borrowing[0].priority, 3);

    rule_info = &sf.matched_rule_infos[1];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000004", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 2);
    EXPECT_EQ(rule_info->borrowing_num, 2);
    uuid_unparse(rule_info->borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000005", uuid_str);
    EXPECT_EQ(rule_info->borrowing[0].priority, 3);
    uuid_unparse(rule_info->borrowing[1].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000006", uuid_str);
    EXPECT_EQ(rule_info->borrowing[1].priority, 4);

    rule_info = &sf.matched_rule_infos[2];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 2);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    shaping_engine_destroy(ctx);
    stub_clear_resource();
}

TEST(shaping_flow, update_rule_dscp)
{
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow sf;
    struct shaping_rule_info *rule_info;
    const char *rule_uuid_strs[] = {"00000000-0000-0000-0000-000000000001", "00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003", "00000000-0000-0000-0000-000000000004"};
    int prioritys[] = {1, 2, 3, 4};
    int profile_nums[] = {1, 1, 1, 1};
    const char *profile_uuid_strs[][MAX_REF_PROFILE] = {{"00000000-0000-0000-0000-000000000001"}, 
                                                        {"00000000-0000-0000-0000-000000000002"}, 
                                                        {"00000000-0000-0000-0000-000000000003"}, 
                                                        {"00000000-0000-0000-0000-000000000004"}};

    stub_init();

    ctx = shaping_engine_init();

    stub_set_matched_shaping_rules(4, rule_uuid_strs, prioritys, profile_nums, profile_uuid_strs);
    stub_set_profile_type("00000000-0000-0000-0000-000000000001", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000002", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000003", PROFILE_TYPE_GENERIC);
    stub_set_profile_type("00000000-0000-0000-0000-000000000004", PROFILE_TYPE_GENERIC);

    stub_set_shaping_rule_dscp_value(rule_uuid_strs[0], 10);//AF11
    stub_set_shaping_rule_dscp_value(rule_uuid_strs[1], 12);//AF12
    stub_set_shaping_rule_dscp_value(rule_uuid_strs[2], 14);//AF13
    stub_set_shaping_rule_dscp_value(rule_uuid_strs[3], 40);//CS5

    memset(&sf, 0, sizeof(sf));
    sf.priority = SHAPING_PRIORITY_NUM_MAX;

    uuid_t rule_uuids1[2];
    uuid_parse("00000000-0000-0000-0000-000000000001", rule_uuids1[0]);
    uuid_parse("00000000-0000-0000-0000-000000000002", rule_uuids1[1]);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, rule_uuids1, 2);

    EXPECT_EQ(sf.rule_num, 2);
    EXPECT_EQ(sf.dscp_enable, 1);
    EXPECT_EQ(sf.dscp_value, 10);

    rule_info = &sf.matched_rule_infos[0];
    char uuid_str[UUID_STR_LEN];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    rule_info = &sf.matched_rule_infos[1];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    uuid_t rule_uuid2;
    uuid_parse("00000000-0000-0000-0000-000000000003", rule_uuid2);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, &rule_uuid2, 1);
    EXPECT_EQ(sf.rule_num, 3);
    EXPECT_EQ(sf.dscp_enable, 1);
    EXPECT_EQ(sf.dscp_value, 10);

    rule_info = &sf.matched_rule_infos[2];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    uuid_t rule_uuid3;
    uuid_parse("00000000-0000-0000-0000-000000000004", rule_uuid3);
    shaper_rules_update(&ctx->thread_ctx[0], &sf, &rule_uuid3, 1);
    EXPECT_EQ(sf.rule_num, 4);
    EXPECT_EQ(sf.dscp_enable, 1);
    EXPECT_EQ(sf.dscp_value, 40);

    rule_info = &sf.matched_rule_infos[3];
    uuid_unparse(rule_info->uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000004", uuid_str);
    uuid_unparse(rule_info->primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000004", uuid_str);
    EXPECT_EQ(rule_info->primary.priority, 1);
    EXPECT_EQ(rule_info->borrowing_num, 0);

    shaping_engine_destroy(ctx);
    stub_clear_resource();
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}