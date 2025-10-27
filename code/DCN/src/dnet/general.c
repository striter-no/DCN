#include <dnet/general.h>
#include <stddef.h>

void packet_serial(
    struct allocator *allc,
    struct packet *pack,
    struct qblock *out
){
    size_t rssize = pack->data.dsize + sizeof(ullong) * 3 + sizeof(size_t);
    out->data = alc_malloc(allc, rssize);
    out->dsize = rssize;

    size_t offset = 0;
    memcpy(out->data + offset, &pack->from_uid,   sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->to_uid,     sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->muid,       sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->data.dsize, sizeof(size_t)); offset += sizeof(size_t);
    if (pack->data.dsize != 0)
        memcpy(
            out->data + offset,
            pack->data.data,
            pack->data.dsize
        );
}

bool packet_deserial(
    struct allocator *allc,
    struct packet *out,
    struct qblock *data
){
    if (data->dsize < sizeof(ullong) * 3 + sizeof(size_t))
        return false;

    struct qblock *block = &out->data;
    size_t offset = 0;
    memcpy(&out->from_uid, data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->to_uid,   data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->muid,     data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&block->dsize,  data->data + offset, sizeof(size_t)); offset += sizeof(size_t);
    
    if (data->dsize - block->dsize != sizeof(ullong) * 3 + sizeof(size_t))
        return false;

    block->data = alc_malloc(allc, block->dsize);
    memcpy(block->data, data->data + offset, block->dsize);

    return true;
}

void packet_free(
    struct allocator *allc,
    struct packet *pack
){
    qblock_free(allc, &pack->data);
    pack->from_uid = 0;
    pack->to_uid   = 0;
    pack->muid     = 0;
}

void packet_init(
    struct allocator *allc,
    struct packet *pack,
    char *data, size_t sz,

    ullong from_uid,
    ullong to_uid,
    ullong muid
){
    qblock_init(&pack->data);
    qblock_fill(allc, &pack->data, data, sz);
    
    pack->from_uid = from_uid;
    pack->to_uid = to_uid;
    pack->muid = muid;
}

void qpacket_init(
    struct allocator *allc,
    struct packet *pack,
    struct qblock *data,

    ullong from_uid,
    ullong to_uid,
    ullong muid
){
    qblock_init(&pack->data);
    qblock_copy(allc, &pack->data, data);
    
    pack->from_uid = from_uid;
    pack->to_uid = to_uid;
    pack->muid = muid;
}

void packet_fill(
    struct allocator *allc,
    struct packet *pack,
    char *data, size_t sz
){
    qblock_fill(
        allc, &pack->data, data, sz
    );
}

void packet_templ(
    struct allocator *allc,
    struct packet *pack,
    char *data, size_t sz
){
    qblock_init(&pack->data);
    qblock_fill(allc, &pack->data, data, sz);
}
