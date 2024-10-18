#pragma once
#include "shaper.h"

struct shaping_rule {
    uuid_t uuid;
    uuid_t primary_pf_uuid;
    uuid_t borrow_pf_uuid_array[SHAPING_REF_PROFILE_NUM_MAX];
    int borrow_pf_num;
    int vsys_id;
    int priority;
    int fair_factor;
    unsigned char dscp_enable;
    unsigned char dscp_value;
};

struct shaping_profile {
    uuid_t uuid;
    enum shaping_profile_type type;
    enum shaper_aqm_type aqm_type;
    enum shaping_profile_limit_direction limit_direction;
    int in_limit_bandwidth;
    int out_limit_bandwidth;
    int bidirection_limit_bandwidth;
    int valid;
};

void shaper_rule_ex_new(const char *table_name, const char *key, const char *table_line, void **ad, long argl, void *argp);
void shaper_rule_ex_dup(const char *table_name, void **to, void **from, long argl, void *argp);
void shaper_rule_ex_free(const char *table_name, void **ad, long argl, void *argp);
void shaper_profile_ex_new(const char *table_name, const char *key, const char *table_line, void **ad, long argl, void *argp);
void shaper_profile_ex_dup(const char *table_name, void **to, void **from, long argl, void *argp);
void shaper_profile_ex_free(const char *table_name, void **ad, long argl, void *argp);

int shaper_rule_is_enabled(struct shaping_thread_ctx *ctx, uuid_t rule_uuid);

struct shaping_profile *shaper_maat_profile_get(struct shaping_thread_ctx *ctx, uuid_t profile_uuid);

void shaper_rules_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, uuid_t *rule_uuids, int rule_num);
int shaper_maat_init(const char *instance_name);
void shaper_maat_destroy();