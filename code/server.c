#include "DCN/include/queue.h"
#include <netw/epdplx-s.h>
#include <attp/proto.h>
#include <asyncio.h>

void worker(
    struct qblock *inp,
    struct qblock *_ommit
){
    struct worker_task task;
    generic_qbout(inp, &task, sizeof(struct task));

    struct client *cli = task.cli_ptr;
    struct queue  *rq  = &cli->read_q;
    struct queue  *wq  = &cli->write_q;

    struct attp_message msg;
    struct qblock block;
    qblock_init(&block);
    pop_block(rq, &block);

    if(!attp_msg_deserial(&block, &msg)){
        fprintf(stderr, "error: cannot deserialize msg\n");
        qblock_free(&block);
        return;
    }
    
    qblock_free(&block);
    printf(
        "new msg from %zu (uid: %zu): %s\n", 
        msg.from_uid, 
        msg.uid, 
        msg.data.data
    );
    
    struct qblock out_block;
    struct attp_message out;
    out.from_uid = msg.from_uid;
    out.uid = msg.uid;
    qblock_init(&out.data);
    qblock_copy(&out.data, &msg.data);

    attp_msg_copy(&out, &out_block);
    attp_free_msg(&msg);

    push_block(&cli->write_q, &out_block);
    qblock_free(&out_block);
    attp_free_msg(&out);
}

void attp_worker(
    struct qblock *inp,
    struct qblock *out
){
    ;
}

int main(){
    atomic_bool is_running = true;

    struct pool attp_pool;
    struct pool worker_pool;
    pool_init(&worker_pool, worker, 4);
    pool_init(&attp_pool, attp_worker, 4);
    pool_start(&worker_pool);

    struct socket_md server;
    if (screate_socket(&server, "127.0.0.1", 9000) != 0){
        fprintf(stderr, "cannot create socket: %s\n", strerror(errno));
        return -1;
    }
    
    run_server(&server, &worker_pool, &is_running, NULL);
    close(server.fd);
    
    pool_free(&worker_pool);
}
