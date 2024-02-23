#ifndef _RAW_PACKET_H
#define _RAW_PACKET_H

#ifdef __cpluscplus
extern "C"
{
#endif

#include <stdint.h>

enum layer_type
{
    // 数据链路层
    LAYER_TYPE_ETHER = 1 << 0,
    LAYER_TYPE_PPP = 1 << 1,
    LAYER_TYPE_HDLC = 1 << 2,
    LAYER_TYPE_L2 = (LAYER_TYPE_ETHER | LAYER_TYPE_PPP | LAYER_TYPE_HDLC),

    // 数据链路层 -- 隧道
    LAYER_TYPE_VLAN = 1 << 3,
    LAYER_TYPE_PPPOE = 1 << 4,
    LAYER_TYPE_MPLS = 1 << 5,
    LAYER_TYPE_L2_TUN = (LAYER_TYPE_VLAN | LAYER_TYPE_PPPOE | LAYER_TYPE_MPLS),

    // 网络层
    LAYER_TYPE_IPV4 = 1 << 6,
    LAYER_TYPE_IPV6 = 1 << 7,
    LAYER_TYPE_L3 = (LAYER_TYPE_IPV4 | LAYER_TYPE_IPV6),

    // 网络层 -- 隧道

    // 传输层
    LAYER_TYPE_UDP = 1 << 8,
    LAYER_TYPE_TCP = 1 << 9,
    LAYER_TYPE_L4 = (LAYER_TYPE_UDP | LAYER_TYPE_TCP),

    // 传输层 -- 隧道
    LAYER_TYPE_G_VXLAN = 1 << 10,
    LAYER_TYPE_GTPV1_U = 1 << 11,

    // ALL
    LAYER_TYPE_ALL = (LAYER_TYPE_L2 | LAYER_TYPE_L2_TUN | LAYER_TYPE_L3 | LAYER_TYPE_L4 | LAYER_TYPE_G_VXLAN | LAYER_TYPE_GTPV1_U),

    // UNKNOWN
    LAYER_TYPE_UNKNOWN,
};

enum ldbc_method
{
    LDBC_METHOD_HASH_INT_IP = 1,
    LDBC_METHOD_HASH_EXT_IP = 2,
    LDBC_METHOD_HASH_INT_IP_AND_EXT_IP = 3,
    LDBC_METHOD_HASH_INNERMOST_INT_IP = 4,
    LDBC_METHOD_HASH_INNERMOST_EXT_IP = 5,
};

struct layer_result
{
    uint16_t offset;
    enum layer_type type;
};

struct layer_results
{
    struct layer_result layers[16];
    uint16_t layers_used;
    uint16_t layers_size;
};

struct raw_pkt_parser
{
    enum layer_type expect_type;
    struct layer_results results;

    const void *ptr_pkt_start;
    uint64_t pkt_trace_id;
};

void raw_packet_parser_init(struct raw_pkt_parser *handler, uint64_t pkt_trace_id, enum layer_type expect_type, uint16_t expect_results_num);
// return most inner payload
const void *raw_packet_parser_parse(struct raw_pkt_parser *handler, const void *data, size_t length);

// return  0 : success
// return -1 : error
int raw_packet_parser_get_most_inner_tuple4(struct raw_pkt_parser *handler, struct addr_tuple4 *addr);
int raw_packet_parser_get_most_outer_tuple4(struct raw_pkt_parser *handler, struct addr_tuple4 *addr);

// return  0 : success
// return -1 : error
int raw_packet_parser_get_most_inner_address(struct raw_pkt_parser *handler, struct addr_tuple4 *addr);
int raw_packet_parser_get_most_outer_address(struct raw_pkt_parser *handler, struct addr_tuple4 *addr);

uint64_t raw_packet_parser_get_hash_value(struct raw_pkt_parser *handler, enum ldbc_method method, int dir_is_internal);

int raw_packet_tcp_payload_len_get(struct raw_pkt_parser *handler);
void raw_packet_set_dscp(struct ethhdr *eth_hdr, uint8_t dscp);

#ifdef __cpluscplus
}
#endif

#endif
