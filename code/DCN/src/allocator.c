#include <allocator.h>
#include <threads.h>

size_t hash_ptr(void *ptr, size_t table_size){
    return ((uintptr_t)ptr * 2654435761UL) % table_size;
}

void hash_table_resize(struct allocator *allc, size_t new_capacity) {
    mtx_lock(&allc->_mtx);
    struct bucket **old_buckets = allc->buckets;
    size_t old_capacity = allc->capacity;
    
    allc->buckets = calloc(new_capacity, sizeof(struct bucket*));
    if (allc->buckets == NULL) {
        allc->buckets = old_buckets;
        return;
    }

    allc->capacity = new_capacity;
    allc->size = 0;
    
    for (size_t i = 0; i < old_capacity; i++) {
        struct bucket *entry = old_buckets[i];
        while (entry != NULL) {
            struct bucket *next = entry->next;
            
            size_t new_index = hash_ptr(entry->ptr, new_capacity);
            
            entry->next = allc->buckets[new_index];
            allc->buckets[new_index] = entry;
            allc->size++;
            
            entry = next;
        }
    }
    
    free(old_buckets);
    mtx_unlock(&allc->_mtx);
}

void allocator_init(
    struct allocator *allc
){
    allc->buckets = calloc(HASH_TABLE_SIZE, sizeof(struct bucket *));
    allc->capacity = HASH_TABLE_SIZE;
    allc->size = 0;
    mtx_init(&allc->_mtx, mtx_recursive);
}

void allocator_end(
    struct allocator *allc
){
    alc_gcollect(allc);
    free(allc->buckets);
    mtx_destroy(&allc->_mtx);
}

void *alc_malloc(
    struct allocator *allc,
    size_t bytes
){
    void *ptr = malloc(bytes);
    if (ptr == NULL) return NULL;
    mtx_lock(&allc->_mtx);
    
    if ((float)allc->size / allc->capacity > LOAD_FACTOR_THRESHOLD) {
        hash_table_resize(allc, allc->capacity * 2);
    }

    size_t index = hash_ptr(ptr, allc->capacity);

    struct bucket *bk = malloc(sizeof(struct bucket));
    bk->ptr = ptr;
    bk->next = allc->buckets[index];
    allc->buckets[index] = bk;
    allc->size++;

    mtx_unlock(&allc->_mtx);
    return ptr;
}

void alc_free(
    struct allocator *allc,
    void *ptr
){
    if (ptr == NULL) return;
    
    mtx_lock(&allc->_mtx);
    size_t inx = hash_ptr(ptr, allc->capacity);
    struct bucket **prev = &allc->buckets[inx];
    struct bucket *current = allc->buckets[inx];

    while (current != NULL){
        if (current->ptr == ptr){
            *prev = current->next;
            free(current->ptr);
            free(current);
            allc->size--;
            mtx_unlock(&allc->_mtx);
            return;
        }

        prev = &current->next;
        current = current->next;
    }
    mtx_unlock(&allc->_mtx);
}

void *alc_calloc(
    struct allocator *allc, 
    size_t count, 
    size_t size
){
    void *ptr = calloc(count, size);
    if (ptr == NULL) return NULL;

    mtx_lock(&allc->_mtx);
    size_t index = hash_ptr(ptr, allc->capacity);

    struct bucket *bk = malloc(sizeof(struct bucket));
    bk->ptr = ptr;
    bk->next = allc->buckets[index];
    allc->buckets[index] = bk;
    allc->size++;

    mtx_unlock(&allc->_mtx);
    return ptr;
}

void *alc_realloc(
    struct allocator *allc, 
    void *ptr, 
    size_t new_bytes
){
    if (ptr == NULL)
        return alc_malloc(allc, new_bytes);
    
    if (new_bytes == 0) {
        alc_free(allc, ptr);
        return NULL;
    }

    mtx_lock(&allc->_mtx);
    
    size_t old_index = hash_ptr(ptr, allc->capacity);
    struct bucket **prev = &allc->buckets[old_index];
    struct bucket *current = allc->buckets[old_index];
    struct bucket *old_bucket = NULL;
    
    while (current != NULL) {
        if (current->ptr == ptr) {
            old_bucket = current;
            *prev = current->next;
            allc->size--;
            break;
        }
        prev = &current->next;
        current = current->next;
    }
    
    if (old_bucket == NULL) {
        mtx_unlock(&allc->_mtx);
        return NULL;
    }
    
    void *new_ptr = realloc(ptr, new_bytes);
    if (new_ptr == NULL) {
        old_bucket->next = allc->buckets[old_index];
        allc->buckets[old_index] = old_bucket;
        allc->size++;
        mtx_unlock(&allc->_mtx);
        return NULL;
    }
    
    if (new_ptr != ptr) {
        size_t new_index = hash_ptr(new_ptr, allc->capacity);
        
        old_bucket->ptr = new_ptr;
        old_bucket->next = allc->buckets[new_index];
        allc->buckets[new_index] = old_bucket;
        allc->size++;
    } else {
        old_bucket->next = allc->buckets[old_index];
        allc->buckets[old_index] = old_bucket;
        allc->size++;
    }
    
    if ((float)allc->size / allc->capacity > LOAD_FACTOR_THRESHOLD)
        hash_table_resize(allc, allc->capacity * 2);
    
    mtx_unlock(&allc->_mtx);
    return new_ptr;
}

void alc_gcollect(
    struct allocator *allc
){
    mtx_lock(&allc->_mtx);
    for (size_t i = 0; i < allc->capacity; i++){
        struct bucket *current = allc->buckets[i];
        while (current != NULL){
            struct bucket *next = current->next;
            free(current->ptr);
            free(current);
            current = next;
        }
        allc->buckets[i] = NULL;
    }
    allc->size = 0;
    mtx_unlock(&allc->_mtx);
}
