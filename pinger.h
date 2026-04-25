#ifndef PING4_PINGER_H
#define PING4_PINGER_H

#include "ping4.h"
#include "socket.h"

int pinger_loop(run_state* state, socket_st* sock4);
int pinger_run(run_state* state, struct addrinfo* ai, socket_st* sock4);

#endif //PING4_PINGER_H
