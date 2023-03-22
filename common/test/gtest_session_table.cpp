#include <gtest/gtest.h>
#include <arpa/inet.h>

#include "session_table.h"

TEST(STREAM_TABLE, INSERT)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == -1);
	EXPECT_TRUE(session_table_count(table) == 1);

	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == -1);
	EXPECT_TRUE(session_table_count(table) == 2);

	// TEST Destory
	session_table_destory(table);
}

TEST(STREAM_TABLE, SEARCH_BY_ID)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);
	INIT_ADDR_V4(addr3, "1.1.1.1", 1111, "2.2.2.2", 2222);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_count(table) == 2);

	// TEST Search By Session ID
	struct session_node *node = NULL;
	node = session_table_search_by_id(table, 1);
	EXPECT_TRUE(node != nullptr);
	EXPECT_STREQ((const char *)node->val_data, "HELLO");
	node = session_table_search_by_id(table, 2);
	EXPECT_TRUE(node != nullptr);
	EXPECT_STREQ((const char *)node->val_data, "WORLD");
	node = session_table_search_by_id(table, 3);
	EXPECT_TRUE(node == nullptr);

	// TEST Destory
	session_table_destory(table);
}

TEST(STREAM_TABLE, SEARCH_BY_ADDR)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);
	INIT_ADDR_V4(addr3, "1.1.1.1", 1111, "2.2.2.2", 2222);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_count(table) == 2);

	// TEST Search By Session Addr
	struct session_node *node = NULL;
	node = session_table_search_by_addr(table, &addr1);
	EXPECT_TRUE(node != nullptr);
	EXPECT_STREQ((const char *)node->val_data, "HELLO");
	node = session_table_search_by_addr(table, &addr2);
	EXPECT_TRUE(node != nullptr);
	EXPECT_STREQ((const char *)node->val_data, "WORLD");
	node = session_table_search_by_addr(table, &addr3);
	EXPECT_TRUE(node == nullptr);

	// TEST Destory
	session_table_destory(table);
}

TEST(STREAM_TABLE, SEARCH_BY_REVERSE_ADDR)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);
	struct addr_tuple4 addr1_reverse;
	struct addr_tuple4 addr2_reverse;
	addr_tuple4_reverse(&addr1, &addr1_reverse);
	addr_tuple4_reverse(&addr2, &addr2_reverse);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_count(table) == 2);

	// TEST Search By Session Reverse Addr
	struct session_node *node = NULL;
	node = session_table_search_by_addr(table, &addr1_reverse);
	EXPECT_TRUE(node != nullptr);
	EXPECT_STREQ((const char *)node->val_data, "HELLO");
	node = session_table_search_by_addr(table, &addr2_reverse);
	EXPECT_TRUE(node != nullptr);
	EXPECT_STREQ((const char *)node->val_data, "WORLD");

	// TEST Destory
	session_table_destory(table);
}

TEST(STREAM_TABLE, DELETE_BY_ID)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);
	struct addr_tuple4 addr1_reverse;
	struct addr_tuple4 addr2_reverse;
	addr_tuple4_reverse(&addr1, &addr1_reverse);
	addr_tuple4_reverse(&addr2, &addr2_reverse);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_count(table) == 2);

	// TEST Delete By Session ID
	EXPECT_TRUE(session_table_delete_by_id(table, 1) == 0);
	EXPECT_TRUE(session_table_search_by_id(table, 1) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr1) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr1_reverse) == NULL);
	EXPECT_TRUE(session_table_count(table) == 1);

	EXPECT_TRUE(session_table_delete_by_id(table, 2) == 0);
	EXPECT_TRUE(session_table_search_by_id(table, 2) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr2) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr2_reverse) == NULL);
	EXPECT_TRUE(session_table_count(table) == 0);

	// TEST Destory
	session_table_destory(table);
}

TEST(STREAM_TABLE, DELETE_BY_ADDR)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);
	struct addr_tuple4 addr1_reverse;
	struct addr_tuple4 addr2_reverse;
	addr_tuple4_reverse(&addr1, &addr1_reverse);
	addr_tuple4_reverse(&addr2, &addr2_reverse);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_count(table) == 2);

	// TEST Delete By Session Addr
	EXPECT_TRUE(session_table_delete_by_addr(table, &addr1) == 0);
	EXPECT_TRUE(session_table_search_by_id(table, 1) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr1) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr1_reverse) == NULL);
	EXPECT_TRUE(session_table_count(table) == 1);

	EXPECT_TRUE(session_table_delete_by_addr(table, &addr2) == 0);
	EXPECT_TRUE(session_table_search_by_id(table, 2) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr2) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr2_reverse) == NULL);
	EXPECT_TRUE(session_table_count(table) == 0);

	// TEST Destory
	session_table_destory(table);
}

TEST(STREAM_TABLE, DELETE_BY_REVERSE_ADDR)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);
	struct addr_tuple4 addr1_reverse;
	struct addr_tuple4 addr2_reverse;
	addr_tuple4_reverse(&addr1, &addr1_reverse);
	addr_tuple4_reverse(&addr2, &addr2_reverse);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_count(table) == 2);

	// TEST Delete By Session Reverse Addr
	EXPECT_TRUE(session_table_delete_by_addr(table, &addr1_reverse) == 0);
	EXPECT_TRUE(session_table_search_by_id(table, 1) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr1) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr1_reverse) == NULL);
	EXPECT_TRUE(session_table_count(table) == 1);

	EXPECT_TRUE(session_table_delete_by_addr(table, &addr2_reverse) == 0);
	EXPECT_TRUE(session_table_search_by_id(table, 2) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr2) == NULL);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr2_reverse) == NULL);
	EXPECT_TRUE(session_table_count(table) == 0);

	// TEST Destory
	session_table_destory(table);
}

TEST(STREAM_TABLE, RESET)
{
	// TEST Create
	struct session_table *table = session_table_create();
	EXPECT_TRUE(table != nullptr);

	char *val_hello = strdup("HELLO");
	char *val_world = strdup("WORLD");
	INIT_ADDR_V4(addr1, "1.2.3.4", 1234, "4.3.2.1", 4321);
	INIT_ADDR_V6(addr2, "2:3:4::5", 2345, "5:4:3::2", 5342);

	// TEST Insert
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 1, &addr1, val_hello, free) == -1);
	EXPECT_TRUE(session_table_count(table) == 1);

	// TEST Reset
	session_table_reset(table);
	EXPECT_TRUE(session_table_search_by_id(table, 1) == nullptr);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr1) == nullptr);
	EXPECT_TRUE(session_table_count(table) == 0);

	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == 0);
	EXPECT_TRUE(session_table_insert(table, 2, &addr2, val_world, free) == -1);
	EXPECT_TRUE(session_table_search_by_id(table, 2) != nullptr);
	EXPECT_TRUE(session_table_search_by_addr(table, &addr2) != nullptr);
	EXPECT_TRUE(session_table_count(table) == 1);

	// TEST Destory
	session_table_destory(table);
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}