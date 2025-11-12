#include <dnet/router.h>
#include <threads.h>

void router_init(
    struct ev_loop   *loop,
    struct allocator *allc,
    struct logger    *logger,
    struct router    *router
){
    router->loop   = loop;
    router->allc   = allc;
    router->logger = logger;

    router->states   = NULL;
    router->states_n = 0;
    router->states_cap = 0;

    srand(time(NULL));
    router->uids = rand();

    atomic_store(&router->is_running, false);
}

void router_link(
    struct router  *router,
    char           *serv_ip,
    unsigned short  serv_port
){
    if (atomic_load(&router->is_running)) {
        dblog(router->logger, ERROR, "router_link called while router is running");
        return;
    }

    if (router->states_n == router->states_cap) {
        size_t new_cap = router->states_cap == 0 ? 1 : router->states_cap * 2;
        router->states = alc_realloc(
            router->allc,
            router->states,
            sizeof(struct dnet_state*) * new_cap
        );
        router->states_cap = new_cap;
    }

    struct dnet_state *state = alc_malloc(router->allc, sizeof(struct dnet_state));
    dnet_state(
        state,
        router->loop,
        router->allc,
        serv_ip,
        serv_port,
        router->uids++
    );

    router->states[router->states_n++] = state;
}

int __router_runner(void *_args){
    struct router_task *task   = _args;
    struct router      *router = task->router;
    struct logger      *lgr    = router->logger; 
    
    struct dnet_state  *mst     = router->states[task->my_dstate];
    struct dcn_session *session = &mst->session;


    session->lgr = lgr;
    dblog(router->logger, INFO, "at %zu dnet_run...", task->my_dstate);

    dnet_run(mst);
    dblog(router->logger, INFO, "at %zu ping...", task->my_dstate);
    await(ping(session));

    dblog(router->logger, INFO, "running %zu", task->my_dstate);
    while (atomic_load(&router->is_running)){
        size_t num_states = router->states_n;
        if (task->my_dstate >= num_states){
            dblog(router->logger, ERROR, "%zu is out of bounds (%zu)", task->my_dstate, num_states);
            thrd_yield();
            continue;
        }

        dblog(router->logger, INFO, "waiting requests");
        Future *any_req = async_misc_grequests(session, -1.0f);
        struct packet *req_packet = await(any_req);

        if (req_packet == NULL){
            dblog(lgr, ERROR, "Incoming request is null in DSTATE: %zu", task->my_dstate);
            continue;
        }

        if (!(req_packet->packtype == SIG_BROADCAST)){
            dblog(lgr, ERROR, "Incoming request is not sig-broadcast: %i", req_packet->packtype);
            packet_free(session->client->allc, req_packet);
            alc_free(session->client->allc, req_packet);
            continue;
        }

        ullong orig_trav_fuid = req_packet->trav_fuid == 0 ? req_packet->from_uid : req_packet->trav_fuid;

        for (size_t i = 0; i < num_states; i++){
            if (i == task->my_dstate) continue;
            struct dnet_state *tstate = router->states[i];
            struct dcn_session *tsession = &tstate->session;

            struct packet *packet_copy = copy_packet(session->client->allc, req_packet);

            Future *retr = request(
                tsession, 
                packet_copy, 
                0, 
                orig_trav_fuid, 
                req_packet->packtype
            );
            await(retr);
            
            // For signals, request() doesn't free the packet, so we need to free it here
            packet_free(session->client->allc, packet_copy);
            alc_free(session->client->allc, packet_copy);

            dblog(lgr, INFO, 
                "  broadcasting #%zu from %s:%i to %s:%i (from %llu)", i, 
                mst->socket.ip, mst->socket.port,
                tstate->socket.ip, tstate->socket.port,
                orig_trav_fuid
            );
        }

        packet_free(session->client->allc, req_packet);
        alc_free(session->client->allc, req_packet);
    }
    
    alc_free(router->allc, task);
    return thrd_success;
}

void router_run(struct router *router){
    atomic_store(&router->is_running, true);

    thrd_t *threads = alc_malloc(router->allc, sizeof(thrd_t) * router->states_n);
    
    dblog(router->logger, INFO, "linked with %zu servers", router->states_n);
    for (size_t i = 0; i < router->states_n; i++){
        struct router_task *task = alc_malloc(router->allc, sizeof(struct router_task));
        task->router = router;
        task->my_dstate = i;

        dblog(router->logger, INFO, " running __router_runner at %zu server", router->states_n);
        thrd_create(
            &threads[i], 
            __router_runner, 
            task
        );
    }

    dblog(router->logger, INFO, "joining threads");
    for (size_t i = 0; i < router->states_n; i++)
        thrd_join(threads[i], NULL);

    alc_free(router->allc, threads);
}

void router_stop(struct router *router){
    atomic_store(&router->is_running, false);
    for (size_t i = 0; i < router->states_n; i++){
        struct dnet_state *state = router->states[i];
        if (state == NULL) continue;
        dnet_stop(state);
        alc_free(router->allc, state);
    }
    router->states_n = 0;
    router->states_cap = 0;
    alc_free(router->allc, router->states);
    router->states = NULL;
}

