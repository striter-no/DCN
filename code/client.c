#define DEBUGGING
#include "DCN/include/dnet/general.h"
#include <asyncio.h>
#include <allocator.h>
#include <dnet/client.h>

int main(int argc, char *argv[]){
    ullong MY_UID = atoll(argv[1]);
    ullong TO_UID = atoll(argv[2]);

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
    
    printf("dcn creation\n");
    dcn_cli_init(&allc, &client, &loop, &md);
    dcn_new_session(&session, &client, MY_UID);
    dcn_cli_run(&session);
    loop_run(&loop);
    
    {
        printf("packet creation\n");
        struct packet pack;
        packet_templ(
            &allc, &pack, 
            "Hello", 6
        );

        // no need to free(resp) cuz it is allocated in &allc
        // request is send (await to get response)
        printf("sending request\n");
        Future *resp = request(&session, &pack, TO_UID, true);
        
        // wait for sent request
        printf("waiting for incoming requests\n");
        struct packet *req_packet;
        Future *req  = async_grequests(&session, TO_UID);
        req_packet = await(req);

        printf("req_packet pointer: %p\n", (void*)req_packet);
        printf("req_packet size: expected %zu, actual? \n", sizeof(struct packet));

        if (req_packet == NULL){
            printf("no incoming requests\n");
            free(req_packet);
            exit(1);
        }

        printf("Packet sanity check:\n");
        printf("  from_uid: %llu\n", req_packet->from_uid);
        printf("  to_uid: %llu\n", req_packet->to_uid);
        printf("  muid: %llu\n", req_packet->muid);
        printf("  data.dsize: %zu\n", req_packet->data.dsize);
        printf("  data.data pointer: %p\n", (void*)req_packet->data.data);

        printf("got incoming request (%zu bytes)\n", req_packet->data.dsize);
        fwrite(req_packet->data.data, 1, req_packet->data.dsize, stdout);
        printf("\n");
        // answer with echo
        await(request(&session, &pack, TO_UID, false)); 
        packet_free(&allc, req_packet);

        // wait for response
        struct packet *resp_packet = await(resp);
        printf("got response (%zu bytes): ", resp_packet->data.dsize);
        fwrite(resp_packet->data.data, 1, resp_packet->data.dsize, stdout);
        printf("\n");

        packet_free(&allc, resp_packet);
    }

    printf("ending session\n");
    dcn_end_session(&session);
    close(md.fd);

    printf("ending loop and allocator\n");
    loop_stop(&loop);
    allocator_end(&allc);
    printf("end\n");
}
