#include <stdlib.h>
#include "libavl.h"
#include "avl_tree.h"

#define AVL_NODE_IN_TREE 0x01

struct avl_node {
    unsigned long long unique_key;
    void *data;
    void(*free_cb) (void *);
    unsigned int flag;
    struct avl_tree_node node;
};

struct avl_tree {
    struct avl_tree_node *root;
    unsigned int max_node_num;
    unsigned int curr_node_num;
};

struct avl_tree* avl_tree_init(unsigned int max_node_num)
{
    struct avl_tree *tree = NULL;

    tree = (struct avl_tree*)calloc(1, sizeof(struct avl_tree));

    if (tree) {
        tree->max_node_num = max_node_num;
    }

    return tree;
}

struct avl_node *avl_tree_node_new(unsigned long long unique_key, void *data, void (*free_cb)(void *))
{
    struct avl_node *ret = NULL;

    ret = (struct avl_node*)calloc(1, sizeof(struct avl_node));
    if (ret) {
        ret->unique_key = unique_key;
        ret->data = data;
        ret->free_cb = free_cb;
    }

    return ret;
}

int avl_tree_node_key_set(struct avl_node *pnode, unsigned long long key)
{
    if (!pnode) {
        return -1;
    }

    pnode->unique_key = key;
    return 0;
}

unsigned long long avl_tree_node_key_get(struct avl_node *pnode)
{
    if (!pnode) {
        return 0;
    }
    return pnode->unique_key;
}

void *avl_tree_node_data_get(struct avl_node *pnode)
{
    if (!pnode) {
        return NULL;
    }
    return pnode->data;
}

void avl_tree_node_free(struct avl_node *pnode)
{
    if (pnode) {
        if (pnode->free_cb) {
            pnode->free_cb(pnode->data);
        }
        free(pnode);
        pnode = NULL;
    }
    return;
}

static void avl_tree_traverse_node_free(struct avl_tree_node **root)
{
    struct avl_tree_node *temp_tree_node = NULL;
    struct avl_node *temp_node = NULL;

    temp_tree_node = avl_tree_first_in_order(*root);
    while (temp_tree_node) {
        temp_node = avl_tree_entry(temp_tree_node, struct avl_node, node);
        avl_tree_remove(root, temp_tree_node);
        avl_tree_node_free(temp_node);
        temp_tree_node = avl_tree_first_in_order(*root);
    }
    return;
}

void avl_tree_destroy(struct avl_tree *tree)
{
    avl_tree_traverse_node_free(&tree->root);
    free(tree);

    return;
}

static int
cmp_nodes_value(const struct avl_tree_node *node1,
	      const struct avl_tree_node *node2)
{
    long long diff;
    struct avl_node *temp1 = avl_tree_entry(node1, struct avl_node, node);
    struct avl_node *temp2 = avl_tree_entry(node2, struct avl_node, node);

    diff = temp1->unique_key - temp2->unique_key;

    if (diff > 0) {
        return 1;
    } else if (diff < 0) {
        return -1;
    } else {
        return 0;
    }
}

int avl_node_in_tree(struct avl_node* pnode)
{
    if (!pnode) {
        return 0;
    }

    if (pnode->flag & AVL_NODE_IN_TREE) {
        return 1;
    }

    return 0;
}

struct avl_node *avl_tree_node_lookup(struct avl_tree *tree, unsigned long long key)
{
    struct avl_node lookup;
    struct avl_tree_node *temp;
    struct avl_node *ret = NULL;

    lookup.unique_key = key;
    temp = avl_tree_lookup_node(tree->root, &lookup.node, cmp_nodes_value);
    if (temp) {
        ret = avl_tree_entry(temp, struct avl_node, node);
    }

    return ret;

}

int avl_tree_node_insert(struct avl_tree *tree, struct avl_node* pnode)
{
    struct avl_tree_node *temp = NULL;

    if (!pnode) {
        return -1;
    }

    if (tree->curr_node_num == tree->max_node_num) {
        return -1;
    }
    
    temp = avl_tree_insert(&tree->root, &pnode->node, cmp_nodes_value);
    if (temp) {
        return -1;
    }

    pnode->flag |= AVL_NODE_IN_TREE;
    tree->curr_node_num++;

    return 0;
}

void avl_tree_node_remove(struct avl_tree *tree, struct avl_node* pnode)
{
    if (!pnode || !(pnode->flag & AVL_NODE_IN_TREE)) {
        return;
    }

    avl_tree_remove(&tree->root, &pnode->node);
    pnode->flag &= ~AVL_NODE_IN_TREE;
    tree->curr_node_num--;
    return;
}

struct avl_node *avl_tree_minimum_node_get(struct avl_tree *tree)
{
    struct avl_node *ret = NULL;
    struct avl_tree_node *temp = NULL;

    temp = avl_tree_first_in_order(tree->root);
    if (temp) {
        ret = avl_tree_entry(temp, struct avl_node, node);
    }

    return ret;
}

struct avl_node *avl_tree_minimum_node_get_and_pop(struct avl_tree* tree)
{
    struct avl_node *node = NULL;

    node = avl_tree_minimum_node_get(tree);
    if (node) {
        avl_tree_node_remove(tree, node);
    }

    return node;
}

struct avl_node *avl_tree_next_in_order_node_get(struct avl_node* pnode)
{
    struct avl_node *node = NULL;
    struct avl_tree_node *temp = NULL;

    temp = avl_tree_next_in_order(&pnode->node);
    if (temp) {
        node = avl_tree_entry(temp, struct avl_node, node);
    }

    return node;
}