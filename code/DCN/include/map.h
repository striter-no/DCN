#pragma once
#include <string.h>
#include <stdbool.h>
#include <threads.h>

struct map {
    void *keys;
    void *values;
    size_t k_size;
    size_t v_size;

    size_t len;
    mtx_t  _mtx;
};

void map_init(struct map *map, size_t k_size, size_t v_size);
void map_clear(struct map *map);
void map_free(struct map *map);

bool map_at(struct map *map, void *key, void **out);
bool map_copy_at(struct map *map, void *key, void *out);
bool map_in(struct map *map, void *key);
bool map_key_by_val(struct map *map, void *val, void **out);
bool map_value_in(struct map *map, void *value);
bool map_empty(struct map *map);

int map_set(struct map *map, void *key, void *val);
int map_erase(struct map *map, void *key);
int map_key_at(struct map *map, void *out, size_t inx);
int map_val_at(struct map *map, void *out, size_t inx);
