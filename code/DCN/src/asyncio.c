#include <asyncio.h>
#include <stdatomic.h>
#include <threads.h>

void __coroutine_init(
    struct coroutine *crt,
    void *(*worker)(void *args),
    void *args
){
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
    strc->workers = NULL;
    strc->workers_num = 0;
}

void __workers_strct_free(
    struct __workers_strct *strc
){
    free(strc->workers);
    strc->workers_num = 0;
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
    qblock_free(&event->data);
    event->trigger.func = NULL;
    event->looop = NULL;
    event->uid = 0;
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

void asyncio_subscribe(
    struct ev_loop *loop,
    ullong event_uid,
    struct function worker
){
    if (!map_in()){
        return
    }
}

void asyncio_remevent(
    ullong event_uid
){

}


// async function can return pointer to smth
Future *async_create(
    struct ev_loop *loop,
    void *(*worker)(void *args),
    void *args
){
    struct coroutine *crt = malloc(sizeof(struct coroutine));
    __coroutine_init(crt, worker, args);

    Future *fut = malloc(sizeof(Future));
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
    await_future(fut, &out);
    free(fut);

    return out.data;
}

void __loop_worker(
    struct qblock *inp, 
    struct qblock *out
){
    struct coroutine *crt = NULL;
    generic_qbout(inp, &crt, sizeof(struct coroutine *));
    void *result = crt->worker(crt->args);

    if (result != NULL)
        generic_qbfill(out, result, sizeof(void *));

    __coroutine_free(crt);
    free(crt);
}

void loop_create(
    struct ev_loop *loop
){
    mtx_init(&loop->events_mtx, mtx_plain);
    pool_init(&loop->working_pool, __loop_worker, sysconf(_SC_NPROCESSORS_ONLN) * 2);
    loop->g_euid = 0;
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
}

void loop_run(
    struct ev_loop *loop
){
    pool_start(&loop->working_pool);
}

void loop_stop(
    struct ev_loop *loop
){
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
