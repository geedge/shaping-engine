#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <sys/prctl.h>
#include <jemalloc/jemalloc.h>

#include "log.h"
#include "utils.h"
#include "shaper.h"
#include "shaper_stat.h"
#include "shaper_marsio.h"
#include "shaper_session.h"
#include "shaper_swarmkv.h"
#include "shaper_global_stat.h"

static int quit = 0;

static void *shaper_thread_loop(void *data)
{
    char thread_name[16] = {0};
    struct shaping_thread_ctx *ctx = (struct shaping_thread_ctx *)data;
    int output_interval_s = ctx->ref_ctx->global_stat->output_interval_s;

    snprintf(thread_name, sizeof(thread_name), "shape-work-%d", ctx->thread_index);
    prctl(PR_SET_NAME, (unsigned long long)thread_name, NULL, NULL, NULL);

    if (marsio_thread_init(ctx->marsio_info->instance) != 0) {
        LOG_ERROR("%s: marsio_thread_init failed", LOG_TAG_SHAPING);
        return NULL;
    }

    swarmkv_register_thread(ctx->swarmkv_db);

    //loop to process pkts
    while(!quit) {
        shaper_packet_recv_and_process(ctx);
        if (__atomic_load_n(&ctx->session_need_reset, __ATOMIC_SEQ_CST) > 0) {
            session_table_reset_with_callback(ctx->session_table, shaper_session_data_free_cb, ctx);
            __atomic_fetch_and(&ctx->session_need_reset, 0, __ATOMIC_SEQ_CST);
        }
        marsio_poll_wait(ctx->marsio_info->instance, &ctx->marsio_info->mr_dev, 1, ctx->thread_index, output_interval_s * 1000);
    }

    shaper_thread_resource_clear();

    return NULL;
}

static void sig_handler(int signo)
{
    if (signo == SIGHUP)
    {
        LOG_INFO("%s: recv SIGHUP, reload zlog.conf and swarmkv log level", LOG_TAG_SHAPING);
        LOG_RELOAD();
        swarmkv_reload_log_level();
    }

    if (signo == SIGQUIT) {
        quit = 1;
    }

    return;
}

int main(int argc, char **argv)
{
    struct shaping_ctx *ctx = NULL;
    time_t last_update_time = time(NULL);

    if (LOG_INIT("./conf/zlog.conf") == -1)
    {
        return -1;
    }

    if (signal(SIGHUP, sig_handler) == SIG_ERR)
    {
        LOG_ERROR("%s: unable to register SIGHUP signal handler, error %d: %s", LOG_TAG_SHAPING, errno, strerror(errno));
        LOG_CLOSE();
        return -1;
    }

    if (signal(SIGQUIT, sig_handler) == SIG_ERR)
    {
        LOG_ERROR("%s: unable to register SIGQUIT signal handler, error %d: %s", LOG_TAG_SHAPING, errno, strerror(errno));
        LOG_CLOSE();
        return -1;
    }

    bool flag = true;
    int ret = mallctl("background_thread", NULL, NULL, &flag, sizeof(flag));
    if (ret != 0) {
        LOG_ERROR("%s: mallctl(background_thread) failed, ret %d", LOG_TAG_SHAPING, ret);
        LOG_CLOSE();
        return -1;
    }

    ctx = shaping_engine_init();
    if (!ctx) {
        return -1;
    }

    for (int i = 0; i < ctx->thread_num; i++) {
        int ret = pthread_create(&ctx->thread_ctx[i].tid, NULL, shaper_thread_loop, &ctx->thread_ctx[i]);
        if (ret < 0) {
            LOG_ERROR("%s: create thread failed, error %d: %s", LOG_TAG_SHAPING, errno, strerror(errno));
        }
    }

    while(!quit) {
        time_t curr_time = time(NULL);
        if (curr_time - last_update_time >= ctx->global_stat->output_interval_s) {
            shaper_global_stat_refresh(ctx);
            last_update_time = curr_time;
        }
        sleep(ctx->global_stat->output_interval_s);
    }

    for (int i = 0; i < ctx->thread_num; i++) {
        pthread_join(ctx->thread_ctx[i].tid, NULL);
    }
    shaping_engine_destroy(ctx);

    return 0;
}