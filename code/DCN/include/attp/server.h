#pragma once
#include <netw/epdplx-s.h>
#include <attp/proto.h>
#include <asyncio.h>

struct msg_task {
    struct client *cli;
    struct queue  *wq;
    struct attp_message *msg;
    
    void *state_holder;
    void (*handler)(
        struct client *cli, 
        struct attp_message *input, 
        struct attp_message *output,
        void *state_holder
    );

    void (*raw_writer)(
        struct client *cli,
        struct queue  *wq,
        void *state_holder
    );
};

struct attp_server {
    struct ev_loop *loop;
    struct pool worker_pool;

    void *state_holder;
    void (*handler)(
        struct client *cli, 
        struct attp_message *input, 
        struct attp_message *output,
        void *state_holder
    );

    void (*raw_writer)(
        struct client *cli,
        struct queue  *wq,
        void *state_holder
    );

    thrd_t writer_thread;
    atomic_bool *is_running;

    // ullong (uid): pointer (struct client *cli)
    struct map   clients;
    struct queue unsorted_clients;
};

void *__serv_attp_task(void *_args);

void attp_init(
    struct attp_server *serv,
    struct ev_loop *loop,
    ssize_t threads_n,
    void *state_holder,
    void (*handler)(
        struct client *cli, 
        struct attp_message *input, 
        struct attp_message *output,
        void *state_holder
    ),
    void (*raw_writer)(
        struct client *cli,
        struct queue  *wq,
        void *state_holder
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
