#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>
#include "ping4.h"
#include "output.h"
#include "packet.h"

void print_line(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void print_packet(run_state *state)
{
    printf(ANSI_BOLD "PING" ANSI_RESET ANSI_YELLOW " %s" ANSI_RESET ANSI_DIM " (%s)" ANSI_RESET
           ": %d data bytes\n",
           state->hostname, state->target, state->datalen);
}

static const char *icmp_unreach_desc(int code)
{
    switch (code) {
        case ICMP_UNREACH_NET:           return "Network Unreachable";
        case ICMP_UNREACH_HOST:          return "Host Unreachable";
        case ICMP_UNREACH_PROTOCOL:      return "Protocol Unreachable";
        case ICMP_UNREACH_PORT:          return "Port Unreachable";
        case ICMP_UNREACH_NEEDFRAG:      return "Fragmentation Required";
        case ICMP_UNREACH_SRCFAIL:       return "Source Route Failed";
        case ICMP_UNREACH_NET_UNKNOWN:   return "Destination Network Unknown";
        case ICMP_UNREACH_HOST_UNKNOWN:  return "Destination Host Unknown";
        case ICMP_UNREACH_ISOLATED:      return "Source Host Isolated";
        case ICMP_UNREACH_NET_PROHIB:    return "Network Administratively Prohibited";
        case ICMP_UNREACH_HOST_PROHIB:   return "Host Administratively Prohibited";
        case ICMP_UNREACH_TOSNET:        return "Network Unreachable for TOS";
        case ICMP_UNREACH_TOSHOST:       return "Host Unreachable for TOS";
        case ICMP_UNREACH_FILTER_PROHIB: return "Communication Prohibited by Filtering";
        default:                         return "Unreachable";
    }
}

void print_reply(run_state *state, recv_result *result)
{
    char from_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &result->from.sin_addr, from_str, sizeof from_str);

    switch (result->icmp_type) {
        case ICMP_ECHOREPLY: {
            int bytes = PING4_ICMP_HDR_SIZE + state->datalen;
            printf(ANSI_GREEN "%d bytes" ANSI_RESET
                   " from " ANSI_DIM "%s:" ANSI_RESET " icmp_seq=%u ttl=%d time=" ANSI_BOLD "%.1f ms" ANSI_RESET "\n",
                   bytes, from_str, result->seq, result->ttl, result->rtt_ms);
            break;
        }
        case ICMP_UNREACH:
            printf(ANSI_RED "From %s: %s" ANSI_RESET "\n",
                   from_str, icmp_unreach_desc(result->icmp_code));
            break;
        case ICMP_TIMXCEED:
            if (result->icmp_code == ICMP_TIMXCEED_INTRANS) {
                printf(ANSI_YELLOW "From %s: Time to Live Exceeded" ANSI_RESET "\n", from_str);
            } else {
                printf(ANSI_YELLOW "From %s: Fragment Reassembly Timeout" ANSI_RESET "\n", from_str);
            }
            break;
        case ICMP_REDIRECT:
            printf("From %s: Redirect (code %d)\n", from_str, result->icmp_code);
            break;
        case ICMP_SOURCEQUENCH:
            printf("From %s: Source Quench\n", from_str);
            break;
        default:
            printf("From %s: ICMP type=%d code=%d\n",
                   from_str, result->icmp_type, result->icmp_code);
            break;
    }
}

void print_statistics(run_state *state)
{
    printf("\n--- %s ping4 statistics ---\n", state->target);

    long loss_pct = 0;

    if (state->packets_sent > 0) {
        loss_pct = (state->packets_sent - state->packets_recv) * 100 / state->packets_sent;
    }

    printf("%ld packets transmitted, %ld received, %ld%% packet loss\n",
           state->packets_sent, state->packets_recv, loss_pct);

    if (state->packets_recv > 0) {
        double avg  = state->rtt_sum  / (double)state->packets_recv;
        double avg2 = state->rtt_sum2 / (double)state->packets_recv;
        double mdev = sqrt(avg2 - avg * avg);

        printf("rtt min/avg/max/mdev = %.3f/%.3f/%.3f/%.3f ms\n",
               state->rtt_min, avg, state->rtt_max, mdev);
    }
}

void print_version(void)
{
    printf("pingc %s\n", PING4_VERSION);
}
