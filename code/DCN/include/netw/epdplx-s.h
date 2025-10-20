#define _GNU_SOURCE // for accept4
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <thr-pool.h>
#include <queue.h>

#include <fcntl.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX_BUFFER_SIZE  1024
#define MAX_EPOLL_EVENTS 64

struct client {
    int fd;
    struct queue read_q;
    struct queue write_q;
    
    size_t alr_written;
    size_t uid;
};

struct socket_md {
    struct sockaddr_in inaddr;
    socklen_t in_al;
    
    int fd;
    char ip[20];
    unsigned short port;
    size_t last_uid;
};

struct worker_task {
    struct client *cli_ptr;
    void *state_holder;
};

int screate_socket(
    struct socket_md *smd,
    char *ip,
    unsigned short port
);

int serv_start(
    struct socket_md *smd,
    int epfd,
    size_t max_clients
);

int ep_init(
    int    *epfd
);

// 1 Iteration for client
int nbep_read(
    int fd,
    char   **output_buff, // needs to be NULL
    size_t  *output_size
);

int sfull_write(
    struct client *cli,
    char   *data,
    size_t to_write
);

int accept_client(
    struct socket_md *serv_md,
    int epfd,
    struct socket_md *cli_md
);

int close_client(int epfd, struct client *cli);

int run_server(
    struct socket_md *server,
    struct pool *worker_pool,
    atomic_bool *is_running,
    void *state_holder
);
