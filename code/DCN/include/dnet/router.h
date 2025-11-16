#pragma once
#include <dnet/server.h>
#include <dnet/client.h>

#ifdef __cplusplus
extern "C" {
#endif

struct router {
    struct ev_loop   *loop;
    struct allocator *allc;
    struct logger    *logger;

    struct dnet_state **states;
    size_t states_n;
    size_t states_cap;
    ullong uids;

    ATOMIC_BOOL is_running;
};

struct router_task {
    struct router *router;
    size_t my_dstate;
};

void router_init(
    struct ev_loop   *loop,
    struct allocator *allc,
    struct logger    *logger,
    struct router    *router
);

void router_link(
    struct router *router,
    char *serv_ip,
    unsigned short serv_port
);

void router_stop(struct router *router);
void router_run(struct router  *router);

#ifdef __cplusplus
}
#endif
