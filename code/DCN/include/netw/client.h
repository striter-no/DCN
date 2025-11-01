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

#include <dnet/general.h>
#include <queue.h>
#include <asyncio.h>

#define MAX_BUFFER_SIZE 1024

struct worker_args {
    void *state_holder;
    struct queue *qr; // read from
    struct queue *qw; // push to
};

struct socket_md {
    struct sockaddr_in inaddr;
    socklen_t in_al;
    
    int fd;
    char ip[20];
    unsigned short port;
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
    struct allocator *allc,
    struct socket_md *md,
    char   **data,
    size_t *read_sz
);

void run_client(
    struct socket_md *md,
    atomic_bool *is_running,
    struct ev_loop *loop,
    void *(*on_message)(void*),
    struct queue *qread, 
    struct queue *qwrite,
    void *state_holder
);
