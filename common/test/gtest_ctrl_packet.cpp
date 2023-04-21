#include <cstring>
#include <gtest/gtest.h>
#include <msgpack.h>

#include "shaper_marsio.h"

static int construct_shaping_cmsg(const char *tsync, const char *session_id, const char *state, long long *shaping_rule_ids, int shaping_rule_id_num, msgpack_sbuffer *sbuf)
{
	msgpack_packer pk;

	msgpack_packer_init(&pk, sbuf, msgpack_sbuffer_write);

	msgpack_pack_array(&pk, INDEX_MAX);

	// tsync
	msgpack_pack_str(&pk, strlen(tsync));
	msgpack_pack_str_body(&pk, tsync, strlen(tsync));

	//session_id
	msgpack_pack_str(&pk, strlen(session_id));
	msgpack_pack_str_body(&pk, session_id, strlen(session_id));

	// state
	msgpack_pack_str(&pk, strlen(state));
	msgpack_pack_str_body(&pk, state, strlen(state));

	// type:sce
	msgpack_pack_nil(&pk);

	// type:shaper
	msgpack_pack_array(&pk, 1);
	msgpack_pack_array(&pk, shaping_rule_id_num);
	for (int i = 0; i < shaping_rule_id_num; i++)
	{
		msgpack_pack_long_long(&pk, shaping_rule_ids[i]);
	}

	// type:proxy
	msgpack_pack_nil(&pk);

	return 0;

}

TEST(CTRL_PACKET, PARSE)
{
	const char *tsync = "2.0";
	const char *session_id = "123456789";
	const char *state = "active";
	long long shaping_rule_ids[] = {4, 5, 6};
	msgpack_sbuffer sbuf;

	msgpack_sbuffer_init(&sbuf);
	construct_shaping_cmsg(tsync, session_id, state, shaping_rule_ids, 3, &sbuf);

	struct ctrl_pkt_data ctrl_data;
	memset(&ctrl_data, 0, sizeof(ctrl_data));
	EXPECT_EQ(shaper_marsio_ctrl_pkt_data_parse(&ctrl_data, sbuf.data, sbuf.size + 1), 0);

	EXPECT_EQ(ctrl_data.session_id, 123456789);
	EXPECT_EQ(ctrl_data.state, SESSION_STATE_ACTIVE);
	EXPECT_EQ(ctrl_data.shaping_rule_num, 3);
	EXPECT_EQ(ctrl_data.shaping_rule_ids[0], 4);
	EXPECT_EQ(ctrl_data.shaping_rule_ids[1], 5);
	EXPECT_EQ(ctrl_data.shaping_rule_ids[2], 6);

	msgpack_sbuffer_destroy(&sbuf);
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}