#include <dnet/general.h>
#include <iostream>
#include <zlutils.hpp>
#include <xdnet/client.hpp>

int main(int argc, char *argv[]){

    ullong MY_UID = atoll(argv[1]);
    int    PORT   = atoi(argv[2]);
    double timeout_sec = argc > 3 ? atof(argv[3]) : -1;

    zl::Allocator allc;
    zl::EventLoop loop(allc, 3);
    loop.run();

    xdnet::DClient client(&loop, &allc, "127.0.0.1", PORT, MY_UID);
    client.run();

    xdnet::Packet pack(allc, "Hello"), echo_pack(allc, "Echo hello");

    zl::Future grf = client.misc_gather(timeout_sec);
    zl::Future rf  = client.make_request(pack, 0, SIG_BROADCAST);
    xdnet::Packet reqpack = {allc, grf.wait<xdnet::CPack>()};
    
    if (!reqpack){
        std::cout << "no incoming requests" << std::endl;
    } else {
        std::cout << "Got incoming request: " << reqpack.to_string() << std::endl;
        client.make_request(echo_pack, 0, SIG_BROADCAST).wait<void*>();
        reqpack.cleanup();
    }

    echo_pack.cleanup();
    pack.cleanup();

    rf.wait<void*>();
}
