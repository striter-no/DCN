#pragma once
#include <stdint.h>
#include <threads.h>
#include <stdlib.h>

#define HASH_TABLE_SIZE       1024
#define LOAD_FACTOR_THRESHOLD 0.75

struct bucket {
    void *ptr;
    struct bucket *next;
};

struct allocator {
    mtx_t _mtx;
    struct bucket **buckets;
    size_t size;     // num of actuall buckets
    size_t capacity; // num of allocated buckets
};

void allocator_init(
    struct allocator *allc
);

void allocator_end(
    struct allocator *allc
);

void *alc_malloc(
    struct allocator *allc,
    size_t bytes
);

void *alc_calloc(
    struct allocator *allc, 
    size_t count, 
    size_t size
);

void *alc_realloc(
    struct allocator *allc,
    void *ptr, // pointer from alc_malloc
    size_t new_bytes
);

void alc_free(
    struct allocator *allc,
    void *ptr
);

void alc_gcollect(
    struct allocator *allc
);
