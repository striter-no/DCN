#pragma once
#include <map.h>
#include <netw/epdplx-s.h>
#include <threads.h>
#include "proto.h"

struct cli_storage {
    // request msg UID: NULL msg or RESPONSE msg
    struct map _rq_resps;
};

struct attp_server {
    struct queue pending_req;
    // client_uid: cli_storage
    struct map   cli_holder;
};

void __internal_worker(
    struct qblock *inp,
    struct qblock *_ommit
){
    struct worker_task *task;
    generic_qbout(inp, &task, sizeof(struct worker_task));

    struct attp_server *serv = task->state_holder;
    struct client *cli = task->cli_ptr;
    struct queue *rq = &cli->read_q;
    struct queue *wq = &cli->write_q;

    // any message
    if (!queue_empty(rq)){
        struct qblock block;
        struct attp_message msg;
        qblock_init(&block);
        pop_block(rq, &block);
        generic_qbout(&block, &msg, sizeof(struct attp_message));

        struct attp_message empt_msg = { .uid = 0 };
        qblock_init(&empt_msg.data);

        struct cli_storage *storage_ptr = NULL;
        map_at(&serv->cli_holder, &cli->uid, &storage_ptr);
        if (storage_ptr != NULL){
            map_set(
                &storage_ptr->_rq_resps,
                &msg.uid,
                &empt_msg
            );
        } else {
            struct cli_storage storage;
            map_init(
                &storage._rq_resps,
                sizeof(size_t),
                sizeof(struct attp_message)
            );

            map_set( &storage._rq_resps, &msg.uid, &empt_msg );
            map_set( &serv->cli_holder, &cli->uid, &storage );
        }

        // push pending request to a queue
        push_block(&serv->pending_req, &block);
        // after being proceeded at CLI_HOLDER at FROM_UID
        // creates response

        qblock_free(&block);
    }

    struct cli_storage *storage_ptr = NULL;
    map_at(&serv->cli_holder, &cli->uid, &storage_ptr);
    
    struct map *rq_resps = &storage_ptr->_rq_resps;
    mtx_lock(&rq_resps->_mtx);

    for (size_t i = 0; i < rq_resps->len; i++){
        size_t uid = 0; 
        struct attp_message *msg = NULL;
        map_key_at(rq_resps, &uid, i);
        map_at(rq_resps, &uid, &msg);

        struct qblock block;
        qblock_init(&block);
        generic_qbfill(&block, msg, sizeof(struct attp_message));
        push_block(wq, &block);

        qblock_free(&block);
        attp_free_msg(msg);
    }
    map_clear(rq_resps);
    mtx_unlock(&rq_resps->_mtx);
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
