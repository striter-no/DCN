#pragma once
#include <attp/proto.h>
#include <netw/client.h>
#include <asyncio.h>
#include <queue.h>

struct attp_session {
    struct ev_loop *loop;
    struct socket_md *sock;
    thrd_t worker, main_io;

    struct queue pending_msgs;
    struct map   pending_responses;
    size_t client_uid;
    size_t last_uid;

    struct queue qread;
    struct queue qwrite;

    atomic_bool is_running;
};

struct session_args {
    struct attp_session *session;
    struct worker_args  wargs;
};

struct __runner_args {
    struct socket_md *md;
    atomic_bool *is_running;
    struct queue *qread; 
    struct queue *qwrite;
};

void attp_new_session(
    struct attp_session *session,
    struct ev_loop      *loop,
    struct socket_md    *sock,
    size_t uid
);

void attp_end_session(
    struct attp_session *session
);

Future *attp_request(
    struct attp_session *session,
    struct attp_message *msg
);
