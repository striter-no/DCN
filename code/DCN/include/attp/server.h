#pragma once
#include <netw/epdplx-s.h>
#include <attp/proto.h>
#include <asyncio.h>

struct msg_task {
    struct client *cli;
    struct queue  *wq;
    struct attp_message *msg;
    
    void (*handler)(
        struct client *cli, 
        struct attp_message *input, 
        struct attp_message *output
    );
};

struct attp_server {
    struct ev_loop *loop;
    struct pool worker_pool;

    void (*handler)(
        struct client *cli, 
        struct attp_message *input, 
        struct attp_message *output
    );
};

void *__serv_attp_task(void *_args);

void attp_init(
    struct attp_server *serv,
    struct ev_loop *loop,
    ssize_t threads_n,
    void (*handler)(
        struct client *cli, 
        struct attp_message *input, 
        struct attp_message *output
    )
);

void attp_run(
    atomic_bool *is_running,
    struct attp_server *serv,
    struct socket_md   *sock
);

void attp_stop(
    struct attp_server *serv
);
