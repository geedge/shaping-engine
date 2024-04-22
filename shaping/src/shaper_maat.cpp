#include <MESA_prof_load.h>
#include <cstring>
#include <stdio.h>
#include <time.h>

#include <cjson/cJSON.h>
#include <MESA/maat.h>
#include <MESA/MESA_handle_logger.h>

#include "raw_packet.h"
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
    char table_info[512];
    char accept_tags[512];
    char json_cfg_file[512];
    int redis_db_idx;
    char redis_ip[512];
    char redis_port_range[512];
    char log_path[512];
    enum log_level log_level;
};

enum dscp_class
{
    DSCP_CLASS_DF = 0,
    DSCP_CLASS_CS1,
    DSCP_CLASS_AF13,
    DSCP_CLASS_AF12,
    DSCP_CLASS_AF11,
    DSCP_CLASS_CS2,
    DSCP_CLASS_AF23,
    DSCP_CLASS_AF22,
    DSCP_CLASS_AF21,
    DSCP_CLASS_CS3,
    DSCP_CLASS_AF33,
    DSCP_CLASS_AF32,
    DSCP_CLASS_AF31,
    DSCP_CLASS_CS4,
    DSCP_CLASS_AF43,
    DSCP_CLASS_AF42,
    DSCP_CLASS_AF41,
    DSCP_CLASS_CS5,
    DSCP_CLASS_EF,
    DSCP_CLASS_CS6,
    DSCP_CLASS_CS7,
    DSCP_CLASS_MAX,
};
#define DSCP_VALUE_MAX 64
                                                    //   0                1               2               3               4               5                6              7              8               9             10
static int dscp_value_to_priority[DSCP_VALUE_MAX] = {DSCP_CLASS_DF, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_CS1, DSCP_CLASS_MAX, DSCP_CLASS_AF11,
                                                     //  11              12              13              14              15              16              17              18              19              20             21
                                                     DSCP_CLASS_MAX, DSCP_CLASS_AF12, DSCP_CLASS_MAX, DSCP_CLASS_AF13, DSCP_CLASS_MAX, DSCP_CLASS_CS2, DSCP_CLASS_MAX, DSCP_CLASS_AF21, DSCP_CLASS_MAX, DSCP_CLASS_AF22, DSCP_CLASS_MAX,
                                                     //  22              23              24              25              26              27              28              29              30              31             32
                                                     DSCP_CLASS_AF23, DSCP_CLASS_MAX, DSCP_CLASS_CS3, DSCP_CLASS_MAX, DSCP_CLASS_AF31, DSCP_CLASS_MAX, DSCP_CLASS_AF32, DSCP_CLASS_MAX, DSCP_CLASS_AF33, DSCP_CLASS_MAX, DSCP_CLASS_CS4,
                                                     //  33              34              35              36              37              38              39              40              41              42             43
                                                     DSCP_CLASS_MAX, DSCP_CLASS_AF41, DSCP_CLASS_MAX, DSCP_CLASS_AF42, DSCP_CLASS_MAX, DSCP_CLASS_AF43, DSCP_CLASS_MAX, DSCP_CLASS_CS5, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX,
                                                     //  44              45              46              47              48              49              50              51              52              53             54
                                                     DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_EF, DSCP_CLASS_MAX, DSCP_CLASS_CS6, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX,
                                                     //  55              56              57              58              59              60              61              62              63
                                                     DSCP_CLASS_MAX, DSCP_CLASS_CS7, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX, DSCP_CLASS_MAX};

struct maat *g_maat_instance = NULL;

void shaper_rule_ex_new(const char *table_name, int table_id, const char *key, const char *table_line, void **ad, long argl, void *argp)
{
    struct shaping_rule *s_rule;
    cJSON *json=NULL;
    cJSON *tmp_obj = NULL;
    cJSON *tmp_array_obj = NULL;
    cJSON *dscp_obj = NULL;
    int array_size;
    char user_region[1024] = {0};
    int i, ret;

    if (strncmp(table_name, SHAPING_RULE_TABLE_NAME, strlen(table_name)) != 0) {
        return;
    }

    s_rule = (struct shaping_rule*)calloc(1, sizeof(struct shaping_rule));

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

    //dscp_marking
    /*tmp_obj = cJSON_GetObjectItem(json, "dscp_marking");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse dscp_marking failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    dscp_obj = cJSON_GetObjectItem(tmp_obj, "enabled");
    if (dscp_obj && dscp_obj->valueint == 1) {
        dscp_obj = cJSON_GetObjectItem(tmp_obj, "dscp_value");
        if (dscp_obj && dscp_obj->valueint < DSCP_VALUE_MAX && dscp_value_to_priority[dscp_obj->valueint] != DSCP_CLASS_MAX) {
            s_rule->dscp_enable = 1;
            s_rule->dscp_value = dscp_obj->valueint;
        } else {
            LOG_ERROR("%s: json parse dscp_value wrong for table line %s", LOG_TAG_MAAT, table_line);
            goto END;
        }
    }*/

    //profile_chain
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
    if (*from == NULL) {
        return;
    }

    *to = *from;

    return;
}

void shaper_rule_ex_free(int table_id, void **ad, long argl, void *argp)
{
    if (*ad == NULL) {
        return;
    }

    free(*ad);
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

    //parse aqm options
    /*json = cJSON_Parse(aqm_options);
    if (!json) {
        LOG_ERROR("%s: json parse profile aqm options failed for profile id %d, line %s", LOG_TAG_MAAT, s_pf->id, table_line);
        goto END;
    }

    
    tmp_obj = cJSON_GetObjectItem(json, "algorithm");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse algorithm failed for profile id %d, line %s", LOG_TAG_MAAT, s_pf->id, table_line);
        goto END;
    }
    if (strncmp(tmp_obj->valuestring, "none", strlen(tmp_obj->valuestring)) == 0) {
        s_pf->aqm_type = AQM_TYPE_NONE;
    } else if (strncmp(tmp_obj->valuestring, "blue", strlen(tmp_obj->valuestring)) == 0) {
        s_pf->aqm_type = AQM_TYPE_BLUE;
    } else if (strncmp(tmp_obj->valuestring, "codel", strlen(tmp_obj->valuestring)) == 0) {
        s_pf->aqm_type = AQM_TYPE_CODEL;
    } else {
        LOG_ERROR("%s: json parse aqm type wrong for profile id %d, line %s", LOG_TAG_MAAT, s_pf->id, table_line);
        goto END;
    }

    cJSON_Delete(json);*/
    
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

        if (strncmp(tmp_obj->valuestring, "bidirectional", strlen(tmp_obj->valuestring)) == 0) {
            s_pf->limit_direction = PROFILE_LIMIT_DIRECTION_BIDIRECTION;
            s_pf->bidirection_limit_bandwidth = limit_bandwidth;
            break;//config either bidirectional or incoming/outgoing limit
        } 
        
        s_pf->limit_direction = PROFILE_LIMIT_DIRECTION_INCOMING_OUTGOING;
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
    if (*from == NULL) {
        return;
    }

    *to = *from;

    return;
}

void shaper_profile_ex_free(int table_id, void **ad, long argl, void *argp)
{
    if (*ad == NULL) {
        return;
    }

    free(*ad);
    *ad = NULL;

    return;
}

void shaper_profile_update(struct shaping_thread_ctx *ctx, struct shaping_profile_info *s_pf_info, struct shaping_profile *s_pf_ex)
{
    s_pf_info->id = s_pf_ex->id;
    s_pf_info->type = s_pf_ex->type;
    shaper_profile_hash_node_set(ctx, s_pf_info);
    s_pf_info->hash_node->aqm_type = s_pf_ex->aqm_type;
    s_pf_info->hash_node->limit_direction = s_pf_ex->limit_direction;

    return;
}

struct shaping_profile *shaper_maat_profile_get(struct shaping_thread_ctx *ctx, int profile_id)
{
    struct shaping_profile *s_pf = NULL;
    char pf_id_key[8] = {0};

    snprintf(pf_id_key, sizeof(pf_id_key), "%d", profile_id);
    s_pf = (struct shaping_profile *)maat_plugin_table_get_ex_data(g_maat_instance, ctx->maat_info->profile_table_id, pf_id_key, strlen(pf_id_key));
    if (!s_pf) {
        LOG_ERROR("%s maat_plugin_table_get_ex_data get profile failed for key %s", LOG_TAG_MAAT, pf_id_key);
    }

    return s_pf;
}

static int shaper_rule_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, struct shaping_rule_info *s_rule_info, long long rule_compile_id, int *priority_changed)
{
    struct shaping_rule *s_rule = NULL;
    struct shaping_profile *s_pf = NULL;

    s_rule = (struct shaping_rule*)maat_plugin_table_get_ex_data(g_maat_instance, ctx->maat_info->rule_table_id, (char *)&rule_compile_id, sizeof(rule_compile_id));
    if (!s_rule) {
        LOG_ERROR("%s maat_plugin_table_get_ex_data get rule failed for compile id %lld", LOG_TAG_MAAT, rule_compile_id);
        return -1;
    }
    s_rule_info->id = s_rule->id;
    s_rule_info->fair_factor = s_rule->fair_factor;
    s_rule_info->vsys_id = s_rule->vsys_id;

    s_rule_info->is_enabled = 1;

    s_pf = shaper_maat_profile_get(ctx, s_rule->primary_pf_id);
    if (!s_pf) {
        return -1;
    }
    shaper_profile_update(ctx, &s_rule_info->primary, s_pf);
    
    if (sf->processed_pkts <= CONFIRM_PRIORITY_PKTS) {
        if (sf->priority > s_rule->priority) {
            sf->priority = s_rule->priority;
            *priority_changed = 1;
        }
    }

    if (s_rule->dscp_enable) {
        sf->dscp_enable = 1;
        if (dscp_value_to_priority[s_rule->dscp_value] > dscp_value_to_priority[sf->dscp_value]) {
            sf->dscp_value = s_rule->dscp_value;
        }
    }

    if (s_rule->borrow_pf_num == 0) {
        return 0;
    }

    for (int i = 0; i < s_rule->borrow_pf_num; i++) {
        s_pf = shaper_maat_profile_get(ctx, s_rule->borrow_pf_id_array[i]);
        if (!s_pf) {
            return -1;
        }

        shaper_profile_update(ctx, &s_rule_info->borrowing[i], s_pf);
        s_rule_info->borrowing_num++;
    }

    return 0;
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
        return 1;
    }

    return 0;
}

static int shaper_rules_dup_remove(struct shaping_flow *sf, long long *rule_compile_ids, int rule_num, long long *rule_ids_remove_dup)
{
    int i, j;
    int rule_num_remove_dup = 0;

    for (i = 0; i < rule_num; i++) {
        for (j = 0; j < sf->rule_num; j++) {
            if (rule_compile_ids[i] == sf->matched_rule_infos[j].id) {
                break;
            }
        }
        if (j == sf->rule_num) {
            rule_ids_remove_dup[rule_num_remove_dup] = rule_compile_ids[i];
            rule_num_remove_dup++;
        }
    }

    return rule_num_remove_dup;
}

void shaper_rules_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, long long *rule_compile_ids, int rule_num)
{
    int priority_changed = 0;
    long long rule_ids_remove_dup[SHAPING_RULE_NUM_MAX] = {0};
    int rule_num_remove_dup = 0;
    int old_rule_num = sf->rule_num;

    if (rule_num > SHAPING_RULE_NUM_MAX) {
        char *addr_str = addr_tuple4_to_str(&sf->tuple4);
        LOG_ERROR("%s: ctrl_active has too many rules %d exceed limit %d for flow: %s", LOG_TAG_MAAT, rule_num, SHAPING_RULE_NUM_MAX, addr_str);
        if (addr_str) {
            free(addr_str);
        }
        return;
    }

    rule_num_remove_dup = shaper_rules_dup_remove(sf, rule_compile_ids, rule_num, rule_ids_remove_dup);
    if (rule_num_remove_dup == 0) {
        return;
    }

    if (sf->rule_num + rule_num_remove_dup > SHAPING_RULE_NUM_MAX) {
        char *addr_str = addr_tuple4_to_str(&sf->tuple4);
        LOG_ERROR("%s: shaping exceed maat rule num limit %d for flow: %s", LOG_TAG_MAAT, SHAPING_RULE_NUM_MAX, addr_str);
        if (addr_str) {
            free(addr_str);
        }
        return;
    }

    for (int i = 0; i < rule_num_remove_dup; i++) {
        if (shaper_rule_update(ctx, sf, &sf->matched_rule_infos[sf->rule_num], rule_ids_remove_dup[i], &priority_changed) == 0) {
            sf->rule_num++;
        }
    }

    if (old_rule_num > 0 && priority_changed) {
        shaper_stat_refresh(ctx, sf, 1);
    }

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
    MESA_load_profile_string_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "LOG_PATH", conf->log_path, sizeof(conf->log_path), "log/shaping_maat.log");
    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "MAAT", "LOG_LEVEL", (int *)&conf->log_level, LOG_LEVEL_INFO);

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

    maat_options_set_logger(opts, conf.log_path, conf.log_level);

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