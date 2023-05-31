#ifndef _ADDR_TUPLE4_H
#define _ADDR_TUPLE4_H

#ifdef __cpluscplus
extern "C"
{
#endif

#include <netinet/in.h>

    enum addr_tuple4_type
    {
        ADDR_TUPLE4_TYPE_V4,
        ADDR_TUPLE4_TYPE_V6,
    };

    struct addr_v4
    {
        struct in_addr src_addr; /* network order */
        struct in_addr dst_addr; /* network order */
    };

    struct addr_v6
    {
        struct in6_addr src_addr; /* network order */
        struct in6_addr dst_addr; /* network order */
    };

    struct addr_tuple4
    {
        enum addr_tuple4_type addr_type;
        in_port_t src_port; /* network order */
        in_port_t dst_port; /* network order */
        union
        {
            struct addr_v4 addr_v4;
            struct addr_v6 addr_v6;
        };
    };

#define INIT_ADDR_V4(name, src_addr_str, src_port_num, dst_addr_str, dst_port_num) \
    struct addr_tuple4 name;                                                       \
    memset(&name, 0, sizeof(name));                                                \
    (name).addr_type = ADDR_TUPLE4_TYPE_V4;                                        \
    (name).src_port = htons((src_port_num));                                       \
    (name).dst_port = htons((dst_port_num));                                       \
    inet_pton(AF_INET, (src_addr_str), &(name).addr_v4.src_addr);                  \
    inet_pton(AF_INET, (dst_addr_str), &(name).addr_v4.dst_addr);

#define INIT_ADDR_V6(name, src_addr_str, src_port_num, dst_addr_str, dst_port_num) \
    struct addr_tuple4 name;                                                       \
    memset(&name, 0, sizeof(name));                                                \
    (name).addr_type = ADDR_TUPLE4_TYPE_V6;                                        \
    (name).src_port = htons((src_port_num));                                       \
    (name).dst_port = htons((dst_port_num));                                       \
    inet_pton(AF_INET6, (src_addr_str), &(name).addr_v6.src_addr);                 \
    inet_pton(AF_INET6, (dst_addr_str), &(name).addr_v6.dst_addr);

    char *addr_tuple4_to_str(const struct addr_tuple4 *addr);
    void addr_tuple4_reverse(const struct addr_tuple4 *orin, struct addr_tuple4 *out);
    char *addr_src_ip_to_str(const struct addr_tuple4 *addr);

#ifdef __cpluscplus
}
#endif

#endif
