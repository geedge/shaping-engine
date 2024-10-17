#include <gtest/gtest.h>
#include "mpack.h"
#include "shaper.h"
#include "shaper_session.h"
#include "shaper_marsio.h"

static void gtest_shaper_log_parse(struct shaping_flow *sf, const char *data, size_t length)
{
    int shaping_rule_array_size;
    char temp_str[16] = {0};
    mpack_tree_t tree;
    
    mpack_node_t root;
    mpack_node_t tmp_node;
    mpack_node_t tmp_rule_node;
    mpack_node_t tmp_profile_node;

    mpack_tree_init_data(&tree, data, length);
    mpack_tree_parse(&tree);
    root = mpack_tree_root(&tree);

    //tsync
    tmp_node = mpack_node_map_cstr(root, "tsync");
    EXPECT_EQ(mpack_type_str, mpack_node_type(tmp_node));
    memcpy(temp_str, mpack_node_str(tmp_node), mpack_node_strlen(tmp_node));
    EXPECT_STREQ("2.0", temp_str);
    
    //session_id
    tmp_node = mpack_node_map_cstr(root, "session_id");
    EXPECT_EQ(mpack_type_uint, mpack_node_type(tmp_node));
    sf->ctrl_meta.session_id = mpack_node_u64(tmp_node);

    //state
    tmp_node = mpack_node_map_cstr(root, "state");
    EXPECT_EQ(mpack_type_str, mpack_node_type(tmp_node));
    memset(temp_str, 0, sizeof(temp_str));
    memcpy(temp_str, mpack_node_str(tmp_node), mpack_node_strlen(tmp_node));
    EXPECT_STREQ("active", temp_str);
    
    //method
    tmp_node = mpack_node_map_cstr(root, "method");
    EXPECT_EQ(mpack_type_str, mpack_node_type(tmp_node));
    memset(temp_str, 0, sizeof(temp_str));
    memcpy(temp_str, mpack_node_str(tmp_node), mpack_node_strlen(tmp_node));
    EXPECT_STREQ("log_update", temp_str);

    //shaping rules
    tmp_node = mpack_node_map_cstr(root, "params");
    EXPECT_EQ(mpack_type_map, mpack_node_type(tmp_node));
    
    tmp_node = mpack_node_map_cstr(tmp_node, "shaper");
    EXPECT_EQ(mpack_type_array, mpack_node_type(tmp_node));

    shaping_rule_array_size = mpack_node_array_length(tmp_node);
    sf->rule_num = MIN(shaping_rule_array_size, SHAPING_RULE_NUM_MAX);
    for (int i = 0; i < sf->rule_num; i++) {
        ASSERT_EQ(mpack_type_map, mpack_node_type(mpack_node_array_at(tmp_node, i)) );
        tmp_rule_node = mpack_node_map_cstr(mpack_node_array_at(tmp_node, i), "rule_id");
        EXPECT_EQ(mpack_type_bin, mpack_node_type(tmp_rule_node));
        uuid_copy(sf->matched_rule_infos[i].uuid, *(uuid_t*)mpack_node_bin_data(tmp_rule_node));

        tmp_profile_node = mpack_node_map_cstr(mpack_node_array_at(tmp_node, i), "profile_ids");
        ASSERT_EQ(mpack_type_array, mpack_node_type(tmp_profile_node));
        int profile_array_len = mpack_node_array_length(tmp_profile_node);
        sf->matched_rule_infos[i].borrowing_num = profile_array_len - 1;
        for (int j = 0; j < profile_array_len; j++) {
            ASSERT_EQ(mpack_type_bin, mpack_node_type(mpack_node_array_at(tmp_profile_node, j)) );
            if (j == 0) {
                uuid_copy(sf->matched_rule_infos[i].primary.uuid, *(uuid_t*)mpack_node_bin_data(mpack_node_array_at(tmp_profile_node, j)));
            } else {
                uuid_copy(sf->matched_rule_infos[i].borrowing[j - 1].uuid, *(uuid_t*)mpack_node_bin_data(mpack_node_array_at(tmp_profile_node, j)));
            }
        }
    }

    mpack_tree_destroy(&tree);

    return;
}

TEST(MPACK_LOG, PARSE)
{
    struct shaping_flow sf_in;
    struct shaping_flow sf_out;
    char *mpack_data = NULL;
    size_t mpack_size = 0;

    sf_in.ctrl_meta.session_id = 12345678;
    sf_in.rule_num = 3;

    //rule_id 1, primary profile id 1, borrow profile id 2
    uuid_parse("00000000-0000-0000-0000-000000000001", sf_in.matched_rule_infos[0].uuid);
    uuid_parse("00000000-0000-0000-0000-000000000001", sf_in.matched_rule_infos[0].primary.uuid);
    sf_in.matched_rule_infos[0].borrowing_num = 1;
    uuid_parse("00000000-0000-0000-0000-000000000002", sf_in.matched_rule_infos[0].borrowing[0].uuid);

    //rule id 2, primary profile id 3, borrow profile id 4,5
    uuid_parse("00000000-0000-0000-0000-000000000002", sf_in.matched_rule_infos[1].uuid);
    uuid_parse("00000000-0000-0000-0000-000000000003", sf_in.matched_rule_infos[1].primary.uuid);
    sf_in.matched_rule_infos[1].borrowing_num = 2;
    uuid_parse("00000000-0000-0000-0000-000000000004", sf_in.matched_rule_infos[1].borrowing[0].uuid);
    uuid_parse("00000000-0000-0000-0000-000000000005", sf_in.matched_rule_infos[1].borrowing[1].uuid);

    //rule id 3, primary profile id 6, borrow profile id 7,8,9
    uuid_parse("00000000-0000-0000-0000-000000000003", sf_in.matched_rule_infos[2].uuid);
    uuid_parse("00000000-0000-0000-0000-000000000006", sf_in.matched_rule_infos[2].primary.uuid);
    sf_in.matched_rule_infos[2].borrowing_num = 3;
    uuid_parse("00000000-0000-0000-0000-000000000007", sf_in.matched_rule_infos[2].borrowing[0].uuid);
    uuid_parse("00000000-0000-0000-0000-000000000008", sf_in.matched_rule_infos[2].borrowing[1].uuid);
    uuid_parse("00000000-0000-0000-0000-000000000009", sf_in.matched_rule_infos[2].borrowing[2].uuid);

    shaper_session_log_prepare(&sf_in, &mpack_data, &mpack_size);
    gtest_shaper_log_parse(&sf_out, mpack_data, mpack_size);

    EXPECT_EQ(sf_out.ctrl_meta.session_id, 12345678);
    EXPECT_EQ(sf_out.rule_num, 3);

    //rule_id 1, primary profile id 1, borrow profile id 2
    char uuid_str[UUID_STR_LEN];
    uuid_unparse(sf_out.matched_rule_infos[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    uuid_unparse(sf_out.matched_rule_infos[0].primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000001", uuid_str);
    EXPECT_EQ(sf_out.matched_rule_infos[0].borrowing_num, 1);
    uuid_unparse(sf_out.matched_rule_infos[0].borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);

    //rule id 2, primary profile id 3, borrow profile id 4,5
    uuid_unparse(sf_out.matched_rule_infos[1].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000002", uuid_str);
    uuid_unparse(sf_out.matched_rule_infos[1].primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    EXPECT_EQ(sf_out.matched_rule_infos[1].borrowing_num, 2);
    uuid_unparse(sf_out.matched_rule_infos[1].borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000004", uuid_str);
    uuid_unparse(sf_out.matched_rule_infos[1].borrowing[1].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000005", uuid_str);

    //rule id 3, primary profile id 6, borrow profile id 7,8,9
    uuid_unparse(sf_out.matched_rule_infos[2].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000003", uuid_str);
    uuid_unparse(sf_out.matched_rule_infos[2].primary.uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000006", uuid_str);
    EXPECT_EQ(sf_out.matched_rule_infos[2].borrowing_num, 3);
    uuid_unparse(sf_out.matched_rule_infos[2].borrowing[0].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000007", uuid_str);
    uuid_unparse(sf_out.matched_rule_infos[2].borrowing[1].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000008", uuid_str);
    uuid_unparse(sf_out.matched_rule_infos[2].borrowing[2].uuid, uuid_str);
    EXPECT_STREQ("00000000-0000-0000-0000-000000000009", uuid_str);

    if (mpack_data) {
        free(mpack_data);
    }
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}