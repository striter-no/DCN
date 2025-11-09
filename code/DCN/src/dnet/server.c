#include <stdint.h>
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

void traceroute(
    struct dcn_server *server,
    struct client *from,

    ullong UID_TTR
){
    mtx_lock(&server->dcn_clients._mtx);
    size_t len = server->dcn_clients.len;
    mtx_unlock(&server->dcn_clients._mtx);

    array_append(&server->trace_requests, &UID_TTR);

    for (size_t i = 0; i < len; i++){
        ullong key; 
        if (0 != map_key_at(&server->dcn_clients, &key, i))
            continue;

        struct client **to_cli_ptr = NULL;
        if (!map_at(&server->dcn_clients, &key, (void**)&to_cli_ptr))
            continue;
        
        struct client *to_cli = *to_cli_ptr;
        if (!(to_cli_ptr && *to_cli_ptr)) 
            continue;

        if (to_cli->fd == from->fd) 
            continue;

        struct trp_data data;
        data.is_response = false;
        data.success     = false;
        data.req_uid     = UID_TTR;

        struct packet trp = create_traceroute(
            server->allc,
            data, 
            0
        );

        struct qblock trp_block;
        packet_serial(server->allc, &trp, &trp_block);
        push_block(&to_cli->write_q, &trp_block);

        qblock_free(server->allc, &trp_block);
        packet_free(server->allc, &trp);
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

    printf(" Processing packet: from=%llu, to=%llu, muid=%llu, data_size=%zu, packtype=%i, from_os=%i, cmuid=%llu\n", 
       pack.from_uid, pack.to_uid, pack.muid, pack.data.dsize, pack.packtype, pack.from_os, pack.cmuid);

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
        answer.cmuid = 0;

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
    answer.cmuid = 0;

    if (pack.to_uid == 0){
        if (pack.packtype != TRACEROUTE){
            printf(" to_uid == 0 and packtype == %i -> forbidden\n", pack.packtype);

            int forbidden = 102;
            qblock_fill(allc, &answer.data, (char*)&forbidden, sizeof(forbidden));
            struct qblock ansblock;
            packet_serial(allc, &answer, &ansblock);
            push_block(&cli->write_q, &ansblock);
            qblock_free(allc, &ansblock);

            packet_free(allc, &pack);
            printf("dcn_async_worker exit\n");
            return NULL;
        }

        // traceroute
        struct trp_data trp_data;
        if (!trp_data_deserial(&trp_data, &pack.data)){
            printf(" cannot deserial traceroute\n");

            int fail = 500;
            qblock_fill(allc, &answer.data, (char*)&fail, sizeof(fail));
            struct qblock ansblock;
            packet_serial(allc, &answer, &ansblock);
            push_block(&cli->write_q, &ansblock);
            qblock_free(allc, &ansblock);

            packet_free(allc, &pack);
            printf("dcn_async_worker exit\n");
            return NULL;
        }

        // check server requests
        if (trp_data.is_response){
            mtx_lock(&serv->trace_requests._mtx);
            
            size_t inx = array_index(
                &serv->trace_requests,
                &trp_data.req_uid
            );
            
            int code = 200;
            if (inx == SIZE_MAX)
                code = 103;
            else
                array_del(
                    &serv->trace_requests,
                    inx
                );

            mtx_unlock(&serv->trace_requests._mtx);

            qblock_fill(allc, &answer.data, (char*)&code, sizeof(code));
            struct qblock ansblock;
            packet_serial(allc, &answer, &ansblock);
            push_block(&cli->write_q, &ansblock);
            qblock_free(allc, &ansblock);

            packet_free(allc, &pack);
            printf("dcn_async_worker exit\n");
            
        // it is request
        } else {
            mtx_lock(&serv->trace_requests._mtx);
            array_append(
                &serv->trace_requests, 
                &trp_data.req_uid
            );
            traceroute(serv, cli, trp_data.req_uid);
            mtx_unlock(&serv->trace_requests._mtx);
        }

        return NULL;
    }

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

    if (pack.packtype != BROADCAST && pack.packtype != SIG_BROADCAST){

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
        to_cli_answer.cmuid = pack.cmuid;
        to_cli_answer.packtype = pack.packtype;
        packet_free(allc, &pack);
        packet_serial(
            allc, 
            &to_cli_answer, 
            &ansblock
        );
        
        if (to_cli->fd > 0) {
            push_block(&to_cli->write_q, &ansblock);
            printf(
                " message sent (%llu->%llu (%i fd) (#%llu/%llu) %zu bytes) | queue size: %zu\n", 
                to_cli_answer.from_uid, 
                to_cli_answer.to_uid, 
                to_cli->fd,
                to_cli_answer.muid,
                to_cli_answer.cmuid,
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
    } else {
        mtx_lock(&serv->dcn_clients._mtx);
        size_t len = serv->dcn_clients.len;
        mtx_unlock(&serv->dcn_clients._mtx);

        for (size_t i = 0; i < len; i++){
            ullong key; 
            if (0 != map_key_at(&serv->dcn_clients, &key, i))
                continue;

            struct client **to_cli_ptr = NULL;
            if (!map_at(&serv->dcn_clients, &key, (void**)&to_cli_ptr))
                continue;

            if (key == pack.from_uid) continue;
            
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
                key,
                0
            );
            to_cli_answer.from_os = true;
            to_cli_answer.cmuid = pack.cmuid;
            to_cli_answer.packtype = pack.packtype;
            packet_free(allc, &pack);
            packet_serial(
                allc, 
                &to_cli_answer, 
                &ansblock
            );
            
            if (to_cli->fd > 0) {
                push_block(&to_cli->write_q, &ansblock);
                printf(
                    " message sent (%llu->%llu (%i fd) (#%llu/#%llu) %zu bytes) | queue size: %zu\n", 
                    to_cli_answer.from_uid, 
                    to_cli_answer.to_uid, 
                    to_cli->fd,
                    to_cli_answer.muid,
                    to_cli_answer.cmuid,
                    to_cli_answer.data.dsize,
                    to_cli->write_q.bsize
                );
            } else {
                printf(" Client %llu has invalid fd\n", to_cli_answer.to_uid);
            }
            
            packet_free(allc, &to_cli_answer);
            qblock_free(allc, &ansblock);

            printf("dcn_async_worker exit\n");
        }
        return NULL;
    }
}

void dcn_serv_init(
    struct allocator  *allc,
    struct dcn_server *serv,
    struct ev_loop    *loop,
    struct ssocket_md  *sock,
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
