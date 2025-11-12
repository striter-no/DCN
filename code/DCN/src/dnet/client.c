#include <dnet/client.h>
#include <threads.h>
#include <time.h>

// async function in run_client (gather any responses)
void *__dcn_on_message(void *_args){
    //**printf("__dcn_on_message: started\n");
    struct worker_args *args = _args;
    struct queue *readq = args->qr;
    // struct queue *writeq = args->qw;
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
    
    if (pack->from_os){
        dblog(session->lgr, INFO, "Processing user message: from_uid=%llu, packtype=%d, muid=%llu", 
            pack->from_uid, pack->packtype, pack->muid);
        
        mtx_lock(&session->usr_responses_mtx);
        
        struct usr_resp *targ = NULL;
        if (map_at(&session->usr_responses, &pack->from_uid, (void**)&targ)){
            if (pack->packtype != RESPONSE){
                struct qblock data;
                packet_serial(allc, pack, &data);
                push_block(targ->requests, &data);
                qblock_free(targ->requests->allc, &data);
                dblog(session->lgr, INFO, "Added request to req queue for user %llu (new size: %zu)", 
                    pack->from_uid, targ->requests->bsize);
            } else {
                struct packet *cp_pack = copy_packet(allc, pack);
                struct map *responses = &targ->responses;
                map_set(responses, &pack->cmuid, &cp_pack);
            }
            
        } else {
            struct usr_resp loc;
            loc.requests  = alc_malloc(allc, sizeof(struct queue));
            queue_init(loc.requests, allc);
            map_init(&loc.responses, sizeof(ullong), sizeof(struct packet *));

            if (pack->packtype != RESPONSE){
                struct qblock data;
                packet_serial(allc, pack, &data);
                push_block(loc.requests, &data);
                qblock_free(loc.requests->allc, &data);
                dblog(session->lgr, INFO, "Added request to req queue for user %llu (new size: %zu)", 
                    pack->from_uid, loc.requests->bsize);
            } else {
                struct packet *cp_pack = copy_packet(allc, pack);
                struct map *responses = &loc.responses;
                map_set(responses, &pack->cmuid, &cp_pack);
            }

            map_set(&session->usr_responses, &pack->from_uid, (void**)&loc);
            dblog(session->lgr, INFO, "Created new queues for user %llu, added to %s (size: 1)", 
                pack->from_uid, (pack->packtype != RESPONSE) ? "requests" : "responses");
        }
        
        mtx_unlock(&session->usr_responses_mtx);

        struct usr_waiter *waiter = NULL;
        if (map_at(&session->usr_waiters, &pack->from_uid, (void**)&waiter)) {
            if (pack->packtype != RESPONSE) {
                dblog(session->lgr, INFO, "Setting request waiter for user %llu", pack->from_uid);
                waiter_set(waiter->req_waiter);
            } else {
                dblog(session->lgr, INFO, "Setting response waiter for user %llu", pack->from_uid);
                waiter_set(waiter->resp_waiter);
            }
        } else {
            dblog(session->lgr, WARNING, "No waiter found for user %llu", pack->from_uid);
        }
        
        if (pack->packtype != RESPONSE){
            dblog(session->lgr, INFO, "Setting request waiter [just cuz]");
            waiter_set(&session->req_waiter);
        }

        // freeing because its data was copied to req/resp queue
        dblog(session->lgr, INFO, "Freeing original packet from user %llu", pack->from_uid);
        packet_free(allc, pack);
        alc_free(allc, pack);
        return NULL;
    } else if (pack->packtype == TRACEROUTE) {
        struct trp_data data;
        trp_data_deserial(&data, &pack->data);

        if (!data.is_response){
            struct qblock req;
            qblock_init(&req);
            qblock_fill(allc, &req, (char*)&data.req_uid, sizeof(data.req_uid));

            push_block(&session->trr_requests, &req);
            qblock_free(allc, &req);
            
            waiter_set(&session->trr_waiter);
            return NULL;
        }

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
    session->lgr = NULL;

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
    mtx_init(&session->usr_responses_mtx, mtx_plain);
    mtx_init(&session->usr_waiters_mtx, mtx_plain);


    // traceroute section
    queue_init(&session->trr_requests, client->allc);
    map_init(&session->trr_responses, sizeof(ullong), sizeof(ullong));
    waiter_init(client->allc, &session->trr_waiter);
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
        map_free(&val->responses);
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
    mtx_destroy(&session->usr_responses_mtx);
    mtx_destroy(&session->usr_waiters_mtx);
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
            pack->trav_fuid     = src->trav_fuid;
            pack->trav_tuid     = src->trav_tuid;
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
                    case 500: *rcode = SERVER_ERROR; break; // 6

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

    if (pack->packtype != RESPONSE && pack->packtype != SIGNAL && pack->packtype != SIG_BROADCAST){
        // mtx_lock(&session->usr_waiters_mtx);
        if (!map_in(&session->usr_waiters, &pack->to_uid)){
            dblog(lg, INFO, "initing new waiter");
            struct usr_waiter waiter;
            waiter.req_waiter = alc_malloc(allc, sizeof(struct waiter));
            waiter.resp_waiter = alc_malloc(allc, sizeof(struct waiter));
            waiter_init(allc, waiter.req_waiter);
            waiter_init(allc, waiter.resp_waiter);

            map_set(&session->usr_waiters, &pack->to_uid, &waiter);
        }
        // mtx_unlock(&session->usr_waiters_mtx);
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

    if (pack->packtype == RESPONSE){
        dblog(lg, INFO, "skipping waiting, becuse request type is RESPONSE");
        dblevel_pop(lg);
        free(tsk);
        return NULL;
    }

    dblog(lg, INFO, "searching for waiter");
    struct usr_waiter *waiter_str = NULL;

    bool response_exists = false;
    mtx_lock(&session->usr_responses_mtx);
    struct usr_resp *lc_ursp_check = NULL;
    if (map_at(&session->usr_responses, &pack->to_uid, (void**)&lc_ursp_check)) {
        response_exists = !map_empty(&lc_ursp_check->responses);
    }

    if (!response_exists) {
        mtx_unlock(&session->usr_responses_mtx);
        // If there no response
        map_at(&session->usr_waiters, &pack->to_uid, (void**)&waiter_str);
        dblog(lg, INFO, "waiting waiter");
        if (waiter_str) {
            waiter_wait(waiter_str->resp_waiter);
        }

    } else {
        dblog(lg, INFO, "response already exists, skipping wait");
    }

    // doubtfully 
    // map_erase  (&session->usr_waiters, &pack->to_uid);
    
    dblog(lg, INFO, "searching response");
    struct map *urs = &session->usr_responses;
    struct usr_resp *lc_ursp = NULL;

    // this user (to_uid) didn't send anything (very unlikely)
    if (!map_at(urs, &tsk->pack->to_uid, (void**)&lc_ursp )){
        mtx_unlock(&session->usr_responses_mtx);

        dblog(lg, ERROR, "searching response failed! (uid unregistered)");
        // alc_free(allc, answer_packet);
        dblevel_pop(lg);
        free(tsk);
        return NULL;
    }

    struct packet **pack_ptr = NULL;

    // no blocks in responses queue (very unlikely)
    if (!map_at(&lc_ursp->responses, &pack->cmuid, (void**)&pack_ptr)){
        mtx_unlock(&session->usr_responses_mtx);
        dblog(lg, ERROR, "no responses (map contains no such CMUID or is empty)");
        // alc_free(allc, answer_packet);
        dblevel_pop(lg);
        free(tsk);
        return NULL;
    }
    
    struct packet *answer_packet = copy_packet(allc, *pack_ptr);
    dblog(lg, INFO, "[REQ_TYPE:%i] deserializing packet (%zu bytes): %s ",
        pack->packtype, answer_packet->data.dsize, answer_packet->data.data);
    packet_free(allc, *pack_ptr);
    alc_free(allc, *pack_ptr);

    map_erase(&lc_ursp->responses, &pack->cmuid);
    mtx_unlock(&session->usr_responses_mtx);

    
    dblevel_pop(lg);
    free(tsk);
    return (void*)answer_packet;
}

// future -> struct packet * (get response) allc
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
    pack->trav_fuid = 0;
    pack->trav_tuid = 0;

    if (to_uid != 0){
        ullong *last_cmuid = NULL;
        if (map_at(&session->last_c_muids, &to_uid, (void**)&last_cmuid)){
            pack->cmuid = (packtype != RESPONSE ? (*last_cmuid) + 1: *last_cmuid);
        } else if (packtype == RESPONSE) {
            dblog(session->lgr, FATAL, "First packet to request with is RESPONSE");
            return NULL;
        } else {
            pack->cmuid = 0;
            map_set(&session->last_c_muids, &to_uid, &pack->cmuid);
        }
    } else {
        pack->cmuid = 0;
    }

    struct dcn_task *tsk = malloc(sizeof(struct dcn_task));
    tsk->session = session; 
    tsk->pack = pack;

    struct ev_loop *loop = session->client->loop;
    return async_create(loop, __async_req, tsk);
}

void *__async_ping(void *_args){
    struct dcn_task *tsk = _args;
    struct dcn_session *session = tsk->session;
    struct packet *pack = tsk->pack;

    dblog(session->lgr, INFO, "__async_ping called");
    struct allocator *allc = session->client->allc;
    RESP_CODE rcode;
    while (true) {
        struct packet answer;
        struct waiter *waiter = NULL;

        dblog(session->lgr, INFO, "__async_ping: ping requested");
        ullong resp_n = dcn_request(session, pack, pack->to_uid, &waiter);
        dblog(session->lgr, INFO, "__async_ping: awaiting ping");
        wait_response(session, waiter, resp_n);

        dcn_getresp(session, resp_n, &answer, &rcode);
        dblog(session->lgr, INFO, "__async_ping: %i", rcode);
        if (rcode == OK_STATUS){
            packet_free(allc, &answer);
            break;
        }

        packet_free(allc, &answer);
    }

    packet_free(allc, pack);
    alc_free(allc, tsk);
    return NULL;
}

Future *ping(
    struct dcn_session *session
){
    struct packet *pack = alc_malloc(session->client->allc, sizeof(struct packet));
    packet_init(session->client->allc, pack, NULL, 0, session->cli_uid, 0, 0);
    pack->packtype = PING;

    struct dcn_task *tsk = malloc(sizeof(struct dcn_task));
    tsk->session = session; 
    tsk->pack = pack;

    return async_create(
        session->client->loop,
        __async_ping,
        tsk
    );
}

void *__async_grequests(void *_args){
    struct grsps_task *tsk = _args;
    struct dcn_session *session = tsk->session;
    struct allocator *allc = session->client->allc;
    ullong wait_from = tsk->from_uid;
    double timeout_sec = tsk->timeout_sec;
    bool has_timeout = timeout_sec >= 0.0;
    struct logger *lg = session->lgr;
    
    dblevel_push(lg, "__async_grequests");
    dblog(lg, INFO, "gathering from %i", wait_from);
    dblog(lg, INFO, "timeout is %f", timeout_sec);
    
    struct packet *answer_packet = alc_calloc(allc, 1, sizeof(struct packet));
    packet_init(allc, answer_packet, NULL, 0, 0, 0, 0);

    // misc mode
    if (wait_from == 0){
        bool ready = true;
        if (has_timeout) {
            ready = waiter_wait_for(&session->req_waiter, timeout_sec);
        } else {
            waiter_wait(&session->req_waiter);
        }

        if (!ready){
            dblog(lg, WARNING, "waiting for miscellaneous request timed out (%.3fs)", timeout_sec);
            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }

        struct map *urs = &session->usr_responses;
        mtx_lock(&urs->_mtx);
        size_t len = urs->len;
        mtx_unlock(&urs->_mtx);
        
        bool was_req = false;
        struct usr_resp *lc_ursp = NULL;
        for (size_t i = 0; i < len; i++){
            ullong wait_from; 
            map_key_at(urs, &wait_from, i);
            
            mtx_lock(&session->usr_responses_mtx);
            if (!map_at(urs, &wait_from, (void**)&lc_ursp )) {
                mtx_unlock(&session->usr_responses_mtx);
                continue;
            }

            struct qblock resp_block;
            qblock_init(&resp_block);

            // no blocks in responses queue
            if (1 == pop_block(lc_ursp->requests, &resp_block)) {
                mtx_unlock(&session->usr_responses_mtx);
                continue;
            }

            packet_deserial(allc, answer_packet, &resp_block);
            qblock_free(lc_ursp->requests->allc, &resp_block);
            was_req = true;
            mtx_unlock(&session->usr_responses_mtx);
            break;
        }

        // very unlikely
        if (!was_req){
            dblog(lg, ERROR, "[got \"very unlikely\"] no requests after misc gathering");

            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }

    } else {
        dblog(lg, INFO, "waiting for request from %llu", wait_from);
        struct usr_waiter *waiter = NULL;
        
        // mtx_lock(&session->usr_waiters_mtx);
        if (!map_at(&session->usr_waiters, &wait_from, (void**)&waiter)){
            // mtx_unlock(&session->usr_waiters_mtx);
            dblog(lg, ERROR, "no usr_waiter for %llu", wait_from);
            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }
        // mtx_unlock(&session->usr_waiters_mtx);

        dblog(lg, INFO, "waiter wait for %llu", wait_from);
        bool ready = true;
        if (has_timeout) {
            ready = waiter_wait_for(waiter->req_waiter, timeout_sec);
        } else {
            waiter_wait(waiter->req_waiter);
        }
        if (!ready){
            dblog(lg, WARNING, "waiting for request from %llu timed out (%.3fs)", wait_from, timeout_sec);
            alc_free(allc, answer_packet);
            dblevel_pop(lg);
            free(tsk);
            return NULL;
        }
        dblog(lg, INFO, "request from %llu received", wait_from);
        
        struct map *urs = &session->usr_responses;
        struct usr_resp *lc_ursp = NULL;

        mtx_lock(&session->usr_responses_mtx);
        // this user (to_uid) didn't send anything (very unlikely)
        if (!map_at(urs, &wait_from, (void**)&lc_ursp )){
            mtx_unlock(&session->usr_responses_mtx);
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
            mtx_unlock(&session->usr_responses_mtx);
            dblog(lg, WARNING, "no blocks in requests queue (very unlikely)");
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
        mtx_unlock(&session->usr_responses_mtx);
    }

    dblevel_pop(lg);
    free(tsk);
    
    if (answer_packet == NULL){
        dblog(lg, ERROR, "answer packet is NULL");
    }

    return (void*)answer_packet; // return received struct packet * (allocated)
}

// future -> struct packet * (get request) allc
Future *async_grequests(
    struct dcn_session *session,
    ullong from_uid
){
    struct grsps_task *task = malloc(sizeof(struct grsps_task));
    task->from_uid = from_uid;
    task->session = session;
    task->timeout_sec = -1.0;

    return async_create(
        session->client->loop,
        __async_grequests,
        task
    );
}

// future -> struct packet * (get request) allc
Future *async_misc_grequests(
    struct dcn_session *session,
    double timeout_sec
){
    struct grsps_task *task = malloc(sizeof(struct grsps_task));
    task->from_uid = 0;
    task->session = session;
    task->timeout_sec = timeout_sec;

    return async_create(
        session->client->loop,
        __async_grequests,
        task
    );
}

void *__async_traceroute(void *_args){
    struct trr_task *task = _args;
    struct dcn_session *session = task->session;
    struct allocator   *allc = session->client->allc;
    struct packet      *trp = task->trp;

    RESP_CODE rcode;
    
    struct packet answer;
    struct waiter *waiter = NULL;
    ullong resp_n = dcn_request(session, trp, trp->to_uid, &waiter);

    wait_response(session, waiter, resp_n);
    
    dcn_getresp(session, resp_n, &answer, &rcode);
    if (answer.data.dsize != sizeof(struct trp_data)){
        packet_free(allc, &answer);
        packet_free(allc, trp);
        alc_free(allc, task);
        return NULL;
    }

    struct trp_data *trp_data = alc_malloc(allc, sizeof(struct trp_data));
    trp_data_deserial(trp_data, &answer.data);

    packet_free(allc, &answer);
    packet_free(allc, trp);
    alc_free(allc, task);
    return (void*)trp_data;
}

// future -> struct trp_data* (allc)
Future *traceroute(
    struct dcn_session *session,
    ullong uid_ttr
){
    struct trp_data trp_data;
    trp_data.is_response = false;
    trp_data.req_uid     = uid_ttr;
    trp_data.success     = false;

    struct packet trp = create_traceroute(
        session->client->allc, 
        trp_data,
        session->cli_uid
    );

    struct trr_task *trr_task = alc_malloc(
        session->client->allc, 
        sizeof(struct trr_task)
    );

    trr_task->session = session;
    trr_task->trp = copy_packet(
        session->client->allc, 
        &trp
    );

    return async_create(
        session->client->loop,
        __async_traceroute,
        trr_task
    );
}

void *__async_gtraceroutes(void *_args){
    struct dcn_session *session = _args;
    struct allocator   *allc    = session->client->allc;
    struct qblock       tdata;
    qblock_init(&tdata);

    waiter_wait(&session->trr_waiter);
    if (1 == pop_block(&session->trr_requests, &tdata)){
        return NULL;
    }

    struct trp_data *trp = alc_malloc(allc, sizeof(struct trp_data));
    trp_data_deserial(trp, &tdata);

    qblock_free(allc, &tdata);
    return (void*)trp;
}

// future -> struct trp_data * (allocated/allc)
Future *async_gtraceroutes(
    struct dcn_session *session
){
    return async_create(
        session->client->loop,
        __async_gtraceroutes,
        session
    );
}

void *__async_traceroute_ans(void *_args){
    struct grsps_task  *task = _args;
    struct dcn_session *session = task->session;
    struct allocator   *allc = session->client->allc;
    ullong who_exists = task->from_uid;

    struct trp_data tr_data;
    tr_data.is_response = true;
    tr_data.req_uid = who_exists;
    tr_data.success = true;
    struct packet pack = create_traceroute(allc, tr_data, session->cli_uid);
    
    RESP_CODE rcode;
    struct packet answer;
    struct waiter *waiter = NULL;
    ullong resp_n = dcn_request(session, &pack, 0, &waiter);

    
    wait_response(session, waiter, resp_n);
    dcn_getresp(session, resp_n, &answer, &rcode);
    
    bool *is_ok = alc_malloc(allc, sizeof(bool));
    *is_ok = rcode == OK_STATUS;
    
    packet_free(allc, &answer);
    packet_free(allc, &pack);
    alc_free(allc, task);

    alc_free(allc, task);
    return (void*)is_ok;
}

// future -> bool* (allc)
Future *traceroute_ans(
    struct dcn_session *session,
    ullong who_exists
){
    struct grsps_task *task = alc_malloc(session->client->allc, sizeof(struct grsps_task));
    task->session = session;
    task->from_uid = who_exists;
    task->timeout_sec = -1.0;

    return async_create(
        session->client->loop,
        __async_traceroute_ans,
        task
    );
}

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
