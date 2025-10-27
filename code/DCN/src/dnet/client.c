#include <dnet/client.h>
#include <threads.h>

// async function in run_client (gather any responses)
void *__dcn_on_message(void *_args){
    struct worker_args *args = _args;
    struct queue *readq = args->qr;
    struct queue *writeq = args->qw;
    struct dcn_session *session = args->state_holder;
    struct allocator *allc = session->client->allc;
    
    struct qblock input;
    qblock_init(&input);
    if (1 == pop_block(readq, &input))
        return NULL;

    struct packet *pack = alc_malloc(allc, sizeof(struct packet));
    packet_deserial(allc, pack, &input);

    if (pack->to_uid != session->cli_uid){
        packet_free(allc, pack);
        return NULL;
    }

    map_set(&session->responses, &pack->muid, &pack);
    
    struct waiter **waiter_ptr = NULL;
    if (map_at(&session->cnd_responses, &pack->muid, (void**)&waiter_ptr)){
        waiter_set(*waiter_ptr);
    }

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
    session->last_muid = 0;
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

    queue_init(&session->readq, client->allc);
    queue_init(&session->writeq, client->allc);
}

void dcn_end_session(
    struct dcn_session *session
){
    atomic_store(&session->is_active, false);
    thrd_join(session->runnerthr, NULL);

    session->last_muid = 0;
    session->cli_uid = 0;
    session->client = NULL;
    
    queue_free(&session->writeq);
    queue_free(&session->readq);
    map_free(&session->cnd_responses);
    map_free(&session->responses);
}

ullong dcn_request(
    struct dcn_session *session,
    struct packet *packet,
    ullong to_uid,

    struct waiter **waiter
){    
    packet->from_uid = session->cli_uid;
    packet->muid = session->last_muid;
    packet->to_uid = to_uid;

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
    struct packet *pack
){
    struct packet **pack_ptr = NULL;
    if (map_at(&session->responses, &resp_n, (void**)&pack_ptr)){
        if (pack_ptr && *pack_ptr) {
            struct packet *src = *pack_ptr;

            pack->from_uid = src->from_uid;
            pack->to_uid = src->to_uid;
            pack->muid = src->muid;
            qblock_init(&pack->data);
            qblock_copy(session->client->allc, &pack->data, &src->data);
            
            packet_free(session->client->allc, src);
            alc_free(session->client->allc, src);
            map_erase(&session->responses, &resp_n);
            return true;
        }
    }
    return false;
}

void wait_response(
    struct dcn_session *session,
    struct waiter *waiter,
    ullong muid
){
    waiter_wait(waiter);
    waiter_free(session->client->allc, waiter);
    map_erase(&session->cnd_responses, &muid);
}
