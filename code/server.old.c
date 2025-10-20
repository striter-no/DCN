// tcp networking
#include <netw/epdplx-s.h>

// attp_message
#include <attp/proto.h>
#include <threads.h>
#include <queue.h>
#include <map.h>

struct cli_storage {
    // request msg UID: NULL msg or RESPONSE msg
    struct map _rq_resps;
};

struct attp_server {
    struct queue pending_req;
    // client_uid: cli_storage
    struct map   cli_holder;
    mtx_t cli_mtx;
};

void attp_sinit(struct attp_server *serv){
    mtx_init(&serv->cli_mtx, mtx_plain);
    queue_init(&serv->pending_req);
    map_init(
        &serv->cli_holder, 
        sizeof(size_t), 
        sizeof(struct cli_storage)
    );
}

void attp_sfree(struct attp_server *serv){
    mtx_destroy(&serv->cli_mtx);
    queue_free(&serv->pending_req);
    // free map
}

void __internal_worker(
    struct qblock *inp,
    struct qblock *_ommit
){
    struct worker_task task;
    generic_qbout(inp, &task, sizeof(struct worker_task));

    struct attp_server *serv = task.state_holder;
    struct client *cli = task.cli_ptr;
    struct queue *rq = &cli->read_q;
    struct queue *wq = &cli->write_q;

    // any message
    if (!queue_empty(rq)){
        
        struct qblock block;
        struct attp_message msg;
        qblock_init(&block);
        pop_block(rq, &block);
        if(!attp_msg_deserial(&block, &msg)){
            fprintf(stderr, "error: cannot deserialize msg\n");
            qblock_free(&block);
            return;
        }
        
        printf(
            "new msg from %zu (uid: %zu): %s\n", 
            msg.from_uid, 
            msg.uid, 
            msg.data.data
        );

        printf("before lock\n");
        mtx_lock(&serv->cli_mtx);
        struct cli_storage *storage_ptr = NULL;
        map_at(&serv->cli_holder, &msg.from_uid, (void**)&storage_ptr);
        if (storage_ptr == NULL){
            printf("initializing... %p\n", (void*)serv);
            struct cli_storage storage = {0};
            map_set( &serv->cli_holder, &msg.from_uid, &storage );
            map_at(&serv->cli_holder, &msg.from_uid, (void**)&storage_ptr);
            if (storage_ptr){
                map_init(
                    &storage_ptr->_rq_resps, 
                    sizeof(size_t), 
                    sizeof(struct attp_message)
                );
                printf("nested init\n");
            } else
                printf("failed to init storage\n");
        }
        mtx_unlock(&serv->cli_mtx);
        printf("after lock\n");

        // push pending request to a queue
        push_block(&serv->pending_req, &block);
        // after being proceeded at CLI_HOLDER at FROM_UID
        // creates response

        qblock_free(&block);
    }

    struct cli_storage *storage_ptr = NULL;
    map_at(&serv->cli_holder, &cli->uid, (void**)&storage_ptr);
    
    struct map *rq_resps = &storage_ptr->_rq_resps;
    mtx_lock(&rq_resps->_mtx);

    for (size_t i = 0; i < rq_resps->len; i++){
        size_t uid = 0; 
        struct attp_message *msg = NULL;
        map_key_at(rq_resps, &uid, i);
        map_at(rq_resps, &uid, (void**)&msg);

        struct qblock block;
        attp_msg_copy(msg, &block);
        push_block(wq, &block);

        qblock_free(&block);

        // can free msg, because of full copy
        attp_free_msg(msg);
    }
    map_clear(rq_resps);
    mtx_unlock(&rq_resps->_mtx);
}

int watch_requests(void *_args){
    struct attp_server *serv = _args;
    struct qblock block;
    qblock_init(&block);

    while (true){
        if (1 == pop_block(&serv->pending_req, &block))
            continue;

        struct attp_message msg;
        attp_msg_deserial(&block, &msg);
        // actually result is the same to the one after qblock_init()
        qblock_free(&block);

        struct attp_message resp;
        resp.uid = msg.uid; // same for request and response by protocol
        qblock_init(&resp.data);
        qblock_copy(&resp.data, &msg.data);

        struct qblock resp_block;
        attp_msg_copy(&resp, &resp_block); // copy

        printf("before lock 1  server=%p\n", (void*)serv);
        mtx_lock(&serv->cli_mtx);
        struct cli_storage *storage_ptr = NULL;
        map_at(&serv->cli_holder, &msg.from_uid, (void**)&storage_ptr);
        if (storage_ptr != NULL)
            map_set(&storage_ptr->_rq_resps, &resp.uid, &resp);
        else
            printf("storage_ptr is NULL\n");
        mtx_unlock(&serv->cli_mtx);
        printf("after lock 1\n");

        // cleaning msgs
        attp_free_msg(&msg);

        // dont need to free the response
        // because it will be freed at __internal_worker
    }

    return thrd_success;
}

int main(){
    atomic_bool is_running = true;

    struct pool worker_pool;
    pool_init(&worker_pool, __internal_worker, 8);
    pool_start(&worker_pool);
    
    struct socket_md server;
    if (screate_socket(&server, "127.0.0.1", 9000) != 0){
        fprintf(stderr, "cannot create socket: %s\n", strerror(errno));
        return -1;
    }
    
    struct attp_server attp_server;
    attp_sinit(&attp_server);
    
    thrd_t thread;
    thrd_create(&thread, watch_requests, &attp_server);

    run_server(&server, &worker_pool, &is_running, &attp_server);
    thrd_join(thread, NULL);

    close(server.fd);
    pool_free(&worker_pool);
}

/*

client sends:
qblock <- generic(msg)
msg:
    uid: 12345
    data: Hello world

server reads:
qblock -> msg:
    uid: 12345
    data: Hello world

    push it to pending requests

server worker (network worker):
    for i, p in pending_resp.items()
        push (p) to write_queue
*/
