#include <assert.h>

#include "session_table.h"
#include "utils.h"
#include "log.h"

struct session_table
{
    struct session_node *root_by_id;
    struct session_node *root_by_addr;
    unsigned long long session_node_count;
};

struct session_table *session_table_create()
{
    struct session_table *table = (struct session_table *)calloc(1, sizeof(struct session_table));
    assert(table);
    table->session_node_count = 0;

    return table;
}

void session_table_destory(struct session_table *table)
{
    if (table)
    {
        struct session_node *temp = NULL;
        struct session_node *node = NULL;
        HASH_ITER(hh1, table->root_by_id, node, temp)
        {
            HASH_DELETE(hh1, table->root_by_id, node);
            HASH_DELETE(hh2, table->root_by_addr, node);

            if (node->val_freecb && node->val_data)
            {
                node->val_freecb(node->val_data);
            }

            free(node);
            node = NULL;
        }

        free(table);
        table = NULL;
    }
}

void session_table_reset(struct session_table *table)
{
    if (table)
    {
        struct session_node *temp = NULL;
        struct session_node *node = NULL;
        HASH_ITER(hh1, table->root_by_id, node, temp)
        {
            HASH_DELETE(hh1, table->root_by_id, node);
            HASH_DELETE(hh2, table->root_by_addr, node);

            if (node->val_freecb && node->val_data)
            {
                node->val_freecb(node->val_data);
            }

            free(node);
            node = NULL;

            table->session_node_count--;
        }
    }
}

void session_table_reset_with_callback(struct session_table *table, void (*cb)(void *session_data, void *data), void *data)
{
    if (table)
    {
        struct session_node *temp = NULL;
        struct session_node *node = NULL;
        HASH_ITER(hh1, table->root_by_id, node, temp)
        {
            HASH_DELETE(hh1, table->root_by_id, node);
            HASH_DELETE(hh2, table->root_by_addr, node);

            if (cb && node->val_data)
            {
                cb(node->val_data, data);
            }

            free(node);
            node = NULL;

            table->session_node_count--;
        }
    }
}

uint64_t session_table_count(struct session_table *table)
{
    if (table)
    {
        return table->session_node_count;
    }
    else
    {
        return 0;
    }
}

// session_addr : deep copy
// val_data     : shallow copy (malloc by user, free by val_freecb)
int session_table_insert(struct session_table *table, uint64_t session_id, const struct addr_tuple4 *session_addr, void *val_data, fn_free_cb *val_freecb)
{
    struct session_node *temp = NULL;
    HASH_FIND(hh1, table->root_by_id, &session_id, sizeof(session_id), temp);
    if (temp)
    {
        LOG_ERROR("%s: insert: key %lu exists", LOG_TAG_STABLE, session_id);
        return -1;
    }

    temp = (struct session_node *)calloc(1, sizeof(struct session_node));
    assert(temp);

    temp->session_id = session_id;
    memcpy(&temp->session_addr, session_addr, sizeof(struct addr_tuple4));
    temp->val_data = val_data;
    temp->val_freecb = val_freecb;

    HASH_ADD(hh1, table->root_by_id, session_id, sizeof(temp->session_id), temp);
    HASH_ADD(hh2, table->root_by_addr, session_addr, sizeof(temp->session_addr), temp);

    //LOG_DEBUG("%s: insert: key %lu success", LOG_TAG_STABLE, session_id);
    table->session_node_count++;

    return 0;
}

int session_table_delete_by_id(struct session_table *table, uint64_t session_id)
{
    struct session_node *temp = NULL;
    HASH_FIND(hh1, table->root_by_id, &session_id, sizeof(session_id), temp);
    if (!temp)
    {
        LOG_ERROR("%s: delete: key %lu not exists", LOG_TAG_STABLE, session_id);
        return -1;
    }

    HASH_DELETE(hh1, table->root_by_id, temp);
    HASH_DELETE(hh2, table->root_by_addr, temp);

    if (temp->val_freecb && temp->val_data)
    {
        temp->val_freecb(temp->val_data);
        temp->val_data = NULL;
    }

    free(temp);
    temp = NULL;

    //LOG_DEBUG("%s: delete: key %lu success", LOG_TAG_STABLE, session_id);
    table->session_node_count--;

    return 0;
}

int session_table_delete_by_addr(struct session_table *table, const struct addr_tuple4 *session_addr)
{
    struct session_node *temp = NULL;
    char *addr_str = addr_tuple4_to_str(session_addr);
    HASH_FIND(hh2, table->root_by_addr, session_addr, sizeof(struct addr_tuple4), temp);
    if (!temp)
    {
        struct addr_tuple4 reverse_addr;
        addr_tuple4_reverse(session_addr, &reverse_addr);
        HASH_FIND(hh2, table->root_by_addr, &reverse_addr, sizeof(struct addr_tuple4), temp);
        if (!temp)
        {
            LOG_ERROR("%s: delete: key %s not exists", LOG_TAG_STABLE, addr_str);
            free(addr_str);
            return -1;
        }
    }

    HASH_DELETE(hh1, table->root_by_id, temp);
    HASH_DELETE(hh2, table->root_by_addr, temp);

    if (temp->val_freecb && temp->val_data)
    {
        temp->val_freecb(temp->val_data);
        temp->val_data = NULL;
    }

    free(temp);
    temp = NULL;

    //LOG_DEBUG("%s: delete: key %s success", LOG_TAG_STABLE, addr_str);
    free(addr_str);
    addr_str = NULL;
    table->session_node_count--;

    return 0;
}

struct session_node *session_table_search_by_id(struct session_table *table, uint64_t session_id)
{
    struct session_node *temp = NULL;
    HASH_FIND(hh1, table->root_by_id, &session_id, sizeof(session_id), temp);
    //if (!temp)
    //{
        //LOG_DEBUG("%s: search: key %lu not exists", LOG_TAG_STABLE, session_id);
    //    return NULL;
    //}

    //LOG_DEBUG("%s: search: key %lu success", LOG_TAG_STABLE, session_id);

    return temp;
}

struct session_node *session_table_search_by_addr(struct session_table *table, const struct addr_tuple4 *session_addr)
{
    struct session_node *temp = NULL;
    char *addr_str = addr_tuple4_to_str(session_addr);
    HASH_FIND(hh2, table->root_by_addr, session_addr, sizeof(struct addr_tuple4), temp);
    if (!temp)
    {
        struct addr_tuple4 reverse_addr;
        addr_tuple4_reverse(session_addr, &reverse_addr);
        HASH_FIND(hh2, table->root_by_addr, &reverse_addr, sizeof(struct addr_tuple4), temp);
        if (!temp)
        {
            //LOG_DEBUG("%s: search: key %s not exists", LOG_TAG_STABLE, addr_str);
            free(addr_str);
            addr_str = NULL;
            return NULL;
        }
    }

    //LOG_DEBUG("%s: search: key %s success", LOG_TAG_STABLE, addr_str);
    free(addr_str);
    addr_str = NULL;

    return temp;
}