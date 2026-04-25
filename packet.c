#include "packet.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include "packet.h"
#include "socket.h"
#include "utils.h"

uint16_t packet_checksum(const void* buf, int len)
{
    const uint16_t* data = (const uint16_t*)buf;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *data++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(const uint8_t*)data;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return (uint16_t)~sum;
}

int packet_send(run_state* state, socket_st* sock)
{
    /*
     * The packet is split across two iovec segments:
     *   iov[0] — ICMP header (8 bytes)
     *   iov[1] — data payload (state->datalen bytes)
     *
     * Payload layout:
     *   [struct timespec (send timestamp)][repeating pattern bytes]
     */
    static uint8_t staging[PING4_ICMP_HDR_SIZE + PING4_ICMP_MAX_DATALEN];

    ping4_icmp_hdr hdr = {
        .type     = ICMP_ECHO,
        .code     = 0,
        .checksum = 0,
        .id       = htons(state->ident),
        .seq      = htons(state->seq),
    };

    uint8_t* data = staging + PING4_ICMP_HDR_SIZE;

    /* Embed send timestamp at start of payload for RTT calculation on receipt. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    memcpy(data, &now, sizeof now);

    /* Fill remainder of payload with a repeating pattern. */
    for (int i = (int)sizeof now; i < state->datalen; i++) {
        data[i] = (uint8_t)(i & 0xff);
    }

    /* Compute checksum over contiguous header + data in staging buffer. */
    memcpy(staging, &hdr, PING4_ICMP_HDR_SIZE);
    hdr.checksum = packet_checksum(staging, PING4_ICMP_HDR_SIZE + state->datalen);

    struct iovec iov[2] = {
        { .iov_base = &hdr, .iov_len = sizeof hdr             },
        { .iov_base = data, .iov_len = (size_t)state->datalen }
    };

    struct msghdr msg = {
        .msg_name    = &state->target_addr,
        .msg_namelen = sizeof state->target_addr,
        .msg_iov     = iov,
        .msg_iovlen  = 2,
    };

    if (sendmsg(sock->fd, &msg, 0) == -1) {
        return -1;
    }

    state->packets_sent++;
    state->seq++;

    return 0;
}

/*
 * Receive one ICMP packet using recvmsg + iovec.
 *
 * Ancillary data (msg_control) is used to extract the received TTL via
 * IP_RECVTTL, which must already be enabled on the socket before calling.
 *
 * On macOS, both SOCK_RAW and SOCK_DGRAM/IPPROTO_ICMP deliver the full IP
 * packet including the IP header. The IP header presence is detected by
 * inspecting the version nibble of the first byte rather than the socket type.
 *
 * Returns one of: PINGC_RECV_OK, PINGC_RECV_TIMEOUT,
 *                 PINGC_RECV_FILTERED, PINGC_RECV_ERROR.
 */
int packet_recv(run_state* state, socket_st* sock, recv_result* result)
{
    static uint8_t recv_buf[IP_MAXPACKET];
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    struct sockaddr_in from = { 0 };

    struct iovec iov = {
        .iov_base = recv_buf,
        .iov_len  = sizeof recv_buf
    };

    struct msghdr msg = {
        .msg_name       = &from,
        .msg_namelen    = sizeof from,
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = ctrl_buf,
        .msg_controllen = sizeof ctrl_buf
    };

    ssize_t n = recvmsg(sock->fd, &msg, 0);

    if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return PING4_RECV_TIMEOUT;
        }
        return PING4_RECV_ERROR;
    }

    /*
     * Locate the ICMP header by detecting whether an IP header is present.
     *
     * On Linux, SOCK_DGRAM/IPPROTO_ICMP ping sockets strip the IP header
     * before delivery. On macOS, SOCK_DGRAM/IPPROTO_ICMP includes the full
     * IP header — the same as SOCK_RAW. Checking the version nibble of the
     * first byte is more reliable than branching on sock->socktype, since
     * macOS SOCK_DGRAM behaves like SOCK_RAW in this regard.
     *
     * All ICMP types we handle have type values < 16 (high nibble = 0).
     * An IPv4 header always starts with 0x4x (high nibble = 4).
     */
    int ip_hdr_len = 0;

    if (n >= (ssize_t)sizeof(struct ip) && (recv_buf[0] >> 4) == 4) {
        const struct ip* ip_hdr = (const struct ip*)recv_buf;
        ip_hdr_len = ip_hdr->ip_hl * 4;

        if (n < ip_hdr_len + (ssize_t)PING4_ICMP_HDR_SIZE) {
            return PING4_RECV_FILTERED;
        }
    }
    else {
        if (n < (ssize_t)PING4_ICMP_HDR_SIZE) {
            return PING4_RECV_FILTERED;
        }
    }

    const ping4_icmp_hdr* icmp = (const ping4_icmp_hdr*)(recv_buf + ip_hdr_len);
    int icmp_len = (int)(n - ip_hdr_len);

    /* Verify ICMP checksum — valid packet sums to zero. */
    if (packet_checksum(icmp, icmp_len) != 0) {
        return PING4_RECV_BADCKSUM;
    }

    /* Filter to only the ICMP types we care about. */
    switch (icmp->type) {
        case ICMP_ECHOREPLY:
        case ICMP_UNREACH:
        case ICMP_SOURCEQUENCH:
        case ICMP_REDIRECT:
        case ICMP_TIMXCEED:
        case ICMP_PARAMPROB:
            break;
        default:
            return PING4_RECV_FILTERED;
    }

    /* For echo replies, validate this packet belongs to our process. */
    if (icmp->type == ICMP_ECHOREPLY && ntohs(icmp->id) != state->ident) {
        return PING4_RECV_FILTERED;
    }

    /*
     * Extract TTL. When the IP header is present in the buffer we read it
     * directly — this is always available and reliable. Fall back to the
     * IP_RECVTTL ancillary data only when the IP header was stripped (Linux
     * SOCK_DGRAM ping sockets).
     */
    result->ttl = 0;

    if (ip_hdr_len > 0) {
        result->ttl = ((const struct ip*)recv_buf)->ip_ttl;
    }
    else {
        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
            if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_RECVTTL) {
                result->ttl = *(uint8_t*)CMSG_DATA(cm);
            }
        }
    }

    result->from      = from;
    result->icmp_type = icmp->type;
    result->icmp_code = icmp->code;
    result->seq       = ntohs(icmp->seq);

    if (icmp->type == ICMP_ECHOREPLY) {
        /* Read send timestamp from payload to compute RTT. */
        const uint8_t *payload = (const uint8_t *)icmp + PING4_ICMP_HDR_SIZE;
        struct timespec sent = {0};
        struct timespec now = {0};
        memcpy(&sent, payload, sizeof sent);
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);

        result->rtt_ms = (double)(now.tv_sec  - sent.tv_sec)  * 1000.0 +
                         (double)(now.tv_nsec - sent.tv_nsec) / 1e6;

        state->packets_recv++;

        if (state->packets_recv == 1 || result->rtt_ms < state->rtt_min) {
            state->rtt_min = result->rtt_ms;
        }

        if (result->rtt_ms > state->rtt_max) {
            state->rtt_max = result->rtt_ms;
        }

        state->rtt_sum  += result->rtt_ms;
        state->rtt_sum2 += result->rtt_ms * result->rtt_ms;
    }

    return PING4_RECV_OK;
}
