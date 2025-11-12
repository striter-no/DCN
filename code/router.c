#include <logger.h>
#include <allocator.h>
#include <asyncio.h>
#include <dnet/router.h>

int main(){
    struct ev_loop   loop;
    struct allocator allc;
    struct logger    logger;

    allocator_init(&allc);
    loop_create(&allc, &loop, 4);
    logger_init(&logger, stdout);

    loop_run(&loop);

    struct router router;
    router_init(&loop, &allc, &logger, &router);
    router_link(&router, "127.0.0.1", 9000);
    router_link(&router, "127.0.0.1", 9001);
    
    router_run(&router);
    router_stop(&router);

    loop_stop(&loop);
    allocator_end(&allc);
    logger_stop(&logger);
}
