#include "shaper.h"

struct shaping_maat_info {
    int rule_table_id;
    int profile_table_id;
};

struct shaping_rule {
    int vsys_id;
    int id;
    int priority;
    int primary_pf_id;
    int borrow_pf_id_array[SHAPING_REF_PROFILE_NUM_MAX];
    int borrow_pf_num;
    int fair_factor;
    int ref_cnt;
};

struct shaping_profile {
    int id;
    enum shaping_profile_type type;
    int in_limit_bandwidth;
    int out_limit_bandwidth;
    int valid;
    int ref_cnt;
};

void shaper_rule_ex_new(const char *table_name, int table_id, const char *key, const char *table_line, void **ad, long argl, void *argp);
void shaper_rule_ex_dup(int table_id, void **to, void **from, long argl, void *argp);
void shaper_rule_ex_free(int table_id, void **ad, long argl, void *argp);
void shaper_profile_ex_new(const char *table_name, int table_id, const char *key, const char *table_line, void **ad, long argl, void *argp);
void shaper_profile_ex_dup(int table_id, void **to, void **from, long argl, void *argp);
void shaper_profile_ex_free(int table_id, void **ad, long argl, void *argp);


void shaper_rules_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, long long *rule_compile_ids, int rule_num);
struct shaping_maat_info* shaper_maat_init(const char *instance_name);
void shaper_maat_destroy(struct shaping_maat_info *maat_info);