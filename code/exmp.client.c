#include <netw/client.h>
#include <queue.h>

void worker(struct qblock *inp, struct qblock *out){
    printf("got from server: %s\n", inp->data);
    qblock_copy(out, inp);
}

int main(){
    struct socket_md md;
    ccreate_socket(&md, "127.0.0.1", 9000);
    if (connect_to(&md) != 0){
        printf("cannot connect: %s\n", strerror(errno));
        return -1;
    }

    struct c_state state; 
    cstate_init(&md, &state, worker);

    struct qblock block;
    qblock_init(&block);
    qblock_fill(&block, "Hello", 6);
    push_block(&state.qwrite, &block);
    qblock_free(&block);
    
    cstate_run(&md, &state);
    cstate_free(&state);
    close(md.fd);
}
