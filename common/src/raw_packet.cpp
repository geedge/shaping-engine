#include <assert.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>

#include <netinet/ip.h>
#include <netinet/ip6.h>
#define __FAVOR_BSD 1
#include <netinet/tcp.h>
#include <netinet/ether.h>
#include <linux/ppp_defs.h>

#include "log.h"
#include "utils.h"
#include "uthash.h"
#include "addr_tuple4.h"
#include "raw_packet.h"

/******************************************************************************
 * Struct
 ******************************************************************************/
#define ETH_MARSIO_CHECK 0xAAAA  /*marsio check packet*/

enum parse_status
{
    PARSE_STATUS_CONTINUE,
    PARSE_STATUS_STOP
};

struct vlan_hdr
{
    uint16_t vlan_cfi;
    uint16_t protocol;
} __attribute__((__packed__));

struct vxlan_hdr
{
    uint8_t flags[2];
    uint16_t gdp; // group policy id
    uint8_t vni[3];
    uint8_t reserved;
} __attribute__((__packed__));

struct gtp_hdr
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned char flags;
    unsigned char msg_type;
    unsigned short len;
    unsigned int teid;
#elif __BYTE_ORDER == __BIG_ENDIAN
    unsigned int teid;
    unsigned short len;
    unsigned char msg_type;
    unsigned char flags;
#else
#error "Please check <endian.h>"
#endif
} __attribute__((__packed__));

#define GTP_HDR_VER_MASK (0xE0)
#define GTP_HDR_FLAG_N_PDU (0x01)
#define GTP_HDR_FLAG_SEQ_NUM (0x02)
#define GTP_HDR_FLAG_NEXT_EXT_HDR (0x04)

/******************************************************************************
 * Static API
 ******************************************************************************/

static int raw_packet_parser_push(struct raw_pkt_parser *handler, enum layer_type type, uint16_t offset);
static enum parse_status raw_packet_parser_status(struct raw_pkt_parser *handler, const void *data, enum layer_type this_type);

static const char *ldbc_method_to_string(enum ldbc_method ldbc_method);

// parser utils
static void set_addr_tuple4(const void *data, enum layer_type layer_type, struct addr_tuple4 *addr);
static const char *layer_type2str(enum layer_type this_type);
static uint16_t parse_gtphdr_len(const struct gtp_hdr *gtph);

// parser protocol
static const void *parse_ether(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_ipv4(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_ipv6(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_tcp(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_udp(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_pppoe_ses(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_vxlan(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_vlan8021q(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_gtpv1_u(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);
static const void *parse_mpls(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type);

/******************************************************************************
 * Public API
 ******************************************************************************/

void raw_packet_parser_init(struct raw_pkt_parser *handler, uint64_t pkt_trace_id, enum layer_type expect_type, uint16_t expect_results_num)
{
    memset(handler, 0, sizeof(struct raw_pkt_parser));

    handler->expect_type = expect_type;
    handler->results.layers_used = 0;
    handler->results.layers_size = MIN(expect_results_num, (sizeof(handler->results.layers) / sizeof(handler->results.layers[0])));
    handler->ptr_pkt_start = NULL;
    handler->pkt_trace_id = pkt_trace_id;
}

// return most inner payload
const void *raw_packet_parser_parse(struct raw_pkt_parser *handler, const void *data, size_t length)
{
    handler->ptr_pkt_start = data;

    // TESTED
    return parse_ether(handler, data, length, LAYER_TYPE_ETHER);
}

static int get_tcp_payload_len(const void *l3_data, enum layer_type l3_layer_type, const void *tcp_data)
{
    int tcp_hdr_len;
    struct tcphdr *tcp_hdr = (struct tcphdr *)tcp_data;

    tcp_hdr_len = tcp_hdr->th_off << 2;

    if (l3_layer_type == LAYER_TYPE_IPV4) {
        int ip_total_len;
        int ip_hdr_len;
        struct ip *ip_hdr = (struct ip *)l3_data;
        ip_hdr_len = (ip_hdr->ip_hl & 0xf) * 4u;
        ip_total_len = ntohs(ip_hdr->ip_len);
        return ip_total_len - ip_hdr_len - tcp_hdr_len;
    } else {
        int ip6_payload_len;
        struct ip6_hdr *ip6_hdr = (struct ip6_hdr *)l3_data;
        ip6_payload_len = ntohs(ip6_hdr->ip6_ctlun.ip6_un1.ip6_un1_plen);
        return ip6_payload_len - tcp_hdr_len;
    }
}

int raw_packet_tcp_payload_len_get(struct raw_pkt_parser *handler)
{
    const char *l3_layer_data = NULL;
    const char *tcp_layer_data = NULL;
    const struct layer_result *tcp_layer_result = NULL;
    const struct layer_result *l3_layer_result = NULL;
    struct layer_results *results = &handler->results;

    for (int8_t i = results->layers_used - 1; i >= 0; i--)
    {
        const struct layer_result *layer = &results->layers[i];
        enum layer_type type = layer->type;

        //LOG_DEBUG("%s: find most inner tcp, pkt_trace_id: %lu, layer[%d/%d]: %s", LOG_TAG_RAWPKT, handler->pkt_trace_id, i, results->layers_size, layer_type2str(type));

        // first get L4 layer
        if (type & LAYER_TYPE_TCP)
        {
            tcp_layer_result = layer;
            continue;
        }

        // second get L3 layer
        if ((type & LAYER_TYPE_L3) && tcp_layer_result)
        {
            l3_layer_result = layer;
            break;
        }
    }

    if (tcp_layer_result && l3_layer_result) {
        l3_layer_data = (const char *)handler->ptr_pkt_start + l3_layer_result->offset;
        tcp_layer_data = (const char *)handler->ptr_pkt_start + tcp_layer_result->offset;
        return get_tcp_payload_len(l3_layer_data, l3_layer_result->type, tcp_layer_data);
    }

    return -1;
}

// return  0 : success
// return -1 : error
int raw_packet_parser_get_most_inner_tuple4(struct raw_pkt_parser *handler, struct addr_tuple4 *addr)
{
    const char *l3_layer_data = NULL;
    const char *l4_layer_data = NULL;
    const struct layer_result *l3_layer_result = NULL;
    const struct layer_result *l4_layer_result = NULL;
    struct layer_results *results = &handler->results;

    // search L4 layer and L3 layer in reverse order
    for (int8_t i = results->layers_used - 1; i >= 0; i--)
    {
        const struct layer_result *layer = &results->layers[i];
        enum layer_type type = layer->type;

        //LOG_DEBUG("%s: find most inner tuple4, pkt_trace_id: %lu, layer[%d/%d]: %s", LOG_TAG_RAWPKT, handler->pkt_trace_id, i, results->layers_size, layer_type2str(type));

        // first get L4 layer
        if (type & LAYER_TYPE_L4)
        {
            l4_layer_result = layer;
            continue;
        }

        // second get L3 layer
        if (type & LAYER_TYPE_L3)
        {
            l3_layer_result = layer;
            break;
        }
    }

    if (l3_layer_result)
    {
        l3_layer_data = (const char *)handler->ptr_pkt_start + l3_layer_result->offset;
        set_addr_tuple4(l3_layer_data, l3_layer_result->type, addr);
    }

    if (l4_layer_result)
    {
        l4_layer_data = (const char *)handler->ptr_pkt_start + l4_layer_result->offset;
        set_addr_tuple4(l4_layer_data, l4_layer_result->type, addr);
    }

    if (l3_layer_result && l4_layer_result)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

// return  0 : success
// return -1 : error
int raw_packet_parser_get_most_outer_tuple4(struct raw_pkt_parser *handler, struct addr_tuple4 *addr)
{
    const char *l3_layer_data = NULL;
    const char *l4_layer_data = NULL;
    const struct layer_result *l3_layer_result = NULL;
    const struct layer_result *l4_layer_result = NULL;
    struct layer_results *results = &handler->results;

    // search L3 layer and L4 layer in order
    for (int8_t i = 0; i <= results->layers_used - 1; i++)
    {
        const struct layer_result *layer = &results->layers[i];
        enum layer_type type = layer->type;

        //LOG_DEBUG("%s: find most outer tuple4, pkt_trace_id: %lu, layer[%d/%d]: %s", LOG_TAG_RAWPKT, handler->pkt_trace_id, i, results->layers_size, layer_type2str(type));

        // first get L3 layer
        if (type & LAYER_TYPE_L3)
        {
            l3_layer_result = layer;
            continue;
        }

        // second get L4 layer
        if (type & LAYER_TYPE_L4)
        {
            l4_layer_result = layer;
            break;
        }
    }

    if (l3_layer_result)
    {
        l3_layer_data = (const char *)handler->ptr_pkt_start + l3_layer_result->offset;
        set_addr_tuple4(l3_layer_data, l3_layer_result->type, addr);
    }

    if (l4_layer_result)
    {
        l4_layer_data = (const char *)handler->ptr_pkt_start + l4_layer_result->offset;
        set_addr_tuple4(l4_layer_data, l4_layer_result->type, addr);
    }

    if (l3_layer_result && l4_layer_result)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

// return  0 : success
// return -1 : error
int raw_packet_parser_get_most_inner_address(struct raw_pkt_parser *handler, struct addr_tuple4 *addr)
{
    const char *l3_layer_data = NULL;
    struct layer_results *results = &handler->results;

    // search L3 layer in reverse order
    for (int8_t i = results->layers_used - 1; i >= 0; i--)
    {
        const struct layer_result *layer = &results->layers[i];
        enum layer_type type = layer->type;

        //LOG_DEBUG("%s: find most inner address, pkt_trace_id: %lu, layer[%d/%d]: %s", LOG_TAG_RAWPKT, handler->pkt_trace_id, i, results->layers_size, layer_type2str(type));
        if (type & LAYER_TYPE_L3)
        {
            l3_layer_data = (const char *)handler->ptr_pkt_start + layer->offset;
            set_addr_tuple4(l3_layer_data, type, addr);
            return 0;
        }
    }

    return -1;
}

// return  0 : success
// return -1 : error
int raw_packet_parser_get_most_outer_address(struct raw_pkt_parser *handler, struct addr_tuple4 *addr)
{
    const char *l3_layer_data = NULL;
    struct layer_results *results = &handler->results;

    // search L3 layer in order
    for (int8_t i = 0; i <= results->layers_used - 1; i++)
    {
        const struct layer_result *layer = &results->layers[i];
        enum layer_type type = layer->type;

        //LOG_DEBUG("%s: find most outer address, pkt_trace_id: %lu, layer[%d/%d]: %s", LOG_TAG_RAWPKT, handler->pkt_trace_id, i, results->layers_size, layer_type2str(type));
        if (type & LAYER_TYPE_L3)
        {
            l3_layer_data = (const char *)handler->ptr_pkt_start + layer->offset;
            set_addr_tuple4(l3_layer_data, type, addr);
            return 0;
        }
    }

    return -1;
}

uint64_t raw_packet_parser_get_hash_value(struct raw_pkt_parser *handler, enum ldbc_method method, int dir_is_internal)
{
    uint64_t temp = 0;
    uint64_t hash_value = 1;

    int inner_addr_len = 0;
    int outer_addr_len = 0;
    const char *inner_src_addr = NULL;
    const char *inner_dst_addr = NULL;
    const char *outer_src_addr = NULL;
    const char *outer_dst_addr = NULL;

    struct addr_tuple4 inner_addr;
    struct addr_tuple4 outer_addr;
    memset(&inner_addr, 0, sizeof(inner_addr));
    memset(&outer_addr, 0, sizeof(outer_addr));

    if (handler == NULL)
    {
        return hash_value;
    }

    if (raw_packet_parser_get_most_inner_address(handler, &inner_addr) == -1)
    {
        return hash_value;
    }

    if (raw_packet_parser_get_most_outer_address(handler, &outer_addr) == -1)
    {
        return hash_value;
    }

    if (inner_addr.addr_type == ADDR_TUPLE4_TYPE_V4)
    {
        inner_src_addr = (const char *)&(inner_addr.addr_v4.src_addr);
        inner_dst_addr = (const char *)&(inner_addr.addr_v4.dst_addr);
        inner_addr_len = sizeof(inner_addr.addr_v4.dst_addr);
    }
    else
    {
        inner_src_addr = (const char *)&(inner_addr.addr_v6.src_addr);
        inner_dst_addr = (const char *)&(inner_addr.addr_v6.dst_addr);
        inner_addr_len = sizeof(inner_addr.addr_v6.dst_addr);
    }

    if (outer_addr.addr_type == ADDR_TUPLE4_TYPE_V4)
    {
        outer_src_addr = (const char *)&(outer_addr.addr_v4.src_addr);
        outer_dst_addr = (const char *)&(outer_addr.addr_v4.dst_addr);
        outer_addr_len = sizeof(outer_addr.addr_v4.dst_addr);
    }
    else
    {
        outer_src_addr = (const char *)&(outer_addr.addr_v6.src_addr);
        outer_dst_addr = (const char *)&(outer_addr.addr_v6.dst_addr);
        outer_addr_len = sizeof(outer_addr.addr_v6.dst_addr);
    }

    switch (method)
    {
    case LDBC_METHOD_HASH_INT_IP:
        if (dir_is_internal)
        {
            // outer src ip
            HASH_VALUE(outer_src_addr, outer_addr_len, hash_value);
        }
        else
        {
            // outer dst ip
            HASH_VALUE(outer_dst_addr, outer_addr_len, hash_value);
        }
        break;
    case LDBC_METHOD_HASH_EXT_IP:
        if (dir_is_internal)
        {
            // outer dst ip
            HASH_VALUE(outer_dst_addr, outer_addr_len, hash_value);
        }
        else
        {
            // outer src ip
            HASH_VALUE(outer_src_addr, outer_addr_len, hash_value);
        }
        break;
    case LDBC_METHOD_HASH_INT_IP_AND_EXT_IP:
        // outer dst ip ^ outer src ip
        HASH_VALUE(outer_src_addr, outer_addr_len, hash_value);
        HASH_VALUE(outer_dst_addr, outer_addr_len, temp);
        hash_value = hash_value ^ temp;
        break;
    case LDBC_METHOD_HASH_INNERMOST_INT_IP:
        if (dir_is_internal)
        {
            // innner src ip
            HASH_VALUE(inner_src_addr, inner_addr_len, hash_value);
        }
        else
        {
            // innner dst ip
            HASH_VALUE(inner_dst_addr, inner_addr_len, hash_value);
        }
        break;
    case LDBC_METHOD_HASH_INNERMOST_EXT_IP:
        if (dir_is_internal)
        {
            // innner dst ip
            HASH_VALUE(inner_dst_addr, inner_addr_len, hash_value);
        }
        else
        {
            // innner src ip
            HASH_VALUE(inner_src_addr, inner_addr_len, hash_value);
        }
        break;
    default:
        return hash_value;
    }

    //char *inner_addr_str = addr_tuple4_to_str(&inner_addr);
    //char *outer_addr_str = addr_tuple4_to_str(&outer_addr);
    //LOG_DEBUG("%s: pkt_trace_id: %lu, outer_addr: %s, inner_addr: %s, is_internal: %d, hash_method: %s, hash_value: %lu",
    //          LOG_TAG_RAWPKT, handler->pkt_trace_id, outer_addr_str, inner_addr_str, dir_is_internal, ldbc_method_to_string(method), hash_value);
    //free(inner_addr_str);
    //free(outer_addr_str);

    return hash_value;
}

/******************************************************************************
 * Private API
 ******************************************************************************/

// return       0 : success
// return -ENOMEM : error
static int raw_packet_parser_push(struct raw_pkt_parser *handler, enum layer_type type, uint16_t offset)
{
    struct layer_results *result = &handler->results;

    if (result->layers_used >= result->layers_size)
    {
        return -ENOMEM;
    }

    result->layers[result->layers_used].offset = offset;
    result->layers[result->layers_used].type = type;
    result->layers_used++;

    return 0;
}

// return PARSE_STATUS_CONTINUE
// return PARSE_STATUS_STOP
static enum parse_status raw_packet_parser_status(struct raw_pkt_parser *handler, const void *data, enum layer_type this_type)
{
    /*
     * only when this_type & handler->expect_type is true,
     * the information of the current layer will be recorded in results.
     */
    if (!(this_type & handler->expect_type))
    {
        return PARSE_STATUS_CONTINUE;
    }

    uint16_t offset = (uintptr_t)data - (uintptr_t)(handler->ptr_pkt_start);
    if (raw_packet_parser_push(handler, this_type, offset) < 0)
    {
        return PARSE_STATUS_STOP;
    }
    else
    {
        return PARSE_STATUS_CONTINUE;
    }
}

static const char *ldbc_method_to_string(enum ldbc_method ldbc_method)
{
    switch (ldbc_method)
    {
    case LDBC_METHOD_HASH_INT_IP:
        return "outter_internal_ip";
    case LDBC_METHOD_HASH_EXT_IP:
        return "outter_external_ip";
    case LDBC_METHOD_HASH_INT_IP_AND_EXT_IP:
        return "outter_internal_ip_and_external_ip";
    case LDBC_METHOD_HASH_INNERMOST_INT_IP:
        return "inner_internal_ip";
    case LDBC_METHOD_HASH_INNERMOST_EXT_IP:
        return "inner_external_ip";
    default:
        return "unknown";
    }
}

static void set_addr_tuple4(const void *data, enum layer_type layer_type, struct addr_tuple4 *addr)
{
    const struct tcphdr *tcp_hdr = NULL;
    const struct udp_hdr *udp_hdr = NULL;
    const struct ip *ipv4_hdr = NULL;
    const struct ip6_hdr *ipv6_hdr = NULL;

    switch (layer_type)
    {
    case LAYER_TYPE_TCP:
        tcp_hdr = (const struct tcphdr *)data;
        addr->src_port = tcp_hdr->th_sport;
        addr->dst_port = tcp_hdr->th_dport;
        break;
    case LAYER_TYPE_UDP:
        udp_hdr = (const struct udp_hdr *)data;
        addr->src_port = udp_hdr->uh_sport;
        addr->dst_port = udp_hdr->uh_dport;
        break;
    case LAYER_TYPE_IPV4:
        ipv4_hdr = (const struct ip *)data;
        addr->addr_type = ADDR_TUPLE4_TYPE_V4;
        addr->addr_v4.src_addr = ipv4_hdr->ip_src;
        addr->addr_v4.dst_addr = ipv4_hdr->ip_dst;
        break;
    case LAYER_TYPE_IPV6:
        ipv6_hdr = (const struct ip6_hdr *)data;
        addr->addr_type = ADDR_TUPLE4_TYPE_V6;
        memcpy(&addr->addr_v6.src_addr, &ipv6_hdr->ip6_src, sizeof(addr->addr_v6.src_addr));
        memcpy(&addr->addr_v6.dst_addr, &ipv6_hdr->ip6_dst, sizeof(addr->addr_v6.dst_addr));
        break;
    default:
        break;
    }
}

static const char *layer_type2str(enum layer_type this_type)
{
    switch (this_type)
    {
    case LAYER_TYPE_ETHER:
        return "ETHER";
    case LAYER_TYPE_PPP:
        return "PPP";
    case LAYER_TYPE_HDLC:
        return "HDLC";
    case LAYER_TYPE_VLAN:
        return "VLAN";
    case LAYER_TYPE_PPPOE:
        return "PPPOE";
    case LAYER_TYPE_MPLS:
        return "MPLS";
    case LAYER_TYPE_IPV4:
        return "IPV4";
    case LAYER_TYPE_IPV6:
        return "IPV6";
    case LAYER_TYPE_UDP:
        return "UDP";
    case LAYER_TYPE_TCP:
        return "TCP";
    case LAYER_TYPE_G_VXLAN:
        return "G_VXLAN";
    case LAYER_TYPE_GTPV1_U:
        return "GTPV1_U";
    default:
        return "UNKNOWN";
    }
}

// FROM SAPP
static uint16_t parse_gtphdr_len(const struct gtp_hdr *gtph)
{
    const unsigned char *p_ext_hdr = (unsigned char *)gtph + sizeof(struct gtp_hdr);
    unsigned char next_hdr_type;
    unsigned char this_ext_field_cont_len;

    // v0 太古老已废弃，目前仅支持 GTPv1 版本
    if (((gtph->flags & GTP_HDR_VER_MASK) >> 5) != 1)
    {
        return -1;
    }

    if (gtph->flags & (GTP_HDR_FLAG_SEQ_NUM | GTP_HDR_FLAG_N_PDU | GTP_HDR_FLAG_NEXT_EXT_HDR))
    {
        // skip seq field (2 bytes)
        p_ext_hdr += 2;

        // skip N-PDU field (1 byte)
        p_ext_hdr++;

        // 解析 GTP 扩展头部字段，参考 wireshark 源码 packet-gtp.c->dissect_gtp_common()
        next_hdr_type = *p_ext_hdr;
        if (gtph->flags & GTP_HDR_FLAG_NEXT_EXT_HDR)
        {
            while (next_hdr_type != 0)
            {
                // 指向长度字段, 以4个字节为单位
                p_ext_hdr++;
                this_ext_field_cont_len = *p_ext_hdr * 4 - 2;

                // 指向数据部分第一个字节
                p_ext_hdr++;
                p_ext_hdr += this_ext_field_cont_len;

                // 指向下一个头部字段
                next_hdr_type = *p_ext_hdr;
                p_ext_hdr++;
            }
        }
        else
        {
            p_ext_hdr++;
        }
    }

    return (char *)p_ext_hdr - (char *)gtph;
}

static const void *parse_ether(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct ethhdr))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    struct ethhdr *hdr = (struct ethhdr *)data;
    uint16_t next_proto = ntohs(hdr->h_proto);
    uint16_t hdr_len = sizeof(struct ethhdr);
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    switch (next_proto)
    {
    case ETH_P_8021Q:
        // TESTED
        return parse_vlan8021q(handler, data_next_layer, data_next_length, LAYER_TYPE_VLAN);
    case ETH_P_8021AD:
        // TODO
        return parse_ether(handler, data_next_layer, data_next_length, LAYER_TYPE_ETHER);
    case ETH_P_IP:
        // TESTED
        return parse_ipv4(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV4);
    case ETH_P_IPV6:
        // TESTED
        return parse_ipv6(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV6);
    case ETH_P_PPP_SES:
        // TODO
        return parse_pppoe_ses(handler, data_next_layer, data_next_length, LAYER_TYPE_PPPOE);
    case ETH_P_MPLS_UC:
        // TESTED
        return parse_mpls(handler, data_next_layer, data_next_length, LAYER_TYPE_MPLS);
    case ETH_MARSIO_CHECK:
        return data_next_layer;
    default:
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, stop parse next protocol %d", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), next_proto);
        return data_next_layer;
    }
}

static const void *parse_ipv4(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct ip))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    struct ip *hdr = (struct ip *)data;
    uint16_t next_proto = hdr->ip_p;
    uint16_t hdr_len = (hdr->ip_hl & 0xf) * 4u;
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    switch (next_proto)
    {
    case IPPROTO_TCP:
        // TESTED
        return parse_tcp(handler, data_next_layer, data_next_length, LAYER_TYPE_TCP);
    case IPPROTO_UDP:
        // TESTED
        return parse_udp(handler, data_next_layer, data_next_length, LAYER_TYPE_UDP);
    case IPPROTO_IPIP:
        // TESTED
        return parse_ipv4(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV4);
    case IPPROTO_IPV6:
        // TESTED
        return parse_ipv6(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV6);
    default:
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, stop parse next protocol %d", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), next_proto);
        return data_next_layer;
    }
}

static const void *parse_ipv6(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct ip6_hdr))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    struct ip6_hdr *hdr = (struct ip6_hdr *)data;
    uint16_t next_proto = hdr->ip6_nxt;
    uint16_t hdr_len = sizeof(struct ip6_hdr);
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    switch (next_proto)
    {
    case IPPROTO_TCP:
        // TESTED
        return parse_tcp(handler, data_next_layer, data_next_length, LAYER_TYPE_TCP);
    case IPPROTO_UDP:
        // TESTED
        return parse_udp(handler, data_next_layer, data_next_length, LAYER_TYPE_UDP);
    case IPPROTO_IPIP:
        // TESTED
        return parse_ipv4(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV4);
    case IPPROTO_IPV6:
        // TESTED
        return parse_ipv6(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV6);
    default:
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, stop parse next protocol %d", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), next_proto);
        return data_next_layer;
    }
}

static const void *parse_tcp(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct tcphdr))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    struct tcphdr *hdr = (struct tcphdr *)data;
    uint16_t hdr_len = hdr->th_off << 2;
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);

    return data_next_layer;
}

static const void *parse_udp(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct udp_hdr))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    struct udp_hdr *hdr = (struct udp_hdr *)data;
    uint16_t hdr_len = sizeof(struct udp_hdr);
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    switch (ntohs(hdr->uh_dport))
    {
    // VXLAN_DPORT
    case 4789:
        // TESTED
        return parse_vxlan(handler, data_next_layer, data_next_length, LAYER_TYPE_G_VXLAN);
    // GTP1U_PORT
    case 2152:
        // TESTED
        return parse_gtpv1_u(handler, data_next_layer, data_next_length, LAYER_TYPE_GTPV1_U);
    default:
        return data_next_layer;
    }
}

static const void *parse_pppoe_ses(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < 8)
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    uint16_t next_proto = *((uint16_t *)data + 3);
    uint16_t hdr_len = 8;
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    switch (next_proto)
    {
        // PPPOE_TYPE_IPV4
    case 0x2100:
        // TESTED
        return parse_ipv4(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV4);
        // PPPOE_TYPE_IPV6
    case 0x5700:
        // TODO
        return parse_ipv6(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV6);
    default:
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, stop parse next protocol %d", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), next_proto);
        return data_next_layer;
    }
}

static const void *parse_vxlan(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct vxlan_hdr))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return NULL;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    //struct vxlan_hdr *vxlan_hdr = (struct vxlan_hdr *)data;
    uint16_t hdr_len = sizeof(struct vxlan_hdr);
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    // TESTED
    return parse_ether(handler, data_next_layer, data_next_length, LAYER_TYPE_ETHER);
}

static const void *parse_vlan8021q(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct vlan_hdr))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return NULL;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    struct vlan_hdr *hdr = (struct vlan_hdr *)data;
    uint16_t next_proto = ntohs(hdr->protocol);
    uint16_t hdr_len = sizeof(struct vlan_hdr);
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    switch (next_proto)
    {
    case ETH_P_8021Q:
        // TESTED
        return parse_vlan8021q(handler, data_next_layer, data_next_length, LAYER_TYPE_VLAN);
    case ETH_P_IP:
        // TESTED
        return parse_ipv4(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV4);
    case ETH_P_IPV6:
        // TODO
        return parse_ipv6(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV6);
    case ETH_P_PPP_SES:
        // TESTED
        return parse_pppoe_ses(handler, data_next_layer, data_next_length, LAYER_TYPE_PPPOE);
    case ETH_P_MPLS_UC:
        // TODO
        return parse_mpls(handler, data_next_layer, data_next_length, LAYER_TYPE_MPLS);
    default:
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, stop parse next protocol %d", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), next_proto);
        return data_next_layer;
    }
}

static const void *parse_gtpv1_u(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < sizeof(struct gtp_hdr))
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return NULL;
    }

    uint16_t hdr_len = parse_gtphdr_len((const struct gtp_hdr *)data);
    if (hdr_len < 0)
    {
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

    uint8_t next_proto = (((const uint8_t *)((const char *)data + hdr_len))[0]) >> 4;
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
    switch (next_proto)
    {
    case 4:
        // TESTED
        return parse_ipv4(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV4);
    case 6:
        // TESTED
        return parse_ipv6(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV6);
    default:
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, stop parse next protocol %d", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), next_proto);
        return data_next_layer;
    }
}

static const void *parse_mpls(struct raw_pkt_parser *handler, const void *data, size_t length, enum layer_type this_type)
{
    if (length < 4)
    {
        LOG_ERROR("%s: pkt_trace_id: %lu, this_layer: %s, err: data not enough", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type));
        return data;
    }

    if (raw_packet_parser_status(handler, data, this_type) == PARSE_STATUS_STOP)
    {
        return data;
    }

#define MPLS_LABEL_MASK (0xFFFFF000)
#define MPLS_EXP_MASK (0x00000E00)
#define MPLS_BLS_MASK (0x00000100)
#define MPLS_TTL_MASK (0x000000FF)

    /*
     * MPLS Format
     * 0                   1                   2                   3
     * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |                Label                  | Exp |S|       TTL     |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * Label : Label Value      20 bits
     * Exp   : Experimental Use  3 bits
     * S     : Bottom of Stack   1 bit
     * TTL   : Time to Live      8 bits
     */

    uint32_t *hdr = (uint32_t *)data;
    // unsigned int mpls_label = (ntohl(*hdr) & MPLS_LABEL_MASK) >> 12;
    // unsigned int mpls_exp = (ntohl(*hdr) & MPLS_EXP_MASK) >> 9;
    unsigned int mpls_bls = (ntohl(*hdr) & MPLS_BLS_MASK) >> 8;
    // unsigned int mpls_ttl = (ntohl(*hdr) & MPLS_TTL_MASK);

    uint16_t hdr_len = 4;
    const void *data_next_layer = (const char *)data + hdr_len;
    size_t data_next_length = length - hdr_len;

    if (mpls_bls == 1)
    {
        uint8_t ip_version = (((uint8_t *)data_next_layer)[0]) >> 4;
        if (ip_version == 0)
        {
            /*
             * PW Ethernet Control Word
             * 0                   1                   2                   3
             * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |0 0 0 0|   Reserved            |       Sequence Number         |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * Reference: https://tools.ietf.org/html/rfc4448
             */
            data_next_layer = (const char *)data_next_layer + 4;
            data_next_length = data_next_length - 4;

            //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
            // TESTED
            return parse_ether(handler, data_next_layer, data_next_length, LAYER_TYPE_ETHER);
        }
        else if (ip_version == 4)
        {
            //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
            // TESTED
            return parse_ipv4(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV4);
        }
        else if (ip_version == 6)
        {
            //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
            // TODO
            return parse_ipv6(handler, data_next_layer, data_next_length, LAYER_TYPE_IPV6);
        }
        else
        {
            //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
            // TODO
            return parse_ether(handler, data_next_layer, data_next_length, LAYER_TYPE_ETHER);
        }
    }
    else
    {
        //LOG_DEBUG("%s: pkt_trace_id: %lu, this_layer: %s, payload_len: [%lu/%lu]", LOG_TAG_RAWPKT, handler->pkt_trace_id, layer_type2str(this_type), data_next_length, length);
        // TESTED
        return parse_mpls(handler, data_next_layer, data_next_length, LAYER_TYPE_MPLS);
    }
}

static unsigned short ip_header_checksum_calculate(struct iphdr *iph)
{
    unsigned short *buffer = (unsigned short *)iph;
    unsigned int length = iph->ihl * 4;
    unsigned int sum = 0;

    while (length > 1)
    {
        sum += *buffer++;
        length -= 2;
    }

    if (length > 0)
    {
        sum += ((*buffer) & htons(0xFF00));
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

void raw_packet_set_dscp(struct ethhdr *eth_hdr, uint8_t dscp)
{
    if (eth_hdr->h_proto == htons(ETH_P_IP))
    {
        struct iphdr *ip_hdr = (struct iphdr *)(eth_hdr + 1);
        ip_hdr->tos = (ip_hdr->tos & 0x03) | (dscp << 2);
        //recalculate checksum
        ip_hdr->check = 0;
        ip_hdr->check = ip_header_checksum_calculate(ip_hdr);
    } 
    else if (eth_hdr->h_proto == htons(ETH_P_IPV6))
    {
        struct ip6_hdr *ip6_hdr = (struct ip6_hdr *)(eth_hdr + 1);
        unsigned int flow = ntohl(ip6_hdr->ip6_flow);
        flow &= 0xF03FFFFF;
        flow |= (dscp << 22);
        ip6_hdr->ip6_flow = htonl(flow);
    }

    return;
}