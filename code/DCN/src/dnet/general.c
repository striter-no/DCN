#include <dnet/general.h>
#include <stddef.h>

size_t packet_general_ofs(){
    return sizeof(ullong) * 3 + sizeof(size_t) + sizeof(PACKET_TYPE) + sizeof(bool);
}

void packet_serial(
    struct allocator *allc,
    struct packet *pack,
    struct qblock *out
){
    size_t rssize = pack->data.dsize + packet_general_ofs();
    out->data = alc_malloc(allc, rssize);
    out->dsize = rssize;

    size_t offset = 0;
    memcpy(out->data + offset, &pack->from_uid,   sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->to_uid,     sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->muid,       sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->packtype,   sizeof(PACKET_TYPE)); offset += sizeof(PACKET_TYPE);
    memcpy(out->data + offset, &pack->from_os,    sizeof(bool));   offset += sizeof(bool);
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
    if (data->dsize < packet_general_ofs()){
        fprintf(stderr, "[error][1] deserial failed: %zu < %zu", data->dsize, packet_general_ofs());
        return false;
    }

    struct qblock *block = &out->data;
    size_t offset = 0;
    memcpy(&out->from_uid,   data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->to_uid,     data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->muid,       data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->packtype,   data->data + offset, sizeof(PACKET_TYPE));   offset += sizeof(PACKET_TYPE);
    memcpy(&out->from_os,    data->data + offset, sizeof(bool));   offset += sizeof(bool);
    
    size_t data_size;
    memcpy(&data_size, data->data + offset, sizeof(size_t)); offset += sizeof(size_t);
    block->dsize = data_size;
    
    if (data->dsize - block->dsize != packet_general_ofs()){
        fprintf(stderr, "[error][2] deserial failed %zu != %zu\n", data->dsize - block->dsize, packet_general_ofs());
        return false;
    }

    //**printf("deserializing %zu bytes\n", block->dsize);
    block->data = alc_malloc(allc, block->dsize);
    memcpy(block->data, data->data + offset, block->dsize);

    return true;
}

void packet_free(
    struct allocator *allc,
    struct packet *pack
){
    qblock_free(allc, &pack->data);
    pack->from_uid   = 0;
    pack->to_uid     = 0;
    pack->muid       = 0;
    pack->packtype   = REQUEST;
    pack->from_os    = false;
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
    if (data)
        qblock_fill(allc, &pack->data, data, sz);
    
    pack->from_uid = from_uid;
    pack->to_uid   = to_uid;
    pack->muid     = muid;
    pack->from_os  = false;
    pack->packtype = REQUEST;
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
    pack->to_uid   = to_uid;
    pack->from_os  = false;
    pack->muid     = muid;
    pack->packtype = REQUEST;
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

struct packet *copy_packet(struct allocator *allc, const struct packet *src) {
    struct packet *dest = alc_calloc(allc, 1, sizeof(struct packet));
    dest->from_uid = src->from_uid;
    dest->to_uid = src->to_uid;
    dest->muid = src->muid;
    dest->packtype = src->packtype;
    dest->from_os = src->from_os;
    
    qblock_init(&dest->data);
    qblock_copy(allc, &dest->data, &src->data);
    
    return dest;
}

struct packet *move_packet(struct allocator *allc, struct packet *src) {
    
    struct packet *dest = alc_calloc(allc, 1, sizeof(struct packet));
    dest->from_uid = src->from_uid;
    dest->to_uid = src->to_uid;
    dest->muid = src->muid;
    dest->packtype = src->packtype;
    dest->from_os = src->from_os;
    
    qblock_init(&dest->data);
    qblock_copy(allc, &dest->data, &src->data);
    
    packet_free(allc, src);
    alc_free(allc, src);
    
    //**printf("destination is %p\n", (void*)dest);
    return dest;
}
