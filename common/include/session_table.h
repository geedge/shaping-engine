#ifndef _SESSION_TABLE_H
#define _SESSION_TABLE_H

#include "uthash.h"
#include "addr_tuple4.h"

typedef void fn_free_cb(void *args);
struct session_node
{
    uint64_t session_id;             /* first key */
    struct addr_tuple4 session_addr; /* second key */

    void *val_data;
    fn_free_cb *val_freecb;

    UT_hash_handle hh1; /* handle for first hash table */
    UT_hash_handle hh2; /* handle for second hash table */
};

struct session_table;

struct session_table *session_table_create();
void session_table_destory(struct session_table *table);
void session_table_reset(struct session_table *table);
void session_table_reset_with_callback(struct session_table *table, void (*cb)(void *session_data, void *data), void *data);
uint64_t session_table_count(struct session_table *table);

// session_addr : deep copy
// val_data     : shallow copy (malloc by user, free by val_freecb)
// return  0 : suceess
// return -1 : key exists
int session_table_insert(struct session_table *table, uint64_t session_id, const struct addr_tuple4 *session_addr, void *val_data, fn_free_cb *val_freecb);

// return  0 : success
// return -1 : key not exists
int session_table_delete_by_id(struct session_table *table, uint64_t session_id);
int session_table_delete_by_addr(struct session_table *table, const struct addr_tuple4 *session_addr);

// return   NULL : key not exists
// return UnNULL : success
struct session_node *session_table_search_by_id(struct session_table *table, uint64_t session_id);
struct session_node *session_table_search_by_addr(struct session_table *table, const struct addr_tuple4 *session_addr);

#endif