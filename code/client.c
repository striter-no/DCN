#include <asyncio.h>
#include <allocator.h>
#include <dnet/client.h>
#include <dnet/general.h>
#include <logger.h>
#include <stdio.h>

struct dnet_state {
    struct ev_loop   *loop;
    struct allocator *allc;

    struct socket_md   socket;
    struct dcn_client  client;
    struct dcn_session session;
};

int dnet_state(
    struct dnet_state *out,
    struct ev_loop   *loop,
    struct allocator *allc,

    char *serv_ip,
    unsigned short port,
    ullong my_uid
){
    out->loop = loop;
    out->allc = allc;
    
    ccreate_socket(&out->socket, serv_ip, port);
    if (connect_to(&out->socket) != 0){
        fprintf(stderr, "[error] cannot connect: %s\n", strerror(errno));
        return -1;
    }

    dcn_cli_init(allc, &out->client, loop, &out->socket);
    dcn_new_session(&out->session, &out->client, my_uid);

    return 0;
}

void dnet_run(struct dnet_state *state){
    dcn_cli_run(&state->session);
}

void dnet_stop(struct dnet_state *state){
    dcn_end_session(&state->session);
    close(state->socket.fd);
}

int main(int argc, char *argv[]){
    struct logger lgr;
    logger_init(&lgr, stderr);
    // logger_deact(&lgr);

    ullong MY_UID = atoll(argv[1]);
    ullong TO_UID = atoll(argv[2]);

    dblog(&lgr, INFO, "init allc & loop");
    struct ev_loop loop;
    struct allocator allc;
    allocator_init(&allc);
    loop_create(&allc, &loop, 3);
    loop_run(&loop);

    dblog(&lgr, INFO, "init state");
    struct dnet_state state;
    dnet_state(&state, &loop, &allc, "127.0.0.1", 9000, MY_UID);
    
    dblog(&lgr, INFO, "dnet run");
    dnet_run(&state);

    dblog(&lgr, INFO, "pack creation");
    struct dcn_session *session = &state.session;
    struct packet pack, echopack;
    packet_templ(&allc, &pack, "Hello", 6);
    packet_templ(&allc, &echopack, "Hello echo", 11);
    session->lgr = &lgr;

    // send request
    // and get incoming request
    dblog(&lgr, INFO, "sending request");
    struct packet *req_packet;
    Future *resp = request(session, &pack /*data to send*/, TO_UID, BROADCAST);

    dblog(&lgr, INFO, "gathering request");
    // ... add wait untill async_grequests(..., bool wait_for);
    Future *req  = async_grequests(session, TO_UID, true);
    dblog(&lgr, INFO, "awaiting requests");
    req_packet = await(req);

    if (req_packet != NULL){
        dblog(&lgr, INFO, "incoming request...");
        printf("got incoming request (%zu bytes): %s\n", req_packet->data.dsize, req_packet->data.data);
        packet_free(&allc, req_packet);
        
        // answer to the incoming request
        await(request(session, &echopack /*data to answer with*/, TO_UID, RESPONSE));
    } else {
        dblog(&lgr, WARNING, "no incoming requests");
        printf("no incoming requests\n");
        free(req_packet);
    }

    dblog(&lgr, INFO, "awaiting response");
    struct packet *resp_packet = await(resp); /*awaiting for get response */
    if (resp_packet == NULL){
        dblog(&lgr, ERROR, "response is null");
    } else {
        dblog(&lgr, INFO, "got response");
        printf(
            "got response (%zu bytes): %s\n", 
            resp_packet->data.dsize, 
            resp_packet->data.data
        );

        packet_free(&allc, resp_packet);
    }

    dblog(&lgr, INFO, "dnet stop");
    dnet_stop(&state);

    dblog(&lgr, INFO, "loop & allc stop");
    loop_stop(&loop);
    allocator_end(&allc);
    logger_stop(&lgr);
}
