#pragma once
#include "queue.h"
#include <map.h>
#include <thr-pool.h>
#include <threads.h>
#include <unistd.h>

typedef struct future Future;
#define asyncdef void *

struct coroutine {
    void *args;
    void *(*worker)(void *args);

    cnd_t *is_done;
    mtx_t *cnd_mtx;
    size_t uid;
};

struct ev_loop {
    struct pool working_pool;
};

void __coroutine_init(
    struct coroutine *crt,
    void *(*worker)(void *args),
    void *args
);

void __coroutine_free(
    struct coroutine *crt
);

// async function can return pointer to smth
struct future *async_create(
    struct ev_loop *loop,
    void *(*worker)(void *args),
    void *args
);

void *await(struct future *fut);
void __loop_worker(
    struct qblock *inp, 
    struct qblock *out
);

void loop_create(struct ev_loop *loop);
void loop_run(struct ev_loop *loop);
void loop_stop(struct ev_loop *loop);


Future *asyncio_sleep(
    struct ev_loop *loop,
    float seconds
);

void **asyncio_gather(
    Future **futures,
    size_t fut_sz
);
