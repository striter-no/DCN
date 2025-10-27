#pragma once
#include <stdbool.h>
#include <sys/types.h>
#include <threads.h>
#include <stdatomic.h>
#include <unistd.h>
#include "queue.h"
#include <allocator.h>

struct pool {
    struct allocator *allc;
    void (*working_f)(struct qblock *inp, struct qblock *out);

    cnd_t has_tasks;

    thrd_t *threads;
    size_t  workers;
    struct queue q;

    atomic_bool is_active;
};

struct future {
    struct pool   *pool;
    struct qblock inp_block;
    struct qblock out_block;

    atomic_bool is_ready;
    mtx_t    cond_mtx;
    cnd_t    is_done;

    atomic_bool   *shared_is_ready;
    mtx_t         *shared_cond_mtx;
    cnd_t         *shared_is_done;
    atomic_size_t *shared_done;
};

struct task {
    bool is_future;
    void *tsk;
};

int  __thr_work(void *_args);
void __future_init(
    struct future *fut, 
    struct pool   *pool, 
    struct qblock *inp
);
void __future_free(struct future *fut);

void pool_init(
    struct allocator *allc,
    struct pool *pool, 
    void (*working_f)(struct qblock *inp, struct qblock *out), 
    size_t workers
);

void pool_free(struct pool *pool);
void pool_sumbit(
    struct pool *pool, 
    struct qblock *inp, 
    void *awaitable
);

void gnr_pool_sumbit(
    struct pool *pool, 
    void *data, 
    size_t sz, 
    struct future *fut
);

void pool_start(struct pool *pool);

void await_future(
    struct future *fut, 
    struct qblock *out
);
