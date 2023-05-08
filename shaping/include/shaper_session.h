#include "session_table.h"

struct shaping_flow* shaper_session_opening(struct shaping_thread_ctx *ctx, struct metadata *meta, struct ctrl_pkt_data *ctrl_data, struct raw_pkt_parser *raw_parser);
struct shaping_flow* shaper_session_close(struct shaping_thread_ctx *ctx, struct metadata *meta);
struct shaping_flow* shaper_session_active(struct shaping_thread_ctx *ctx, struct metadata *meta, struct ctrl_pkt_data *ctrl_data, struct raw_pkt_parser *raw_parser);
struct shaping_flow* shaper_session_reset_all(struct shaping_thread_ctx *ctx, struct metadata *meta);
void shaper_session_data_free_cb(void *session_data, void *data);

void shaper_session_log_prepare(struct shaping_flow *sf, char **mpack_data, size_t *mpack_size);