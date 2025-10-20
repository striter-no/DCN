#include <netw/epdplx-s.h>
#include <asyncio.h>

void worker(
    struct qblock *inp,
    struct qblock *_ommit
){
    struct worker_task task;
    struct client *cli = task.cli_ptr;

    generic_qbout(inp, &task, sizeof(struct task));

    struct qblock block;
    qblock_init(&block);

    pop_block(&cli->read_q, &block);
    push_block(&cli->write_q, &block);
    qblock_free(&block);
}

int main(){
    atomic_bool is_running = true;

    struct pool worker_pool;
    pool_init(&worker_pool, worker, 8);
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
