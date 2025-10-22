#include <asyncio.h>
#include <attp/server.h>

struct ev_loop loop;

void attp_handler(
    struct client *cli,
    struct attp_message *input, 
    struct attp_message *output
){
    printf(
        "got from %s:%i (f%zu-u%zu): (%zu bytes) %s\n", 
        cli->ip, cli->port,
        input->from_uid, input->uid,
        input->data.dsize, input->data.data
    );
    await(asyncio_sleep(&loop, 2));

    // echo
    qblock_copy(&output->data, &input->data);
}

int main(){
    atomic_bool is_running = true;
    struct attp_server attp_serv;

    struct socket_md socket;
    if (screate_socket(&socket, "127.0.0.1", 9000) != 0){
        fprintf(stderr, "cannot create socket: %s\n", strerror(errno));
        return -1;
    }

    loop_create(&loop, 2);
    loop_run(&loop);
    
    attp_init(
        &attp_serv, &loop,
        8, attp_handler
    );
    attp_run(&is_running, &attp_serv, &socket);
    
    attp_stop(&attp_serv);
    loop_stop(&loop);

    close(socket.fd);
}
