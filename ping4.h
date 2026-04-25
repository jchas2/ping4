#ifndef PING4_PING4_H
#define PING4_PING4_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdint.h>
#include <time.h>

#define PING4_VERSION        "1.0"

#define PING4_USAGE           2

#define PING4_DEF_DATALEN     (64 - 8)
#define PING4_MAX_PACKETS     10
#define PING4_DEF_INTERVAL_MS 1000
#define PING4_MIN_INTERVAL_MS 10
#define PING4_TOS_MIN_DELAY   0x20

/* IPv4 packet size - IPv4 header size - ICMP header size */
#define	PING4_MAX_PACKET       65535
#define PING4_IPV4_HDR_SIZE    20
#define PING4_ICMP_HDR_SIZE    8
#define PING4_ICMP_MAX_DATALEN (PING4_MAX_PACKET - PING4_IPV4_HDR_SIZE - PING4_ICMP_HDR_SIZE)

typedef struct {
    char     target[INET_ADDRSTRLEN];
    char     hostname[NI_MAXHOST];
    char*    device;
    long     max_packets;                 /* Max packets to transmit */
    long     packets_sent;
    long     packets_recv;
    int      interval;                    /* Interval between packets (ms) */
    int      datalen;
    int      deadline;
    int      timeout;
    int      ttl;
    uint16_t ident;                       /* ICMP identifier = getpid() & 0xffff */
    uint16_t seq;                         /* Current outgoing sequence number */
    double   rtt_min;
    double   rtt_max;
    double   rtt_sum;                     /* For average */
    double   rtt_sum2;                    /* Sum of squares for stddev */
    volatile int       exit_signalled;
    struct timespec    start_time;
    struct timespec    curr_time;
    struct sockaddr_in target_addr;       /* Target to ping */
    struct sockaddr_in source_addr;       /* Source NIC address */
    unsigned int
        opt_loop:1,
        opt_is_broadcast:1,
        opt_ttl:1,
        opt_verbose:1;
} run_state;

#endif //PING4_PING4_H
