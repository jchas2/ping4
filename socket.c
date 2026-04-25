#include "socket.h"
#include "output.h"
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <unistd.h>

int socket_bind(socket_st* sock_st, struct sockaddr_in* addr, socklen_t sock_len)
{
    if (sock_st->fd == -1) {
        return -1;
    }

    errno = 0;
    return bind(sock_st->fd, (struct sockaddr*)addr, sock_len);
}

int socket_connect(socket_st* sock_st, struct sockaddr_in* addr, socklen_t sock_len)
{
    if (sock_st->fd == -1) {
        return -1;
    }

    errno = 0;

    if (connect(sock_st->fd, (struct sockaddr*)addr, sock_len) == -1) {
        switch (errno) {
            case EACCES:
                print_line("permission denied");
                return -1;
            case EHOSTUNREACH:
                print_line("host unreachable");
                return -1;
            case ENETUNREACH:
                print_line("network unreachable");
                return -1;
            default:
                print_line("connect error");
                return -1;
        }
    }

    return 0;
}

void socket_create(run_state* state, socket_st* sock_st)
{
    errno = 0;
    bool use_raw = 0;

    if (sock_st->socktype == SOCK_DGRAM) {
        sock_st->fd = socket(sock_st->domain, sock_st->socktype, sock_st->protocol);

        /* Kernel doesn't support ping sockets or user not allowed to use ping sockets. */
        if (sock_st->fd == -1 && (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT || errno == EACCES)) {
            if (state->opt_verbose) {
                print_line("datagram or icmp protocol not supported");
            }

            use_raw = 1;
        }
    }

    /* Use or fallback to raw socket which requires elevated privileges. */
    if (sock_st->socktype == SOCK_RAW || use_raw) {
        if (state->opt_verbose) {
            print_line("using raw socket");
        }

        sock_st->socktype = SOCK_RAW;
        sock_st->fd = socket(sock_st->domain, sock_st->socktype, sock_st->protocol);
    }
}

void socket_close(socket_st* sock_st)
{
    errno = 0;

    if (sock_st->fd != -1) {
        close(sock_st->fd);
        sock_st->fd = -1;
    }
}

int socket_name(socket_st* sock_st, struct sockaddr_in* addr, socklen_t* sock_len)
{
    if (sock_st->fd == -1) {
        return -1;
    }

    errno = 0;
    return getsockname(sock_st->fd, (struct sockaddr*)addr, sock_len);
}

int socket_set_option(socket_st* sock_st, int level, int opt_name, const void* opt_val, socklen_t sock_len)
{
    if (sock_st->fd == -1) {
        return -1;
    }

    errno = 0;
    return setsockopt(sock_st->fd, level, opt_name, opt_val, sock_len);
}
