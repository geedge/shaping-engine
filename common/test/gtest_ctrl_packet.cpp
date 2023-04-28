#include <gtest/gtest.h>
#include "mpack.h"

#include "shaper_marsio.h"

static int construct_shaping_cmsg(const char *tsync, unsigned long long session_id, const char *state, long long *shaping_rule_ids, int shaping_rule_id_num, char **mpack_data, size_t *mpack_size)
{
	mpack_writer_t writer;

	mpack_writer_init_growable(&writer, mpack_data, mpack_size);
	mpack_build_map(&writer);

	//tsync
	mpack_write_cstr(&writer, "tsync");
	mpack_write_cstr(&writer, "2.0");

	//session_id
	mpack_write_cstr(&writer, "session_id");
	mpack_write_u64(&writer, session_id);

	//state
	mpack_write_cstr(&writer, "state");
	mpack_write_cstr(&writer, state);

	//method: policy_update
	mpack_write_cstr(&writer, "method");
	mpack_write_cstr(&writer, "policy_update");

	//params
	mpack_write_cstr(&writer, "params");
	mpack_build_map(&writer);
	mpack_write_cstr(&writer, "shaper");
	mpack_build_map(&writer);
	mpack_write_cstr(&writer, "rule_ids");
	mpack_build_array(&writer);
	for (int i = 0; i < shaping_rule_id_num; i++) {
		mpack_write_i64(&writer, shaping_rule_ids[i]);
	}
	mpack_complete_array(&writer);//build array rule_ids
	mpack_complete_map(&writer);//build map shaper
	mpack_complete_map(&writer);//build map params
	mpack_complete_map(&writer);//build map at the beginning

	mpack_writer_destroy(&writer);

	return 0;

}

TEST(CTRL_PACKET, PARSE)
{
	const char *tsync = "2.0";
	unsigned long long session_id = 123456789;
	const char *state = "active";
	long long shaping_rule_ids[] = {4, 5, 6};
	char *mpack_data = NULL;
	size_t mpack_size = 0;

	construct_shaping_cmsg(tsync, session_id, state, shaping_rule_ids, 3, &mpack_data, &mpack_size);

	struct ctrl_pkt_data ctrl_data;
	memset(&ctrl_data, 0, sizeof(ctrl_data));
	EXPECT_EQ(shaper_marsio_ctrl_pkt_data_parse(&ctrl_data, mpack_data, mpack_size), 0);

	EXPECT_EQ(ctrl_data.session_id, 123456789);
	EXPECT_EQ(ctrl_data.state, SESSION_STATE_ACTIVE);
	EXPECT_EQ(ctrl_data.shaping_rule_num, 3);
	EXPECT_EQ(ctrl_data.shaping_rule_ids[0], 4);
	EXPECT_EQ(ctrl_data.shaping_rule_ids[1], 5);
	EXPECT_EQ(ctrl_data.shaping_rule_ids[2], 6);

	if (mpack_data) {
		free(mpack_data);
	}
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}