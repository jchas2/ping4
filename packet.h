#ifndef PING4_PACKET_H
#define PING4_PACKET_H

#include <stdint.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "ping4.h"
#include "socket.h"

/* Return codes for packet_recv */
#define PING4_RECV_OK       0   /* valid echo reply received */
#define PING4_RECV_TIMEOUT  1   /* no packet within timeout */
#define PING4_RECV_FILTERED 2   /* packet received but not ours */
#define PING4_RECV_ERROR    3   /* fatal recv error */
#define PING4_RECV_BADCKSUM 4   /* ICMP checksum verification failed */

/*
  Minimal ICMP echo header — 8 bytes, used for both building and parsing.
  Avoids the platform-specific layout of struct icmp's union.
 */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} ping4_icmp_hdr;

typedef struct {
    struct sockaddr_in from;
    double   rtt_ms;
    int      ttl;
    uint16_t seq;
    int      icmp_type;
    int      icmp_code;
} recv_result;

uint16_t packet_checksum(const void* buf, int len);
int      packet_send(run_state* state, socket_st* sock);
int      packet_recv(run_state* state, socket_st* sock, recv_result* result);

#endif //PING4_PACKET_H
