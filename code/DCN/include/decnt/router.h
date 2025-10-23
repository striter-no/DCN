/*
decnt - Decentralized Module (router.h)

server that servers:
- incoming connections from one nodes to another
- transeiveing messages from node to node
- be transperent proxy for nodes
*/

#pragma once

#include <asyncio.h>
#include <attp/server.h>

struct dcn_router {
    atomic_bool      *is_running;
    struct socket_md *sock;
    struct ev_loop   *loop;

    struct attp_server aserv;

    // to_someone: queue<attp_message*>
    struct map     pending_msgs;
    struct map     clients;
};

void dcn_router_init(
    struct dcn_router *router,
    atomic_bool      *is_running,
    struct socket_md *sock,
    struct ev_loop   *loop,
    ssize_t threads_num
);

void dcn_router_start(
    struct dcn_router *router
);

void dcn_router_stop(
    struct dcn_router *router
);
