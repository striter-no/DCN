#include <attp/server.h>

void __serv_worker(
    struct qblock *inp,
    struct qblock *_ommit
){
    struct worker_task task;
    generic_qbout(inp, &task, sizeof(struct task));

    struct client *cli = task.cli_ptr;
    struct queue  *rq  = &cli->read_q;
    struct queue  *wq  = &cli->write_q;

    struct attp_message *msg = malloc(sizeof(struct attp_message));
    struct qblock block;
    qblock_init(&block);
    pop_block(rq, &block);

    if(!attp_msg_deserial(&block, msg)){
        fprintf(stderr, "error: cannot deserialize msg\n");
        qblock_free(&block);
        return;
    }
    
    qblock_free(&block);
    
    struct msg_task *msg_task = malloc(sizeof(struct msg_task));
    msg_task->cli = cli;
    msg_task->wq = wq;
    msg_task->msg = msg;
    msg_task->handler = ((struct attp_server*)(task.state_holder))->handler;

    async_create(((struct attp_server*)(task.state_holder))->loop, __serv_attp_task, msg_task);
}

void *__serv_attp_task(void *_args){
    struct msg_task *task = _args;

    struct attp_message answer;
    qblock_init(&answer.data);
    answer.from_uid = task->msg->from_uid;
    answer.uid = task->msg->uid;

    task->handler(task->cli, task->msg, &answer);

    struct qblock converted;
    attp_msg_copy(&answer, &converted);
    push_block(task->wq, &converted);
    qblock_free(&converted);

    attp_free_msg(task->msg);
    free(task->msg);
    free(task);
    return NULL;
}

void attp_init(
    struct attp_server *serv,
    struct ev_loop *loop,
    ssize_t threads_n,
    void (*handler)(
        struct client *cli, 
        struct attp_message *input, 
        struct attp_message *output
    )
){
    pool_init(&serv->worker_pool, __serv_worker, threads_n);
    serv->handler = handler;
    serv->loop = loop;
}

void attp_run(
    atomic_bool *is_running,
    struct attp_server *serv,
    struct socket_md   *sock
){
    pool_start(&serv->worker_pool);
    run_server(sock, &serv->worker_pool, is_running, serv);
}

void attp_stop(
    struct attp_server *serv
){
    pool_free(&serv->worker_pool);
}
