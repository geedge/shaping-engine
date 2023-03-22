extern "C" {
#include "libavl.h"
}
#include <MESA/stream.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include <x86intrin.h>

#define TEST_NUM_COUNT 10000

#define MAX_TREE_NODE_NUM 10000

static int
cmp_long(const void *p1, const void *p2)
{
    if (*(const unsigned long long *)p1 > *(const unsigned long long *)p2) {
        return 1;
    } else if (*(const unsigned long long *)p1 < *(const unsigned long long *)p2) {
        return -1;
    }

	return 0;
}

TEST(LibavlTest, EnqueueDequeueInOrder)
{
    struct avl_node *pnode = NULL;
    struct avl_tree *tree = NULL;
    unsigned long long time_array[TEST_NUM_COUNT];
    int i;

    tree = avl_tree_init(MAX_TREE_NODE_NUM);
    for (i = 0; i < TEST_NUM_COUNT; i++) {
        time_array[i] = _rdtsc();
        pnode = avl_tree_node_new(time_array[i], NULL, NULL);
        EXPECT_TRUE(NULL != pnode);

        EXPECT_TRUE(0 == avl_tree_node_insert(tree, pnode));

        EXPECT_EQ(pnode, avl_tree_node_lookup(tree, avl_tree_node_key_get(pnode)));
    }

    qsort(time_array, TEST_NUM_COUNT, sizeof(unsigned long long), cmp_long);
    for (i = 0; i < TEST_NUM_COUNT; i++) {
        pnode = avl_tree_minimum_node_get(tree);
        EXPECT_TRUE(NULL != pnode);
        EXPECT_EQ(time_array[i], avl_tree_node_key_get(pnode)) << "time != time in node at index " << i;
        avl_tree_node_remove(tree, pnode);
        avl_tree_node_free(pnode);
    }

    avl_tree_destroy(tree);
}

TEST(LibavlTest, EnqueueDequeueRandom)
{
    struct avl_node *pnode = NULL;
    struct avl_node *pnode2 = NULL;
    struct avl_tree *tree;
    unsigned long long time_array[TEST_NUM_COUNT];
    int i;

    tree = avl_tree_init(MAX_TREE_NODE_NUM);
    for (i = 0; i < TEST_NUM_COUNT; i++) {
        time_array[i] = _rdtsc();
        pnode = avl_tree_node_new(time_array[i], NULL, NULL);
        EXPECT_TRUE(NULL != pnode);

        EXPECT_TRUE(0 == avl_tree_node_insert(tree, pnode));
    }

    qsort(time_array, TEST_NUM_COUNT, sizeof(unsigned long long), cmp_long);
    for (i = 0; i < TEST_NUM_COUNT; i++) {
        pnode = avl_tree_minimum_node_get_and_pop(tree);
        EXPECT_TRUE(NULL != pnode);
        EXPECT_EQ(time_array[0], avl_tree_node_key_get(pnode));
        
        pnode2 = avl_tree_minimum_node_get_and_pop(tree);
        EXPECT_TRUE(NULL != pnode2);
        EXPECT_EQ(time_array[1], avl_tree_node_key_get(pnode2));

        avl_tree_node_insert(tree, pnode);
        avl_tree_node_insert(tree, pnode2);
    }

    avl_tree_destroy(tree);
}

TEST(LibavlTest, ExceedMaxNumLimit)
{
    struct avl_node *pnode = NULL;
    struct avl_tree *tree;
    int i;

    tree = avl_tree_init(MAX_TREE_NODE_NUM);
    for (i = 0; i < MAX_TREE_NODE_NUM; i++) {
        pnode = avl_tree_node_new(_rdtsc(), NULL, NULL);
        EXPECT_TRUE(NULL != pnode);

        EXPECT_TRUE(0 == avl_tree_node_insert(tree, pnode));
    }

    pnode = avl_tree_node_new(_rdtsc(), NULL, NULL);
    EXPECT_TRUE(-1 == avl_tree_node_insert(tree, pnode));
    avl_tree_node_free(pnode);

    avl_tree_destroy(tree);
}

TEST(LibavlTest, GetNextInOrder)
{
    struct avl_node *pnode = NULL;
    struct avl_tree *tree = NULL;
    unsigned long long time_array[TEST_NUM_COUNT];
    int i;

    tree = avl_tree_init(MAX_TREE_NODE_NUM);
    for (i = 0; i < TEST_NUM_COUNT; i++) {
        time_array[i] = _rdtsc();
        pnode = avl_tree_node_new(time_array[i], NULL, NULL);
        EXPECT_TRUE(NULL != pnode);

        EXPECT_TRUE(0 == avl_tree_node_insert(tree, pnode));

        EXPECT_EQ(pnode, avl_tree_node_lookup(tree, avl_tree_node_key_get(pnode)));
    }

    qsort(time_array, TEST_NUM_COUNT, sizeof(unsigned long long), cmp_long);

    pnode = avl_tree_minimum_node_get(tree);
    for (i = 0; i < TEST_NUM_COUNT; i++) {
        EXPECT_TRUE(NULL != pnode);
        EXPECT_EQ(time_array[i], avl_tree_node_key_get(pnode)) << "time != time in node at index " << i;
        pnode = avl_tree_next_in_order_node_get(pnode);
    }

    avl_tree_destroy(tree);
}

int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}