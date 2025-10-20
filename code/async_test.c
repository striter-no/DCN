#include <asyncio.h>

struct ev_loop loop;
asyncdef async_func1(void *args){
    
    await(asyncio_sleep(&loop, 3));
    printf("Hello ");

    return NULL;
}

asyncdef async_func2(void *args){
    printf("world\n");

    return NULL;
}

int main(){
    loop_create(&loop);
    loop_run(&loop);

    Future *futures[2] = {
        async_create(&loop, async_func1, NULL),
        async_create(&loop, async_func2, NULL)
    };

    void **results = asyncio_gather(futures, 2);
    free(results);

    loop_stop(&loop);
}
