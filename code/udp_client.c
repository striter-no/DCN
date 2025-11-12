#include <asyncio.h>
#include <allocator.h>
#include <dnet/client.h>
#include <dnet/general.h>
#include <logger.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]){

    ullong MY_UID = atoll(argv[1]);
    int PORT   = atoi(argv[2]);
    double timeout_sec = argc > 3 ? atof(argv[3]) : 5.0;

    struct ev_loop loop;
    struct allocator allc;
    allocator_init(&allc);
    loop_create(&allc, &loop, 3);
    loop_run(&loop);

    struct dnet_state state;
    dnet_state(&state, &loop, &allc, "127.0.0.1", PORT, MY_UID);
    
    dnet_run(&state);

    struct dcn_session *session = &state.session;
    struct packet pack, echopack;
    packet_templ(&allc, &pack, "Hello", 6);
    packet_templ(&allc, &echopack, "Hello echo", 11);

    struct packet *req_packet;
    await(request(session, &pack, 0, SIG_BROADCAST));
    req_packet = await(async_misc_grequests(session, timeout_sec));
    await(request(session, &pack, 0, SIG_BROADCAST));

    if (req_packet != NULL){
        printf("got incoming request (%zu bytes): %s\n", req_packet->data.dsize, req_packet->data.data);
        packet_free(&allc, req_packet);
    
    } else 
        printf("no incoming requests\n");

    dnet_stop(&state);
    loop_stop(&loop);
    allocator_end(&allc);
}
