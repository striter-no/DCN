#pragma once
#include "queue.h"
#include <map.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/types.h>
#include <thr-pool.h>
#include <threads.h>
#include <unistd.h>
#include <allocator.h>

typedef struct future Future;
#define asyncdef void *
typedef unsigned long long ullong;


struct coroutine {
    struct allocator *allc;
    void *args;
    void *(*worker)(void *args);

    cnd_t *is_done;
    mtx_t *cnd_mtx;
};

struct function {
    void *(*func)(void *args);
};

struct __workers_strct {
    mtx_t *_mtx;
    struct function *workers;
    size_t workers_num;
};

struct ev_loop {
    struct allocator *allc;
    atomic_ullong g_euid;
    struct pool working_pool;

    mtx_t events_mtx;
    // ullong (event uid): asyncio_event
    struct map events;

    // ullong (event uid) : __workers_strct
    struct map  workers;

    // checks every trigger, starts coroutine if triggered
    thrd_t events_thread;
};

struct asyncio_event {
    struct qblock      data;
    struct ev_loop    *looop;
    ullong uid;

    struct function    trigger;
};

struct waiter {
    atomic_bool is_ready;
    mtx_t *cmtx;
    cnd_t *wcond;
};

void waiter_init(
    struct allocator *allc,
    struct waiter *wt
);

void waiter_wait(
    struct waiter *wt
);

void waiter_free(
    struct allocator *allc,
    struct waiter *wt
);

void waiter_set(
    struct waiter *wt
);

void __workers_strct_init(
    struct __workers_strct *strc
);

void __workers_strct_free(
    struct __workers_strct *strc
);

void __event_init(
    struct asyncio_event *event,
    struct ev_loop *loop,
    struct function func
);
void __event_free(struct asyncio_event *event);
ullong asyncio_create_event(
    struct ev_loop *loop,
    struct function func
);
bool asyncio_subscribe(
    struct ev_loop *loop,
    ullong event_uid,
    struct function worker
);
void asyncio_remevent(
    struct ev_loop *loop,
    ullong event_uid
);

void __coroutine_init(
    struct allocator *allc,
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

void loop_create(struct allocator *allc, struct ev_loop *loop, ssize_t cores);
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
