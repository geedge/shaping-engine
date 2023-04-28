#include <MESA_prof_load.h>
#include <MESA/MESA_handle_logger.h>
#include <cjson/cJSON.h>
#include <marsio.h>
#include "mpack.h"

#include "log.h"
#include "raw_packet.h"
#include "shaper.h"
#include "shaper_marsio.h"
#include "utils.h"

struct shaper_marsio_config
{
    int rx_brust_max;
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

    ret = MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "SYSTEM", "RX_BRUST_MAX", &conf->rx_brust_max, 1);
    conf->rx_brust_max = conf->rx_brust_max <= SHAPER_MARSIO_RX_BRUST_MAX ? conf->rx_brust_max : SHAPER_MARSIO_RX_BRUST_MAX;
    
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

struct shaping_marsio_info* shaper_marsio_init(struct shaping_system_conf *sys_conf)
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

    if (marsio_option_set(marsio_info->instance, MARSIO_OPT_EXIT_WHEN_ERR, &opt, sizeof(opt)) != 0) {
        LOG_ERROR("%s: shaping marsio set MARSIO_OPT_EXIT_WHEN_ERR failed", LOG_TAG_MARSIO);
        goto ERROR;
    }

    if (sys_conf->cpu_affinity_enable) {
        if (marsio_option_set(marsio_info->instance, MARSIO_OPT_THREAD_MASK, &sys_conf->cpu_affinity_mask, sizeof(sys_conf->cpu_affinity_mask)) != 0) {
            LOG_ERROR("%s: shaping marsio set MARSIO_OPT_THREAD_MASK failed", LOG_TAG_MARSIO);
            goto ERROR;
        }
    }

    if (marsio_init(marsio_info->instance, conf.app_symbol) != 0) {
        LOG_ERROR("%s: shaping marsio init failed", LOG_TAG_MARSIO);
        goto ERROR;
    }

    marsio_info->mr_dev = marsio_open_device(marsio_info->instance, conf.dev_interface, sys_conf->work_thread_num, sys_conf->work_thread_num);
    if (!marsio_info->mr_dev) {
        LOG_ERROR("%s: shaping marsio open device %s failed", LOG_TAG_MARSIO, conf.dev_interface);
        goto ERROR;
    }

    marsio_info->mr_path = marsio_sendpath_create_by_vdev(marsio_info->mr_dev);
    if (!marsio_info->mr_path) {
        LOG_ERROR("%s: shaping marsio sendpath create failed for %s", LOG_TAG_MARSIO, conf.dev_interface);
        goto ERROR;
    }

    marsio_info->rx_brust_max = conf.rx_brust_max;

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
        if ((char *)payload - (char *)meta->raw_data != meta->l7_offset) {
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
    int shaping_rule_id_array_size;
    mpack_tree_t tree;
    
    mpack_node_t root;
    mpack_node_t tmp_node;
    mpack_node_t shaper;

    mpack_tree_init_data(&tree, data, length);
    mpack_tree_parse(&tree);
    root = mpack_tree_root(&tree);

    //tsync
    tmp_node = mpack_node_map_cstr(root, "tsync");
    if (mpack_type_str != mpack_node_type(tmp_node)) {
        LOG_ERROR("%s: shaping marsio msgpack tsync type wrong, type is %d", LOG_TAG_MARSIO, mpack_node_type(tmp_node) );
        goto ERROR;
    }
    if (strncmp("2.0", mpack_node_str(tmp_node), mpack_node_strlen(tmp_node)) != 0) {
        char temp[128] = {0};
        memcpy(temp, mpack_node_str(tmp_node), MIN(sizeof(temp), mpack_node_strlen(tmp_node)));
        LOG_ERROR("%s: shaping marsio msgpack tsync version wrong, version is %s", LOG_TAG_MARSIO, temp);
        goto ERROR;
    }

    //session_id
    tmp_node = mpack_node_map_cstr(root, "session_id");
    if (mpack_type_uint != mpack_node_type(tmp_node)) {
        LOG_ERROR("%s: shaping marsio msgpack session_id type wrong, type is %d", LOG_TAG_MARSIO, mpack_node_type(tmp_node));
        goto ERROR;
    }
    ctrl_data->session_id = mpack_node_u64(tmp_node);

    //state
    tmp_node = mpack_node_map_cstr(root, "state");
    if (mpack_type_str != mpack_node_type(tmp_node)) {
        LOG_ERROR("%s: shaping marsio msgpack state type wrong, type is %d", LOG_TAG_MARSIO, mpack_node_type(tmp_node));
        goto ERROR;
    }
    if (strncasecmp("opening", mpack_node_str(tmp_node), mpack_node_strlen(tmp_node)) == 0) {
        ctrl_data->state = SESSION_STATE_OPENING;
    } else if (strncasecmp("active", mpack_node_str(tmp_node), mpack_node_strlen(tmp_node)) == 0) {
        ctrl_data->state = SESSION_STATE_ACTIVE;
    } else if (strncasecmp("closing", mpack_node_str(tmp_node), mpack_node_strlen(tmp_node)) == 0) {
        ctrl_data->state = SESSION_STATE_CLOSING;
    } else if (strncasecmp("resetall", mpack_node_str(tmp_node), mpack_node_strlen(tmp_node)) == 0) {
        ctrl_data->state = SESSION_STATE_RESETALL;
    } else {
        char temp[128] = {0};
        memcpy(temp, mpack_node_str(tmp_node), MIN(sizeof(temp), mpack_node_strlen(tmp_node)));
        LOG_ERROR("%s: shaping marsio parse invalid session state %s", LOG_TAG_MARSIO, temp);
        goto ERROR;
    }

    if (ctrl_data->state != SESSION_STATE_ACTIVE) {
        goto SUCCESS;
    }

    //shaping rules
    tmp_node = mpack_node_map_cstr(root, "params");
    if (mpack_type_map != mpack_node_type(tmp_node)) {
        LOG_ERROR("%s: shaping marsio msgpack params type wrong, type is %d", LOG_TAG_MARSIO, mpack_node_type(tmp_node));
        goto ERROR;
    }
    shaper = mpack_node_map_cstr(tmp_node, "shaper");
    if (mpack_type_map != mpack_node_type(shaper)) {
        LOG_ERROR("%s: shaping marsio msgpack shaper type wrong, type is %d", LOG_TAG_MARSIO, mpack_node_type(shaper));
        goto ERROR;
    }
    tmp_node = mpack_node_map_cstr(shaper, "rule_ids");
    if (mpack_type_array != mpack_node_type(tmp_node)) {
        LOG_ERROR("%s: shaping marsio msgpack shaper type wrong, type is %d", LOG_TAG_MARSIO, mpack_node_type(tmp_node));
        goto ERROR;
    }

    shaping_rule_id_array_size = mpack_node_array_length(tmp_node);
    ctrl_data->shaping_rule_num = MIN(shaping_rule_id_array_size, SHAPING_RULE_NUM_MAX);
    for (int i = 0; i < ctrl_data->shaping_rule_num; i++) {
        if (mpack_type_uint != mpack_node_type(mpack_node_array_at(tmp_node, i))) {
            LOG_ERROR("%s: shaping marsio msgpack shaping rule id type wrong at index %d, type is %d", LOG_TAG_MARSIO, i, mpack_node_type(mpack_node_array_at(tmp_node, i)));
            goto ERROR;
        }
        ctrl_data->shaping_rule_ids[i] = mpack_node_i64(mpack_node_array_at(tmp_node, i));
    }

SUCCESS:
    mpack_tree_destroy(&tree);
    return 0;

ERROR:
    mpack_tree_destroy(&tree);
    return -1;
}