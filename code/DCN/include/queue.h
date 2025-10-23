#pragma once
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <stdbool.h>

#define QUEUE_HEAD_STEP 5

struct qblock {
    char   *data;
    size_t dsize;
};

struct queue {
    mtx_t blocks_mtx;

    struct qblock *blocks;
    size_t bsize;
    size_t head_size;
};

void queue_init(struct queue *q);
void queue_free(struct queue *q);
bool queue_empty(struct queue *q);

void qblock_init(struct qblock *b);
void qblock_free(struct qblock *b);

void qblock_copy(struct qblock *dest, const struct qblock *src);
void qblock_fill(struct qblock *dest, char *data, size_t sz);

int push_block(struct queue *q, struct qblock *b);
int pop_block(struct queue *q, struct qblock *b);
int peek_block(struct queue *q, struct qblock *b);

void generic_qbfill(struct qblock *out, void *data, size_t data_size);
void generic_qbout(struct qblock *inp, void *out, size_t data_size);

bool queue_forward(struct queue *to, struct queue *from, bool peek);
