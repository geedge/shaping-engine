#ifndef _UTILS_H
#define _UTILS_H

#ifdef __cpluscplus
extern "C"
{
#endif

#define MIN(a, b) ((a) > (b) ? (b) : (a))

#ifndef container_of
#define container_of(ptr, type, member) ({			 \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define prefetch(x) __builtin_prefetch(x)

#define LOG_TAG_SHAPING "SHAPING"
#define LOG_TAG_SWARMKV "SWARMKV"
#define LOG_TAG_STAT    "STAT"
#define LOG_TAG_MAAT    "MAAT"
#define LOG_TAG_MARSIO  "MARSIO"
#define LOG_TAG_UTILS "UTILS"
#define LOG_TAG_RAWPKT "RAW_PACKET"
#define LOG_TAG_CTRLPKT "CTRL_PACKET"
#define LOG_TAG_STABLE "SESSION_TABLE"

/******************************************************************************
 * fixed_num_array
 ******************************************************************************/

struct fixed_num_array
{
    int elems[128];
    int num;
    int size;
};

void fixed_num_array_init(struct fixed_num_array *array);
void fixed_num_array_add_elem(struct fixed_num_array *array, int elem);
void fixed_num_array_del_elem(struct fixed_num_array *array, int elem);
int fixed_num_array_is_full(struct fixed_num_array *array);
int fixed_num_array_count_elem(struct fixed_num_array *array);
int fixed_num_array_exist_elem(struct fixed_num_array *array, int elem);
int fixed_num_array_index_elem(struct fixed_num_array *array, int index);

/******************************************************************************
 * sids
 ******************************************************************************/

#include <marsio.h>

struct sids
{
    int num;
    sid_t elems[MR_SID_LIST_MAXLEN];
};

void sids_write_once(struct sids *dst, struct sids *src);
void sids_copy(struct sids *dst, struct sids *src);

/******************************************************************************
 * route_ctx
 ******************************************************************************/

struct route_ctx
{
    char data[64];
    int len;
};

void route_ctx_write_once(struct route_ctx *dst, struct route_ctx *src);
void route_ctx_copy(struct route_ctx *dst, struct route_ctx *src);

/******************************************************************************
 * throughput_metrics
 ******************************************************************************/

struct throughput_metrics
{
    uint64_t n_pkts;
    uint64_t n_bytes;
};

void throughput_metrics_inc(struct throughput_metrics *iterm, uint64_t n_pkts, uint64_t n_bytes);

/******************************************************************************
 * protocol
 ******************************************************************************/

struct udp_hdr
{
    u_int16_t uh_sport; /* source port */
    u_int16_t uh_dport; /* destination port */
    u_int16_t uh_ulen;  /* udp length */
    u_int16_t uh_sum;   /* udp checksum */
} __attribute__((__packed__));

void build_udp_header(const char *l3_hdr, int l3_hdr_len, struct udp_hdr *udp_hdr, u_int16_t udp_sport, u_int16_t udp_dport, int payload_len);
void build_ip_header(struct ip *ip_hdr, u_int8_t next_protocol, const char *src_addr, const char *dst_addr, uint16_t payload_len);
void build_ether_header(struct ethhdr *eth_hdr, uint16_t next_protocol, const char *src_mac, const char *dst_mac);

/******************************************************************************
 * device
 ******************************************************************************/

int get_ip_by_device_name(const char *dev_name, char *ip_buff);
int get_mac_by_device_name(const char *dev_name, char *mac_buff);
int str_to_mac(const char *str, char *mac_buff);

#ifdef __cpluscplus
}
#endif

#endif
