#pragma once
#include <queue.h>
#include <asyncio.h>

struct packet {
    struct qblock data;
    ullong from_uid;
    ullong to_uid;
    ullong muid;
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
