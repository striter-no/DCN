#include <asyncio.h>
#include <stdatomic.h>
#include <threads.h>

void __coroutine_init(
    struct allocator *allc,
    struct coroutine *crt,
    void *(*worker)(void *args),
    void *args
){
    crt->allc = allc;
    crt->args = args;
    crt->worker = worker;
    crt->cnd_mtx = malloc(sizeof(mtx_t));
    crt->is_done = malloc(sizeof(cnd_t));
    mtx_init(crt->cnd_mtx, mtx_plain);
    cnd_init(crt->is_done);
}

void __coroutine_free(
    struct coroutine *crt
){
    crt->worker = NULL;
    crt->args = NULL;
    mtx_destroy(crt->cnd_mtx);
    cnd_destroy(crt->is_done);
    free(crt->cnd_mtx);
    free(crt->is_done);
    crt->cnd_mtx = NULL;
    crt->is_done = NULL;
}

void __workers_strct_init(
    struct __workers_strct *strc
){
    strc->_mtx = malloc(sizeof(mtx_t));
    mtx_init(strc->_mtx, mtx_plain);

    strc->workers = NULL;
    strc->workers_num = 0;
}

void __workers_strct_free(
    struct __workers_strct *strc
){
    mtx_destroy(strc->_mtx);
    free(strc->_mtx);
    strc->_mtx = NULL;

    strc->workers_num = 0;
    free(strc->workers);
    strc->workers = NULL;
}

void __event_init(
    struct asyncio_event *event,
    struct ev_loop *loop,
    struct function func
){
    qblock_init(&event->data);
    event->looop = loop;
    event->uid = atomic_load(&loop->g_euid);
    event->trigger = func;

    atomic_fetch_add(&loop->g_euid, 1);
}

void __event_free(struct asyncio_event *event){
    qblock_free(event->looop->allc, &event->data);
    event->trigger.func = NULL;
    event->looop = NULL;
    event->uid = 0;
}

void waiter_init(
    struct allocator *allc,
    struct waiter *wt
){
    wt->is_ready = false;
    wt->cmtx = alc_malloc(allc, sizeof(mtx_t));
    wt->wcond = alc_malloc(allc, sizeof(cnd_t));
    mtx_init(wt->cmtx, mtx_recursive);
    cnd_init(wt->wcond);
}

void waiter_wait(
    struct waiter *wt
){
    mtx_lock(wt->cmtx);
    while (!atomic_load(&wt->is_ready)){
        cnd_wait(wt->wcond, wt->cmtx);
    }
    mtx_unlock(wt->cmtx);
}

void waiter_free(
    struct allocator *allc,
    struct waiter *wt
){
    mtx_destroy(wt->cmtx);
    cnd_destroy(wt->wcond);
    alc_free(allc, wt->cmtx);
    alc_free(allc, wt->wcond);
    wt->cmtx  = NULL;
    wt->wcond = NULL;
}

void waiter_set(
    struct waiter *wt
){
    atomic_store(&wt->is_ready, true);
    cnd_broadcast(wt->wcond);
}

ullong asyncio_create_event(
    struct ev_loop *loop,
    struct function trigger
){
    struct asyncio_event *event = malloc(sizeof(struct asyncio_event));
    __event_init(event, loop, trigger);

    map_set(&loop->events, &event->uid, event);
    
    return event->uid;
}

bool asyncio_subscribe(
    struct ev_loop *loop,
    ullong event_uid,
    struct function worker
){
    if (!map_in(&loop->events, &event_uid))
        return false;

    struct __workers_strct *strc = NULL;
    map_at(&loop->workers, &event_uid, (void**)&strc);
    if (strc == NULL)
        return false;

    mtx_lock(strc->_mtx);
    strc->workers = realloc(strc->workers, sizeof(struct __workers_strct) * (
        strc->workers_num + 1
    ));
    strc->workers[strc->workers_num] = worker;
    strc->workers_num += 1;
    mtx_unlock(strc->_mtx);

    return true;
}

void asyncio_remevent(
    struct ev_loop *loop,
    ullong event_uid
){
    if (!map_in(&loop->events, &event_uid))
        return;

    struct __workers_strct *strc = NULL;
    map_at(&loop->workers, &event_uid, (void**)&strc);
    if (strc == NULL)
        return;

    struct asyncio_event *event = NULL;
    map_at(&loop->events, &event_uid, (void**)&event);
    if (event == NULL)
        return;

    __event_free(event);
    __workers_strct_free(strc);
}


// async function can return pointer to smth
Future *async_create(
    struct ev_loop *loop,
    void *(*worker)(void *args),
    void *args
){
    struct coroutine *crt = malloc(sizeof(struct coroutine));
    __coroutine_init(loop->allc, crt, worker, args);

    Future *fut = alc_malloc(loop->allc, sizeof(Future));
    gnr_pool_sumbit(
        &loop->working_pool, 
        &crt, 
        sizeof(struct coroutine*),
        fut
    );

    return fut;
}

void *await(Future *fut){
    struct qblock out;
    // save allocator before await_future frees fut (sets fut->pool to NULL)
    
    struct allocator *allc = fut->pool ? fut->pool->allc : NULL;
    if (allc == NULL) {
        return NULL;
    }
    await_future(fut, &out);
    
    if (out.dsize != sizeof(void*) || out.data == NULL) {
        qblock_free(allc, &out);
        return NULL;
    }
    
    void *result;
    memcpy(&result, out.data, sizeof(void*));
    //** printf("in await result: %p\n", result);

    qblock_free(allc, &out);
    return result;
}

void __loop_worker(
    struct qblock *inp, 
    struct qblock *out
){
    struct coroutine *crt = NULL;
    generic_qbout(inp, &crt, sizeof(struct coroutine *));
    void *result = crt->worker(crt->args);

    if (result != NULL){
        generic_qbfill(crt->allc, out, &result, sizeof(void *));
        //**printf("filling out %p\n", result);
    }

    __coroutine_free(crt);
    free(crt);
}

int __events_worker(void *_args){
    struct ev_loop *loop = _args;

    while (atomic_load(&loop->working_pool.is_active)){
        mtx_lock(&loop->events._mtx);
        size_t ev_len = loop->events.len;
        mtx_unlock(&loop->events._mtx);
        
        for (size_t i = 0; i < ev_len; i++){
            ullong ev_uid = 0;
            map_key_at( &loop->events, &ev_uid, i );

            if (ev_uid == 0)
                continue;

            struct asyncio_event *event = NULL;
            map_at(&loop->events, &ev_uid, (void**)&event);

            if (event == NULL)
                continue;

            if (event->trigger.func(loop)){
                struct __workers_strct *strc = NULL;
                map_at(&loop->workers, &ev_uid, (void**)&strc);
                if (strc == NULL)
                    continue;

                mtx_lock(strc->_mtx);
                for (size_t i = 0; i < strc->workers_num; i++){
                    async_create(
                        loop,
                        strc->workers[i].func,
                        loop
                    );
                }
                mtx_unlock(strc->_mtx);
            }
        }
    }

    return thrd_success;
}

void loop_create(
    struct allocator *allc,
    struct ev_loop *loop,
    ssize_t cores
){
    mtx_init(&loop->events_mtx, mtx_plain);
    loop->allc = allc;
    loop->g_euid = 1;
    
    pool_init(
        allc,
        &loop->working_pool, 
        __loop_worker, 
        cores == -1 ? (sysconf(_SC_NPROCESSORS_ONLN) * 2): cores
    );
    
    map_init(
        &loop->workers, 
        sizeof(ullong), 
        sizeof(struct __workers_strct)
    );
    map_init(
        &loop->events, 
        sizeof(ullong), 
        sizeof(struct asyncio_event*)
    );
    thrd_create(&loop->events_thread, __events_worker, loop);
}

void loop_run(
    struct ev_loop *loop
){
    pool_start(&loop->working_pool);
}

void loop_stop(
    struct ev_loop *loop
){
    //**printf("loop_stop\n");
    atomic_store(&loop->working_pool.is_active, false);
    thrd_join(loop->events_thread, NULL);
    mtx_destroy(&loop->events_mtx);
    pool_free(&loop->working_pool);

    for (size_t i = 0; i < loop->workers.len; i++){
        ullong key; map_key_at(&loop->workers, &key, i);
        struct __workers_strct *strct = NULL;
        
        map_at(&loop->workers, &key, (void**)&strct);
        
        free(strct->workers);
        strct->workers = NULL;
        strct->workers_num = 0;
    }

    for (size_t i = 0; i < loop->events.len; i++){
        ullong key; map_key_at(&loop->events, &key, i);
        struct asyncio_event **event = NULL;
        
        map_at(&loop->events, &key, (void**)&event);
        __event_free(*event);
        free(event);
    }
    
    map_free(&loop->workers);
    map_free(&loop->events);
    loop->allc = NULL;
}

// ================= ASYNC SUPPORTIVE FUNCTIONS =====================

void *__async_sleep_worker(void *args){
    float *duration = args;
    thrd_sleep(&(struct timespec){
        .tv_sec = (long)(*duration),
        .tv_nsec = (long)((*duration - (long)(*duration)) * 1000000000L)
    }, NULL);
    free(duration);
    return NULL;
}

Future *asyncio_sleep(
    struct ev_loop *loop,
    float _seconds
){
    float *seconds = malloc(sizeof(float));
    *seconds = _seconds;
    return async_create(
        loop, 
        __async_sleep_worker, 
        seconds
    );
}

void **asyncio_gather(
    Future **futures,
    size_t fut_sz
){
    void **results = malloc(fut_sz * sizeof(void *));

    mtx_t mutex;
    cnd_t cond;
    atomic_size_t done = 0;
    atomic_bool   is_ready = false;
    mtx_init(&mutex, mtx_plain);
    cnd_init(&cond);

    for (size_t i = 0; i < fut_sz; i++){
        futures[i]->shared_cond_mtx = &mutex;
        futures[i]->shared_done = &done;
        futures[i]->shared_is_ready = &is_ready;
        futures[i]->shared_is_done = &cond;
    }

    mtx_lock(&mutex);
    while (done < fut_sz){
        cnd_wait(&cond, &mutex);
    }
    mtx_unlock(&mutex);

    for (size_t i = 0; i < fut_sz; i++){
        if (futures[i]->out_block.data != NULL){
            generic_qbout(
                &futures[i]->out_block,
                &results[i],
                sizeof(void *)
            );
        } else {
            results[i] = NULL;
        }
        __future_free(futures[i]);
        free(futures[i]);
    }

    mtx_destroy(&mutex);
    cnd_destroy(&cond);
    return results;
}
