#include "DCN/include/dnet/general.h"
#include <asyncio.h>
#include <allocator.h>
#include <dnet/client.h>

int main(){
    printf("entry\n");
    struct ev_loop loop;
    struct allocator allc;
    allocator_init(&allc);

    struct dcn_client client;
    struct dcn_session session;

    struct socket_md md;
    ccreate_socket(&md, "127.0.0.1", 9000);
    if (connect_to(&md) != 0){
        printf("cannot connect: %s\n", strerror(errno));
        return -1;
    }

    printf("loop creation\n");
    loop_create(&allc, &loop, 3);
    loop_run(&loop);

    printf("dcn creation\n");
    dcn_cli_init(&allc, &client, &loop, &md);
    dcn_new_session(&session, &client, 456);
    dcn_cli_run(&session);
    {
        printf("packet creation\n");
        struct packet pack;
        packet_templ(
            &allc, &pack, 
            "Hello", 6
        );

        printf("sending request\n");
        struct waiter *waiter = NULL;
        ullong resp_n = dcn_request(&session, &pack, 123, &waiter);

        printf("waiting for response\n");
        wait_response(&session, waiter, resp_n);    

        printf("got response\n");

        struct packet answer;
        dcn_getresp(&session, resp_n, &answer);
        printf("%s\n", answer.data.data);
        
        packet_free(&allc, &answer);
    }

    printf("ending session\n");
    dcn_end_session(&session);
    close(md.fd);

    printf("ending loop and allocator\n");
    loop_stop(&loop);
    allocator_end(&allc);
    printf("end\n");
}
