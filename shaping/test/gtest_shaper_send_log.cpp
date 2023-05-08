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
        EXPECT_EQ(mpack_type_uint, mpack_node_type(tmp_rule_node));
        sf->matched_rule_infos[i].id = mpack_node_u64(tmp_rule_node);

        tmp_profile_node = mpack_node_map_cstr(mpack_node_array_at(tmp_node, i), "profile_ids");
        ASSERT_EQ(mpack_type_array, mpack_node_type(tmp_profile_node));
        int profile_array_len = mpack_node_array_length(tmp_profile_node);
        sf->matched_rule_infos[i].borrowing_num = profile_array_len - 1;
        for (int j = 0; j < profile_array_len; j++) {
            ASSERT_EQ(mpack_type_uint, mpack_node_type(mpack_node_array_at(tmp_profile_node, j)) );
            if (j == 0) {
                sf->matched_rule_infos[i].primary.id = mpack_node_u64(mpack_node_array_at(tmp_profile_node, j));
            } else {
                sf->matched_rule_infos[i].borrowing[j - 1].id = mpack_node_u64(mpack_node_array_at(tmp_profile_node, j));
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

    //rule_id 0, primary profile id 0, borrow profile id 1
    sf_in.matched_rule_infos[0].id = 0;
    sf_in.matched_rule_infos[0].primary.id = 0;
    sf_in.matched_rule_infos[0].borrowing_num = 1;
    sf_in.matched_rule_infos[0].borrowing[0].id = 1;

    //rule id 1, primary profile id 2, borrow profile id 3,4
    sf_in.matched_rule_infos[1].id = 1;
    sf_in.matched_rule_infos[1].primary.id = 2;
    sf_in.matched_rule_infos[1].borrowing_num = 2;
    sf_in.matched_rule_infos[1].borrowing[0].id = 3;
    sf_in.matched_rule_infos[1].borrowing[1].id = 4;

    //rule id 2, primary profile id 5, borrow profile id 6,7,8
    sf_in.matched_rule_infos[2].id = 2;
    sf_in.matched_rule_infos[2].primary.id = 5;
    sf_in.matched_rule_infos[2].borrowing_num = 3;
    sf_in.matched_rule_infos[2].borrowing[0].id = 6;
    sf_in.matched_rule_infos[2].borrowing[1].id = 7;
    sf_in.matched_rule_infos[2].borrowing[2].id = 8;

    shaper_session_log_prepare(&sf_in, &mpack_data, &mpack_size);
    gtest_shaper_log_parse(&sf_out, mpack_data, mpack_size);

    EXPECT_EQ(sf_out.ctrl_meta.session_id, 12345678);
    EXPECT_EQ(sf_out.rule_num, 3);

    //rule_id 0, primary profile id 0, borrow profile id 1
    EXPECT_EQ(sf_out.matched_rule_infos[0].id, 0);
    EXPECT_EQ(sf_out.matched_rule_infos[0].primary.id, 0);
    EXPECT_EQ(sf_out.matched_rule_infos[0].borrowing_num, 1);
    EXPECT_EQ(sf_out.matched_rule_infos[0].borrowing[0].id, 1);

    //rule id 1, primary profile id 2, borrow profile id 3,4
    EXPECT_EQ(sf_out.matched_rule_infos[1].id, 1);
    EXPECT_EQ(sf_out.matched_rule_infos[1].primary.id, 2);
    EXPECT_EQ(sf_out.matched_rule_infos[1].borrowing_num, 2);
    EXPECT_EQ(sf_out.matched_rule_infos[1].borrowing[0].id, 3);
    EXPECT_EQ(sf_out.matched_rule_infos[1].borrowing[1].id, 4);

    //rule id 2, primary profile id 5, borrow profile id 6,7,8
    EXPECT_EQ(sf_out.matched_rule_infos[2].id, 2);
    EXPECT_EQ(sf_out.matched_rule_infos[2].primary.id, 5);
    EXPECT_EQ(sf_out.matched_rule_infos[2].borrowing_num, 3);
    EXPECT_EQ(sf_out.matched_rule_infos[2].borrowing[0].id, 6);
    EXPECT_EQ(sf_out.matched_rule_infos[2].borrowing[1].id, 7);
    EXPECT_EQ(sf_out.matched_rule_infos[2].borrowing[2].id, 8);

    if (mpack_data) {
        free(mpack_data);
    }
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}