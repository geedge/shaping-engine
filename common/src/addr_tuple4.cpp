#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "addr_tuple4.h"

char *addr_tuple4_to_str(const struct addr_tuple4 *addr)
{
    char *str_ret = NULL;

    if (addr->addr_type == ADDR_TUPLE4_TYPE_V4)
    {
        char src_addr[INET_ADDRSTRLEN] = {0};
        char dst_addr[INET_ADDRSTRLEN] = {0};
        uint16_t src_port = ntohs((uint16_t)addr->src_port);
        uint16_t dst_port = ntohs((uint16_t)addr->dst_port);
        inet_ntop(AF_INET, &addr->addr_v4.src_addr, src_addr, sizeof(src_addr));
        inet_ntop(AF_INET, &addr->addr_v4.dst_addr, dst_addr, sizeof(dst_addr));
        asprintf(&str_ret, "%s %u %s %u", src_addr, src_port, dst_addr, dst_port);
    }

    if (addr->addr_type == ADDR_TUPLE4_TYPE_V6)
    {
        char src_addr[INET6_ADDRSTRLEN] = {0};
        char dst_addr[INET6_ADDRSTRLEN] = {0};
        uint16_t src_port = ntohs((uint16_t)addr->src_port);
        uint16_t dst_port = ntohs((uint16_t)addr->dst_port);
        inet_ntop(AF_INET6, &addr->addr_v6.src_addr, src_addr, sizeof(src_addr));
        inet_ntop(AF_INET6, &addr->addr_v6.dst_addr, dst_addr, sizeof(dst_addr));
        asprintf(&str_ret, "%s %u %s %u", src_addr, src_port, dst_addr, dst_port);
    }

    return str_ret;
}

char *addr_src_ip_to_str(const struct addr_tuple4 *addr)//TODO: self test
{
    char *str_ret = NULL;

    if (addr->addr_type == ADDR_TUPLE4_TYPE_V4)
    {
        char src_addr[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &addr->addr_v4.src_addr, src_addr, sizeof(src_addr));
        asprintf(&str_ret, "%s", src_addr);
    }

    if (addr->addr_type == ADDR_TUPLE4_TYPE_V6)
    {
        char src_addr[INET6_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET6, &addr->addr_v6.src_addr, src_addr, sizeof(src_addr));
        asprintf(&str_ret, "%s", src_addr);
    }

    return str_ret;
}

void addr_tuple4_reverse(const struct addr_tuple4 *orin, struct addr_tuple4 *out)
{
    memset(out, 0, sizeof(struct addr_tuple4));

    if (orin->addr_type == ADDR_TUPLE4_TYPE_V4)
    {
        out->addr_type = ADDR_TUPLE4_TYPE_V4;
        out->addr_v4.src_addr = orin->addr_v4.dst_addr;
        out->addr_v4.dst_addr = orin->addr_v4.src_addr;
        out->src_port = orin->dst_port;
        out->dst_port = orin->src_port;
    }

    if (orin->addr_type == ADDR_TUPLE4_TYPE_V6)
    {
        out->addr_type = ADDR_TUPLE4_TYPE_V6;
        out->addr_v6.src_addr = orin->addr_v6.dst_addr;
        out->addr_v6.dst_addr = orin->addr_v6.src_addr;
        out->src_port = orin->dst_port;
        out->dst_port = orin->src_port;
    }
}