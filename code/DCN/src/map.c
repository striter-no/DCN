#include <stdint.h>
#include <threads.h>
#include <map.h>
#include <math.h>

void __print_buffer(
    char *buffer,
    size_t size
){
    size_t line_size = 10;
    size_t lines = ceil((float)size / line_size);

    for (size_t i = 0, rm = 0; i < (lines > 0 ? lines: 1); i++){
        printf("%.10lx ", i * line_size);
        
        for (size_t k = 0; k < line_size; k++){
            if (rm < size){
                printf("%.2x ", (int)buffer[rm]);
                rm++;
            } else {
                printf("-- ");
            }
        }
        
        printf("\n");
    }
}

void map_init(struct map *map, size_t k_size, size_t v_size){
    mtx_init(&map->_mtx, mtx_recursive);
    map->len = 0;
    map->k_size = k_size;
    map->v_size = v_size;
    map->keys = NULL;
    map->values = NULL;
}

void map_free(struct map *map){
    mtx_lock(&map->_mtx);
    map->k_size = 0;
    map->v_size = 0;
    map->len    = 0;
    free(map->keys);
    free(map->values);
    map->keys = NULL;
    map->values = NULL;
    mtx_unlock(&map->_mtx);
    mtx_destroy(&map->_mtx);
}

// returns a pointer, not a copy
bool map_at(struct map *map, void *key, void **out){
    if (map == NULL || key == NULL || out == NULL) {
        printf("map_at: ERROR - null parameters\n");
        return false;
    }
    
    if (map->k_size == 0 || map->v_size == 0) {
        printf("map_at: ERROR - zero sizes: k_size=%zu, v_size=%zu\n", 
               map->k_size, map->v_size);
        return false;
    }
    
    mtx_lock(&map->_mtx);
    
    bool found = false;
    size_t inx = 0;
    
    for (size_t ki = 0; ki < map->len; ki++){
        char *k = (char*)(map->keys) + ki * (map->k_size);
        
        if (memcmp(k, key, map->k_size) == 0){
            inx = ki;
            found = true;
            break;
        }
    }

    if (!found){
        *out = NULL;
        mtx_unlock(&map->_mtx);
        return false;
    }

    *out = (char*)(map->values) + inx * map->v_size;
    
    if (*out == NULL || (uintptr_t)*out < 0x1000) {
        printf("map_at: WARNING - suspicious pointer value: %p\n", *out);
    }
    
    mtx_unlock(&map->_mtx);
    return true;
}

bool map_in(struct map *map, void *key){
    mtx_lock(&map->_mtx);
    bool found = false;
    for (size_t ki = 0; ki < map->len; ki++){
        char *k = (char*)(map->keys) + ki * (map->k_size);
        if (memcmp(k, key, map->k_size) == 0){
            found = true;
            break;
        }
    }
    
    mtx_unlock(&map->_mtx);
    return found;
}

bool map_value_in(struct map *map, void *value){
    mtx_lock(&map->_mtx);
    bool found = false;
    for (size_t ki = 0; ki < map->len; ki++){
        char *k = (char*)(map->values) + ki * (map->v_size);
        if (memcmp(k, value, map->v_size) == 0){
            found = true;
            break;
        }
    }
    
    mtx_unlock(&map->_mtx);
    return found;
}

// returns a shallow copy
bool map_copy_at(struct map *map, void *key, void *out){
    mtx_lock(&map->_mtx);
    bool found = false;
    size_t inx = 0;
    for (size_t ki = 0; ki < map->len; ki++){
        char *k = (char*)(map->keys) + ki * (map->k_size);
        if (memcmp(k, key, map->k_size) == 0){
            inx = ki;
            found = true;
            break;
        }
    }

    if (!found){
        mtx_unlock(&map->_mtx);
        return false;
    }

    memcpy(out, (char*)(map->values) + inx * map->v_size, map->v_size);
    mtx_unlock(&map->_mtx);
    return true;
}

bool map_key_by_val(struct map *map, void *val, void **out){
    mtx_lock(&map->_mtx);
    bool found = false;
    size_t inx = 0;
    for (size_t ki = 0; ki < map->len; ki++){
        char *k = (char*)(map->values) + ki * (map->v_size);
        if (memcmp(k, val, map->v_size) == 0){
            inx = ki;
            found = true;
            break;
        }
    }

    if (!found){
        *out = NULL;
        mtx_unlock(&map->_mtx);
        return false;
    }

    *out = (char*)(map->keys) + inx * map->k_size;
    mtx_unlock(&map->_mtx);
    return true;
}

// takes shallow copy of key and value
int map_set(struct map *map, void *key, void *val){
    if (map == NULL || key == NULL || val == NULL) {
        printf("map_set: ERROR - null parameters\n");
        return -3;
    }
    
    if (map->k_size == 0 || map->v_size == 0) {
        printf("map_set: ERROR - zero sizes: k_size=%zu, v_size=%zu\n", 
               map->k_size, map->v_size);
        return -4;
    }

    mtx_lock(&map->_mtx);
    bool found = false;
    size_t inx = 0;
    for (size_t ki = 0; ki < map->len; ki++){
        char *k = (char*)(map->keys) + ki * (map->k_size);
        if (memcmp(k, key, map->k_size) == 0){
            inx = ki;
            found = true;
            break;
        }
    }

    if (found){
        void *target = (char*)(map->values) + inx * map->v_size;
        memcpy(target, val, map->v_size);
    } else {
        char *k_copy = realloc(map->keys, map->k_size * (map->len + 1));
        if (k_copy == NULL)
            return -1;

        char *v_copy = realloc(map->values, map->v_size * (map->len + 1));
        if (v_copy == NULL)
            return -2;

        memcpy(k_copy + map->k_size * map->len, key, map->k_size);
        void *target = v_copy + map->v_size * map->len;
        memcpy(target, val, map->v_size);

        map->keys   = k_copy;
        map->values = v_copy;
        map->len++;
    }
    mtx_unlock(&map->_mtx);
    return 0;
}

int map_erase(struct map *map, void *key){
    mtx_lock(&map->_mtx);
    bool found = false;
    size_t inx = 0;
    for (size_t ki = 0; ki < map->len; ki++){
        char *k = (char*)(map->keys) + ki * (map->k_size);
        if (memcmp(k, key, map->k_size) == 0){
            inx = ki;
            found = true;
            break;
        }
    }

    if (!found){
        mtx_unlock(&map->_mtx);
        return 1;
    }
    
    if (map->len == 1){
        free(map->keys);
        free(map->values);
        map->keys = NULL;
        map->values = NULL;
        map->len--;
        mtx_unlock(&map->_mtx);
        return 0;
    }

    char *k_copy = malloc(map->k_size * (map->len - 1));
    if (k_copy == NULL){
        mtx_unlock(&map->_mtx);
        return -1;
    }

    char *v_copy = malloc(map->v_size * (map->len - 1));
    if (v_copy == NULL){
        mtx_unlock(&map->_mtx);
        return -2;
    }

    if (inx > 0) {
        memcpy(k_copy, map->keys, map->k_size * inx);
        memcpy(v_copy, map->values, map->v_size * inx);
    }

    if (inx < map->len - 1) {
        memcpy(
            k_copy + map->k_size * inx, 
            (char*)map->keys + map->k_size * (inx + 1),
            map->k_size * (map->len - inx - 1)
        );
        memcpy(
            v_copy + map->v_size * inx, 
            (char*)map->values + map->v_size * (inx + 1),
            map->v_size * (map->len - inx - 1)
        );
    }

    free(map->keys);
    free(map->values);
    map->keys = k_copy;
    map->values = v_copy;

    map->len--;
    mtx_unlock(&map->_mtx);
    return 0;
}

// returns a shallow copy
int map_key_at(struct map *map, void *out, size_t inx){
    mtx_lock(&map->_mtx);
    if (inx >= map->len){
        mtx_unlock(&map->_mtx);
        return -1;
    }

    memcpy(out, (char*)(map->keys) + map->k_size * inx, map->k_size);
    mtx_unlock(&map->_mtx);
    return 0;
}

// returns a shallow copy
int map_val_at(struct map *map, void *out, size_t inx){
    mtx_lock(&map->_mtx);
    if (inx >= map->len){
        mtx_unlock(&map->_mtx);
        return -1;
    }

    memcpy(out, (char*)map->values + map->v_size * inx, map->v_size);
    mtx_unlock(&map->_mtx);
    return 0;
}

void map_clear(struct map *map){
    mtx_lock(&map->_mtx);
    map->len    = 0;
    free(map->keys);
    free(map->values);
    map->keys = NULL;
    map->values = NULL;
    mtx_unlock(&map->_mtx);
}
