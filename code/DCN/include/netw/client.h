#pragma once
#define _GNU_SOURCE
#include <stdatomic.h>
#include <threads.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

#include <queue.h>
#include <thr-pool.h>

#define MAX_BUFFER_SIZE 1024

struct worker_args {
    atomic_bool *is_running;
    struct queue *qr; // read from
    struct queue *qw; // push to
    void (*worker_fn)(struct qblock *, struct qblock *);
};

struct socket_md {
    struct sockaddr_in inaddr;
    socklen_t in_al;
    
    int fd;
    char ip[20];
    unsigned short port;
};

struct c_state {
    struct socket_md *sock;
    struct queue qread;
    struct queue qwrite;
    thrd_t _wthread;
    
    void (*worker)(struct qblock *, struct qblock *);
    atomic_bool  is_running;
};

int ccreate_socket(
    struct socket_md *smd,
    char *ip,
    unsigned short port
);

int connect_to(
    struct socket_md *md
);

int cfull_write(
    struct socket_md *md,
    char *data,
    size_t to_write,
    size_t *alr_wr
);

int cfull_read(
    struct socket_md *md,
    char   **data,
    size_t *read_sz
);

void run_client(
    struct socket_md *md,
    atomic_bool *is_running,
    struct queue *qread, 
    struct queue *qwrite
);

void cstate_init(
    struct socket_md *sock,
    struct c_state *state,
    void (*worker)(struct qblock *, struct qblock *)
);

void cstate_run(
    struct socket_md *md,
    struct c_state *state
);

void cstate_free(struct c_state *state);

int __worker(void *_args);
