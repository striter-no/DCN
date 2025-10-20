#include <stdio.h>
#include <attp/proto.h>

void attp_free_msg(struct attp_message *msg){
    qblock_free(&msg->data);
}

void attp_msg_fcopy(
    struct attp_message *from,
    struct qblock *to
){
    struct qblock copied_data;
    qblock_init(&copied_data);
    qblock_copy(&copied_data, &from->data);
    struct attp_message copied = {
        .data = copied_data,
        .uid = from->uid
    };

    qblock_init(to);
    generic_qbfill(to, &copied, sizeof(struct attp_message));
}

void attp_msg_copy(
    struct attp_message *from, 
    struct qblock *to
){
    size_t total = sizeof(size_t) * 3 + from->data.dsize;
    qblock_init(to);
    to->data = malloc(total);
    to->dsize = total;

    char *ptr = to->data;
    memcpy(ptr, &from->uid, sizeof(size_t));          ptr += sizeof(size_t);
    memcpy(ptr, &from->from_uid, sizeof(size_t));     ptr += sizeof(size_t);
    memcpy(ptr, &from->data.dsize, sizeof(size_t));   ptr += sizeof(size_t);
    if (from->data.dsize > 0)
        memcpy(ptr, from->data.data, from->data.dsize);
}

bool attp_msg_deserial(
    struct qblock *qb, 
    struct attp_message *msg
){
    if (qb->dsize < sizeof(size_t) * 3){
        fprintf(stderr, "dsize (%zu) < 24\n", qb->dsize);
        return false;
    }

    const char *ptr = qb->data;
    memcpy(&msg->uid, ptr, sizeof(size_t));           ptr += sizeof(size_t);
    memcpy(&msg->from_uid, ptr, sizeof(size_t));      ptr += sizeof(size_t);

    size_t data_sz;
    memcpy(&data_sz, ptr, sizeof(size_t));            ptr += sizeof(size_t);

    if (qb->dsize != sizeof(size_t) * 3 + data_sz){
        fprintf(stderr, "dsize (%zu) != 24 + data_sz (%zu)\n", qb->dsize, data_sz);
        return false;
    }

    qblock_init(&msg->data);
    if (data_sz > 0) {
        msg->data.data = malloc(data_sz);
        msg->data.dsize = data_sz;
        memcpy(msg->data.data, ptr, data_sz);
    } else {
        msg->data.data = NULL;
        msg->data.dsize = 0;
    }

    return true;
}
