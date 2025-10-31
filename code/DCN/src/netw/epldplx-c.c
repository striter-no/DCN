#include <netw/epdplx-s.h>

int screate_socket(
    struct socket_md *smd,
    char *ip,
    unsigned short port
){
    smd->inaddr = (struct sockaddr_in){
        .sin_addr = {inet_addr(ip)},
        .sin_port = htons(port),
        .sin_family = AF_INET
    };
    smd->in_al = sizeof(smd->inaddr);
    smd->port  = port;
    strcpy(smd->ip, ip);

    smd->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (smd->fd == -1)
        return -1;

    setsockopt(smd->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    smd->last_uid = 0;
    return 0;
}

int serv_start(
    struct socket_md *smd,
    int epfd,
    size_t max_clients
){
    if (bind(smd->fd, (const struct sockaddr*)&smd->inaddr, smd->in_al) < 0)
        return -1;
    
    if (listen(smd->fd, max_clients) < 0)
        return -2;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, smd->fd, &(struct epoll_event){
        .events = EPOLLIN,
        .data.ptr = NULL
    }) < 0){
        return -1;
    }

    return 0;
}

int ep_init(
    int    *epfd
){
    *epfd = epoll_create1(0);
    if ((*epfd) < 0)
        return -1;

    return 0;
}

// 1 Iteration for client
int nbep_read(
    int fd,
    char   **output_buff, // needs to be NULL
    size_t  *output_size
){
    // read cycle
    *output_size = 0;
    char buffer[MAX_BUFFER_SIZE] = {0};
    while (true){
        ssize_t ar = read(fd, buffer, MAX_BUFFER_SIZE);
        if (ar > 0){
            char *lcopy = realloc((*output_buff), (*output_size) + ar);
            if (lcopy == NULL)
                return -2;
            memcpy(lcopy + (*output_size), buffer, ar);
            *output_buff = lcopy;
            *output_size += ar;
        } else if (ar == 0){ 
            // fprintf(
            //     stderr, 
            //     "[write][error] nbep_error: %s\n",
            //     strerror(errno)
            // );
            return -4; 
        } else if (errno == EAGAIN) {
            break;
        } else {
            fprintf(
                stderr, 
                "[write][error] nbep_error/unexpected error: %s\n",
                strerror(errno)
            );
            return -3;
        }
    }

    return 0;
}

int sfull_write(
    struct client *cli,
    char   *data,
    size_t to_write
){
    
    ssize_t aw = write(
        cli->fd, 
        data + cli->alr_written, 
        to_write - cli->alr_written
    );
    
    if (aw > 0){
        cli->alr_written += aw;
    } else if (aw < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 1;

        fprintf(
            stderr, 
            "[write][error] full_write: %s\n",
            strerror(errno)
        );
        return -1;
    } else if (aw == 0) {
        fprintf(
            stderr, 
            "[write][error] full_write/unexpected error: %s\n",
            strerror(errno)
        );
        return -2;
    }

    return 0;
}

int accept_client(
    struct allocator *allc,
    struct socket_md *serv_md,
    int epfd,
    struct socket_md *cli_md,
    struct client **cli_out
){
    cli_md->in_al = sizeof(cli_md->inaddr);
    cli_md->fd = accept4(
        serv_md->fd, 
        (struct sockaddr*)&cli_md->inaddr, 
        &cli_md->in_al, 
        O_CLOEXEC | SOCK_NONBLOCK
    );
    
    if (cli_md->fd == -1)
        return -1;
    
    cli_md->port  = ntohs(cli_md->inaddr.sin_port);
    strcpy(cli_md->ip, inet_ntoa(cli_md->inaddr.sin_addr));

    struct client *client = malloc(sizeof(struct client));
    client->fd = cli_md->fd;
    client->alr_written = 0;
    client->intr_uid = ++serv_md->last_uid;
    strcpy(client->ip, cli_md->ip);
    client->port = cli_md->port;
    queue_init(&client->read_q, allc);
    queue_init(&client->write_q, allc);

    *cli_out = client;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, cli_md->fd, &(struct epoll_event){
        .events = EPOLLIN | EPOLLOUT,
        .data.ptr = client
    }) < 0){
        close(client->fd);
        free(client);
        fprintf(
            stderr, 
            "[epoll][error] accept_client: %s\n",
            strerror(errno)
        );
        return -2;
    }

    return 0;   
}

int close_client(int epfd, struct client *cli){
    printf("[DEBUG] Closing client fd=%d, ip=%s, port=%d\n", cli->fd, cli->ip, cli->port);
    if (!cli) return 1;

    if (epoll_ctl(epfd, EPOLL_CTL_DEL, cli->fd, NULL) < 0){
        fprintf(
            stderr, 
            "[epoll][error] close_client: %s\n",
            strerror(errno)
        );
        return -1;
    }

    queue_free(&cli->read_q);
    queue_free(&cli->write_q);
    close(cli->fd);
    free(cli);

    return 0;
}

int run_server(
    struct allocator *allc,
    struct socket_md *server,
    struct ev_loop   *loop,
    atomic_bool *is_running,
    void *(*async_worker)(void *),
    void (*custom_acceptor)(struct client *cli, void *state_holder),
    void (*custom_disconnector)(struct client *cli, void *state_holder),
    void *state_holder
){
    int epfd;
    ep_init(&epfd);
    struct epoll_event events[MAX_EPOLL_EVENTS];
    
    int ret = serv_start(server, epfd, 8);
    if (ret != 0){
        fprintf(stderr, "cannot bind server: %s\n", strerror(errno));
        return ret;
    }

    while (atomic_load(is_running)){
        // -1 means to stop the loop if no events
        // you need actual timeout if you want to skip 
        // this loop while no data is available
        int av_num = epoll_wait(epfd, events, MAX_EPOLL_EVENTS, -1);

        for (int i = 0; i < av_num; i++){
            struct epoll_event ev = events[i];

            if (ev.data.ptr == NULL){
                struct client *ptr = NULL;
                struct socket_md cli_md;
                ret = accept_client(allc, server, epfd, &cli_md, &ptr);
                
                if (ret != 0){
                    fprintf(stderr, "cannot accept client: %s\n", strerror(errno));
                    break;
                }

                custom_acceptor(ptr, state_holder);
            } else {
                struct client *cli = ev.data.ptr;

                if (ev.events & (EPOLLHUP | EPOLLERR)) {
                    custom_disconnector(cli, state_holder);
                    close_client(epfd, cli);
                    continue;
                }
                
                else if (ev.events & EPOLLIN){
                    char  *r_buff = NULL;
                    size_t r_size = 0;

                    ret = nbep_read(cli->fd, &r_buff, &r_size);
                    if (ret == -3 || ret == -4){
                        custom_disconnector(cli, state_holder);
                        close_client(epfd, cli);
                        continue;
                    }

                    if (r_size != 0){
                        struct qblock block;
                        qblock_init(&block);
                        qblock_fill(allc, &block, r_buff, r_size);
                        free(r_buff);

                        push_block(&cli->read_q, &block);
                        qblock_free(allc, &block);

                        struct worker_task *task = malloc(sizeof(struct worker_task));
                        task->cli_ptr = cli;
                        task->state_holder = state_holder;

                        async_create(loop, async_worker, task);

                        // if( epoll_ctl(
                        //     epfd, EPOLL_CTL_MOD, 
                        //     cli->fd, &(struct epoll_event){
                        //         .events = EPOLLIN | EPOLLOUT,
                        //         .data.ptr = cli
                        //     }
                        // ) < 0){
                        //     atomic_store(is_running, false);
                        //     fprintf(
                        //         stderr, 
                        //         "[epoll][error] while epollin: %s\n", strerror(errno)
                        //     );
                        //     break;
                        // }
                    }
                }

                else if (ev.events & EPOLLOUT){
                    if (queue_empty(&cli->write_q)){
                        // printf("EPOLLOUT but write queue is empty for fd=%d\n", cli->fd);
                        continue;
                    }

                    struct qblock block;
                    qblock_init(&block);

                    if (0 == peek_block(&cli->write_q, &block)){
                        printf(">epldplx: writing to %s:%i (%i fd)\n", cli->ip, cli->port, cli->fd);
                        
                        ret = sfull_write(cli, block.data, block.dsize);
                        if (ret == 0){
                            cli->alr_written = 0;
                            //  2 == no blocks left
                            // if (2 == pop_block(&cli->write_q, NULL) && epoll_ctl(
                            //     epfd, EPOLL_CTL_MOD, 
                            //     cli->fd, &(struct epoll_event){
                            //         .events = EPOLLIN,
                            //         .data.ptr = cli
                            //     }
                            // ) < 0){
                            //     atomic_store(is_running, false);
                            //     fprintf(
                            //         stderr, 
                            //         "[epoll][error] while epollout/full write: %s\n", strerror(errno)
                            //     );
                            //     break;
                            // }
                            pop_block(&cli->write_q, NULL);
                        }

                        qblock_free(allc, &block);
                    }
                } else if (!queue_empty(&cli->write_q)){
                    printf(">epldplx: no epollout on %s:%i (%i fd) while WRITEQ is not empty\n", cli->ip, cli->port, cli->fd);
                }
            }
        }
    }
    return 0;
}
