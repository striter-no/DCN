#include <threads.h>
#include <map.h>

void map_init(struct map *map, size_t k_size, size_t v_size){
    mtx_init(&map->_mtx, mtx_recursive);
    map->len = 0;
    map->k_size = k_size;
    map->v_size = v_size;
    map->keys = NULL;
    map->values = NULL;
}

void map_free(struct map *map){
    map->k_size = 0;
    map->v_size = 0;
    map->len    = 0;
    free(map->keys);
    free(map->values);
    map->keys = NULL;
    map->values = NULL;
    mtx_destroy(&map->_mtx);
}

// returns a pointer, not a copy
bool map_at(struct map *map, void *key, void **out){
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

// takes shallow copy of key and value
int map_set(struct map *map, void *key, void *val){
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
        memcpy((char*)(map->values) + inx * map->v_size, val, map->v_size);
    } else {
        char *k_copy = realloc(map->keys, map->k_size * (map->len + 1));
        if (k_copy == NULL)
            return -1;

        char *v_copy = realloc(map->values, map->v_size * (map->len + 1));
        if (v_copy == NULL)
            return -2;

        memcpy(k_copy + map->k_size * map->len, key, map->k_size);
        memcpy(v_copy + map->v_size * map->len, val, map->v_size);

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

    if (!found)
        return 1;
    
    if (map->len == 1){
        free(map->keys);
        free(map->values);
        map->keys = NULL;
        map->values = NULL;
        map->len--;
        return 0;
    }

    char *k_copy = malloc(map->k_size * (map->len - 1));
    if (k_copy == NULL)
        return -1;

    char *v_copy = malloc(map->v_size * (map->len - 1));
    if (v_copy == NULL)
        return -2;

    memcpy(
        k_copy + map->k_size * (inx), 
        (char*)map->keys + map->k_size * (inx + 1),
        map->k_size * (map->len - inx - 1)
    );

    memcpy(
        v_copy + map->v_size * (inx), 
        (char*)map->values + map->v_size * (inx + 1),
        map->v_size * (map->len - inx - 1)
    );

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
