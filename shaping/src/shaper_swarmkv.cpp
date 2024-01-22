#include <MESA/MESA_handle_logger.h>
#include <MESA/MESA_prof_load.h>

#include "log.h"
#include "shaper.h"
#include "utils.h"
#include "shaper_swarmkv.h"

#define PROBABILITY_MAX 500
#define INCREMENT 10
#define DECREMENT 1
#define FREEZE_TIME 1 //unit:s
#define PENDING_QUEUE_LEN_MAX 1500

struct shaper_swarmkv_conf
{
    char swarmkv_cluster_name[64];
    char swarmkv_node_ip[16];
    short swarmkv_node_port;
    char swarmkv_consul_ip[16];
    short swarmkv_consul_port;
    char swarmkv_cluster_announce_ip[16];
    short swarmkv_cluster_announce_port;
    short swarmkv_health_check_port;
    short swarmkv_health_check_announce_port;
    short swarmkv_log_level;
    int swarmkv_worker_thread_num;
};

static int shaper_swarmkv_config_load(struct shaper_swarmkv_conf *conf)
{
    int ret;

    ret = MESA_load_profile_string_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_CLUSTER_NAME", 
                                conf->swarmkv_cluster_name, sizeof(conf->swarmkv_cluster_name));
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_CLUSTER_NAME failed", LOG_TAG_SWARMKV);
        return ret;
    }

    ret = MESA_load_profile_string_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_NODE_IP", 
                                conf->swarmkv_node_ip, sizeof(conf->swarmkv_node_ip));
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_NODE_IP failed", LOG_TAG_SWARMKV);
        return ret;
    }
    
    ret = MESA_load_profile_short_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_NODE_PORT", &conf->swarmkv_node_port);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_NODE_PORT failed", LOG_TAG_SWARMKV);
        return ret;
    }

    ret = MESA_load_profile_string_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_CONSUL_IP",
                                        conf->swarmkv_consul_ip, sizeof(conf->swarmkv_consul_ip));
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_CONSUL_IP failed", LOG_TAG_SWARMKV);
        return ret;
    }

    ret = MESA_load_profile_short_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_CONSUL_PORT", &conf->swarmkv_consul_port);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_CONSUL_PORT failed", LOG_TAG_SWARMKV);
        return ret;
    }

    ret = MESA_load_profile_string_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_CLUSTER_ANNOUNCE_IP",
                                        conf->swarmkv_cluster_announce_ip, sizeof(conf->swarmkv_cluster_announce_ip));
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_CLUSTER_ANNOUNCE_IP failed", LOG_TAG_SWARMKV);
        return ret;
    }

    ret = MESA_load_profile_short_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_CLUSTER_ANNOUNCE_PORT", &conf->swarmkv_cluster_announce_port);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_CLUSTER_ANNOUNCE_PORT failed", LOG_TAG_SWARMKV);
        return ret;
    }

    ret = MESA_load_profile_short_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_HEALTH_CHECK_PORT", &conf->swarmkv_health_check_port);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_HEALTH_CHECK_PORT failed", LOG_TAG_SWARMKV);
        return ret;
    }

    ret = MESA_load_profile_short_nodef(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_HEALTH_CHECK_ANNOUNCE_PORT", &conf->swarmkv_health_check_announce_port);
    if (ret < 0) {
        LOG_ERROR("%s: shaping init global conf SWARMKV_HEALTH_CHECK_ANNOUNCE_PORT failed", LOG_TAG_SWARMKV);
        return ret;
    }

    MESA_load_profile_int_def(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_WORKER_THREAD_NUM", &conf->swarmkv_worker_thread_num, 1);

    MESA_load_profile_short_def(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_LOG_LEVEL", &conf->swarmkv_log_level, 4);

    return 0;
}

void swarmkv_reload_log_level()
{
#if 0 //TODO: need interface to reload log level
    short log_level = 0;

    MESA_load_profile_short_def(SHAPING_GLOBAL_CONF_FILE, "SWARMKV", "SWARMKV_LOG_LEVEL", &log_level, 4);
#endif
    return;
}

int shaper_swarmkv_pending_queue_aqm_drop(struct shaping_thread_ctx *ctx)
{
    long long pending_queue_len = swarmkv_caller_get_pending_commands(ctx->swarmkv_db);
    time_t now = time(NULL);

    if (now - ctx->swarmkv_aqm_update_time < FREEZE_TIME) {
        goto END;
    }

    if (pending_queue_len > PENDING_QUEUE_LEN_MAX) {
        if (ctx->swarmkv_aqm_prob < PROBABILITY_MAX) {
            ctx->swarmkv_aqm_prob += INCREMENT;
        }
        LOG_DEBUG("%s: shaping pending queue len %lld, aqm prob %d", LOG_TAG_SWARMKV, pending_queue_len, ctx->swarmkv_aqm_prob);
    } else {
        if (ctx->swarmkv_aqm_prob >= DECREMENT) {
            ctx->swarmkv_aqm_prob -= DECREMENT;
        }
        LOG_DEBUG("%s: shaping pending queue len %lld, aqm prob %d", LOG_TAG_SWARMKV, pending_queue_len, ctx->swarmkv_aqm_prob);
    }
    ctx->swarmkv_aqm_update_time = now;

END:
    if (rand() % PROBABILITY_MAX < ctx->swarmkv_aqm_prob) {
        return 1;
    }

    return 0;
}

struct swarmkv* shaper_swarmkv_init(int caller_thread_num)
{
    struct swarmkv_options *swarmkv_opts = NULL;
    struct swarmkv *swarmkv_db = NULL;
    struct shaper_swarmkv_conf conf;
    char *err = NULL;

    memset(&conf, 0, sizeof(conf));
    if (shaper_swarmkv_config_load(&conf) < 0) {
        return NULL;
    }

    swarmkv_opts = swarmkv_options_new();
    swarmkv_options_set_bind_address(swarmkv_opts, conf.swarmkv_node_ip);
    swarmkv_options_set_cluster_port(swarmkv_opts, conf.swarmkv_node_port);
    swarmkv_options_set_consul_host(swarmkv_opts, conf.swarmkv_consul_ip);
    swarmkv_options_set_consul_port(swarmkv_opts, conf.swarmkv_consul_port);
    swarmkv_options_set_cluster_announce_ip(swarmkv_opts, conf.swarmkv_cluster_announce_ip);
    swarmkv_options_set_cluster_announce_port(swarmkv_opts, conf.swarmkv_cluster_announce_port);
    swarmkv_options_set_health_check_port(swarmkv_opts, conf.swarmkv_health_check_port);
    swarmkv_options_set_health_check_announce_port(swarmkv_opts, conf.swarmkv_health_check_announce_port);
    swarmkv_options_set_log_path(swarmkv_opts, "log");
    swarmkv_options_set_log_level(swarmkv_opts, conf.swarmkv_log_level);
    swarmkv_options_set_caller_thread_number(swarmkv_opts, caller_thread_num);
    swarmkv_options_set_worker_thread_number(swarmkv_opts, conf.swarmkv_worker_thread_num);

    swarmkv_db = swarmkv_open(swarmkv_opts, conf.swarmkv_cluster_name, &err);
    if (err) {
        LOG_ERROR("%s: shaping init swarmkv %s failed: %s", LOG_TAG_SWARMKV, conf.swarmkv_cluster_name, err);
        free(err);
        return NULL;
    }

    LOG_DEBUG("%s: shaping open swarmkv: %s", LOG_TAG_SWARMKV, conf.swarmkv_cluster_name);

    char cmd[256] = {0};//重启之后自动执行一次heal
    snprintf(cmd, sizeof(cmd), "/opt/tsg/framework/bin/swarmkv-cli -n %s -c %s:%d --exec CLUSTER SANITY heal", conf.swarmkv_cluster_name, conf.swarmkv_consul_ip, conf.swarmkv_consul_port);
    system(cmd);
    
    return swarmkv_db;
}

void shaper_swarmkv_destroy(struct swarmkv* swarmkv_db)
{
    if (swarmkv_db) {
        swarmkv_close(swarmkv_db);
    }

    return;
}