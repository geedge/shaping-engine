#include <MESA_prof_load.h>
#include <cstring>
#include <stdio.h>
#include <time.h>

#include <cjson/cJSON.h>
#include <MESA/maat.h>
#include <MESA/MESA_handle_logger.h>

#include "log.h"
#include "shaper.h"
#include "shaper_maat.h"
#include "shaper_stat.h"
#include "utils.h"

#define SHAPING_STREAM_TIMEOUT 3600
#define SHAPING_RULE_TABLE_NAME "TRAFFIC_SHAPING_COMPILE"
#define SHAPING_PROFILE_TABLE_NAME "TRAFFIC_SHAPING_PROFILE"

enum input_mode
{
    MAAT_INPUT_JSON = 0,
    MAAT_INPUT_REDIS = 1,
    MAAT_INPUT_FILE = 2,
};

struct shaper_maat_config {
    enum input_mode input_mode;
    char table_info[2048];
    char accept_tags[2048];
    char json_cfg_file[2048];
    int redis_db_idx;
    char redis_ip[2048];
    char redis_port_range[2048];
};

struct maat *g_maat_instance = NULL;

void shaper_rule_ex_new(const char *table_name, int table_id, const char *key, const char *table_line, void **ad, long argl, void *argp)
{
    struct shaping_rule *s_rule;
    cJSON *json=NULL;
    cJSON *tmp_obj = NULL;
    cJSON *tmp_array_obj = NULL;
    int array_size;
    char user_region[1024] = {0};
    int i, ret;

    if (strncmp(table_name, SHAPING_RULE_TABLE_NAME, strlen(table_name)) != 0) {
        return;
    }

    s_rule = (struct shaping_rule*)calloc(1, sizeof(struct shaping_rule));
    s_rule->ref_cnt = 1;

    ret = sscanf(table_line, "%d\t%*d\t%*d\t%*d\t%*d\t%*s\t%s\t", &s_rule->id, user_region);
    if (ret != 2) {
        LOG_ERROR("%s: sscanf parse rule failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    json = cJSON_Parse(user_region);
    if (!json) {//required
        LOG_ERROR("%s: json parse rule failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    tmp_obj = cJSON_GetObjectItem(json, "vsys_id");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse vsys_id failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    s_rule->vsys_id = tmp_obj->valueint;

    tmp_obj = cJSON_GetObjectItem(json, "priority");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse priority failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    s_rule->priority = tmp_obj->valueint;

    tmp_obj = cJSON_GetObjectItem(json, "fair_factor");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse fair-factor failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    s_rule->fair_factor = tmp_obj->valueint;

    tmp_obj = cJSON_GetObjectItem(json, "profile_chain");
    if (!tmp_obj) {//required
        LOG_ERROR("%s: json parse profile_chain failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
        
    array_size = cJSON_GetArraySize(tmp_obj);
    if (array_size < 1) {
        LOG_ERROR("%s: json parse profile_chain empty for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    tmp_array_obj = cJSON_GetArrayItem(tmp_obj, 0);
    s_rule->primary_pf_id = tmp_array_obj->valueint;

    s_rule->borrow_pf_num = array_size - 1;
    for (i = 1; i < array_size; i++) {
        tmp_array_obj = cJSON_GetArrayItem(tmp_obj, i);
        s_rule->borrow_pf_id_array[i - 1] = tmp_array_obj->valueint;
    }

END:
    *ad = s_rule;
    if (json) {
        cJSON_Delete(json);
    }
    return;
}

void shaper_rule_ex_dup(int table_id, void **to, void **from, long argl, void *argp)
{
    struct shaping_rule *s_rule;

    if (*from == NULL) {
        return;
    }

    s_rule = (struct shaping_rule*)*from;
    __atomic_add_fetch(&s_rule->ref_cnt, 1, __ATOMIC_SEQ_CST);
    *to = s_rule;

    return;
}

void shaper_rule_ex_free(int table_id, void **ad, long argl, void *argp)
{
    struct shaping_rule *s_rule;

    if (*ad == NULL) {
        return;
    }

    s_rule = (struct shaping_rule*)*ad;
    if (__atomic_sub_fetch(&s_rule->ref_cnt, 1, __ATOMIC_SEQ_CST) > 0) {
        return;
    }

    free(s_rule);
    *ad = NULL;

    return;
}

void shaper_profile_ex_new(const char *table_name, int table_id, const char *key, const char *table_line, void **ad, long argl, void *argp)
{
    struct shaping_profile *s_pf;
    cJSON *json=NULL;
    cJSON *tmp_array_obj = NULL;
    cJSON *tmp_obj = NULL;
    char profile_type[64] = {0};
    char type_arg[64] = {0};
    char limits[128] = {0};
    char aqm_options[64] = {0};
    char dscp_marking[64] = {0};
    char volume_based_shaping[64] = {0};
    int limit_bandwidth;
    int array_size, i;
    int ret;

    if (strncmp(table_name, SHAPING_PROFILE_TABLE_NAME, strlen(table_name)) != 0) {
        return;
    }

    s_pf = (struct shaping_profile*)calloc(1, sizeof(struct shaping_profile));
    s_pf->ref_cnt = 1;

    ret = sscanf(table_line, "%d\t%63s\t%63s\t%127s\t%63s\t%63s\t%63s\t%d",
            &s_pf->id, profile_type, type_arg, limits, aqm_options, dscp_marking, volume_based_shaping, &s_pf->valid);
    if (ret != 8) {
        LOG_ERROR("%s: sscanf parse failed for profile line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    if (strcmp(profile_type, "generic") == 0) {
        s_pf->type = PROFILE_TYPE_GENERIC;
    } else if (strcmp(profile_type, "fair_share") == 0) {
        if (strcmp(type_arg, "host_fairness") == 0) {
            s_pf->type = PROFILE_TYPE_HOST_FARINESS;
        } else if (strcmp(type_arg, "max_min_host_fairness") == 0) {
            s_pf->type = PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS;
        } else {
            LOG_ERROR("%s: profile type argument wrong for profile line %s", LOG_TAG_MAAT, table_line);
            goto END;
        }
    } else if (strcmp(profile_type, "split_by") == 0) {
        if (strcmp(type_arg, "local_host") == 0) {
            s_pf->type = PROFILE_TYPE_SPLIT_BY_LOCAL_HOST;
        } else {
            LOG_ERROR("%s: profile type argument wrong for profile line %s", LOG_TAG_MAAT, table_line);
            goto END;
        }
    } else {
        LOG_ERROR("%s: profile type wrong for profile line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    
    //parse limits of profile
    json = cJSON_Parse(limits);
    if (!json) {
        LOG_ERROR("%s: json parse profile limits failed for profile id %d, line %s", LOG_TAG_MAAT, s_pf->id, table_line);
        goto END;
    }

    array_size = cJSON_GetArraySize(json);
    for (i = 0; i < array_size; i++) {
        tmp_array_obj = cJSON_GetArrayItem(json, i);
        tmp_obj = cJSON_GetObjectItem(tmp_array_obj, "bandwidth");
        if (!tmp_obj) {
            LOG_ERROR("%s: json parse limit bandwidth failed for profile id %d, line %s", LOG_TAG_MAAT, s_pf->id, table_line);
            goto END;
        }
        limit_bandwidth = tmp_obj->valueint;

        tmp_obj = cJSON_GetObjectItem(tmp_array_obj, "direction");
        if (!tmp_obj) {
            LOG_ERROR("%s: json parse limit direction failed for profile id %d, line %s", LOG_TAG_MAAT, s_pf->id, table_line);
            goto END;
        }
        if (strncmp(tmp_obj->valuestring, "incoming", strlen(tmp_obj->valuestring)) == 0) {
            s_pf->in_limit_bandwidth = limit_bandwidth;
        } else {
            s_pf->out_limit_bandwidth = limit_bandwidth;
        }
    }

END:
    *ad = s_pf;
    if (json) {
        cJSON_Delete(json);
    }

    return;
}

void shaper_profile_ex_dup(int table_id, void **to, void **from, long argl, void *argp)
{
    struct shaping_profile *s_pf;

    if (*from == NULL) {
        return;
    }

    s_pf = (struct shaping_profile*)*from;
    __atomic_add_fetch(&s_pf->ref_cnt, 1, __ATOMIC_SEQ_CST);
    *to = s_pf;

    return;
}

void shaper_profile_ex_free(int table_id, void **ad, long argl, void *argp)
{
    struct shaping_profile *s_pf;
    
    s_pf = (struct shaping_profile*)*ad;
    if (__atomic_sub_fetch(&s_pf->ref_cnt, 1, __ATOMIC_SEQ_CST) > 0) {
        return;
    }

    free(s_pf);
    *ad = NULL;

    return;
}

void shaper_profile_update(struct shaping_profile_info *s_pf_info, struct shaping_profile *s_pf_ex)
{
    s_pf_info->id = s_pf_ex->id;
    s_pf_info->type = s_pf_ex->type;

    shaper_profile_ex_free(0, (void **)&s_pf_ex, 0, NULL);

    return;
}

static void shaper_rule_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, struct shaping_rule_info *s_rule_info, long long rule_compile_id, int *priority_changed)
{
    struct shaping_rule *s_rule = NULL;
    struct shaping_profile *s_pf = NULL;
    char pf_id_key[8] = {0};

    s_rule = (struct shaping_rule*)maat_plugin_table_get_ex_data(g_maat_instance, ctx->maat_info->rule_table_id, (char *)&rule_compile_id, sizeof(rule_compile_id));
    if (!s_rule) {
        LOG_ERROR("%s maat_plugin_table_get_ex_data get rule failed for compile id %lld", LOG_TAG_MAAT, rule_compile_id);
        goto END;
    }
    s_rule_info->id = s_rule->id;
    s_rule_info->fair_factor = s_rule->fair_factor;
    s_rule_info->vsys_id = s_rule->vsys_id;

    s_rule_info->is_enabled = 1;

    snprintf(pf_id_key, sizeof(pf_id_key), "%d", s_rule->primary_pf_id);
    s_pf = (struct shaping_profile *)maat_plugin_table_get_ex_data(g_maat_instance, ctx->maat_info->profile_table_id, pf_id_key, strlen(pf_id_key));
    if (!s_pf) {
        LOG_ERROR("%s maat_plugin_table_get_ex_data get profile failed for key %s", LOG_TAG_MAAT, pf_id_key);
        goto END;
    }
    shaper_profile_update(&s_rule_info->primary, s_pf);
    
    if (sf->processed_pkts <= CONFIRM_PRIORITY_PKTS) {
        if (sf->priority > s_rule->priority) {
            sf->priority = s_rule->priority;
            *priority_changed = 1;
        }
    }

    if (s_rule->borrow_pf_num == 0) {
        goto END;
    }

    for (int i = 0; i < s_rule->borrow_pf_num; i++) {
        memset(pf_id_key, 0, sizeof(pf_id_key));
        snprintf(pf_id_key, sizeof(pf_id_key), "%d", s_rule->borrow_pf_id_array[i]);
        s_pf = (struct shaping_profile *)maat_plugin_table_get_ex_data(g_maat_instance, ctx->maat_info->profile_table_id, pf_id_key, strlen(pf_id_key));
        if (!s_pf) {
            LOG_ERROR("%s maat_plugin_table_get_ex_data get profile failed for key %s", LOG_TAG_MAAT, pf_id_key);
            goto END;
        }

        shaper_profile_update(&s_rule_info->borrowing[i], s_pf);
        s_rule_info->borrowing_num++;
    }

END:
    if (s_rule) {
        shaper_rule_ex_free(ctx->maat_info->rule_table_id, (void **)&s_rule, 0, NULL);
    }
    return;
}

static void shaper_profiles_priority_update(struct shaping_flow *sf)
{
    for (int i = 0; i < sf->rule_num; i++) {
        sf->matched_rule_infos[i].primary.priority = sf->priority;

        for (int j = 0; j < sf->matched_rule_infos[i].borrowing_num; j++) {
            sf->matched_rule_infos[i].borrowing[j].priority = MIN(sf->priority + j + 1, SHAPING_PRIORITY_NUM_MAX - 1);
        }
    }

    return;
}

int shaper_rule_is_enabled(struct shaping_thread_ctx *ctx, long long rule_id)
{
    struct shaping_rule *s_rule = (struct shaping_rule*)maat_plugin_table_get_ex_data(g_maat_instance, ctx->maat_info->rule_table_id, (char *)&rule_id, sizeof(rule_id));

    if (s_rule) {
        shaper_rule_ex_free(ctx->maat_info->rule_table_id, (void **)&s_rule, 0, NULL);
        return 1;
    }

    return 0;
}

void shaper_rules_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, long long *rule_compile_ids, int rule_num)
{
    int i, j;
    int priority_changed = 0;

    if (sf->rule_num + rule_num > SHAPING_RULE_NUM_MAX) {
        char *addr_str = addr_tuple4_to_str(&sf->tuple4);
        LOG_ERROR("%s: shaping exceed maat rule num limit %d for flow: %s", LOG_TAG_MAAT, SHAPING_RULE_NUM_MAX, addr_str);
        if (addr_str) {
            free(addr_str);
        }
        return;
    }

    for (i = sf->rule_num, j = 0; i < sf->rule_num + rule_num; i++, j++) {
        shaper_rule_update(ctx, sf, &sf->matched_rule_infos[i], rule_compile_ids[j], &priority_changed);
    }

    if (sf->rule_num > 0 && priority_changed) {
        shaper_stat_refresh(ctx->stat, sf, ctx->thread_index, 1);
    }

    sf->rule_num += rule_num;  
    shaper_profiles_priority_update(sf);

    return;
}

static int shaper_maat_config_load(struct shaper_maat_config *conf)
{
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "INPUT_MODE", (int *)&conf->input_mode, MAAT_INPUT_REDIS);
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "TABLE_INFO", conf->table_info, sizeof(conf->table_info), "conf/table_info.json");
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "JSON_FILE", conf->json_cfg_file, sizeof(conf->json_cfg_file), "conf/shaping_maat.json");
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "REDIS_DB_IDX", &conf->redis_db_idx, 0);
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "REDIS_IP", conf->redis_ip, sizeof(conf->redis_ip), "127.0.0.1");
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "REDIS_PORT", conf->redis_port_range, sizeof(conf->redis_port_range), "6379");

    return 0;
}

struct shaping_maat_info* shaper_maat_init(const char *instance_name)
{    
    struct maat_options *opts;
    struct shaping_maat_info *maat_info;
    struct shaper_maat_config conf;
    int ret;

    if (shaper_maat_config_load(&conf) < 0) {
        return NULL;
    }
    
    maat_info = (struct shaping_maat_info *)calloc(1, sizeof(struct shaping_maat_info));

    opts = maat_options_new();
    maat_options_set_instance_name(opts, instance_name);
    switch (conf.input_mode) {
        case MAAT_INPUT_JSON:
            maat_options_set_json_file(opts, conf.json_cfg_file);
            break;
        case MAAT_INPUT_REDIS:
            int port_begin, port_end;
            int port;
            ret = sscanf(conf.redis_port_range, "%d-%d", &port_begin, &port_end);
            switch (ret) {
                case 1:
                    port = port_begin;
                    break;
                case 2:
                    srand(time(NULL));
                    port = port_begin + rand() % (port_end - port_begin);
                    break;
                default:
                    LOG_ERROR("%s: shaping maat invalid redis port range %s", LOG_TAG_MAAT, conf.redis_port_range);
                    goto ERROR;
            }
            maat_options_set_redis(opts, conf.redis_ip, port, conf.redis_db_idx);
            break;
        default:
            LOG_ERROR("%s: shaping maat invalid input_mode %d", LOG_TAG_MAAT, conf.input_mode);
            goto ERROR;
    }

    g_maat_instance = maat_new(opts, conf.table_info);
    maat_options_free(opts);
    if (!g_maat_instance) {
        LOG_ERROR("%s: shaping maat init by maat_new failed", LOG_TAG_MAAT);
        goto ERROR;
    }

    maat_info->rule_table_id = maat_get_table_id(g_maat_instance, SHAPING_RULE_TABLE_NAME);
    if (maat_info->rule_table_id < 0) {
        LOG_ERROR("%s: shaping maat register table %s failed", LOG_TAG_MAAT, SHAPING_RULE_TABLE_NAME);
        goto ERROR;
    }
    maat_info->profile_table_id = maat_get_table_id(g_maat_instance, SHAPING_PROFILE_TABLE_NAME);
    if (maat_info->profile_table_id < 0) {
        LOG_ERROR("%s: shaping maat register table %s failed", LOG_TAG_MAAT, SHAPING_PROFILE_TABLE_NAME);
        goto ERROR;
    }

    ret = maat_plugin_table_ex_schema_register(g_maat_instance, SHAPING_RULE_TABLE_NAME, shaper_rule_ex_new, shaper_rule_ex_free, shaper_rule_ex_dup, 0, NULL);
    if (ret < 0) {
        LOG_ERROR("%s: shaping maat register callback funcs for table %s failed", LOG_TAG_MAAT, SHAPING_RULE_TABLE_NAME);
        goto ERROR;
    }

    ret = maat_plugin_table_ex_schema_register(g_maat_instance, SHAPING_PROFILE_TABLE_NAME, shaper_profile_ex_new, shaper_profile_ex_free, shaper_profile_ex_dup, 0, NULL);
    if (ret < 0) {
        LOG_ERROR("%s: shaping maat register callback funcs for table %s failed", LOG_TAG_MAAT, SHAPING_PROFILE_TABLE_NAME);
        goto ERROR;
    }

    LOG_DEBUG("%s: shaping maat init complete", LOG_TAG_MAAT);

    return maat_info;
ERROR:
    shaper_maat_destroy(maat_info);
    return NULL;
}

void shaper_maat_destroy(struct shaping_maat_info *maat_info)
{
    if (maat_info) {
        free(maat_info);
    }
    if (g_maat_instance) {
        maat_free(g_maat_instance);
    }

    return;
}