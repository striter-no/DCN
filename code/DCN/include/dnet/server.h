#pragma once

#include <netw/epdplx-s.h>
#include <dnet/general.h>
#include <allocator.h>
#include <asyncio.h>

/*
How it works

struct packet {
    struct qblock data;
    ullong from_uid;
    ullong to_uid;
};

1. client connects to server
2. client sends his UID in a PACKET (to_uid == 0 => to server)
    - if there this UID already, sent error code
    - if there no this UID, say echo-hello (or OK status)
      and register new pair

            UID <-> struct client*

3. client can send a packet
4. packet is pushed to WRITE QUEUE of to_uid client

Client can register himself as a router

Router:
- is a client and server at one time
- connects to another routers
- broadcasts a message to another routers if 
  to_uid is not at this server

Tunneling:
- router establishes a stable connection b/w himself
  and a nearby client (connected to him)

- registers this client as a "virtual" router
- it is good for accessing machines behind NAT
  and linking local servers with global net

*/

struct dcn_server {
    atomic_bool *is_running;
    struct allocator *allc;

    struct ev_loop   *loop;
    struct socket_md *md;

    struct map dcn_clients;
};

void dcn_serv_init(
    struct allocator  *allc,
    struct dcn_server *serv,
    struct ev_loop    *loop,
    struct socket_md  *sock,
    atomic_bool *is_running
);

void dcn_serv_stop(
    struct dcn_server *serv
);

void dcn_serv_run(
    struct dcn_server *serv
);
