#pragma once
#include <dnet/client.h>
#include <dnet/general.h>

#include <vector>
#include <string>
#include <zlutils.hpp>

namespace xdnet {

    using CPack = struct packet;
    class Packet {
        zl::Allocator *allc = nullptr;
        CPack         *c_ver     = nullptr;
        
        public:
        std::vector<char> data;

        PACKET_TYPE type     = PACKET_TYPE::REQUEST;
        ullong      from_uid  = 0;
        ullong      to_uid    = 0;
        ullong      trav_fuid = 0;

        CPack       *get_c();
        std::string to_string();

        void templ_compile();
        void full_compile(PACKET_TYPE type, ullong muid);

        Packet(zl::Allocator &allc, std::string data, ullong from_uid, ullong to_uid);
        Packet(zl::Allocator &allc, std::vector<char> data, ullong from_uid, ullong to_uid);
        Packet(zl::Allocator &allc, std::string data);
        Packet(zl::Allocator &allc, std::vector<char> data);

        Packet(zl::Allocator &allc, CPack *pack);
        
        void cleanup();
        ~Packet();

        operator bool();
        Packet(const Packet &other);
    };

    class DClient {
        zl::EventLoop *loop = nullptr;
        zl::Allocator *allc = nullptr;

        struct dnet_state   state;
        struct dcn_session *session = nullptr;

        public:

        void run();

        zl::Future make_request(Packet pack, ullong from_uid, ullong to_uid, PACKET_TYPE type);
        zl::Future make_request(Packet pack, ullong to_uid, PACKET_TYPE type);
        zl::Future make_request(Packet pack);

        zl::Future misc_gather(double timeout_sec = -1);
        zl::Future gather_from(ullong from_uid);

        DClient(
            zl::EventLoop *loop,
            zl::Allocator *allc,

            std::string    ip,
            unsigned short port,
            ullong         UID
        );

        ~DClient();
    };
}