#pragma once

#include <netw/client.h>
#include <dnet/general.h>
#include <allocator.h>
#include <asyncio.h>
#include <array.h>

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

typedef enum {
    UNDEFINED_ERROR,
    FROM_USER,
    NO_SUCH_USER,
    OK_STATUS,
    FORBIDEN_UID,
    NO_DATA
} RESP_CODE; 

struct dcn_client {
    struct allocator *allc;

    struct ev_loop   *loop;
    struct socket_md *md;
    atomic_bool *is_running;
};

struct usr_resp {
    struct queue  *requests; // is_request: true
    struct queue *responses; // is_request: false
};

struct usr_waiter {
    struct waiter  *req_waiter;
    struct waiter *resp_waiter;
};

struct dcn_session {
    struct dcn_client *client;
    atomic_bool is_active;

    ullong last_muid;
    ullong cli_uid;

    // ullong (to_uid): ullong (last muid)
    struct map last_c_muids;

    struct queue readq;
    struct queue writeq;

    // ullong (msg_uid): struct packet* (allocated)
    struct map responses;
    // ullong (msg_uid): waiter* (allocated)
    struct map cnd_responses;
    // ullong (usr_uid): 
    //      <strc 
    //        queue (struct packet *) requests
    //        queue (struct packet *) responses
    //      >
    struct map usr_responses;
    
    // ullong (usr_uid): 
    //      <strc
    //          struct waiter *(allocated)
    //          struct waiter *(allocated)
    //      >
    struct map usr_waiters;
    struct waiter req_waiter;
    thrd_t runnerthr;
};

struct dcn_task {
    struct dcn_session *session;
    struct packet      *pack;
};

struct grsps_task {
    struct dcn_session *session;
    ullong from_uid; // if from_uid == 0 then it is misc mode
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
    struct packet *pack,
    RESP_CODE *rcode
);

void wait_response(
    struct dcn_session *session,
    struct waiter *waiter,
    ullong muid
);


// User interface functions


// void uwait(
//     struct dcn_session *session,
//     struct waiter **waiter,
//     ullong from_uid
// );

// send smth and get response
Future* request(
    struct dcn_session *session,
    struct packet *pack,
    ullong to_uid,
    bool is_request
);

// get incoming requests from uid
Future *async_grequests(
    struct dcn_session *session,
    ullong from_uid
);

// get any incoming request
Future *async_misc_grequests(
    struct dcn_session *session
);
