#include <MESA_prof_load.h>
#include <MESA/MESA_handle_logger.h>
#include <MESA/cJSON.h>

#include "log.h"
#include "raw_packet.h"
#include "shaper.h"
#include "shaper_marsio.h"
#include "utils.h"

struct shaper_marsio_config
{
    char app_symbol[256];
    char dev_interface[256];
};

static int shaper_marsio_config_load(struct shaper_marsio_config *conf)
{
    int ret;

    ret = MESA_load_profile_string_nodef(SHAPING_GLOBAL_CONF_FILE, "MARSIO", "DEV_INTERFACE", conf->dev_interface, sizeof(conf->dev_interface));
    if (ret < 0) {
        LOG_ERROR("%s: shaping load MARSIO conf DEV_INTERFACE failed", LOG_TAG_MARSIO);
        return ret;
    }

    ret = MESA_load_profile_string_nodef(SHAPING_GLOBAL_CONF_FILE, "MARSIO", "APP_SYMBOL", conf->app_symbol, sizeof(conf->app_symbol));
    if (ret < 0) {
        LOG_ERROR("%s: shaping load MARSIO conf APP_SYMBOL failed", LOG_TAG_MARSIO);
        return ret;
    }
    
    return 0;
}

void shaper_marsio_destroy(struct shaping_marsio_info *marsio_info)
{
    if (!marsio_info) {
        return;
    }

    if (marsio_info->mr_path) {
        marsio_sendpath_destory(marsio_info->mr_path);
    }

    if (marsio_info->mr_dev) {
        marsio_close_device(marsio_info->mr_dev);
    }

    if (marsio_info->instance) {
        marsio_destory(marsio_info->instance);
    }

    free(marsio_info);

    return;
}

struct shaping_marsio_info* shaper_marsio_init(int thread_num)
{
    struct shaper_marsio_config conf;
    struct shaping_marsio_info *marsio_info;
    int opt = 1;

    memset(&conf, 0, sizeof(conf));
    if (shaper_marsio_config_load(&conf) < 0) {
        return NULL;
    }

    marsio_info = (struct shaping_marsio_info *)calloc(1, sizeof(struct shaping_marsio_info));

    marsio_info->instance = marsio_create();
    if (!marsio_info->instance) {
        LOG_ERROR("%s: shaping marsio_create() failed", LOG_TAG_MARSIO);
        goto ERROR;
    }

    if (marsio_option_set(marsio_info->instance, MARSIO_OPT_EXIT_WHEN_ERR, &opt, sizeof(opt)) != 0)
    {
        LOG_ERROR("%s: shaping marsio set MARSIO_OPT_EXIT_WHEN_ERR failed", LOG_TAG_MARSIO);
        goto ERROR;
    }

    if (marsio_init(marsio_info->instance, conf.app_symbol) != 0) {
        LOG_ERROR("%s: shaping marsio init failed", LOG_TAG_MARSIO);
        goto ERROR;
    }

    marsio_info->mr_dev = marsio_open_device(marsio_info->instance, conf.dev_interface, thread_num, thread_num);
    if (!marsio_info->mr_dev) {
        LOG_ERROR("%s: shaping marsio open device %s failed", LOG_TAG_MARSIO, conf.dev_interface);
        goto ERROR;
    }

    marsio_info->mr_path = marsio_sendpath_create_by_vdev(marsio_info->mr_dev);
    if (!marsio_info->mr_path) {
        LOG_ERROR("%s: shaping marsio sendpath create failed for %s", LOG_TAG_MARSIO, conf.dev_interface);
        goto ERROR;
    }

    return marsio_info;

ERROR:
    shaper_marsio_destroy(marsio_info);
    return NULL;
}

int shaper_marsio_pkt_metadata_get(marsio_buff_t *rx_buff, struct metadata *meta, int is_ctrl_buff, struct raw_pkt_parser *raw_parser)
{
    int dir;
    const void *payload;

    memset(meta, 0, sizeof(struct metadata));

    if (marsio_buff_get_metadata(rx_buff, MR_BUFF_SESSION_ID, &meta->session_id, sizeof(meta->session_id)) <= 0) {
        LOG_ERROR("%s: shaping marsio get session_id failed from metadata", LOG_TAG_MARSIO);
        return -1;
    }

    meta->raw_len = marsio_buff_datalen(rx_buff);
    meta->raw_data = marsio_buff_mtod(rx_buff);
    if (!meta->raw_data || meta->raw_len == 0) {
        LOG_ERROR("%s: shaping marsio get raw_data failed from metadata", LOG_TAG_MARSIO);
        return -1;
    }

    /*1 for E2I
      0 for I2E*/
    if (marsio_buff_get_metadata(rx_buff, MR_BUFF_DIR, &dir, sizeof(dir)) <= 0) {
        LOG_ERROR("%s: shaping marsio get dir failed from metadata", LOG_TAG_MARSIO);
        return -1;
    }
    if (dir == 1) {
        meta->dir = SHAPING_DIR_IN;
    } else {
        meta->dir = SHAPING_DIR_OUT;
    }

    if (is_ctrl_buff) {
        meta->is_ctrl_pkt = 1;
        if (marsio_buff_get_metadata(rx_buff, MR_BUFF_PAYLOAD_OFFSET, &meta->l7_offset, sizeof(meta->l7_offset)) <= 0) {
            LOG_ERROR("%s: shaping marsio get l7_offset failed from metadata", LOG_TAG_MARSIO);
            return -1;
        }
    }

    raw_packet_parser_init(raw_parser, meta->session_id, LAYER_TYPE_ALL, 8);
    payload = raw_packet_parser_parse(raw_parser, (const void *)meta->raw_data, meta->raw_len);
    if (is_ctrl_buff) {
        if ((char *)payload - (char *)&meta->raw_data != meta->l7_offset) {
            LOG_ERROR("%s: incorrect dataoffset in the control zone of session %lu", LOG_TAG_SHAPING, meta->session_id);
        }
    }

    if (raw_packet_tcp_payload_len_get(raw_parser) == 0) {
        meta->is_tcp_pure_ctrl = 1;
    }

    return 0;
}

int shaper_marsio_ctrl_pkt_data_parse(struct ctrl_pkt_data *ctrl_data, const char *data, size_t length)
{
    cJSON *json = NULL;
    cJSON *tmp_obj = NULL;
    cJSON *elem = NULL;
    int i = 0;

    memset(ctrl_data, 0, sizeof(struct ctrl_pkt_data));

    json = cJSON_Parse(data);
    if (!json) {
        LOG_ERROR("%s: shaping marsio parse ctrl pkt json data failed, data: %s", LOG_TAG_MARSIO, data);
        goto ERROR;
    }

    tmp_obj = cJSON_GetObjectItem(json, "session_id");
    if (!tmp_obj || !cJSON_IsString(tmp_obj)) {
        LOG_ERROR("%s: shaping marsio parse session_id failed from ctrl pkt json data, data: %s", LOG_TAG_MARSIO, data);
        goto ERROR;
    }
    ctrl_data->session_id = atoll(tmp_obj->valuestring);

    tmp_obj = cJSON_GetObjectItem(json, "state");
    if (!tmp_obj || !cJSON_IsString(tmp_obj)) {
        LOG_ERROR("%s: shaping marsio parse session state failed from ctrl pkt json data, data: %s", LOG_TAG_MARSIO, data);
        goto ERROR;
    }

    if (strcasecmp(tmp_obj->valuestring, "opening") == 0) {
        ctrl_data->state = SESSION_STATE_OPENING;
    } else if (strcasecmp(tmp_obj->valuestring, "active") == 0) {
        ctrl_data->state = SESSION_STATE_ACTIVE;
    } else if (strcasecmp(tmp_obj->valuestring, "closing") == 0) {
        ctrl_data->state = SESSION_STATE_CLOSING;
    } else if (strcasecmp(tmp_obj->valuestring, "resetall") == 0) {
        ctrl_data->state = SESSION_STATE_RESETALL;
    } else {
        LOG_ERROR("%s: shaping marsio parse invalid session state from ctrl pkt json data, data: %s", LOG_TAG_MARSIO, data);
        goto ERROR;
    }

    if (ctrl_data->state != SESSION_STATE_ACTIVE) {
        goto SUCCESS;
    }

    tmp_obj = cJSON_GetObjectItem(json, "method");
    if (!tmp_obj || !cJSON_IsString(tmp_obj)) {
        LOG_ERROR("%s: shaping marsio parse method failed, data: %s", LOG_TAG_MARSIO, data);
        goto ERROR;
    }
    if (strcasecmp(tmp_obj->valuestring, "policy_update") != 0)
    {
        LOG_ERROR("%s: shaping marsio parse invalid method, data: %s", LOG_TAG_CTRLPKT, data);
        goto ERROR;
    }

    tmp_obj = cJSON_GetObjectItem(json, "params");
    if (!tmp_obj || !cJSON_IsObject(tmp_obj)) {
        LOG_ERROR("%s: shaping marsio parse params failed, data: %s", LOG_TAG_MARSIO, data);
        goto ERROR;
    }
    tmp_obj = cJSON_GetObjectItem(tmp_obj, "shaping");
    if (!tmp_obj || !cJSON_IsArray(tmp_obj) || !cJSON_GetArraySize(tmp_obj)) {
        LOG_ERROR("%s: shaping marsio parse shaping rule failed, data: %s", LOG_TAG_MARSIO, data);
        goto ERROR;
    }

    ctrl_data->shaping_rule_num = MIN(cJSON_GetArraySize(tmp_obj), SHAPING_RULE_NUM_MAX);
    cJSON_ArrayForEach(elem, tmp_obj) {
        if (!cJSON_IsNumber(elem)) {
            LOG_ERROR("%s: shaping marsio parse shaping rule id failed, data: %s", LOG_TAG_MARSIO, data);
            goto ERROR;
        }
        ctrl_data->shaping_rule_ids[i] = elem->valueint;
        i++;
        if (i == ctrl_data->shaping_rule_num) {
            break;
        }
    }

SUCCESS:
    if (json) {
        cJSON_Delete(json);
    }
    return 0;
ERROR:
    if (json) {
        cJSON_Delete(json);
    }
    return -1;
}