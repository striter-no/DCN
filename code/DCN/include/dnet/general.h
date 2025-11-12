#pragma once
#include <queue.h>
#include <asyncio.h>
#include <stdio.h>

typedef enum {
    REQUEST,
    RESPONSE,
    SIGNAL,
    BROADCAST,
    SIG_BROADCAST,
    TRACEROUTE,
    PING
} PACKET_TYPE;

struct packet {
    PACKET_TYPE packtype;
    bool        from_os;
    
    ullong from_uid;
    ullong to_uid;
    
    ullong trav_fuid;
    ullong trav_tuid;

    ullong muid;
    ullong cmuid;
    
    struct qblock data;
};

struct trp_data {
    bool is_response;
    bool   success;
    ullong req_uid;
};

void trp_data_serial(
    struct allocator *allc,
    struct trp_data  *trpd,
    struct qblock    *out
);

bool trp_data_deserial(
    struct trp_data  *trpd,
    struct qblock    *data
);

void packet_serial(
    struct allocator *allc,
    struct packet *pack,
    struct qblock *out
);

bool packet_deserial(
    struct allocator *allc,
    struct packet *out,
    struct qblock *data
);

void packet_free(
    struct allocator *allc,
    struct packet *pack
);

void packet_init(
    struct allocator *allc,
    struct packet *pack,
    char *data, size_t sz,

    ullong from_uid,
    ullong to_uid,
    ullong muid
);

void qpacket_init(
    struct allocator *allc,
    struct packet *pack,
    struct qblock *data,

    ullong from_uid,
    ullong to_uid,
    ullong muid
);

void packet_fill(
    struct allocator *allc,
    struct packet *pack,
    char *data, size_t sz
);

void packet_templ(
    struct allocator *allc,
    struct packet *pack,
    char *data, size_t sz
);

struct packet *copy_packet(
    struct allocator *allc, 
    const struct packet *src
);

struct packet *move_packet(
    struct allocator *allc, 
    struct packet *src
);

struct packet create_traceroute(
    struct allocator *allc,
    struct trp_data   tr_data,
    ullong from_uid
);

size_t packet_general_ofs(void);
