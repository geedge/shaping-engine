#include <gtest/gtest.h>

#include "utils.h"

TEST(UTILS, FIXED_NUM_ARRAY)
{
	struct fixed_num_array array;
	fixed_num_array_init(&array);

	fixed_num_array_add_elem(&array, 1);
	fixed_num_array_add_elem(&array, 2);
	fixed_num_array_add_elem(&array, 3);
	fixed_num_array_add_elem(&array, 1);
	fixed_num_array_add_elem(&array, 2);

	EXPECT_TRUE(fixed_num_array_count_elem(&array) == 5);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 0) == 1);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 1) == 2);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 2) == 3);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 3) == 1);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 4) == 2);

	fixed_num_array_del_elem(&array, 3); // 1,2,1,2
	EXPECT_TRUE(fixed_num_array_count_elem(&array) == 4);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 0) == 1);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 1) == 2);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 2) == 1);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 3) == 2);

	fixed_num_array_del_elem(&array, 2); // 1,1
	EXPECT_TRUE(fixed_num_array_count_elem(&array) == 2);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 0) == 1);
	EXPECT_TRUE(fixed_num_array_index_elem(&array, 1) == 1);

	fixed_num_array_del_elem(&array, 1);
	EXPECT_TRUE(fixed_num_array_count_elem(&array) == 0);
}

TEST(UTILS, SIDS)
{
	struct sids src;
	struct sids dst;

	// set src
	for (int i = 0; i < MR_SID_LIST_MAXLEN; i++)
	{
		src.elems[i] = i;
	}
	src.num = MR_SID_LIST_MAXLEN;

	// copy src to dst
	memset(&dst, 0, sizeof(struct sids));
	sids_write_once(&dst, &src);

	// check dst
	EXPECT_TRUE(dst.num == src.num);
	for (int i = 0; i < MR_SID_LIST_MAXLEN; i++)
	{
		EXPECT_TRUE(dst.elems[i] == i);
	}

	// update src
	for (int i = 0; i < MR_SID_LIST_MAXLEN; i++)
	{
		src.elems[i] = i * 2;
	}

	// copy src to dst
	sids_write_once(&dst, &src);

	// check dst
	EXPECT_TRUE(dst.num == src.num);
	for (int i = 0; i < MR_SID_LIST_MAXLEN; i++)
	{
		EXPECT_TRUE(dst.elems[i] == i);
	}
}

TEST(UTILS, THROUGHPUT_METRICS)
{
	struct throughput_metrics iterm = {.n_pkts = 0, .n_bytes = 0};
	throughput_metrics_inc(&iterm, 1, 2);
	EXPECT_TRUE(iterm.n_pkts == 1);
	EXPECT_TRUE(iterm.n_bytes == 2);
}

TEST(UTILS, DEVICE)
{
	const char *dev_name = "lo";
	char ip_buff[16] = {0};
	char mac_buff[32] = {0};

	EXPECT_TRUE(get_ip_by_device_name(dev_name, ip_buff) == 0);
	EXPECT_TRUE(get_mac_by_device_name(dev_name, mac_buff) == 0);

	printf("Dev : %s, IP  : %s\n", dev_name, ip_buff);
	printf("Dev : %s, MAC : %s\n", dev_name, mac_buff);

	EXPECT_STREQ(ip_buff, "127.0.0.1");
	EXPECT_STREQ(mac_buff, "00:00:00:00:00:00");
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}