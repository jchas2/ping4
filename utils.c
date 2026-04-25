#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include "utils.h"

#define CASE_TYPE(str) case str: return #str;

void utils_error(int status, int errnum, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    if (errnum) {
        fprintf(stderr, ": %s\n", strerror(errnum));
    }
    else {
        fprintf(stderr, "\n");
    }

    if (status) {
        exit(status);
    }
}

int utils_is_broadcast_addr(struct in_addr* addr)
{
    if (IN_MULTICAST(ntohl(addr->s_addr)) ||
        addr->s_addr == htonl(INADDR_BROADCAST)) {
        return 1;
    }

    struct ifaddrs *ifap;
    struct ifaddrs *ifa;

    errno = 0;

    if (getifaddrs(&ifap) == -1) {
        return -1;
    }

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (!(ifa->ifa_flags & IFF_BROADCAST) || !ifa->ifa_broadaddr) {
            continue;
        }

        struct sockaddr_in *brd = (struct sockaddr_in *)ifa->ifa_broadaddr;

        if (addr->s_addr == brd->sin_addr.s_addr) {
            freeifaddrs(ifap);
            return 1;
        }
    }

    freeifaddrs(ifap);
    return 0;
}

int utils_reverse_dns_lookup(const char *ip_addr, char *buf, int buflen)
{
    struct sockaddr_in temp_addr;
    socklen_t len = sizeof(struct sockaddr_in);

    temp_addr.sin_family      = AF_INET;
    temp_addr.sin_addr.s_addr = inet_addr(ip_addr);

    if (getnameinfo((struct sockaddr *)&temp_addr, len, buf, buflen, NULL, 0, NI_NAMEREQD)) {
        return -1;
    }

    /* buf should be populated. */
    return 0;
}

char *utils_str_family(int family)
{
    switch (family) {
        CASE_TYPE(AF_UNSPEC)
        CASE_TYPE(AF_INET)
        CASE_TYPE(AF_INET6)
        default:
            utils_error(2, 0, "unknown protocol family: %d", family);
            return "";
    }
}

char *utils_str_sock_type(int socktype)
{
    if (!socktype)
        return "0";

    switch (socktype) {
        CASE_TYPE(SOCK_DGRAM)
        CASE_TYPE(SOCK_RAW)
        default:
            utils_error(2, 0, "unknown sock type: %d", socktype);
            return "";
    }
}

double utils_strtod_or_err(const char* str, const double min, const double max)
{
    errno = 0;

    if (str == NULL || *str == '\0') {
        errno = EINVAL;
        return -1;
    }

    char* end  = NULL;
    double num = strtod(str, &end);

    if (errno != 0 || *end != '\0') {
        return -1;
    }

    if (num < min || num > max) {
        utils_error(0, 0, "'%s': out of range: %g <= value <= %g", str, min, max);
        errno = ERANGE;
        return -1;
    }

    return num;
}

int utils_strtoint_or_err(const char* str, const int min, const int max)
{
    errno = 0;
    long val = utils_strtol_or_err(str, min, max);

    if (errno == ERANGE) {
        return -1;
    }

    return (int)val;
}

long utils_strtol_or_err(const char* str, const long min, const long max)
{
    errno = 0;

    if (str == NULL || *str == '\0') {
        errno = EINVAL;
        return -1;
    }

    char* end = NULL;
    long num  = strtol(str, &end, 10);

    if (errno != 0 || *end != '\0') {
        return -1;
    }

    if (num < min || num > max) {
        utils_error(0, 0, "'%s': out of range: %ld <= value <= %ld", str, min, max);
        errno = ERANGE;
        return -1;
    }

    return num;
}
