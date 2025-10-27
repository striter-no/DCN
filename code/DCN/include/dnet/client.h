#pragma once

#include <netw/client.h>
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

If client is registered as a router:

1. Keep connection to the real router
2. Open local server?
3. Pass incoming messages to real router

*/

struct dcn_client {
    struct allocator *allc;

    struct ev_loop   *loop;
    struct socket_md *md;
    atomic_bool *is_running;
};

struct dcn_session {
    struct dcn_client *client;
    atomic_bool is_active;

    ullong last_muid;
    ullong cli_uid;

    struct queue readq;
    struct queue writeq;

    // ullong (msg_uid): struct packet* (allocated)
    struct map responses;
    // ullong (msg_uid): waiter* (allocated)
    struct map cnd_responses;

    thrd_t runnerthr;
};

struct dcn_task {
    struct dcn_session *session;
    struct packet     *pack;
    bool free_pack;
};


void dcn_cli_init(
    struct allocator  *allc,
    struct dcn_client *cli,
    struct ev_loop    *loop,
    struct socket_md  *sock
);

void dcn_cli_run(
    struct dcn_session *cli
);

ullong dcn_request(
    struct dcn_session *session,
    struct packet *packet,
    ullong to_uid,
    struct waiter **waiter
);

void dcn_new_session(
    struct dcn_session *session,
    struct dcn_client *client,
    ullong uid
);

void dcn_end_session(
    struct dcn_session *session
);

bool dcn_getresp(
    struct dcn_session *session,
    ullong resp_n,
    struct packet *pack
);

void wait_response(
    struct dcn_session *session,
    struct waiter *waiter,
    ullong muid
);
