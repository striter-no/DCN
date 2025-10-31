#include <dnet/server.h>
#include <stdbool.h>

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

    printf(" Processing packet: from=%llu, to=%llu, muid=%llu, data_size=%zu, is_request=%i, from_os=%i\n", 
       pack.from_uid, pack.to_uid, pack.muid, pack.data.dsize, pack.is_request, pack.from_os);

    map_set(&serv->dcn_sessions, &pack.from_uid, &pack.muid);

    struct client **shadow = NULL;
    bool found = map_at(&serv->dcn_clients, &pack.from_uid, (void**)&shadow);
    if (!found){
        printf(" + new client\n");
        map_set(&serv->dcn_clients, &pack.from_uid, &cli);
    } else if (shadow != NULL && *shadow != NULL) {
        if ((strcmp((*shadow)->ip, cli->ip) != 0) || ((*shadow)->port != cli->port)) {
            printf(" ! client in cache is different from actual, rewriting...\n");
            map_set(&serv->dcn_clients, &pack.from_uid, &cli);
        } else {
            printf(" normal client processing\n");
        }
    } else {
        printf(" WARNING: shadow is NULL, registering new client\n");
        map_set(&serv->dcn_clients, &pack.from_uid, &cli);
    }

    if (pack.from_uid == pack.to_uid){
        printf(" packet from<->to uid, declined\n");
        printf(" answering back to sender with errc 102\n");
        
        packet_fill(
            allc, &answer, 
            (char*)&(int){102}, sizeof(int)
        ); // to_uid == from_uid, forbidden

        answer.from_os = false;
        answer.from_uid = 0;
        answer.to_uid = pack.from_uid;
        answer.muid = pack.muid;

        struct qblock ansblock;
        packet_serial(allc, &answer, &ansblock);
        push_block(&cli->write_q, &ansblock);
        qblock_free(allc, &ansblock);

        packet_free(allc, &pack);

        printf("dcn_async_worker exit\n");
        return NULL;
    }

    printf(" initing answer packet\n");
    struct client **to_cli_ptr = NULL;
    packet_init(
        allc, &answer, 
        NULL, 0,       // data : size
        0,             // ANSWER from UID
        pack.from_uid, // ANSWER TO uid
        pack.muid      // ANSWER Message UID
    );

    if (!map_at(&serv->dcn_clients, &pack.to_uid, (void**)&to_cli_ptr)){
        printf(" %llu - no such client\n", pack.to_uid);
        printf(" answering back to sender with errc 101\n");
        packet_fill(
            allc, &answer, 
            (char*)&(int){101}, sizeof(int)
        ); // no such client

        struct qblock ansblock;
        packet_serial(allc, &answer, &ansblock);
        push_block(&cli->write_q, &ansblock);
        qblock_free(allc, &ansblock);

        packet_free(allc, &pack);
        packet_free(allc, &answer);
        printf("dcn_async_worker exit\n");
        return NULL;
    } else {
        printf(" answering back to sender with errc 200\n");
        packet_fill(
            allc, &answer, 
            (char*)&(int){200}, sizeof(int)
        ); // ok

        struct qblock _ansblock;
        packet_serial(allc, &answer, &_ansblock);
        push_block(&cli->write_q, &_ansblock);
        qblock_free(allc, &_ansblock);

        packet_free(allc, &answer);
    }

    struct client *to_cli = *to_cli_ptr;
    
    if (to_cli_ptr && *to_cli_ptr) {
        printf(" Found target client: fd=%d, ip=%s, port=%d\n", 
            (*to_cli_ptr)->fd, (*to_cli_ptr)->ip, (*to_cli_ptr)->port);
    }

    struct qblock ansblock;
    struct packet to_cli_answer;
    packet_init(
        allc, &to_cli_answer,
        pack.data.data,
        pack.data.dsize,
        pack.from_uid,
        pack.to_uid,
        0
    );
    to_cli_answer.from_os = true;
    to_cli_answer.is_request = pack.is_request;
    packet_free(allc, &pack);
    packet_serial(
        allc, 
        &to_cli_answer, 
        &ansblock
    );
    
    if (to_cli->fd > 0) {
        push_block(&to_cli->write_q, &ansblock);
        printf(
            " message sent (%llu->%llu (%i fd) (#%llu) %zu bytes) | queue size: %zu\n", 
            to_cli_answer.from_uid, 
            to_cli_answer.to_uid, 
            to_cli->fd,
            to_cli_answer.muid,
            to_cli_answer.data.dsize,
            to_cli->write_q.bsize
        );
    } else {
        printf(" Client %llu has invalid fd\n", to_cli_answer.to_uid);
    }
    
    packet_free(allc, &to_cli_answer);
    qblock_free(allc, &ansblock);

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

    map_init(
        &serv->dcn_sessions,
        sizeof(ullong),
        sizeof(ullong)
    );
}

void dcn_serv_stop(
    struct dcn_server *serv
){
    // all clients are freed in epldplx server
    map_free(&serv->dcn_clients);
    map_free(&serv->dcn_sessions);
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
