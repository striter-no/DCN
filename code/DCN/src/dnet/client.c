#include <dnet/client.h>
#include <threads.h>

// async function in run_client (gather any responses)
void *__dcn_on_message(void *_args){
    //**printf("__dcn_on_message: started\n");
    struct worker_args *args = _args;
    struct queue *readq = args->qr;
    struct queue *writeq = args->qw;
    struct dcn_session *session = args->state_holder;
    struct allocator *allc = session->client->allc;
    
    struct qblock input;
    qblock_init(&input);
    if (1 == pop_block(readq, &input)){
        fprintf(stderr,"[warning] on_message: pop block - no blocks left\n");
        return NULL;
    }

    struct packet *pack = alc_calloc(allc, 1, sizeof(struct packet));
    packet_deserial(allc, pack, &input);

    //**printf("__dcn_on_message: packet received - from_uid=%llu, to_uid=%llu, muid=%llu, from_os=%d, packtype=%d, data_size=%zu\n",
    //**   pack->from_uid, pack->to_uid, pack->muid, pack->from_os, pack->packtype, pack->data.dsize);


    if (pack->to_uid != session->cli_uid){
        packet_free(allc, pack);
        fprintf(stderr, "[warning] packet has different session uid (%llu instead of %llu)\n", pack->to_uid, session->cli_uid);
        return NULL;
    }
        
    // SWITCH TO ARRAY
    if (pack->from_os){
        struct usr_resp *targ = NULL;
        if (map_at(&session->usr_responses, &pack->from_uid, (void**)&targ)){
            struct queue *tq = ((
                pack->packtype != RESPONSE
            ) ? targ->requests : targ->responses);
            struct qblock data;
            packet_serial(allc, pack, &data);
            push_block(tq, &data);
            qblock_free(tq->allc, &data);

        } else {
            struct usr_resp loc;
            loc.responses = alc_malloc(allc, sizeof(struct queue));
            loc.requests  = alc_malloc(allc, sizeof(struct queue));
            queue_init(loc.requests, allc);
            queue_init(loc.responses, allc);

            struct queue *tq = ((pack->packtype != RESPONSE)? loc.requests : loc.responses);
            struct qblock data;
            packet_serial(allc, pack, &data);
            push_block(tq, &data);
            qblock_free(tq->allc, &data);

            map_set(&session->usr_responses, &pack->from_uid, (void**)&loc);
        }

        struct usr_waiter *waiter = NULL;
        if (map_at(&session->usr_waiters, &pack->from_uid, (void**)&waiter))
            waiter_set(
                (pack->packtype != RESPONSE) ? waiter->req_waiter : waiter->resp_waiter
            );
        
        if (pack->packtype != RESPONSE){
            waiter_set(&session->req_waiter);
        }

        // freeing because its data was copied to req/resp queue
        printf("Processing as user message with from=%llu\n", pack->from_uid); //**
        packet_free(allc, pack);
        alc_free(allc, pack);
        return NULL;
    } else {
        map_set(&session->responses, &pack->muid, &pack);
    }
    
    struct waiter **waiter_ptr = NULL;
    if (map_at(&session->cnd_responses, &pack->muid, (void**)&waiter_ptr)){
        //**printf(" waiter set (muid %llu)\n", pack->muid);
        waiter_set(*waiter_ptr);

    } else {
        fprintf(stderr, "[warning] waiter does not exists (muid %llu)\n", pack->muid);
    }

    //**printf("__dcn_on_message: exit\n");
    return NULL;
}

void dcn_cli_init(
    struct allocator  *allc,
    struct dcn_client *cli,
    struct ev_loop    *loop,
    struct socket_md  *sock
){
    cli->allc = allc;
    cli->loop = loop;
    cli->md   = sock;
}

int __dcn_runner(void *args){
    struct dcn_session *session = args;
    struct dcn_client *cli = session->client;
    run_client(
        cli->md, 
        &session->is_active, 
        cli->loop,
        __dcn_on_message, 
        &session->readq,
        &session->writeq,
        session
    );

    return thrd_success;
}

void dcn_cli_run(
    struct dcn_session *session
){
    thrd_create(
        &session->runnerthr, 
        __dcn_runner, 
        session
    );
}

void dcn_new_session(
    struct dcn_session *session,
    struct dcn_client *client,
    ullong uid
){
    session->cli_uid = uid;
    session->last_muid = 1;
    session->client = client;
    session->is_active = true;

    map_init(
        &session->responses,
        sizeof(ullong),
        sizeof(struct packet *)
    );

    map_init(
        &session->cnd_responses,
        sizeof(ullong),
        sizeof(struct waiter *)
    );
    
    map_init(
        &session->usr_responses,
        sizeof(ullong),
        sizeof(struct usr_resp)
    );

    map_init(
        &session->last_c_muids,
        sizeof(ullong),
        sizeof(ullong)
    );

    queue_init(&session->readq, client->allc);
    queue_init(&session->writeq, client->allc);

    map_init(
        &session->usr_waiters,
        sizeof(ullong),
        sizeof(struct usr_waiter)
    );

    waiter_init(session->client->allc, &session->req_waiter);
}

void dcn_end_session(
    struct dcn_session *session
){
    struct allocator *allc = session->client->allc;
    //**printf("dcn_end_session: started\n");
    atomic_store(&session->is_active, false);
    //**printf("dcn_end_session: is_active set to false\n");
    thrd_join(session->runnerthr, NULL);
    //**printf("dcn_end_session: runner thread joined\n");

    session->last_muid = 1;
    session->cli_uid = 0;
    session->client = NULL;
    
    queue_free(&session->writeq);
    queue_free(&session->readq);
    map_free(&session->cnd_responses);
    map_free(&session->responses);
    map_free(&session->last_c_muids);
    for (size_t i = 0; i < session->usr_responses.len; i++){
        ullong key;
        if (0 != map_key_at(&session->usr_responses, &key, i))
            continue;

        struct usr_resp *val = NULL;
        if (!map_at(&session->usr_responses, &key, (void**)&val))
            continue;

        // there were packets * (alloc)
        // i hope they were freed
        queue_free(val->requests);
        queue_free(val->responses);
        alc_free(allc, val->responses);
        alc_free(allc, val->requests);
    }
    map_free(&session->usr_responses);

    //**printf("dcn_end_session: waiter free\n");

    for (size_t i = 0; i < session->usr_waiters.len; i++){
        ullong key;
        if (0 != map_key_at(&session->usr_waiters, &key, i))
            continue;

        struct usr_waiter *val = NULL;
        if (!map_at(&session->usr_waiters, &key, (void**)&val))
            continue;

        // there were packets * (alloc)
        // i hope they were freed
        waiter_free(allc, val->req_waiter);
        waiter_free(allc, val->resp_waiter);
        alc_free(allc, val->req_waiter);
        alc_free(allc, val->resp_waiter);
    }
    map_free(&session->usr_waiters);
    //**printf("dcn_end_session: ended\n");

    waiter_free(allc, &session->req_waiter);
}

ullong dcn_request(
    struct dcn_session *session,
    struct packet *packet,
    ullong to_uid,

    struct waiter **waiter
){
    packet->from_uid = session->cli_uid;
    packet->to_uid = to_uid;
    packet->from_os = false;
    packet->muid = session->last_muid;

    struct qblock block;
    packet_serial(session->client->allc, packet, &block);
    push_block(&session->writeq, &block);
    qblock_free(session->client->allc, &block);

    session->last_muid++;
    struct waiter *wt = alc_calloc(session->client->allc, 1, sizeof(struct waiter));
    waiter_init(session->client->allc, wt);
    
    struct waiter **wt_ptr = &wt;
    map_set(&session->cnd_responses, &packet->muid, wt_ptr);

    *waiter = wt;
    return packet->muid;
}

bool dcn_getresp(
    struct dcn_session *session,
    ullong resp_n,
    struct packet *pack,
    RESP_CODE *rcode
){
    struct packet **pack_ptr = NULL;
    if (map_at(&session->responses, &resp_n, (void**)&pack_ptr)){
        if (pack_ptr && *pack_ptr) {
            struct packet *src = *pack_ptr;

            pack->from_uid = src->from_uid;
            pack->to_uid   = src->to_uid;
            pack->from_os  = src->from_os;
            pack->packtype = src->packtype;
            pack->muid     = src->muid;
            qblock_init(&pack->data);
            qblock_copy(session->client->allc, &pack->data, &src->data);
            
            packet_free(session->client->allc, src);
            alc_free(session->client->allc, src);
            map_erase(&session->responses, &resp_n);
            
            if (pack->from_uid == 0){
                int code;
                memcpy(&code, pack->data.data, sizeof(code));
                switch (code) {
                    case 101: *rcode = NO_SUCH_USER; break; // 2
                    case 102: *rcode = FORBIDEN_UID; break; // 4
                    case 103: *rcode = NO_DATA; break; // 5
                    case 200: *rcode = OK_STATUS; break; // 3

                    default:
                        *rcode = UNDEFINED_ERROR; // 0
                }
            } else {
                *rcode = FROM_USER; // 1
            }

            return true;
        }
    }
    *rcode = UNDEFINED_ERROR;
    return false;
}

void wait_response(
    struct dcn_session *session,
    struct waiter *waiter,
    ullong muid
){
    waiter_wait(waiter);
    waiter_free(session->client->allc,   waiter);
    map_erase  (&session->cnd_responses, &muid);
}

void *__async_req(void *_args){
    struct dcn_task *tsk = _args;
    struct dcn_session *session = tsk->session;
    struct packet *pack = tsk->pack;
    struct allocator *allc = tsk->session->client->allc;
    struct logger *lg = session->lgr;

    dblevel_push(lg, "__async_req");
    dblog(lg, INFO, "request to %llu (packtype %d)", pack->to_uid, pack->packtype);

    if (pack->packtype != SIGNAL && pack->packtype != SIG_BROADCAST)
        if (!map_in(&session->usr_waiters, &pack->to_uid)){
            dblog(lg, INFO, "initing new waiter");
            struct usr_waiter waiter;
            waiter.req_waiter = alc_malloc(allc, sizeof(struct waiter));
            waiter.resp_waiter = alc_malloc(allc, sizeof(struct waiter));
            waiter_init(allc, waiter.req_waiter);
            waiter_init(allc, waiter.resp_waiter);

            map_set(&session->usr_waiters, &pack->to_uid, &waiter);
        }

    dblog(lg, INFO, "starting request cycle");
    RESP_CODE rcode;
    while (true) {
        struct packet answer;
        struct waiter *waiter = NULL;

        // dblog(lg, INFO, "starting dcn_request");
        ullong resp_n = dcn_request(session, pack, pack->to_uid, &waiter);
        // dblog(lg, INFO, "waiting for response (%llu muid)", resp_n);
        wait_response(session, waiter, resp_n);

        // dblog(lg, INFO, "getting response...");
        dcn_getresp(session, resp_n, &answer, &rcode);
        if (rcode != NO_SUCH_USER){
            dblog(lg, INFO, "got non-NO_SUCH_USER code: %d", rcode);
            packet_free(allc, &answer);
            break;
        }

        packet_free(allc, &answer);
    }

    dblog(lg, INFO, "end of request cycle");
    
    if (pack->packtype == SIGNAL || pack->packtype == SIG_BROADCAST){
        dblog(lg, INFO, "packtype is sig-like, skipping response waiting");
        dblevel_pop(lg);
        free(tsk);
        return NULL;
    }

    dblog(lg, INFO, "searching for waiter");
    struct usr_waiter *waiter_str = NULL;
    map_at(&session->usr_waiters, &pack->to_uid, (void**)&waiter_str);
    dblog(lg, INFO, "waiting waiter");
    waiter_wait(waiter_str->resp_waiter);

    // doubtfully 
    // map_erase  (&session->usr_waiters, &pack->to_uid);
    
    dblog(lg, INFO, "searching response");
    struct map *urs = &session->usr_responses;
    struct packet *answer_packet = alc_malloc(allc, sizeof(struct packet));
    struct usr_resp *lc_ursp = NULL;

    // this user (to_uid) didn't send anything (very unlikely)
    if (!map_at(urs, &tsk->pack->to_uid, (void**)&lc_ursp )){
        dblog(lg, ERROR, "searching response failed! (uid unregistered)");
        alc_free(allc, answer_packet);
        dblevel_pop(lg);
        free(tsk);
        return NULL;
    }

    struct qblock resp_block;
    qblock_init(&resp_block);

    // no blocks in responses queue (very unlikely)
    if (1 == pop_block(lc_ursp->responses, &resp_block)){
        dblog(lg, ERROR, "no responses (queue is empty)");
        alc_free(allc, answer_packet);
        dblevel_pop(lg);
        free(tsk);
        return NULL;
    }

    dblog(lg, INFO, "deserializing packet");
    packet_deserial(allc, answer_packet, &resp_block);
    qblock_free(lc_ursp->responses->allc, &resp_block);

    dblevel_pop(lg);
    free(tsk);
    return (void*)answer_packet;
}

// future -> struct packet * (get response) non/allc raw
Future* request(
    struct dcn_session *session,
    struct packet *pack,
    ullong to_uid,
    PACKET_TYPE packtype
){
    pack->from_uid = session->cli_uid;
    pack->from_os  = false;
    pack->to_uid   = to_uid;
    pack->muid     = 0;
    pack->packtype = packtype;

    struct dcn_task *tsk = malloc(sizeof(struct dcn_task));
    tsk->session = session; 
    tsk->pack = pack;

    struct ev_loop *loop = session->client->loop;
    return async_create(loop, __async_req, tsk);
}

void *__async_grequests(void *_args){
    struct grsps_task *tsk = _args;
    struct dcn_session *session = tsk->session;
    struct allocator *allc = session->client->allc;
    ullong wait_from = tsk->from_uid;
    struct logger *lg = session->lgr;
    
    dblevel_push(lg, "__async_grequests");
    dblog(lg, INFO, "gathering from %i", wait_from);
    
    struct packet *answer_packet = alc_calloc(allc, 1, sizeof(struct packet));
    packet_init(allc, answer_packet, NULL, 0, 0, 0, 0);

    // misc mode
    if (wait_from == 0){
        waiter_wait(&session->req_waiter);

        struct map *urs = &session->usr_responses;
        mtx_lock(&urs->_mtx);
        size_t len = urs->len;
        mtx_unlock(&urs->_mtx);
        
        bool was_req = false;
        struct usr_resp *lc_ursp = NULL;
        for (size_t i = 0; i < len; i++){
            ullong wait_from; map_key_at(urs, &wait_from, i);
            
            if (!map_at(urs, &wait_from, (void**)&lc_ursp ))
                continue;

            struct qblock resp_block;
            qblock_init(&resp_block);

            // no blocks in responses queue
            if (1 == pop_block(lc_ursp->requests, &resp_block))
                continue;

            packet_deserial(allc, answer_packet, &resp_block);
            qblock_free(lc_ursp->requests->allc, &resp_block);
            was_req = true;
            break;
        }

        // very unlikely
        if (!was_req){
            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }

    } else {
        dblog(lg, INFO, "waiting for request from %llu", wait_from);
        //**printf("waiting for request from %llu\n", wait_from);
        struct usr_waiter *waiter = NULL;
        if (!map_at(&session->usr_waiters, &wait_from, (void**)&waiter)){
            dblog(lg, ERROR, "no usr_waiter for %llu", wait_from);
            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }

        dblog(lg, INFO, "waiter wait for %llu", wait_from);
        waiter_wait(waiter->req_waiter);
        dblog(lg, INFO, "request from %llu received\n", wait_from);
        struct map *urs = &session->usr_responses;
        struct usr_resp *lc_ursp = NULL;

        // this user (to_uid) didn't send anything (very unlikely)
        if (!map_at(urs, &wait_from, (void**)&lc_ursp )){
            dblog(lg, WARNING, "this user (to_uid: %llu) didn't send anything (very unlikely)", wait_from);
            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }

        struct qblock resp_block;
        qblock_init(&resp_block);

        // no blocks in responses queue (very unlikely)
        if (1 == pop_block(lc_ursp->requests, &resp_block)){
            dblog(lg, WARNING, "no blocks in responses queue (very unlikely)");
            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }

        dblog(lg, INFO, "deserializing response");
        packet_deserial(allc, answer_packet, &resp_block);
        dblog(lg, INFO, "response deserialized");
        dblog(lg, INFO, "response: %s", answer_packet->data.data);
        dblog(lg, INFO, "response size: %zu", answer_packet->data.dsize);
        qblock_free(lc_ursp->requests->allc, &resp_block);
    }

    dblevel_pop(lg);
    free(tsk);
    return (void*)answer_packet; // return received struct packet * (allocated)
}

// future -> struct packet * (get request) non/allc raw
Future *async_grequests(
    struct dcn_session *session,
    ullong from_uid
){
    struct grsps_task *task = malloc(sizeof(struct grsps_task));
    task->from_uid = from_uid;
    task->session = session;

    return async_create(
        session->client->loop,
        __async_grequests,
        task
    );
}

// future -> struct packet * (get request) non/allc raw
Future *async_misc_grequests(
    struct dcn_session *session
){
    struct grsps_task *task = malloc(sizeof(struct grsps_task));
    task->from_uid = 0;
    task->session = session;

    return async_create(
        session->client->loop,
        __async_grequests,
        task
    );
}
