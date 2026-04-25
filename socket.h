#ifndef PING4_SOCKET_H
#define PING4_SOCKET_H

#include "ping4.h"

typedef struct {
    int fd;
    int domain;
    int socktype;
    int protocol;
} socket_st;

int  socket_bind(socket_st* sock_st, struct sockaddr_in* addr, socklen_t sock_len);
int  socket_connect(socket_st* sock_st, struct sockaddr_in* addr, socklen_t sock_len);
void socket_create(run_state *state, socket_st *sock_st);
void socket_close(socket_st *sock_st);
int  socket_name(socket_st* sock_st, struct sockaddr_in* addr, socklen_t* sock_len);
int  socket_set_option(socket_st *sock_st, int level, int opt_name, const void *opt_val, socklen_t sock_len);

#endif //PING4_SOCKET_H
