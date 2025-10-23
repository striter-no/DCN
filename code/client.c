#include <attp/client.h>
#include <asyncio.h>
#include <queue.h>

int main(){
    struct ev_loop loop;
    struct socket_md md;
    ccreate_socket(&md, "127.0.0.1", 9000);
    if (connect_to(&md) != 0){
        printf("cannot connect: %s\n", strerror(errno));
        return -1;
    }

    loop_create(&loop, 2);
    loop_run(&loop);

    struct attp_message msg;
    qblock_init(&msg.data);
    qblock_fill(&msg.data, "Hello", 6);
    
    printf("initializing session...\n");
    struct attp_session session;
    attp_new_session(
        &session, 
        &loop, 
        &md, 456
    );
        printf("sending request\n");
        Future *resp = attp_request(&session, &msg);
        attp_free_msg(&msg);

        printf("starting to await future\n");
        struct attp_message *rmsg = (
            struct attp_message *
        ) await(resp);

        printf("got from server: %s\n", rmsg->data.data);
        
        attp_free_msg(rmsg); // hooray
        free(rmsg);
        free(resp);
        
        printf("ending session...\n");
    
    attp_end_session(&session);
    
    
    
    loop_stop(&loop);
    close(md.fd);
    
    printf("end\n");
}
