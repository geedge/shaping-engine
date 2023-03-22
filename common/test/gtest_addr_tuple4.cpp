#include <gtest/gtest.h>
#include <arpa/inet.h>

#include "addr_tuple4.h"

TEST(ADDR_TUPLE4, IPV4)
{
	char *ret_str = NULL;
	struct addr_tuple4 reve_addr;

	INIT_ADDR_V4(orin_addr, "1.2.3.4", 12345, "4.3.2.1", 23456)
	addr_tuple4_reverse(&orin_addr, &reve_addr);

	ret_str = addr_tuple4_to_str(&orin_addr);
	EXPECT_TRUE(ret_str != nullptr);
	EXPECT_STREQ(ret_str, "1.2.3.4 12345 4.3.2.1 23456");
	free(ret_str);

	ret_str = addr_tuple4_to_str(&reve_addr);
	EXPECT_TRUE(ret_str != nullptr);
	EXPECT_STREQ(ret_str, "4.3.2.1 23456 1.2.3.4 12345");
	free(ret_str);
}

TEST(ADDR_TUPLE4, IPV6)
{
	char *ret_str = NULL;
	struct addr_tuple4 reve_addr;

	INIT_ADDR_V6(orin_addr, "1:2::3", 12345, "a:b::c", 23456);
	addr_tuple4_reverse(&orin_addr, &reve_addr);

	ret_str = addr_tuple4_to_str(&orin_addr);
	EXPECT_TRUE(ret_str != nullptr);
	EXPECT_STREQ(ret_str, "1:2::3 12345 a:b::c 23456");
	free(ret_str);

	ret_str = addr_tuple4_to_str(&reve_addr);
	EXPECT_TRUE(ret_str != nullptr);
	EXPECT_STREQ(ret_str, "a:b::c 23456 1:2::3 12345");
	free(ret_str);
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}