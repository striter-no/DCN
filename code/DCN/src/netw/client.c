#include <netw/client.h>
#include <stdatomic.h>

int ccreate_socket(
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

    return 0;
}

int connect_to(
    struct socket_md *md
){
    if (connect(md->fd, &md->inaddr, md->in_al) == -1){
        if (errno != EINPROGRESS)
            return -1;
    }

    struct pollfd pfd = {.fd = md->fd, .events = POLLOUT};
    if (poll(&pfd, 1, 5000) <= 0){
        return 1;
    }

    return 0;
}

int cfull_write(
    struct socket_md *md,
    char *data,
    size_t to_write,
    size_t *alr_wr
){
    ssize_t aw = write(md->fd, data + (*alr_wr), to_write - (*alr_wr));
        
    if (aw < 0){
        if (errno == EWOULDBLOCK || errno == EAGAIN) 
            return 1;
        return -1;
    }

    *alr_wr += aw;
    return 0;
}

int cfull_read(
    struct socket_md *md,
    char   **data,
    size_t *read_sz
){
    ssize_t ar = 0;
    do {
        char buffer[MAX_BUFFER_SIZE];
        ar = read(md->fd, buffer, MAX_BUFFER_SIZE);

        if (ar < 0){
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                return 1;
            return -1;
        }

        if (ar == 0)
            return 2;

        char *lcopy = realloc(*data, (*read_sz) + ar);
        if (lcopy == NULL)
            return -2;
        memcpy(lcopy + (*read_sz), buffer, ar);
        *data = lcopy;
        *read_sz += ar;

    } while (ar == MAX_BUFFER_SIZE);
    
    return 0;
}

void run_client(
    struct socket_md *md,
    atomic_bool *is_running,
    struct queue *qread, 
    struct queue *qwrite
){
    struct pollfd fds[1] = {(struct pollfd){
        .events = POLLIN | POLLOUT,
        .fd = md->fd
    }};

    size_t alr_wr = 0;
    #ifdef DEBUGGING
    printf("starting main loop\n");
    #endif
    while (atomic_load(is_running)){
        int pret = poll(fds, 1, 100);
        if (pret == -1){
            fprintf(stderr, "[main][error] poll returned -1: %s", strerror(errno));
            atomic_store(is_running, false);
            break;
        } else if (pret > 0){
            if (fds[0].revents & POLLIN){
                #ifdef DEBUGGING
                printf("reading from server\n");
                #endif
                struct qblock block;
                qblock_init(&block);
                int rstat = cfull_read(md, &block.data, &block.dsize);
                if (rstat == 2){
                    atomic_store(is_running, false);
                    break;
                }
                #ifdef DEBUGGING
                printf("just read (%zu bytes): %s\n", block.dsize, block.data);
                #endif
                push_block(qread, &block);
                qblock_free(&block);
            } else if (fds[0].revents & POLLOUT){
                if (!queue_empty(qwrite)){
                    struct qblock block;
                    qblock_init(&block);
                    peek_block(qwrite, &block);
                    #ifdef DEBUGGING
                    printf("writing to the server: %s\n", block.data);
                    #endif

                    if (cfull_write(md, block.data, block.dsize, &alr_wr) == 0){
                        pop_block(qwrite, NULL);   
                        alr_wr = 0;
                    }

                    qblock_free(&block);
                }
            }
        }
    }
}

// int __worker(void *_args){
//     struct worker_args *args = _args;

//     while (atomic_load(args->is_running)){
//         struct qblock block, oblock;
//         qblock_init(&block);
//         qblock_init(&oblock);
//         if (1 == pop_block(args->qr, &block))
//             continue;

//         args->worker_fn(&block, &oblock);
        
//         if (oblock.data != NULL){
//             push_block(args->qw, &oblock);
//             qblock_free(&oblock);
//         }
//         qblock_free(&block);
//     }

//     return thrd_success;
// }

// void cstate_init(
//     struct socket_md *sock,
//     struct c_state *state,
//     void (*worker)(struct qblock *, struct qblock *)
// ){
//     state->is_running = true;
//     state->sock = sock;
//     state->worker = worker;

//     queue_init(&state->qwrite);
//     queue_init(&state->qread);
// }

// void cstate_run(
//     struct socket_md *md,
//     struct c_state *state
// ){
//     thrd_create(&state->_wthread, __worker, &(struct worker_args){
//         .qr = &state->qread,
//         .qw = &state->qwrite,
//         .is_running = &state->is_running,
//         .worker_fn = state->worker
//     });

//     run_client(md, &state->is_running, &state->qread, &state->qwrite);
// }

// void cstate_free(
//     struct c_state *state
// ){
//     atomic_store(&state->is_running, false);
//     thrd_join(state->_wthread, NULL);

//     queue_free(&state->qread);
//     queue_free(&state->qwrite);
// }
