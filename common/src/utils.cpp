#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/ether.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "utils.h"
#include "log.h"

/******************************************************************************
 * fixed_num_array
 ******************************************************************************/

void fixed_num_array_init(struct fixed_num_array *array)
{
    memset(array, 0, sizeof(fixed_num_array));
    array->num = 0;
    array->size = sizeof(array->elems) / sizeof(array->elems[0]);
}

void fixed_num_array_add_elem(struct fixed_num_array *array, int elem)
{
    if (array->num < array->size)
    {
        array->elems[array->num] = elem;
        array->num++;
    }
    else
    {
        LOG_ERROR("%s: fixed num array add elem too much !!!", LOG_TAG_UTILS);
    }
}

void fixed_num_array_del_elem(struct fixed_num_array *array, int elem)
{
    for (int i = 0; i < array->num; i++)
    {
        if (array->elems[i] == elem)
        {
            if (i + 1 != array->size)
            {
                memmove(&(array->elems[i]), &(array->elems[i + 1]), sizeof(array->elems[0]) * (array->num - i - 1));
            }
            i--;
            array->num--;
        }
    }
}

int fixed_num_array_is_full(struct fixed_num_array *array)
{
    if (array->num == array->size)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int fixed_num_array_count_elem(struct fixed_num_array *array)
{
    if (array)
    {
        return array->num;
    }
    else
    {
        return 0;
    }
}

int fixed_num_array_exist_elem(struct fixed_num_array *array, int elem)
{
    for (int i = 0; i < array->num; i++)
    {
        if (array->elems[i] == elem)
        {
            return 1;
        }
    }

    return 0;
}

int fixed_num_array_index_elem(struct fixed_num_array *array, int index)
{
    if (index >= array->num)
    {
        assert(0);
    }

    return array->elems[index];
}

/******************************************************************************
 * sids
 ******************************************************************************/

void sids_write_once(struct sids *dst, struct sids *src)
{
    if (dst && src)
    {
        if (dst->num == 0 && src->num > 0)
        {
            sids_copy(dst, src);
        }
    }
}

void sids_copy(struct sids *dst, struct sids *src)
{
    if (dst && src)
    {
        dst->num = src->num;
        memcpy(dst->elems, src->elems, sizeof(dst->elems[0]) * dst->num);
    }
}

/******************************************************************************
 * route_ctx
 ******************************************************************************/

void route_ctx_write_once(struct route_ctx *dst, struct route_ctx *src)
{
    if (dst && src)
    {
        if (dst->len == 0)
        {
            route_ctx_copy(dst, src);
        }
    }
}

void route_ctx_copy(struct route_ctx *dst, struct route_ctx *src)
{
    memcpy(dst->data, src->data, src->len);
    dst->len = src->len;
}

/******************************************************************************
 * throughput_metrics
 ******************************************************************************/

void throughput_metrics_inc(struct throughput_metrics *iterm, uint64_t n_pkts, uint64_t n_bytes)
{
    __atomic_fetch_add(&iterm->n_bytes, n_bytes, __ATOMIC_RELAXED);
    __atomic_fetch_add(&iterm->n_pkts, n_pkts, __ATOMIC_RELAXED);
}

/******************************************************************************
 * protocol
 ******************************************************************************/

#define CHECKSUM_CARRY(x) (x = (x >> 16) + (x & 0xffff), (~(x + (x >> 16)) & 0xffff))

static int checksum(u_int16_t *addr, int len)
{
    int sum = 0;
    int nleft = len;
    u_int16_t ans = 0;
    u_int16_t *w = addr;

    while (nleft > 1)
    {
        sum += *w++;
        nleft -= 2;
    }

    if (nleft == 1)
    {
        *(char *)(&ans) = *(char *)w;
        sum += ans;
    }

    return sum;
}

void build_udp_header(const char *l3_hdr, int l3_hdr_len, struct udp_hdr *udp_hdr, u_int16_t udp_sport, u_int16_t udp_dport, int payload_len)
{
    memset(udp_hdr, 0, sizeof(struct udp_hdr));

    int udp_hlen = sizeof(struct udp_hdr) + payload_len;

    udp_hdr->uh_sport = htons(udp_sport);
    udp_hdr->uh_dport = htons(udp_dport);

    udp_hdr->uh_ulen = htons(udp_hlen);
    udp_hdr->uh_sum = 0;

    int sum = checksum((u_int16_t *)l3_hdr, l3_hdr_len);
    sum += ntohs(IPPROTO_UDP + udp_hlen);
    sum += checksum((u_int16_t *)udp_hdr, udp_hlen);
    udp_hdr->uh_sum = CHECKSUM_CARRY(sum);
}

void build_ip_header(struct ip *ip_hdr, u_int8_t next_protocol, const char *src_addr, const char *dst_addr, uint16_t payload_len)
{
    memset(ip_hdr, 0, sizeof(struct ip));

    ip_hdr->ip_hl = 5;               /* 20 byte header */
    ip_hdr->ip_v = 4;                /* version 4 */
    ip_hdr->ip_tos = 0;              /* IP tos */
    ip_hdr->ip_id = htons(random()); /* IP ID */
    ip_hdr->ip_ttl = 80;             /* time to live */
    ip_hdr->ip_p = next_protocol;    /* transport protocol */
    ip_hdr->ip_src.s_addr = inet_addr(src_addr);
    ip_hdr->ip_dst.s_addr = inet_addr(dst_addr);
    ip_hdr->ip_len = htons(sizeof(struct ip) + payload_len); /* total length */
    ip_hdr->ip_off = htons(0);                               /* fragmentation flags */
    ip_hdr->ip_sum = 0;                                      /* do this later */

    int sum = checksum((u_int16_t *)ip_hdr, 20);
    ip_hdr->ip_sum = CHECKSUM_CARRY(sum);
}

// l3_protocol: ETH_P_IPV6/ETH_P_IP
void build_ether_header(struct ethhdr *eth_hdr, uint16_t next_protocol, const char *src_mac, const char *dst_mac)
{
    memset(eth_hdr, 0, sizeof(struct ethhdr));

    str_to_mac(src_mac, (char *)eth_hdr->h_source);
    str_to_mac(dst_mac, (char *)eth_hdr->h_dest);
    eth_hdr->h_proto = htons(next_protocol);
}

/******************************************************************************
 * device
 ******************************************************************************/

int get_ip_by_device_name(const char *dev_name, char *ip_buff)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1)
    {
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    ifr.ifr_addr.sa_family = AF_INET;
    strcpy(ifr.ifr_name, dev_name);
    if (ioctl(fd, SIOCGIFADDR, &ifr) != 0)
    {
        close(fd);
        return -1;
    }

    strcpy(ip_buff, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    close(fd);

    return 0;
}

int get_mac_by_device_name(const char *dev_name, char *mac_buff)
{
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd == -1)
    {
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    strcpy(ifr.ifr_name, dev_name);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0)
    {
        close(fd);
        return -1;
    }

    unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
    sprintf(mac_buff, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    close(fd);

    return 0;
}

int str_to_mac(const char *str, char *mac_buff)
{
    if (sscanf(str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &(mac_buff[0]), &(mac_buff[1]), &(mac_buff[2]), &(mac_buff[3]), &(mac_buff[4]), &(mac_buff[5])) == 6)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

const char *uuid_print_str(uuid_t uuid)
{
    static thread_local char uuid_str[UUID_STR_LEN] = {0};
    uuid_unparse(uuid, uuid_str);
    return uuid_str;
}