#include <xdnet/client.hpp>

namespace xdnet {

    CPack *Packet::get_c(){
        return c_ver;
    }

    std::string Packet::to_string(){
        return {data.begin(), data.end()};
    }

    void Packet::templ_compile(){
        if (!c_ver) c_ver = allc->malloc<CPack>(sizeof(CPack));

        packet_templ(allc->get_c(), c_ver, &data[0], data.size());
    }

    void Packet::full_compile(PACKET_TYPE type, ullong muid){
        if (!c_ver) c_ver = allc->malloc<CPack>(sizeof(CPack));
        
        packet_init(allc->get_c(), c_ver, &data[0], data.size(), from_uid, to_uid, muid);
        c_ver->packtype = type;
        
        this->type = type;
    }

    Packet::Packet(zl::Allocator &allc, std::string data, ullong from_uid, ullong to_uid):
        allc(&allc), data(data.begin(), data.end()), from_uid(from_uid), to_uid(to_uid)
    {}

    Packet::Packet(zl::Allocator &allc, std::vector<char> data, ullong from_uid, ullong to_uid):
        allc(&allc), data(data), from_uid(from_uid), to_uid(to_uid)
    {}

    Packet::Packet(zl::Allocator &allc, std::string data):
        allc(&allc), data(data.begin(), data.end()) 
    {}

    Packet::Packet(zl::Allocator &allc, std::vector<char> data):
        allc(&allc), data(data)
    {}

    Packet::Packet(zl::Allocator &allc, CPack *pack){
        this->allc = &allc;

        if (!pack){
            this->c_ver = nullptr;
        } else {
            c_ver = copy_packet(allc.get_c(), pack);
            packet_free(allc.get_c(), pack);

            data = std::vector<char>(c_ver->data.data, c_ver->data.data + c_ver->data.dsize);
            from_uid = c_ver->from_uid;
            to_uid   = c_ver->to_uid;
            trav_fuid = c_ver->trav_fuid;
        }

    }

    Packet::operator bool(){
        return c_ver != nullptr;
    }

    Packet::~Packet() = default;

    void Packet::cleanup(){
        if (c_ver){
            packet_free(
                allc->get_c(),
                c_ver
            );
            allc->free(c_ver);
        }
    }

    Packet::Packet(const Packet &other){
        this->allc = other.allc;

        if (other.c_ver)
            this->c_ver = other.c_ver;
        else
            this->c_ver = nullptr;

        this->data = other.data;

        this->type     = other.type;
        this->from_uid = other.from_uid;
        this->to_uid   = other.to_uid;
    }

    void DClient::run(){
        dnet_run(&state);
        session = &state.session;
    }

    zl::Future DClient::make_request(Packet pack, ullong from_uid, ullong to_uid, PACKET_TYPE type){
        pack.from_uid = from_uid;
        pack.to_uid   = to_uid;
        pack.templ_compile();
        
        return {request(
            session,
            pack.get_c(),
            to_uid,
            from_uid,
            type
        )};
    }

    zl::Future DClient::make_request(Packet pack, ullong to_uid, PACKET_TYPE type){
        pack.to_uid = to_uid;
        pack.templ_compile();
        
        return {request(
            session,
            pack.get_c(),
            to_uid,
            session->cli_uid,
            type
        )};
    }

    zl::Future DClient::make_request(Packet pack){
        pack.templ_compile();
        
        return {request(
            session,
            pack.get_c(),
            pack.to_uid,
            pack.from_uid,
            pack.type
        )};
    }

    zl::Future DClient::misc_gather(double timeout_sec){
        return {async_misc_grequests(
            session,
            timeout_sec
        )};
    }

    zl::Future DClient::gather_from(ullong from_uid){
        return {async_grequests(
            session,
            from_uid
        )};
    }

    DClient::DClient(
        zl::EventLoop *loop,
        zl::Allocator *allc,

        std::string    ip,
        unsigned short port,
        ullong         UID
    ): loop(loop), allc(allc) {
        dnet_state(
            &state,
            loop->get_c(),
            allc->get_c(),
            ip.data(),
            port,
            UID
        );
    }

    DClient::~DClient(){
        dnet_stop(&state);
    }
}