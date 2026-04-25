#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ping4.h"
#include "output.h"
#include "pinger.h"
#include "socket.h"
#include "utils.h"

static void usage()
{
    fprintf(stderr, (
            "\nUsage:\n"
            "  pingc [options] <destination>\n"
            "\nOptions:\n"
            "  <destination>    DNS name or IP address\n"
            "  -c <count>       stop after <count> replies\n"
            "  -h               print help and exit\n"
            "  -i <interval>    seconds between sending each packet\n"
            "  -L               no loop\n"
            "  -s <packetsize>  use <packetsize> as number of data bytes to be sent\n"
            "  -t <ttl>         time to live - number of hops before packet dropped\n"
            "  -v               verbose output\n"
            "  -V               print version and exit\n"
            "  -w <deadline>    reply wait <deadline> in seconds\n"
            "  -W <timeout>     time to wait for response\n"
    ));

    exit(PING4_USAGE);
}

int main(int argc, char* argv[])
{
    static run_state state = {
        .interval = PING4_DEF_INTERVAL_MS,
        .max_packets = PING4_MAX_PACKETS,
        .datalen = PING4_DEF_DATALEN,
        .target = "",
        .hostname = "",
        .source_addr.sin_family = AF_INET,
        .source_addr.sin_addr.s_addr = 0,
        .exit_signalled = 0,
        .opt_loop = 1,
        .opt_is_broadcast = 0,
        .opt_verbose = 0
    };

    socket_st sock4 = {
        .fd = -1,
        .domain = AF_INET,
        .socktype = SOCK_DGRAM,
        .protocol = IPPROTO_ICMP
    };

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_protocol = IPPROTO_UDP,
        .ai_socktype = SOCK_DGRAM,
        .ai_flags = AI_CANONNAME
    };

    struct addrinfo* target_info = NULL;
    struct addrinfo* addr_info = NULL;
    int opt_version = 0;
    int ch = 0;

    while ((ch = getopt(argc, argv, "h?" "c:i:s:t:vVw:W:")) != EOF) {
        switch(ch) {
            case 'c':
                state.max_packets = utils_strtol_or_err(optarg, 1, LONG_MAX);
                if (errno) {
                    utils_error(EXIT_FAILURE, errno, "invalid argument");
                }
                break;
            case 'h':
                usage();
                break;
            case 'i': {
                double interval_val = 0;
                interval_val = utils_strtod_or_err(optarg, 1, ((double) INT32_MAX / 1000));
                if (errno) {
                    utils_error(EXIT_FAILURE, errno, "invalid argument");
                }
                state.interval = (int)(interval_val * 1000);
                break;
            }
            case 'L':
                state.opt_loop = 0;
                break;
            case 's':
                state.datalen = utils_strtoint_or_err(optarg, 0, INT32_MAX);
                break;
            case 't':
                state.ttl = utils_strtoint_or_err(optarg, 0, 255);
                state.opt_ttl = 1;
                break;
            case 'v':
                state.opt_verbose = 1;
                break;
            case 'V':
                opt_version = 1;
                break;
            default:
                usage();
                break;
        }
    }

    if (opt_version) {
        print_version();
        exit(EXIT_SUCCESS);
    }

    argc -= optind;
    argv += optind;

    if (!argc) {
        utils_error(PING4_USAGE, 0, "destination required");
    }

    strncpy(state.target, argv[argc - 1], sizeof state.target - 1);
    state.target[sizeof state.target - 1] = '\0';

    if (state.datalen > PING4_ICMP_MAX_DATALEN) {
        utils_error(EXIT_FAILURE,
                    0,
                    "invalid -s value: '%d': out of range: value > 0 and <= %d",
                    state.datalen,
                    PING4_ICMP_MAX_DATALEN);
    }

    socket_create(&state, &sock4);

    if (sock4.fd == -1) {
        utils_error(EXIT_FAILURE, 0, "failed to create socket");
    }

    if (state.opt_verbose) {
        print_line("sock4.fd: %d (socktype: %s), ai_family: %s",
                   sock4.fd,
                   utils_str_sock_type(sock4.socktype),
                   utils_str_family(AF_INET));
    }

    int tos = PING4_TOS_MIN_DELAY;

    if (socket_set_option(&sock4, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == -1) {
        utils_error(EXIT_FAILURE, errno, "set sockopt failed");
    }

    int result = getaddrinfo(state.target, NULL, &hints, &target_info);

    if (result) {
        utils_error(EXIT_FAILURE, 0, "%s: %s", state.target, gai_strerror(result));
    }

    for (addr_info = target_info; addr_info; addr_info = addr_info->ai_next) {
        if (state.opt_verbose) {
            print_line("ai->ai_family: %s, ai->ai_canonname: '%s'",
                       utils_str_family(addr_info->ai_family),
                       addr_info->ai_canonname ? addr_info->ai_canonname : "");
        }

        result = pinger_run(&state, addr_info, &sock4);
        if (result) {

        }
    }

    freeaddrinfo(target_info);
    socket_close(&sock4);

    return 0;
}
