#include <queue.h>

void generic_qbfill(struct allocator *allc, struct qblock *out, void *data, size_t data_size){
    out->data = alc_realloc(allc, out->data, data_size);
    memcpy(out->data, data, data_size);
    out->dsize = data_size;
}

void generic_qbout(struct qblock *inp, void *out, size_t sz){
    memcpy(out, inp->data, sz);
}

void queue_init(struct queue *q, struct allocator *allc){
    mtx_init(&q->blocks_mtx, mtx_recursive);
    q->blocks = NULL;
    q->bsize = 0;
    q->head_size = 0;
    q->allc = allc;
}

void qblock_init(struct qblock *b){
    b->data  = NULL;
    b->dsize = 0;
}

void qblock_free(struct allocator *allc, struct qblock *b){
    alc_free(allc, b->data);
    b->data  = NULL;
    b->dsize = 0;
}

bool qblock_cmp(struct qblock *a, struct qblock *b){
    if (a == NULL || b == NULL) return true;
    if (a->dsize != b->dsize) return false;

    return memcmp(
        a->data, b->data,
        a->dsize
    ) == 0;
}

void qblock_copy(struct allocator *allc, struct qblock *dest, const struct qblock *src){
    if (dest == NULL || src == NULL) return;

    qblock_free(allc, dest);
    
    if (src->data == NULL || src->dsize == 0){
        dest->data = NULL;
        dest->dsize = 0;
        return;
    }
    
    dest->data = alc_realloc(allc, dest->data, src->dsize);
    dest->dsize = src->dsize;
    memcpy(dest->data, src->data, src->dsize);
}

void qblock_fill(struct allocator *allc, struct qblock *dest, char *data, size_t sz){
    dest->data = alc_realloc(allc, dest->data, sz);
    dest->dsize = sz;
    memcpy(dest->data, data, sz);
}

int push_block(struct queue *q, struct qblock *b){
    mtx_lock(&q->blocks_mtx);
    if (q->head_size <= q->bsize + 1){
        q->head_size += QUEUE_HEAD_STEP;
    }
    
    struct qblock *lcopy = alc_realloc( q->allc,
        q->blocks, sizeof(struct qblock) * (q->head_size)
    );

    if (lcopy == NULL){
        mtx_unlock(&q->blocks_mtx);
        return -1;
    }

    lcopy[q->bsize].data  = NULL;
    lcopy[q->bsize].dsize = 0;
    qblock_copy(q->allc, &lcopy[q->bsize], b);

    q->blocks = lcopy;
    q->bsize++;
    mtx_unlock(&q->blocks_mtx);
    return 0;
}

int pop_block(struct queue *q, struct qblock *b){
    mtx_lock(&q->blocks_mtx);
    if (q->bsize == 0){
        mtx_unlock(&q->blocks_mtx);
        return 1;
    }

    if (q->bsize == 1){
        qblock_copy(q->allc, b, &q->blocks[0]);
        mtx_unlock(&q->blocks_mtx);
        queue_free(q);
        return 2;
    }

    qblock_copy(q->allc, b, &q->blocks[0]);
    for (size_t i = 1; i < q->bsize; i++){
        qblock_copy(q->allc, &q->blocks[i - 1], &q->blocks[i]);
    }

    qblock_free(q->allc, &q->blocks[q->bsize - 1]);
    q->blocks = alc_realloc(q->allc, q->blocks, sizeof(struct qblock) * (q->bsize - 1));
    q->bsize--;
    mtx_unlock(&q->blocks_mtx);
    return 0;
}

int peek_block(struct queue *q, struct qblock *b){
    mtx_lock(&q->blocks_mtx);
    if (q->bsize == 0){
        mtx_unlock(&q->blocks_mtx);
        return 1;
    }

    if (q->bsize == 1){
        qblock_copy(q->allc, b, &q->blocks[0]);
        mtx_unlock(&q->blocks_mtx);
        return 0;
    }

    qblock_copy(q->allc, b, &q->blocks[0]);
    mtx_unlock(&q->blocks_mtx);
    return 0;
}

bool queue_empty(struct queue *q){
    mtx_lock(&q->blocks_mtx);
    if (q->bsize == 0){
        mtx_unlock(&q->blocks_mtx);
        return true;
    }
    mtx_unlock(&q->blocks_mtx);
    return false;
}

void queue_free(struct queue *q){
    mtx_lock(&q->blocks_mtx);
    for (size_t i = 0; i < q->bsize; i++){
        qblock_free(q->allc, &q->blocks[i]);
    }
    alc_free(q->allc, q->blocks);
    q->blocks = NULL;
    q->bsize = 0;
    mtx_unlock(&q->blocks_mtx);
    mtx_destroy(&q->blocks_mtx);
}

bool queue_forward(struct queue *to, struct queue *from, bool peek){
    if (queue_empty(from))
        return false;

    struct qblock block;
    qblock_init(&block);
    if (peek){
        peek_block(from, &block);
        push_block(to, &block);
    } else {
        pop_block(from, &block);
        push_block(to, &block);   
    }
    qblock_free(from->allc, &block);

    return true;
}
