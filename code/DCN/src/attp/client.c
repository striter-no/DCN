#include <attp/client.h>
#include <stdatomic.h>
#include <threads.h>

int __runner(void *_args){
    struct __runner_args *args = _args;
    run_client(
        args->md, 
        args->is_running,
        args->qread,
        args->qwrite
    );
    free(args);
    return thrd_success;
}

int __worker(void *_args){
    struct session_args *args = _args;
    struct attp_session *session = args->session;
    struct worker_args  wargs = args->wargs;

    while (atomic_load(wargs.is_running)){
        struct qblock block; // oblock
        qblock_init(&block);
        // qblock_init(&oblock);
        if (1 != pop_block(wargs.qr, &block)){

            struct attp_message *msg = malloc(sizeof(struct attp_message));
            if(attp_msg_deserial(&block, msg)){
                // new response
                #ifdef DEBUGGING
                printf("got new response %zu\n", msg->uid);
                #endif
                map_set(
                    &session->pending_responses,
                    &msg->uid,
                    &msg
                );
                qblock_free(&block);
            }
        }

        while (!queue_empty(&session->pending_msgs)){
            #ifdef DEBUGGING
            printf("forwarding message\n");
            #endif
            queue_forward(
                wargs.qw, 
                &session->pending_msgs, 
                false // no peek
            );
        }
        
        // if (oblock.data != NULL){
        //     push_block(wargs.qw, &oblock);
        //     qblock_free(&oblock);
        // }
        // qblock_free(&block);
    }

    free(args);
    return thrd_success;
}

void attp_new_session(
    struct attp_session *session,
    struct ev_loop      *loop,
    struct socket_md    *sock,
    size_t uid
){
    atomic_init(&session->is_running, true);
    map_init(
        &session->pending_responses, 
        sizeof(size_t),
        // allocated messages (responses)
        sizeof(struct attp_message *) 
    );

    queue_init(&session->pending_msgs);
    queue_init(&session->qread);
    queue_init(&session->qwrite);
    session->last_uid = 1;
    session->client_uid = uid;
    session->loop = loop;
    session->sock = sock;

    struct session_args *args = malloc(sizeof(struct session_args));
    args->session = session;
    args->wargs = (struct worker_args){
        .qr = &session->qread,
        .qw = &session->qwrite,
        .is_running = &session->is_running
    };

    struct __runner_args *run_args = malloc(sizeof(struct __runner_args));
    run_args->is_running = &session->is_running;
    run_args->md = session->sock;
    run_args->qread = &session->qread;
    run_args->qwrite = &session->qwrite;

    thrd_create(&session->worker, __worker, args);
    thrd_create(&session->main_io, __runner, run_args);
}

void attp_end_session(
    struct attp_session *session
){  
    #ifdef DEBUGGING
    printf("storing false to is_running\n");
    #endif
    atomic_store(&session->is_running, false);
    #ifdef DEBUGGING
    printf("waiting __worker()\n");
    #endif
    thrd_join(session->worker, NULL);
    #ifdef DEBUGGING
    printf("waiting __runner()\n");
    #endif
    thrd_join(session->main_io, NULL);
    #ifdef DEBUGGING
    printf("all threads joined\n");
    #endif

    map_free(&session->pending_responses);
    queue_free(&session->pending_msgs);
    queue_free(&session->qread);
    queue_free(&session->qwrite);
    session->last_uid = 1;
    session->client_uid = 0;
    session->loop = NULL;
    session->sock = NULL;
}

struct __waiter_args {
    struct attp_session *session;
    size_t attp_muid;
};

void *__response_waiter(void *_args){
    struct __waiter_args *args = _args;

    struct attp_message *msg = NULL;
    while (msg == NULL && atomic_load(&args->session->is_running)){
        
        map_at(
            &args->session->pending_responses,
            &args->attp_muid,
            (void**)&msg
        );
        // if (msg != NULL){
            #ifdef DEBUGGING
            printf("__reponse_waiter() new response got\n");
            #endif
            // map_erase(&args->session->pending_responses, &args->attp_muid);
        // }

        thrd_sleep(&(struct timespec){
            .tv_sec = 0,
            .tv_nsec = 5000 // 5 ms
        }, NULL);
    }

    free(args);

    // allocated resp msg!
    return msg; 
}

Future *attp_request(
    struct attp_session *session,
    struct attp_message *msg
){

    msg->uid = session->last_uid;
    msg->from_uid = session->client_uid;
    session->last_uid++;

    struct __waiter_args *args = malloc(sizeof(struct __waiter_args));
    args->session = session;
    args->attp_muid = msg->uid;

    struct qblock block;
    attp_msg_copy(msg, &block);
    push_block(&session->pending_msgs, &block);
    qblock_free(&block);

    return async_create(
        session->loop,
        __response_waiter,
        args
    );
}
