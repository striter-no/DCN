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
    router->uids = rand();
}

void router_link(
    struct router  *router,
    char           *serv_ip,
    unsigned short  serv_port
){
    router->states = alc_realloc(
        router->allc,
        router->states, 
        sizeof(struct dnet_state) * (router->states_n + 1)
    );
    router->states_n++;

    dnet_state(
        &router->states[router->states_n - 1],
        router->loop,
        router->allc,
        serv_ip,
        serv_port,
        router->uids++
    );
}

int __router_runner(void *_args){
    struct router_task *task       = _args;
    struct dnet_state  *all_states = task->router->states;
    size_t num_states              = task->router->states_n;

    struct dnet_state  *mst     = &all_states[task->my_dstate];
    struct dcn_session *session = &mst->session;
    struct allocator   *allc = task->router->allc;
    struct logger      *lgr  = task->router->logger; 

    while (true){
        Future *any_req = async_misc_grequests(session);
        struct packet *req_packet = await(any_req);

        if (req_packet == NULL){
            dblog(lgr, ERROR, "Incoming request is null in DSTATE: %zu", task->my_dstate);
            continue;
        }

        for (size_t i = 0; i < num_states; i++){
            if (i == task->my_dstate) continue;
            struct dnet_state *tstate = &all_states[i];
            struct dcn_session *tsession = &tstate->session;

            await(request(tsession, req_packet, 0, BROADCAST));
        }
    }
    
    free(task);
    return thrd_success;
}

void router_run(struct router *router){
    thrd_t *threads = alc_malloc(router->allc, sizeof(thrd_t) * router->states_n);
    
    for (size_t i = 0; i < router->states_n; i++){
        struct router_task *task = alc_malloc(router->allc, sizeof(struct router_task));

        thrd_create(
            &threads[i], 
            __router_runner, 
            &router->states[i]
        );
    }

    for (size_t i = 0; i < router->states_n; i++)
        thrd_join(threads[i], NULL);

    free(threads);
}

void router_stop(struct router *router){
    alc_free(router->allc, router->states);
    router->states_n = 0;
    router->states = 0;
}

