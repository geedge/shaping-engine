#include <marsio.h>
#include "shaper.h"
#include "utils.h"

#define SHAPER_MARSIO_RX_BRUST_MAX 128

enum ctr_pkt_index
{
    INDEX_TSYNC = 0,
    INDEX_SESSION_ID,
    INDEX_STATE,
    INDEX_METHOD,
    INDEX_KEY_SCE,
    INDEX_SCE,
    INDEX_KEY_SHAPER,
    INDEX_SHAPER,
    INDEX_KEY_PROXY,
    INDEX_PROXY,
    INDEX_MAX
};

struct shaping_marsio_info
{
    struct mr_instance *instance;
    struct mr_vdev *mr_dev;
    struct mr_sendpath *mr_path;
    int rx_brust_max;
};

struct metadata
{
    uint64_t session_id;
    char *raw_data;
    int raw_len;
    int dir;
    int is_tcp_pure_ctrl;
    int is_ctrl_pkt;
    uint16_t l7_offset;       // only control packet set l7_offset
    struct sids sids;
    struct route_ctx route_ctx;
};

enum session_state
{
    SESSION_STATE_OPENING = 1,
    SESSION_STATE_CLOSING = 2,
    SESSION_STATE_ACTIVE = 3,
    SESSION_STATE_RESETALL = 4,
};

struct ctrl_pkt_data
{
    uint64_t session_id;
    enum session_state state;
    long long shaping_rule_ids[SHAPING_RULE_NUM_MAX];
    int shaping_rule_num;
};

struct shaping_marsio_info* shaper_marsio_init(struct shaping_system_conf *sys_conf);
void shaper_marsio_destroy(struct shaping_marsio_info *marsio_info);
int shaper_marsio_pkt_metadata_get(marsio_buff_t *rx_buff, struct metadata *meta, int is_ctrl_buff, struct raw_pkt_parser *raw_parser);
int shaper_marsio_ctrl_pkt_data_parse(struct ctrl_pkt_data *ctrl_data, const char *data, size_t length);
void shaper_marsio_metadata_deep_copy(struct metadata *dst, struct metadata *src);
