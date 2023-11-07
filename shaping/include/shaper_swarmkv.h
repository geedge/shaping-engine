#include <MESA/swarmkv.h>

struct swarmkv* shaper_swarmkv_init(int caller_thread_num);
void shaper_swarmkv_destroy(struct swarmkv* swarmkv_db);
void swarmkv_reload_log_level();
int shaper_swarmkv_pending_queue_aqm_drop(struct shaping_thread_ctx *ctx);