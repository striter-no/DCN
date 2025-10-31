#pragma once
#include <queue.h>
#include <asyncio.h>

struct packet {
    struct qblock data;
    ullong from_uid;
    ullong to_uid;
    ullong muid;
    bool   is_request; // or answer if false

    bool   from_os;
};

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
