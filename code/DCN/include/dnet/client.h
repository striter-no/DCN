#pragma once

#include <netw/client.h>
#include <dnet/general.h>
#include <allocator.h>
#include <asyncio.h>
#include <array.h>
#include <logger.h>

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
    NO_DATA,
    SERVER_ERROR
} RESP_CODE; 

struct dcn_client {
    struct allocator *allc;

    struct ev_loop   *loop;
    struct socket_md *md;
    atomic_bool *is_running;
};

struct usr_resp {
    struct queue *requests; // is_request: true
    
    // ullong (cmuid): struct packet * (allocated)
    struct map   responses; // is_request: false
};

struct usr_waiter {
    struct waiter  *req_waiter;
    struct waiter *resp_waiter;
};

struct dcn_session {
    struct logger *lgr;
    struct dcn_client *client;
    atomic_bool is_active;

    mtx_t usr_waiters_mtx;
    mtx_t usr_responses_mtx;
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

    struct map    trr_responses;

    struct queue  trr_requests;
    struct waiter trr_waiter;

    thrd_t runnerthr;
};

struct dcn_task {
    struct dcn_session *session;
    struct packet      *pack;
};

struct grsps_task {
    struct dcn_session *session;
    ullong from_uid; // if from_uid == 0 then it is misc mode
    double timeout_sec;
};

struct trr_task {
    struct dcn_session *session;
    struct packet *trp;
};

struct dnet_state {
    struct ev_loop   *loop;
    struct allocator *allc;

    struct socket_md   socket;
    struct dcn_client  client;
    struct dcn_session session;
};

int dnet_state(
    struct dnet_state *out,
    struct ev_loop   *loop,
    struct allocator *allc,

    char *serv_ip,
    unsigned short port,
    ullong my_uid
);

void dnet_run(struct dnet_state *state);
void dnet_stop(struct dnet_state *state);

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

// ============= User interface functions =============


// send smth and get response
Future* request(
    struct dcn_session *session,
    struct packet *pack,
    ullong to_uid,
    PACKET_TYPE packtype
);

// get incoming requests from uid
Future *async_grequests(
    struct dcn_session *session,
    ullong from_uid
);

Future *ping(
    struct dcn_session *session
);

// get any incoming request
Future *async_misc_grequests(
    struct dcn_session *session,
    double timeout_sec
);

Future *traceroute(
    struct dcn_session *session,
    ullong uid_ttr
);

Future *traceroute_ans(
    struct dcn_session *session,
    ullong who_exists
);

Future *async_gtraceroutes(
    struct dcn_session *session
);
