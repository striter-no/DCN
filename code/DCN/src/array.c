#include <array.h>
#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

void array_init(struct array *arr, size_t element){
    mtx_init(&arr->_mtx, mtx_recursive);
    arr->blocks = NULL;
    arr->len = 0;
    arr->head = 0;
    arr->el_size = element;
}

void array_free(struct array *arr){
    mtx_lock(&arr->_mtx);
    free(arr->blocks);
    arr->blocks = NULL;
    arr->len = 0;
    arr->head = 0;
    mtx_unlock(&arr->_mtx);
    mtx_destroy(&arr->_mtx);
}


size_t array_size(struct array *arr){
    mtx_lock(&arr->_mtx);
    size_t size = arr->len;
    mtx_unlock(&arr->_mtx);
    return size;
}

size_t array_index(struct array *arr, void *block){
    mtx_lock(&arr->_mtx);
    size_t inx = SIZE_MAX;
    for (size_t i = 0; i < arr->len; i++){
        void *current = NULL;
        array_at(arr, &current, i);
        if (current == block){
            inx = i;
            break;
        }
    }
    mtx_unlock(&arr->_mtx);
    return inx;
}

bool array_in(struct array *arr, void *block){
    mtx_lock(&arr->_mtx);
    bool found = false;
    for (size_t i = 0; i < arr->len; i++){
        void *current = NULL;
        array_at(arr, &current, i);
        if (current == block){
            found = true;
            break;
        }
    }
    mtx_unlock(&arr->_mtx);
    return found;
}

bool array_del(struct array *arr, size_t inx){
    mtx_lock(&arr->_mtx);
    
    if (inx >= arr->len){
        mtx_unlock(&arr->_mtx);
        return false;
    }

    if (inx < arr->len - 1) {
        memmove(
            (char*)(arr->blocks) + inx * arr->el_size,
            (char*)(arr->blocks) + (inx + 1) * arr->el_size,
            (arr->len - inx - 1) * arr->el_size
        );
    }
    
    arr->len--;

    if (arr->head - arr->len > ARRAY_HEAD_STEP * 2 && arr->len > 0){
        void *lcopy = realloc(arr->blocks, (arr->len - 1) * arr->el_size);
        if (lcopy == NULL)
            return false;
        arr->blocks = lcopy;
    }

    mtx_unlock(&arr->_mtx);
    return true;
}

int array_append(struct array *arr, void *block){
    mtx_lock(&arr->_mtx);

    if (arr->len >= arr->head){
        struct qblock *lcopy = realloc(
            arr->blocks, 
            (arr->head + ARRAY_HEAD_STEP) * arr->el_size
        );

        if (lcopy == NULL){
            mtx_unlock(&arr->_mtx);
            return -1;
        }
        arr->blocks = lcopy;
        arr->head += ARRAY_HEAD_STEP;
    }

    memcpy(
        (char*)(arr->blocks) + arr->len * arr->el_size,
        block,
        arr->el_size
    );

    arr->len++;
    mtx_unlock(&arr->_mtx);
    return 0;
}

int array_at(struct array *arr, void **out, size_t inx){
    mtx_lock(&arr->_mtx);

    if (inx >= arr->len){
        *out = NULL;
        mtx_unlock(&arr->_mtx);
        return -1;
    }

    *out = (char*)(arr->blocks) + inx * arr->el_size;

    mtx_unlock(&arr->_mtx);
    return 0;
}

int array_copy_at(struct array *arr, void *out, size_t inx){
    mtx_lock(&arr->_mtx);
    
    if (inx >= arr->len){
        mtx_unlock(&arr->_mtx);
        return -1;
    }

    memcpy(out, (char*)(arr->blocks) + inx * arr->el_size, arr->el_size);

    mtx_unlock(&arr->_mtx);
    return 0;
}
