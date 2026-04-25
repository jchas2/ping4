#ifndef PING4_UTILS_H
#define PING4_UTILS_H

#include <string.h>

void   utils_error(int status, int errNum, const char* format, ...);
int    utils_is_broadcast_addr(struct in_addr* addr);
int    utils_reverse_dns_lookup(const char *ip_addr, char *buf, int buflen);
char   *utils_str_family(int family);
char   *utils_str_sock_type(int socktype);
double utils_strtod_or_err(const char* str, const double min, const double max);
int    utils_strtoint_or_err(const char *str, const int min, const int max);
long   utils_strtol_or_err(const char *str, const long min, const long max);

#endif //PING4_UTILS_H
