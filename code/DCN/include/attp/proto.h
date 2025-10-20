#pragma once
#include <queue.h>

struct attp_message {
    struct qblock data;

    size_t from_uid;
    size_t uid;
};

void attp_free_msg(struct attp_message *msg);

void attp_msg_fcopy(
    struct attp_message *from,
    struct qblock *to
);

void attp_msg_copy(
    struct attp_message *from, 
    struct qblock *to
);

bool attp_msg_deserial(
    struct qblock *qb, 
    struct attp_message *msg
);
