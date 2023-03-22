#include <cstring>
#include <gtest/gtest.h>

#include "shaper_marsio.h"

TEST(CTRL_PACKET, PARSE)
{
	const char *data = "{\"tsync\":\"1.0\",\"session_id\":\"123456789\",\"state\":\"active\",\"method\":\"policy_update\",\"params\":{\"service_chaining\":[1,2,3],\"shaping\":[4,5,6]}}";
	size_t length = strlen(data);

	struct ctrl_pkt_data ctrl_data;
	memset(&ctrl_data, 0, sizeof(ctrl_data));
	EXPECT_TRUE(shaper_marsio_ctrl_pkt_data_parse(&ctrl_data, data, length) == 0);

	EXPECT_TRUE(ctrl_data.session_id == 123456789);
	EXPECT_TRUE(ctrl_data.state == SESSION_STATE_ACTIVE);
	EXPECT_TRUE(ctrl_data.shaping_rule_num == 3);
	EXPECT_TRUE(ctrl_data.shaping_rule_ids[0] == 4);
	EXPECT_TRUE(ctrl_data.shaping_rule_ids[1] == 5);
	EXPECT_TRUE(ctrl_data.shaping_rule_ids[2] == 6);
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}