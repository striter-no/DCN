#include <queue.h>
#include <stdatomic.h>
#include <thr-pool.h>
#include <threads.h>

void __future_init(
    struct future *fut, 
    struct pool   *pool, 
    struct qblock *inp
){
    mtx_init(&fut->cond_mtx, mtx_plain);
    cnd_init(&fut->is_done);
    fut->pool = pool;
    qblock_init(&fut->inp_block);
    qblock_init(&fut->out_block);

    qblock_copy(pool->allc, &fut->inp_block, inp);
    atomic_store(&fut->is_ready, false);

    fut->shared_is_ready = NULL;
    fut->shared_cond_mtx = NULL;
    fut->shared_is_done = NULL;
    fut->shared_done = NULL;
}

void __future_free(struct future *fut){
    struct allocator *allc = fut->pool->allc;
    fut->pool      = NULL;
    fut->shared_is_ready = NULL;
    fut->shared_cond_mtx = NULL;
    fut->shared_is_done = NULL;
    fut->shared_done = NULL;
    
    qblock_free(allc, &fut->out_block);
    qblock_free(allc, &fut->inp_block);
    mtx_destroy(&fut->cond_mtx);
    cnd_destroy(&fut->is_done);
}

void pool_init(
    struct allocator *allc,
    struct pool *pool, 
    void (*working_f)(struct qblock *inp, struct qblock *out), 
    size_t workers
){
    pool->allc = allc;
    pool->threads = malloc(sizeof(thrd_t) * workers);
    pool->workers = workers;
    pool->working_f = working_f;

    cnd_init(&pool->has_tasks);
    queue_init(&pool->q, allc);
    atomic_store(&pool->is_active, true);
}

void pool_free(struct pool *pool){
    atomic_store(&pool->is_active, false);
    cnd_broadcast(&pool->has_tasks);
    queue_free(&pool->q);
    for (size_t i = 0; i < pool->workers; i++){
        thrd_join(pool->threads[i], NULL);
    }
    free(pool->threads);
}

void pool_sumbit(struct pool *pool, struct qblock *inp, void *awaitable){
    struct qblock block;
    struct task task;
    if (awaitable != NULL){
        __future_init(awaitable, pool, inp);

        task.is_future = true;
        task.tsk = awaitable;

    } else {
        task.is_future = false;
        task.tsk = malloc(sizeof(struct qblock));
        qblock_init(task.tsk);
        qblock_copy(pool->allc, task.tsk, inp);
    }
    qblock_init(&block);
    generic_qbfill(pool->allc, &block, &task, sizeof(struct task));

    push_block(&pool->q, &block);
    qblock_free(pool->allc, &block);
    cnd_signal(&pool->has_tasks);
}

void gnr_pool_sumbit(struct pool *pool, void *data, size_t sz, struct future *fut){
    struct qblock b;
    qblock_init(&b);
    generic_qbfill(pool->allc, &b, data, sz);

    pool_sumbit(pool, &b, fut);
    qblock_free(pool->allc, &b);
}

int __thr_work(void *_args){
    struct pool *pool = _args;

    while (atomic_load(&pool->is_active)){
        
        mtx_lock(&pool->q.blocks_mtx);
        while (pool->q.bsize == 0 && atomic_load(&pool->is_active))
            cnd_wait(&pool->has_tasks, &pool->q.blocks_mtx);
        
        if (pool->q.bsize == 0){
            mtx_unlock(&pool->q.blocks_mtx);
            continue;
        }

        struct task task;
        struct qblock block;
        qblock_init(&block);

        pop_block(&pool->q, &block);
        
        generic_qbout(&block, &task, sizeof(struct task));
        qblock_free(pool->allc, &block);
        mtx_unlock(&pool->q.blocks_mtx);
        
        if (task.is_future){
            struct future *fut = task.tsk;
            pool->working_f(&fut->inp_block, &fut->out_block);
            atomic_store(&fut->is_ready, true);
            cnd_signal(&fut->is_done);

            if (fut->shared_is_ready){
                atomic_store(fut->shared_is_ready, true);
                atomic_fetch_add(fut->shared_done, 1);
                mtx_lock(fut->shared_cond_mtx);
                cnd_broadcast(fut->shared_is_done);
                mtx_unlock(fut->shared_cond_mtx);
            }
        } else {
            struct qblock *inp = task.tsk;
            pool->working_f(inp, NULL);
            qblock_free(pool->allc, inp);
        }
    }

    return thrd_success;
}

void pool_start(struct pool *pool){

    for (size_t i = 0; i < pool->workers; i++){
        thrd_create(&pool->threads[i], __thr_work, pool);
    }
}

void await_future(struct future *fut, struct qblock *out){
    mtx_lock(&fut->cond_mtx);
    while (!atomic_load(&fut->is_ready)){
        cnd_wait(&(fut->is_done), &(fut->cond_mtx));
    }
    mtx_unlock(&(fut->cond_mtx));
    
    qblock_init(out);
    qblock_copy(fut->pool->allc, out, &(fut->out_block));
    __future_free(fut);
}
