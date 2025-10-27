#include <dnet/server.h>

void __dcn_acceptor(
    struct client *cli,
    void *state
){
    printf("new client: %s:%i\n", cli->ip, cli->port);
}

void __dcn_disconnector(
    struct client *cli,
    void *state
){
    struct dcn_server *serv = state;

    ullong *key;
    if(map_key_by_val(&serv->dcn_clients, &cli, (void**)&key)){
        printf("disconnected %s:%i\n", cli->ip, cli->port);
        map_erase(&serv->dcn_clients, key);
    } else {
        printf("[unregistered] disconnected %s:%i\n", cli->ip, cli->port);
    }
}

void *dcn_async_worker(void *_args){
    printf("dcn_async_worker entry\n");
    struct worker_task *args = _args;
    struct client      *cli  = args->cli_ptr;
    struct dcn_server  *serv = args->state_holder;
    struct allocator   *allc = serv->allc;

    struct qblock input;
    qblock_init(&input);

    if (1 == pop_block(&cli->read_q, &input)){
        printf("dcn_async_worker exit\n");
        return NULL;
    }

    printf(" packet deserial...\n");
    struct packet pack, answer;
    packet_deserial(allc, &pack, &input);
    qblock_free(allc, &input);
    printf(" deserial done\n");

    struct client *shadow = NULL;
    if (!map_at(&serv->dcn_clients, &pack.from_uid, (void**)&shadow)){
        printf(" + new client\n");
        map_set(&serv->dcn_clients, &pack.from_uid, &cli);
    } else if ((strcmp(shadow->ip, cli->ip) != 0) || (shadow->port != cli->port)) {
        printf(" ! client in cache is different from actual, rewriting...\n");
        map_set(&serv->dcn_clients, &pack.from_uid, &cli);
    } else {
        printf(" normal client processing\n");
    }

    if (pack.from_uid == pack.to_uid){
        printf(" packet from<->to uid, declined\n");
        packet_free(allc, &pack);
        printf("dcn_async_worker exit\n");
        return NULL;
    }

    printf(" initing answer packet\n");
    struct client *to_cli = NULL;
    packet_init(allc, &answer, NULL, 0, 0, pack.from_uid, pack.muid);

    if (!map_at(&serv->dcn_clients, &pack.to_uid, (void**)&to_cli)){
        printf(" %llu - no such client\n", pack.to_uid);
        packet_fill(
            allc, &answer, 
            (char*)&(int){101}, sizeof(int)
        ); // no such client

        struct qblock ansblock;
        packet_serial(allc, &answer, &ansblock);
        push_block(&cli->write_q, &ansblock);
        qblock_free(allc, &ansblock);

        packet_free(allc, &pack);
        printf("dcn_async_worker exit\n");
        return NULL;
    }

    packet_fill(
        allc, &answer, 
        (char*)&(int){200}, sizeof(int)
    ); // ok status

    struct qblock ansblock;
    qblock_copy(allc, &answer.data, &pack.data);
    packet_serial(allc, &answer, &ansblock);

    push_block(&to_cli->write_q, &ansblock);
    qblock_free(allc, &ansblock);

    packet_free(allc, &pack);
    printf("dcn_async_worker exit\n");
    return NULL;
}

void dcn_serv_init(
    struct allocator  *allc,
    struct dcn_server *serv,
    struct ev_loop    *loop,
    struct socket_md  *sock,
    atomic_bool *is_running
){
    serv->allc = allc;
    serv->loop = loop;
    serv->md   = sock;
    serv->is_running = is_running;

    map_init(
        &serv->dcn_clients,
        sizeof(ullong),
        sizeof(struct client *)
    );
}

void dcn_serv_stop(
    struct dcn_server *serv
){
    // all clients are freed in epldplx server
    map_free(&serv->dcn_clients);
}

void dcn_serv_run(
    struct dcn_server *serv
){
    run_server(
        serv->allc,
        serv->md,
        serv->loop,
        serv->is_running,
        dcn_async_worker,
        __dcn_acceptor, 
        __dcn_disconnector,
        serv
    );
}
