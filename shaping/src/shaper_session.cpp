#include "session_table.h"
#include "raw_packet.h"
#include "utils.h"
#include "log.h"
#include "shaper_marsio.h"
#include "shaper_session.h"
#include "shaper_maat.h"
#include "shaper_stat.h"
#include "shaper_global_stat.h"
#include "shaper.h"

struct shaping_flow* shaper_session_opening(struct shaping_thread_ctx *ctx, struct metadata *meta, struct ctrl_pkt_data *ctrl_data, struct raw_pkt_parser *raw_parser)
{
    struct shaping_flow *sf = NULL;
    struct session_node *node = NULL;

    node  = session_table_search_by_id(ctx->session_table, meta->session_id);
    if (node) {
        sf = (struct shaping_flow *)node->val_data;
        LOG_ERROR("%s: session id %lu for %s has already exist", LOG_TAG_SHAPING, meta->session_id, addr_tuple4_to_str(&sf->tuple4));
        return NULL;
    }

    sf = shaping_flow_new();
    raw_packet_parser_get_most_inner_tuple4(raw_parser, &sf->tuple4);
    //shaper_rules_update(ctx, sf, ctrl_data->shaping_rule_ids, ctrl_data->shaping_rule_num);
    shaper_marsio_metadata_deep_copy(sf->ctrl_meta, meta);

    session_table_insert(ctx->session_table, meta->session_id, &sf->tuple4, sf, NULL);

    shaper_global_stat_curr_session_inc(ctx->global_stat);

    return sf;
}

static void shaper_session_log_send(struct shaping_thread_ctx *ctx, struct shaping_flow *sf)
{
    if (!sf->ctrl_meta) {
        LOG_ERROR("%s: ctrl meta for session %s doesn't exist", LOG_TAG_SHAPING, addr_tuple4_to_str(&sf->tuple4));
        return;
    }

    /************************/
    //TODO: add content to msgpack
    /************************/

    marsio_buff_t *tx_buff;
    char *pkt_header_data = sf->ctrl_meta->raw_data;
    int pkt_header_len = sf->ctrl_meta->l7_offset;
    struct metadata *ctrl_meta = sf->ctrl_meta;
    struct sids sids;

    marsio_buff_malloc_global(ctx->marsio_info->instance, &tx_buff, 1, 0, ctx->thread_index);
    char *dst = marsio_buff_append(tx_buff, pkt_header_len + msgpack_len);
    memcpy(dst, pkt_header_data, pkt_header_len);
    memcpy(dst + pkt_header_len, msgpack_data, msgpack_len);

    if (marsio_buff_set_metadata(tx_buff, MR_BUFF_SESSION_ID, &ctrl_meta->session_id, sizeof(ctrl_meta->session_id)) != 0) {
        LOG_ERROR("%s: marsio_buff_set_metadata MR_BUFF_SESSION_ID failed for session %s", LOG_TAG_SHAPING, addr_tuple4_to_str(&sf->tuple4));
        return;
    }

    if (marsio_buff_set_metadata(tx_buff, MR_BUFF_PAYLOAD_OFFSET, &pkt_header_len, sizeof(pkt_header_len)) != 0) {
        LOG_ERROR("%s: marsio_buff_set_metadata MR_BUFF_PAYLOAD_OFFSET failed for session %s", LOG_TAG_SHAPING, addr_tuple4_to_str(&sf->tuple4));
        return;
    }

    sids.elems[0] = ctx->firewall_sid;
    sids.num = 1;
    if (marsio_buff_set_sid_list(tx_buff, sids.elems, sids.num) != 0) {
        LOG_ERROR("%s: marsio_buff_set_sid_list failed for session %s", LOG_TAG_SHAPING, addr_tuple4_to_str(&sf->tuple4));
        return;
    }

    if (marsio_buff_set_metadata(tx_buff, MR_BUFF_ROUTE_CTX, ctrl_meta->route_ctx.data, ctrl_meta->route_ctx.len) != 0) {
        LOG_ERROR("%s: marsio_buff_set_metadata MR_BUFF_ROUTE_CTX failed for session %s", LOG_TAG_SHAPING, addr_tuple4_to_str(&sf->tuple4));
        return;
    }

    marsio_send_burst(ctx->marsio_info->mr_path, ctx->thread_index, &tx_buff, 1);
    shaper_global_stat_session_log_send_num_inc(ctx->global_stat);

    return;
}

struct shaping_flow* shaper_session_close(struct shaping_thread_ctx *ctx, struct metadata *meta)
{
    struct session_node *session_node = NULL;
    struct shaping_flow *sf = NULL;

    session_node = session_table_search_by_id(ctx->session_table, meta->session_id);
    if (!session_node) {
        return NULL;
    }

    sf = (struct shaping_flow *)session_node->val_data;
    sf->flag |= STREAM_CLOSE;

    shaper_session_log_send(ctx, sf);

    session_table_delete_by_id(ctx->session_table, meta->session_id);
    shaper_global_stat_curr_session_dec(ctx->global_stat);

    return sf;
}

struct shaping_flow* shaper_session_active(struct shaping_thread_ctx *ctx, struct metadata *meta, struct ctrl_pkt_data *ctrl_data, struct raw_pkt_parser *raw_parser)
{
    struct shaping_flow *sf = NULL;
    struct session_node *node = NULL;

    node = session_table_search_by_id(ctx->session_table, meta->session_id);
    if (!node) {
        sf = shaper_session_opening(ctx, meta, ctrl_data, raw_parser);
    } else {
        sf = (struct shaping_flow *)node->val_data;
    }
    
    shaper_rules_update(ctx, sf, ctrl_data->shaping_rule_ids, ctrl_data->shaping_rule_num);

    return sf;
}

struct shaping_flow* shaper_session_reset_all(struct shaping_thread_ctx *ctx, struct metadata *meta)
{
    struct shaping_ctx *shaping_ctx = ctx->ref_ctx;

    LOG_ERROR("%s: session %lu resetall: notification clears all session tables !!!", LOG_TAG_SHAPING, meta->session_id);
    for (int i = 0; i < shaping_ctx->thread_num; i++) {
        __atomic_add_fetch(&shaping_ctx->thread_ctx[i].session_need_reset, 1, __ATOMIC_SEQ_CST);
    }

    return NULL;
}

void shaper_session_data_free_cb(void *session_data, void *data)
{
    struct shaping_flow *sf = (struct shaping_flow *)session_data;
    struct shaping_thread_ctx *ctx = (struct shaping_thread_ctx *)data;

    if (sf) {
        shaper_queue_clear(sf, ctx);
        shaping_flow_free(sf);
    }

    shaper_global_stat_curr_session_dec(ctx->global_stat);

    return;
}