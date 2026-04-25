#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "ping4.h"
#include "output.h"
#include "packet.h"
#include "pinger.h"
#include "socket.h"
#include "utils.h"

static volatile sig_atomic_t running   = 1;
static volatile sig_atomic_t send_next = 1;

static void signal_exit(__attribute__((__unused__)) int sig)
{
    running = 0;
}

static void signal_alarm(__attribute__((__unused__)) int sig)
{
    send_next = 1;
}

static void set_signal(int signal_no, void (*handler)(int))
{
    struct sigaction sa = { 0 };
    sa.sa_handler = handler;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(signal_no, &sa, NULL)) {
        utils_error(EXIT_FAILURE, errno, "signal setup failed");
    }
}

int pinger_loop(run_state* state, socket_st* sock4)
{
    set_signal(SIGINT,  signal_exit);
    set_signal(SIGTERM, signal_exit);
    set_signal(SIGALRM, signal_alarm);

    int sec  = state->interval / 1000;
    int usec = (state->interval % 1000) * 1000;

    struct itimerval it = {
        .it_value    = { .tv_sec = sec, .tv_usec = usec },
        .it_interval = { .tv_sec = sec, .tv_usec = usec }
    };

    setitimer(ITIMER_REAL, &it, NULL);
    clock_gettime(CLOCK_MONOTONIC_RAW, &state->start_time);

    while (running) {
        if (send_next) {
            send_next = 0;

            if (state->max_packets > 0 && state->packets_sent >= state->max_packets) {
                running = 0;
                break;
            }

            if (packet_send(state, sock4) == -1) {
                if (errno != EINTR) {
                    utils_error(0, errno, "send error");
                }
            }
        }

        recv_result result = { 0 };
        int ret = packet_recv(state, sock4, &result);

        switch (ret) {
            case PING4_RECV_OK:
                print_reply(state, &result);
                break;
            case PING4_RECV_TIMEOUT:
            case PING4_RECV_FILTERED:
                break;
            case PING4_RECV_BADCKSUM:
                if (state->opt_verbose) {
                    utils_error(0, 0, "bad ICMP checksum, packet discarded");
                }
                break;
            case PING4_RECV_ERROR:
                utils_error(0, errno, "recv error");
                break;
        }

        if (state->deadline > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC_RAW, &now);

            if ((now.tv_sec - state->start_time.tv_sec) >= state->deadline) {
                running = 0;
            }
        }
    }

    struct itimerval zero = { 0 };
    setitimer(ITIMER_REAL, &zero, NULL);

    print_statistics(state);
    return 0;
}

int pinger_run(run_state* state, struct addrinfo* ai, socket_st* sock4)
{
    struct sockaddr_in temp_dest;

    state->ident = (uint16_t)(getpid() & 0xffff);
    state->seq   = 0;

    /* Resolve hostname if a literal IPv4 address was entered. */
    if (inet_aton(state->target, &state->target_addr.sin_addr) == 1) {
        state->target_addr.sin_family = AF_INET;
        state->target_addr.sin_port   = 0;
        state->hostname[0] = '\0';

        if (utils_is_broadcast_addr(&state->target_addr.sin_addr) == 1) {
            state->opt_is_broadcast = 1;
            strncpy(state->hostname, state->target, sizeof state->hostname - 1);
            state->hostname[sizeof state->hostname - 1] = '\0';
        }
        else if (utils_reverse_dns_lookup(state->target, state->hostname, sizeof state->hostname) == -1) {
            strncpy(state->hostname, state->target, sizeof state->hostname - 1);
            state->hostname[sizeof state->hostname - 1] = '\0';
        }
    }
    else {
        memcpy(&state->target_addr, ai->ai_addr, sizeof state->target_addr);
        memset(state->hostname, 0, sizeof state->hostname);

        if (ai->ai_canonname) {
            strncpy(state->hostname, ai->ai_canonname, sizeof state->hostname - 1);
        }
        else {
            strncpy(state->hostname, state->target, sizeof state->hostname - 1);
        }

        state->hostname[sizeof state->hostname - 1] = '\0';
        struct sockaddr_in *addr = (struct sockaddr_in *)ai->ai_addr;

        if (inet_ntop(AF_INET, &addr->sin_addr, state->target, sizeof state->target) == NULL) {
            utils_error(EXIT_FAILURE, errno, "inet_ntop");
        }
    }

    /* Use a probe UDP socket to discover which source address the kernel
     * would use to route to the target, then bind the ICMP socket to it. */
    if (state->source_addr.sin_addr.s_addr == 0) {
        temp_dest.sin_addr   = state->target_addr.sin_addr;
        temp_dest.sin_len    = state->target_addr.sin_len;
        temp_dest.sin_family = AF_INET;
        temp_dest.sin_port   = htons(1025);

        socket_st probe_sock = {
            .fd       = -1,
            .domain   = AF_INET,
            .socktype = SOCK_DGRAM,
            .protocol = IPPROTO_UDP
        };

        socket_create(state, &probe_sock);

        if (probe_sock.fd == -1) {
            utils_error(EXIT_FAILURE, 0, "failed to create probe socket");
        }

        if (socket_connect(&probe_sock, &temp_dest, sizeof temp_dest) == -1) {
            utils_error(EXIT_FAILURE, errno, "probe connect error");
        }

        socklen_t src_len = sizeof state->source_addr;

        if (socket_name(&probe_sock, &state->source_addr, &src_len) == -1) {
            utils_error(EXIT_FAILURE, errno, "getsockname");
        }

        state->source_addr.sin_port = 0;
        socket_close(&probe_sock);
    }

    if (socket_bind(sock4, &state->source_addr, sizeof state->source_addr) == -1) {
        utils_error(EXIT_FAILURE, errno, "bind error");
    }

    /*
     * On macOS, SOCK_DGRAM/IPPROTO_ICMP sockets use the socket's local port
     * as the ICMP identifier in both outgoing and incoming packets — the kernel
     * overwrites whatever id we put in the packet. After bind() with port=0,
     * getsockname() reveals the port the kernel actually assigned, which is
     * the ICMP id the replies will carry. Update state->ident accordingly so
     * the recv filter matches correctly.
     *
     * For SOCK_RAW, we control the id ourselves so no update is needed.
     */
    if (sock4->socktype == SOCK_DGRAM) {
        struct sockaddr_in bound_addr;
        socklen_t bound_len = sizeof bound_addr;

        if (socket_name(sock4, &bound_addr, &bound_len) == 0) {
            state->ident = ntohs(bound_addr.sin_port);
        }
    }

    /* IP_RECVTTL — deliver received TTL as ancillary data via recvmsg. */
    int val = 1;
    if (socket_set_option(sock4, IPPROTO_IP, IP_RECVTTL, &val, sizeof val)) {
        utils_error(0, errno, "WARNING: setsockopt IP_RECVTTL failed");
    }

    /* Increase send and receive buffers to handle broadcast reply bursts. */
    val = 1 << 16;
    const struct { int opt; const char *name; } buf_opts[] = {
        { SO_SNDBUF, "SO_SNDBUF" },
        { SO_RCVBUF, "SO_RCVBUF" },
    };

    for (int i = 0; i < (int)(sizeof buf_opts / sizeof buf_opts[0]); i++) {
        if (socket_set_option(sock4, SOL_SOCKET, buf_opts[i].opt, &val, sizeof val)) {
            utils_error(0, errno, "WARNING: setsockopt %s failed", buf_opts[i].name);
        }
    }

    if (state->opt_is_broadcast) {
        int on = 1;

        if (socket_set_option(sock4, SOL_SOCKET, SO_BROADCAST, &on, sizeof on)) {
            utils_error(EXIT_FAILURE, errno, "cannot set broadcast");
        }
    }

    if (state->opt_ttl) {
        unsigned char ttl = (unsigned char)state->ttl;

        if (socket_set_option(sock4, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl)) {
            utils_error(EXIT_FAILURE, errno, "cannot set multicast ttl");
        }
        if (socket_set_option(sock4, IPPROTO_IP, IP_TTL, &state->ttl, sizeof state->ttl)) {
            utils_error(EXIT_FAILURE, errno, "cannot set unicast ttl");
        }
    }

    int interval = state->interval <= PING4_MIN_INTERVAL_MS ? PING4_MIN_INTERVAL_MS : state->interval;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

    if (state->interval < 1000) {
        tv.tv_usec = 1000 * interval;
    }
    else {
        tv.tv_sec = interval / 1000;
    }

    if (socket_set_option(sock4, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv)) {
        utils_error(EXIT_FAILURE, errno, "cannot set send timeout");
    }

    tv.tv_sec  = interval / 1000;
    tv.tv_usec = 1000 * (interval % 1000);

    if (socket_set_option(sock4, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv)) {
        utils_error(EXIT_FAILURE, errno, "cannot set receive timeout");
    }

    print_packet(state);
    return pinger_loop(state, sock4);
}
