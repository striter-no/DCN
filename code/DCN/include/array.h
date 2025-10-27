#pragma once
#define ARRAY_HEAD_STEP 10
#include <string.h>
#include <stdlib.h>
#include <queue.h>

struct array {
    void *blocks;

    mtx_t _mtx;
    size_t len;
    size_t head;
    size_t el_size;
};

void array_init(struct array *arr, size_t element);
void array_free(struct array *arr);

size_t array_size(struct array *arr);
bool array_in(struct array *arr, void *block);
bool array_del(struct array *arr, size_t inx);

size_t array_index(struct array *arr, void *block);
int array_append(struct array *arr, void *block);
int array_at(struct array *arr, void **out, size_t inx);
int array_copy_at(struct array *arr, void *out, size_t inx);
