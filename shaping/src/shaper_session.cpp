#include "addr_tuple4.h"
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
#include "mpack.h"

struct shaping_flow* shaper_session_opening(struct shaping_thread_ctx *ctx, struct metadata *meta, struct ctrl_pkt_data *ctrl_data, struct raw_pkt_parser *raw_parser)
{
    struct shaping_flow *sf = NULL;
    struct session_node *node = NULL;

    node  = session_table_search_by_id(ctx->session_table, meta->session_id);
    if (node) {
        sf = (struct shaping_flow *)node->val_data;

        char *addr_str = addr_tuple4_to_str(&sf->tuple4);
        LOG_ERROR("%s: session id %lu for %s has already exist", LOG_TAG_SHAPING, meta->session_id, addr_str);

        if (addr_str) {
            free(addr_str);
        }

        return NULL;
    }

    sf = shaping_flow_new(ctx);
    raw_packet_parser_get_most_inner_tuple4(raw_parser, &sf->tuple4);
    sf->src_ip_str = addr_src_ip_to_str(&sf->tuple4);
    sf->src_ip_str_len = strlen(sf->src_ip_str);

    //shaper_rules_update(ctx, sf, ctrl_data->shaping_rule_ids, ctrl_data->shaping_rule_num);
    shaper_marsio_metadata_deep_copy(&sf->ctrl_meta, meta);

    session_table_insert(ctx->session_table, meta->session_id, &sf->tuple4, sf, NULL);

    shaper_global_stat_curr_session_inc(&ctx->thread_global_stat);

    return sf;
}

void shaper_session_log_prepare(struct shaping_flow *sf, char **mpack_data, size_t *mpack_size)
{
    mpack_writer_t writer;
    struct shaping_rule_info *rule_info;

	mpack_writer_init_growable(&writer, mpack_data, mpack_size);
	mpack_build_map(&writer);

    //tsync
	mpack_write_cstr(&writer, "tsync");
	mpack_write_cstr(&writer, "2.0");

	//session_id
	mpack_write_cstr(&writer, "session_id");
	mpack_write_u64(&writer, sf->ctrl_meta.session_id);

	//state
	mpack_write_cstr(&writer, "state");
	mpack_write_cstr(&writer, "active");

    //method
	mpack_write_cstr(&writer, "method");
	mpack_write_cstr(&writer, "log_update");

    char *addr_str = addr_tuple4_to_str(&sf->tuple4);
    LOG_DEBUG("%s: shaper prepare log to sapp for session %s, id %lu", LOG_TAG_SHAPING, addr_str, sf->ctrl_meta.session_id);
    if (addr_str) {
        free(addr_str);
    }

    //params
	mpack_write_cstr(&writer, "params");
	mpack_build_map(&writer);
	mpack_write_cstr(&writer, "shaper");
	mpack_build_array(&writer);
    for (int i = 0; i < sf->rule_num; i++) {
        rule_info = &sf->matched_rule_infos[i];

        mpack_build_map(&writer);
        mpack_write_cstr(&writer, "rule_id");
        mpack_write_i64(&writer, rule_info->id);
        LOG_DEBUG("%s: rule id %d", LOG_TAG_SHAPING, rule_info->id);

        mpack_write_cstr(&writer, "profile_ids");
        mpack_build_array(&writer);
        mpack_write_i64(&writer, rule_info->primary.id);
        LOG_DEBUG("%s: primary_profile id %d", LOG_TAG_SHAPING, rule_info->primary.id);

        for (int j = 0; j < rule_info->borrowing_num; j++) {
            mpack_write_i64(&writer, rule_info->borrowing[j].id);
            LOG_DEBUG("%s: borrow_profile id %d", LOG_TAG_SHAPING, rule_info->borrowing[j].id);
        }
        mpack_complete_array(&writer);//end build array for profile_ids
        mpack_complete_map(&writer);
    }
    mpack_complete_array(&writer);//end build array for shaper
    mpack_complete_map(&writer);//end build map for params
    mpack_complete_map(&writer);//end build map at the beginning

    mpack_writer_destroy(&writer);

    LOG_DEBUG("%s: log length is %lu", LOG_TAG_SHAPING, *mpack_size);
    return;
}

static void shaper_session_log_send(struct shaping_thread_ctx *ctx, struct shaping_flow *sf)
{
    char *mpack_data = NULL;
    size_t mpack_size = 0;

    shaper_session_log_prepare(sf, &mpack_data, &mpack_size);

    marsio_buff_t *tx_buff = NULL;
    char *pkt_header_data = sf->ctrl_meta.raw_data;
    int pkt_header_len = sf->ctrl_meta.l7_offset;
    struct metadata *ctrl_meta = &sf->ctrl_meta;
    struct sids sids;
    char *dst = NULL;
    int marsio_buff_free_flag = 1;
    char *addr_str = addr_tuple4_to_str(&sf->tuple4);

    if (marsio_buff_malloc_global(ctx->marsio_info->instance, &tx_buff, 1, 0, ctx->thread_index) < 0) {
        LOG_ERROR("%s: marsio_buff_malloc_global failed for session %s", LOG_TAG_SHAPING, addr_str);
        goto END;
    }

    marsio_buff_set_ctrlbuf(tx_buff);
    dst = marsio_buff_append(tx_buff, pkt_header_len + mpack_size);
    if (dst == NULL) {
        LOG_ERROR("%s: marsio_buff_append failed for session %s", LOG_TAG_SHAPING, addr_str);
        goto END;
    }

    memcpy(dst, pkt_header_data, pkt_header_len);
    memcpy(dst + pkt_header_len, mpack_data, mpack_size);

    if (marsio_buff_set_metadata(tx_buff, MR_BUFF_SESSION_ID, &ctrl_meta->session_id, sizeof(ctrl_meta->session_id)) != 0) {
        LOG_ERROR("%s: marsio_buff_set_metadata MR_BUFF_SESSION_ID failed for session %s", LOG_TAG_SHAPING, addr_str);
        goto END;
    }

    if (marsio_buff_set_metadata(tx_buff, MR_BUFF_PAYLOAD_OFFSET, &pkt_header_len, sizeof(pkt_header_len)) != 0) {
        LOG_ERROR("%s: marsio_buff_set_metadata MR_BUFF_PAYLOAD_OFFSET failed for session %s", LOG_TAG_SHAPING, addr_str);
        goto END;
    }

    sids.elems[0] = ctx->conf.firewall_sid;
    sids.num = 1;
    if (marsio_buff_set_sid_list(tx_buff, sids.elems, sids.num) != 0) {
        LOG_ERROR("%s: marsio_buff_set_sid_list failed for session %s", LOG_TAG_SHAPING, addr_str);
        goto END;
    }

    if (marsio_buff_set_metadata(tx_buff, MR_BUFF_ROUTE_CTX, ctrl_meta->route_ctx.data, ctrl_meta->route_ctx.len) != 0) {
        LOG_ERROR("%s: marsio_buff_set_metadata MR_BUFF_ROUTE_CTX failed for session %s", LOG_TAG_SHAPING, addr_str);
        goto END;
    }

    marsio_buff_free_flag = 0;
    marsio_send_burst(ctx->marsio_info->mr_path, ctx->thread_index, &tx_buff, 1);
    shaper_global_stat_session_log_send_num_inc(&ctx->thread_global_stat);

END:
    if (addr_str) {
        free(addr_str);
    }

    if (mpack_data) {
        free(mpack_data);
    }

    if (marsio_buff_free_flag && tx_buff) {
        marsio_buff_free(ctx->marsio_info->instance, &tx_buff, 1, 0, ctx->thread_index);
    }

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

    if (shaper_queue_empty(sf)) {
        shaping_flow_free(ctx, sf);
    } else {
        sf->flag |= SESSION_CLOSE;
    }

    shaper_global_stat_ctrlpkt_active_close_inc(&ctx->thread_global_stat);

    session_table_delete_by_id(ctx->session_table, meta->session_id);
    shaper_global_stat_curr_session_dec(&ctx->thread_global_stat);

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
    shaper_session_log_send(ctx, sf);//send log of rules and profiles when receive new matched rules

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
        shaping_flow_free(ctx, sf);
    }

    shaper_global_stat_curr_session_dec(&ctx->thread_global_stat);

    return;
}