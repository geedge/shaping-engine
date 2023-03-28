#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include "shaper.h"
#include "shaper_marsio.h"
#include "shaper_session.h"

static void *shaper_thread_loop(void *data)
{
    struct shaping_thread_ctx *ctx = (struct shaping_thread_ctx *)data;

    marsio_thread_init(ctx->marsio_info->instance);

    //loop to process pkts
    while(1) {
        shaper_packet_recv_and_process(ctx);
        if (__atomic_load_n(&ctx->session_need_reset, __ATOMIC_SEQ_CST) > 0) {
            session_table_reset_with_callback(ctx->session_table, shaper_session_data_free_cb, ctx);
            __atomic_fetch_and(&ctx->session_need_reset, 0, __ATOMIC_SEQ_CST);
        }
        marsio_poll_wait(ctx->marsio_info->instance, &ctx->marsio_info->mr_dev, 1, ctx->tid, 1);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    struct shaping_ctx *ctx = NULL;

    ctx = shaping_engine_init();
    if (!ctx) {
        return 0;
    }

    for (int i = 0; i < ctx->thread_num; i++) {
        pthread_create(&ctx->thread_ctx[i].tid, NULL, shaper_thread_loop, &ctx->thread_ctx[i]);
    }

    //TODO:主线程保留？
    while(1) {
        sleep(1);
    }

    return 0;
}