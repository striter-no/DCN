#include <decnt/router.h>

/*
attp-msg

UID (to)\n
TEXT (data)

*/
void __dcn_handler(
    struct client *cli,
    struct attp_message *input,
    struct attp_message *output,
    void *holder
){
    struct dcn_router *router = holder;
    struct qblock *iblock = &input->data;

    char *to_uid = malloc(10);
    size_t size = 0, head = 0;

    bool no_uid = true;
    for (size_t i = 0;i < iblock->dsize;i++){
        char ch = iblock->data[i];
        if (ch == '\n'){
            no_uid = i != iblock->dsize - 1;
            break;
        }

        to_uid[i] = ch;
        
        size++;
        if (size >= head){
            to_uid = realloc(to_uid, head + 5);
            head += 5;
        }
    }

    if (no_uid){
        free(to_uid);

        struct qblock out;
        qblock_init(&out);
        qblock_fill(&out, "cannot process the message, no to_uid param", 44);
        
        qblock_copy(&output->data, &out);
        qblock_free(&out);
        return;
    }

    // reading part
    struct qblock main_data;
    qblock_init(&main_data);
    qblock_fill(&main_data, iblock->data + size, iblock->dsize - size); 

    ullong ullto_uid = atoll(to_uid);
    free(to_uid);

    mtx_lock(&router->pending_msgs._mtx);
    
    struct queue **pending = NULL;
    map_at(
        &router->pending_msgs,
        &ullto_uid,
        (void**)&pending
    );

    if (pending != NULL){
        push_block(*pending, &main_data);
        qblock_free(&main_data);
    } else {
        struct queue *_pending = malloc(sizeof(struct queue));
        queue_init(_pending);
        push_block(_pending, &main_data);
        qblock_free(&main_data);

        map_set(
            &router->pending_msgs,
            &ullto_uid,
            &_pending
        );
    }

    mtx_unlock(&router->pending_msgs._mtx);
}

void *__dcn_writer(void *_args){
    struct dcn_router *router = _args;

    while (atomic_load(router->is_running)){
        
        
        mtx_lock(&router->pending_msgs._mtx);
        for (size_t i = 0; i < router->pending_msgs.len; i++){
            ullong ullto = 0; map_key_at(&router->pending_msgs, &ullto, i);
            if (ullto == 0) continue;
            
            struct queue **pending = NULL;
            map_at(
                &router->pending_msgs,
                &ullto,
                (void**)pending
            );

            if (pending == NULL)
                continue;

            
        }
        mtx_unlock(&router->pending_msgs._mtx);

        thrd_sleep(&(struct timespec){
            .tv_sec = 0,
            .tv_nsec = 5000 // 5 ms
        }, NULL);
    }

    return NULL;
}

void dcn_router_init(
    struct dcn_router *router,
    atomic_bool      *is_running,
    struct socket_md *sock,
    struct ev_loop   *loop,
    ssize_t threads_num
){
    router->loop = loop;
    router->is_running = is_running;
    router->sock = sock;
    
    attp_init(
        &router->aserv, loop,
        threads_num,
        router,
        __dcn_handler,
        NULL
    );

    async_create(loop, __dcn_writer, router);
}

void dcn_router_start(
    struct dcn_router *router
){
    attp_run(
        router->is_running,
        &router->aserv,
        router->sock
    );
}

void dcn_router_stop(
    struct dcn_router *router
){
    attp_stop(&router->aserv);
}
