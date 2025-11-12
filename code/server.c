#include <asyncio.h>
#include <allocator.h>
#include <dnet/server.h>


int main(int argc, char *argv[]){
    unsigned short PORT = atoi(argv[1]);

    atomic_bool is_running = true;
    struct ev_loop loop;
    struct allocator allc;

    allocator_init(&allc);

    struct dcn_server serv;
    struct ssocket_md socket;
    if (screate_socket(&socket, "127.0.0.1", PORT) != 0){
        fprintf(stderr, "cannot create socket: %s\n", strerror(errno));
        return -1;
    }

    loop_create(&allc, &loop, 2);
    loop_run(&loop);

    dcn_serv_init(&allc, &serv, &loop, &socket, &is_running);
    dcn_serv_run(&serv);
    dcn_serv_stop(&serv);

    loop_stop(&loop);
    close(socket.fd);

    allocator_end(&allc);
}
