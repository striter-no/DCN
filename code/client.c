#include <attp/proto.h>
#include <netw/client.h>
#include <queue.h>

void worker(struct qblock *inp, struct qblock *out){
    struct attp_message msg;
    if(!attp_msg_deserial(inp, &msg)){
        fprintf(stderr, "error: cannot deserialize message\n");
        return;
    }
    
    printf("got from server: %s\n", msg.data.data);
    attp_free_msg(&msg);
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
    struct attp_message msg;
    msg.uid = 1234;
    msg.from_uid = 5678;
    qblock_init(&msg.data);
    qblock_fill(&msg.data, "Hello", 6);
    
    attp_msg_copy(&msg, &block);
    push_block(&state.qwrite, &block);
    qblock_free(&block);
    attp_free_msg(&msg);
    
    cstate_run(&md, &state);
    cstate_free(&state);
    close(md.fd);
    
}
