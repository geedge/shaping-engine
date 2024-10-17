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
#define SHAPING_RULE_TABLE_NAME "TRAFFIC_SHAPING_RULE"
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

void shaper_rule_ex_new(const char *table_name, const char *key, const char *table_line, void **ad, long argl, void *argp)
{
    struct shaping_rule *s_rule;
    cJSON *json=NULL;
    cJSON *tmp_obj = NULL;
    cJSON *action_para_obj = NULL;
    cJSON *tmp_array_obj = NULL;
    cJSON *dscp_obj = NULL;
    int array_size;
    int i;

    if (strncmp(table_name, SHAPING_RULE_TABLE_NAME, strlen(table_name)) != 0) {
        return;
    }

    s_rule = (struct shaping_rule*)calloc(1, sizeof(struct shaping_rule));

    json = cJSON_Parse(table_line);
    if (!json) {
        LOG_ERROR("%s: json parse rule failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    tmp_obj = cJSON_GetObjectItem(json, "uuid");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse uuid failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    uuid_parse(tmp_obj->valuestring, s_rule->uuid);

    action_para_obj = cJSON_GetObjectItem(json, "action_parameter");
    if (!action_para_obj) {
        LOG_ERROR("%s: json parse action_parameter failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    tmp_obj = cJSON_GetObjectItem(action_para_obj, "vsys_id");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse vsys_id failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    s_rule->vsys_id = tmp_obj->valueint;

    tmp_obj = cJSON_GetObjectItem(action_para_obj, "priority");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse priority failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    s_rule->priority = tmp_obj->valueint;

    tmp_obj = cJSON_GetObjectItem(action_para_obj, "fair_factor");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse fair_factor failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    s_rule->fair_factor = tmp_obj->valueint;

    dscp_obj = cJSON_GetObjectItem(action_para_obj, "dscp_marking");
    if (!dscp_obj) {
        LOG_ERROR("%s: json parse dscp_marking failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    tmp_obj = cJSON_GetObjectItem(dscp_obj, "enabled");
    if (tmp_obj && tmp_obj->valueint == 1) {
        s_rule->dscp_enable = 1;
        tmp_obj = cJSON_GetObjectItem(dscp_obj, "dscp_value");
        if (tmp_obj && tmp_obj->valueint < DSCP_VALUE_MAX && dscp_value_to_priority[tmp_obj->valueint] != DSCP_CLASS_MAX) {
            s_rule->dscp_value = tmp_obj->valueint;
        } else {
            LOG_ERROR("%s: json parse dscp_value wrong for table line %s", LOG_TAG_MAAT, table_line);
            goto END;
        }
    }

    tmp_array_obj = cJSON_GetObjectItem(action_para_obj, "profile_chain");
    if (!tmp_array_obj) {
        LOG_ERROR("%s: json parse profile_chain failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    array_size = cJSON_GetArraySize(tmp_array_obj);
    if (array_size < 1) {
        LOG_ERROR("%s: json parse profile_chain empty for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    tmp_obj = cJSON_GetArrayItem(tmp_array_obj, 0);
    uuid_parse(tmp_obj->valuestring, s_rule->primary_pf_uuid);

    s_rule->borrow_pf_num = array_size - 1;
    for (i = 1; i < array_size; i++) {
        tmp_obj = cJSON_GetArrayItem(tmp_array_obj, i);
        uuid_parse(tmp_obj->valuestring, s_rule->borrow_pf_uuid_array[i - 1]);
    }

END:
    *ad = s_rule;
    if (json) {
        cJSON_Delete(json);
    }
    return;
}

void shaper_rule_ex_dup(const char *table_name, void **to, void **from, long argl, void *argp)
{
    if (*from == NULL) {
        return;
    }

    *to = *from;

    return;
}

void shaper_rule_ex_free(const char *table_name, void **ad, long argl, void *argp)
{
    if (*ad == NULL) {
        return;
    }

    free(*ad);
    *ad = NULL;

    return;
}

void shaper_profile_ex_new(const char *table_name, const char *key, const char *table_line, void **ad, long argl, void *argp)
{
    struct shaping_profile *s_pf;
    cJSON *json=NULL;
    cJSON *tmp_array_obj = NULL;
    cJSON *tmp_obj = NULL;
    cJSON *type_arg_obj = NULL;
    cJSON *aqm_options_obj = NULL;
    cJSON *limits_obj = NULL;
    int limit_bandwidth;

    if (strncmp(table_name, SHAPING_PROFILE_TABLE_NAME, strlen(table_name)) != 0) {
        return;
    }

    s_pf = (struct shaping_profile*)calloc(1, sizeof(struct shaping_profile));

    json = cJSON_Parse(table_line);
    if (!json) {
        LOG_ERROR("%s: json parse profile failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }

    tmp_obj = cJSON_GetObjectItem(json, "uuid");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse uuid failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    uuid_parse(tmp_obj->valuestring, s_pf->uuid);

    //parse profile type
    tmp_obj = cJSON_GetObjectItem(json, "type");
    if (!tmp_obj) {
        LOG_ERROR("%s: json parse type failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    type_arg_obj = cJSON_GetObjectItem(json, "type_argument");
    if (!type_arg_obj) {
        LOG_ERROR("%s: json parse type_argument failed for table line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    if (strcmp(tmp_obj->valuestring, "generic") == 0) {
        s_pf->type = PROFILE_TYPE_GENERIC;
    } else if (strcmp(tmp_obj->valuestring, "fair_share") == 0) {
        if (strcmp(type_arg_obj->valuestring, "host_fairness") == 0) {
            s_pf->type = PROFILE_TYPE_HOST_FARINESS;
        } else if (strcmp(type_arg_obj->valuestring, "max_min_host_fairness") == 0) {
            s_pf->type = PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS;
        } else {
            LOG_ERROR("%s: profile type argument wrong for profile line %s", LOG_TAG_MAAT, table_line);
            goto END;
        }
    } else if (strcmp(tmp_obj->valuestring, "split_by") == 0) {
        if (strcmp(type_arg_obj->valuestring, "local_host") == 0) {
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
    aqm_options_obj = cJSON_GetObjectItem(json, "aqm_options");
    if (!aqm_options_obj) {
        LOG_ERROR("%s: json parse aqm options failed for line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    tmp_obj = cJSON_GetObjectItem(aqm_options_obj, "algorithm");
    if (!tmp_obj || tmp_obj->type != cJSON_String || !tmp_obj->valuestring) {
        LOG_ERROR("%s: json parse aqm algorithm failed for line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    if (strncmp(tmp_obj->valuestring, "none", strlen(tmp_obj->valuestring)) == 0) {
        s_pf->aqm_type = AQM_TYPE_NONE;
    } else if (strncmp(tmp_obj->valuestring, "blue", strlen(tmp_obj->valuestring)) == 0) {
        s_pf->aqm_type = AQM_TYPE_BLUE;
    } else if (strncmp(tmp_obj->valuestring, "codel", strlen(tmp_obj->valuestring)) == 0) {
        s_pf->aqm_type = AQM_TYPE_CODEL;
    } else {
        LOG_ERROR("%s: json parse aqm type wrong for line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    
    //parse limits of profile
    limits_obj = cJSON_GetObjectItem(json, "limits");
    if (!limits_obj) {
        LOG_ERROR("%s: json parse limits failed for line %s", LOG_TAG_MAAT, table_line);
        goto END;
    }
    cJSON_ArrayForEach(tmp_array_obj, limits_obj) {
        tmp_obj = cJSON_GetObjectItem(tmp_array_obj, "bandwidth");
        if (!tmp_obj) {
            LOG_ERROR("%s: json parse limit bandwidth failed for line %s", LOG_TAG_MAAT, table_line);
            goto END;
        }
        limit_bandwidth = tmp_obj->valueint;

        tmp_obj = cJSON_GetObjectItem(tmp_array_obj, "direction");
        if (!tmp_obj) {
            LOG_ERROR("%s: json parse limit direction failed for line %s", LOG_TAG_MAAT, table_line);
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

void shaper_profile_ex_dup(const char *table_name, void **to, void **from, long argl, void *argp)
{
    if (*from == NULL) {
        return;
    }

    *to = *from;

    return;
}

void shaper_profile_ex_free(const char *table_name, void **ad, long argl, void *argp)
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
    uuid_copy(s_pf_info->uuid, s_pf_ex->uuid);
    s_pf_info->type = s_pf_ex->type;
    shaper_profile_hash_node_set(ctx, s_pf_info);
    s_pf_info->hash_node->aqm_type = s_pf_ex->aqm_type;
    s_pf_info->hash_node->limit_direction = s_pf_ex->limit_direction;

    return;
}

struct shaping_profile *shaper_maat_profile_get(struct shaping_thread_ctx *ctx, uuid_t profile_uuid)
{
    struct shaping_profile *s_pf = NULL;
    char uuid_str[UUID_STR_LEN] = {0};

    uuid_unparse(profile_uuid, uuid_str);
    s_pf = (struct shaping_profile *)maat_plugin_table_get_ex_data(g_maat_instance, SHAPING_PROFILE_TABLE_NAME, uuid_str, strlen(uuid_str));
    if (!s_pf) {
        LOG_ERROR("%s maat_plugin_table_get_ex_data get profile failed for key %s", LOG_TAG_MAAT, uuid_str);
    }

    return s_pf;
}

static int shaper_rule_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, struct shaping_rule_info *s_rule_info, uuid_t rule_uuid, int *priority_changed)
{
    struct shaping_rule *s_rule = NULL;
    struct shaping_profile *s_pf = NULL;
    char uuid_str[UUID_STR_LEN] = {0};

    uuid_unparse(rule_uuid, uuid_str);
    s_rule = (struct shaping_rule*)maat_plugin_table_get_ex_data(g_maat_instance, SHAPING_RULE_TABLE_NAME, uuid_str, strlen(uuid_str));
    if (!s_rule) {
        LOG_ERROR("%s maat_plugin_table_get_ex_data get rule failed for rule id %s", LOG_TAG_MAAT, uuid_str);
        return -1;
    }

    uuid_copy(s_rule_info->uuid, s_rule->uuid);
    s_rule_info->fair_factor = s_rule->fair_factor;
    s_rule_info->vsys_id = s_rule->vsys_id;

    s_rule_info->is_enabled = 1;

    s_pf = shaper_maat_profile_get(ctx, s_rule->primary_pf_uuid);
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
        s_pf = shaper_maat_profile_get(ctx, s_rule->borrow_pf_uuid_array[i]);
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

int shaper_rule_is_enabled(struct shaping_thread_ctx *ctx, uuid_t rule_uuid)
{
    char uuid_str[UUID_STR_LEN] = {0};
    uuid_unparse(rule_uuid, uuid_str);
    struct shaping_rule *s_rule = (struct shaping_rule*)maat_plugin_table_get_ex_data(g_maat_instance, SHAPING_RULE_TABLE_NAME, uuid_str, sizeof(uuid_str));

    if (s_rule) {
        return 1;
    }

    return 0;
}

static int shaper_rules_dup_remove(struct shaping_flow *sf, uuid_t *rule_uuids, int rule_num, uuid_t *rule_uuids_remove_dup)
{
    int i, j;
    int rule_num_remove_dup = 0;

    for (i = 0; i < rule_num; i++) {
        for (j = 0; j < sf->rule_num; j++) {
            if (uuid_compare(rule_uuids[i], sf->matched_rule_infos[j].uuid) == 0) {
                break;
            }
        }
        if (j == sf->rule_num) {
            uuid_copy(rule_uuids_remove_dup[rule_num_remove_dup], rule_uuids[i]);
            rule_num_remove_dup++;
        }
    }

    return rule_num_remove_dup;
}

void shaper_rules_update(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, uuid_t *rule_uuids, int rule_num)
{
    int priority_changed = 0;
    uuid_t rule_uuids_remove_dup[SHAPING_RULE_NUM_MAX];
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

    rule_num_remove_dup = shaper_rules_dup_remove(sf, rule_uuids, rule_num, rule_uuids_remove_dup);
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
        if (shaper_rule_update(ctx, sf, &sf->matched_rule_infos[sf->rule_num], rule_uuids_remove_dup[i], &priority_changed) == 0) {
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

int shaper_maat_init(const char *instance_name)
{    
    struct maat_options *opts;
    struct shaper_maat_config conf;
    int ret;

    if (shaper_maat_config_load(&conf) < 0) {
        return -1;
    }

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

    return 0;
ERROR:
    shaper_maat_destroy();
    return -1;
}

void shaper_maat_destroy()
{
    if (g_maat_instance) {
        maat_free(g_maat_instance);
    }

    return;
}