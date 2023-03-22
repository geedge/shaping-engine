struct avl_node;
struct avl_tree;

struct avl_tree* avl_tree_init(unsigned int max_node_num);
void avl_tree_destroy(struct avl_tree* tree);

struct avl_node *avl_tree_node_new(unsigned long long unique_key, void *data, void (*free_cb)(void *));
void avl_tree_node_free(struct avl_node *pnode);

int avl_tree_node_key_set(struct avl_node *pnode, unsigned long long key);
unsigned long long avl_tree_node_key_get(struct avl_node *pnode);
void *avl_tree_node_data_get(struct avl_node *pnode);

int avl_node_in_tree(struct avl_node* pnode);
struct avl_node *avl_tree_node_lookup(struct avl_tree *tree, unsigned long long key);

int avl_tree_node_insert(struct avl_tree *tree, struct avl_node* pnode);

void avl_tree_node_remove(struct avl_tree *tree, struct avl_node* pnode);

struct avl_node *avl_tree_minimum_node_get(struct avl_tree* tree);
struct avl_node *avl_tree_minimum_node_get_and_pop(struct avl_tree* tree);

struct avl_node *avl_tree_next_in_order_node_get(struct avl_node* pnode);