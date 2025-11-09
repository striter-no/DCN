#include <asyncio.h>
#include <allocator.h>
#include <dnet/client.h>
#include <dnet/general.h>
#include <logger.h>
#include <stdio.h>

int main(int argc, char *argv[]){

    ullong MY_UID = atoll(argv[1]);
    ullong TO_UID = atoll(argv[2]);

    struct ev_loop loop;
    struct allocator allc;
    allocator_init(&allc);
    loop_create(&allc, &loop, 3);
    loop_run(&loop);

    struct dnet_state state;
    dnet_state(&state, &loop, &allc, "127.0.0.1", 9000, MY_UID);
    
    dnet_run(&state);

    struct dcn_session *session = &state.session;
    struct packet pack, echopack;
    packet_templ(&allc, &pack, "Hello", 6);
    packet_templ(&allc, &echopack, "Hello echo", 11);

    struct packet *req_packet;
    Future *resp = request(session, &pack /*data to send*/, TO_UID, BROADCAST);

    Future *req  = async_grequests(session, TO_UID);
    req_packet = await(req);

    if (req_packet != NULL){
        printf("got incoming request (%zu bytes): %s\n", req_packet->data.dsize, req_packet->data.data);
        packet_free(&allc, req_packet);
        
        await(request(session, &echopack /*data to answer with*/, TO_UID, RESPONSE));
    } else printf("no incoming requests\n");

    struct packet *resp_packet = await(resp); /*awaiting for get response */
    if (resp_packet != NULL){
        printf(
            "got response (%zu bytes): %s\n", 
            resp_packet->data.dsize, 
            resp_packet->data.data
        );

        packet_free(&allc, resp_packet);
    }

    dnet_stop(&state);
    loop_stop(&loop);
    allocator_end(&allc);
}
