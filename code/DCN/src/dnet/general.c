#include <dnet/general.h>
#include <stddef.h>

void packet_serial(
    struct allocator *allc,
    struct packet *pack,
    struct qblock *out
){
    size_t rssize = pack->data.dsize + sizeof(ullong) * 3 + sizeof(size_t) + 2 * sizeof(bool);
    out->data = alc_malloc(allc, rssize);
    out->dsize = rssize;

    size_t offset = 0;
    memcpy(out->data + offset, &pack->from_uid,   sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->to_uid,     sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->muid,       sizeof(ullong)); offset += sizeof(ullong);
    memcpy(out->data + offset, &pack->is_request, sizeof(bool));   offset += sizeof(bool);
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
    if (data->dsize < sizeof(ullong) * 3 + sizeof(size_t) + 2 * sizeof(bool)){
        printf("[1] deserial failed: %zu < %zu", data->dsize, sizeof(ullong) * 3 + sizeof(size_t) + 2 * sizeof(bool));
        return false;
    }

    struct qblock *block = &out->data;
    size_t offset = 0;
    memcpy(&out->from_uid,   data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->to_uid,     data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->muid,       data->data + offset, sizeof(ullong)); offset += sizeof(ullong);
    memcpy(&out->is_request, data->data + offset, sizeof(bool));   offset += sizeof(bool);
    memcpy(&out->from_os,    data->data + offset, sizeof(bool));   offset += sizeof(bool);
    
    size_t data_size;
    memcpy(&data_size, data->data + offset, sizeof(size_t)); offset += sizeof(size_t);
    block->dsize = data_size;
    
    if (data->dsize - block->dsize != sizeof(ullong) * 3 + sizeof(size_t) + 2 * sizeof(bool)){
        printf("[2] deserial failed %zu != %zu\n", data->dsize - block->dsize, sizeof(ullong) * 3 + sizeof(size_t) + 2 * sizeof(bool));
        return false;
    }

    printf("deserializing %zu bytes\n", block->dsize);
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
    pack->is_request = true;
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
    qblock_fill(allc, &pack->data, data, sz);
    
    pack->from_uid = from_uid;
    pack->to_uid   = to_uid;
    pack->muid     = muid;
    pack->from_os  = false;
    pack->is_request = true;
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
    pack->is_request = true;
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
    dest->is_request = src->is_request;
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
    dest->is_request = src->is_request;
    dest->from_os = src->from_os;
    
    qblock_init(&dest->data);
    qblock_copy(allc, &dest->data, &src->data);
    
    packet_free(allc, src);
    // alc_free(allc, src);
    
    printf("destination is %p\n", (void*)dest);
    return dest;
}
