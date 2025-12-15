#ifndef HASH_MAP_H_
#define HASH_MAP_H_

#include <stdio.h>

#include "memory.h"

typedef struct {
    u64 value;
    u32 size;
    u8 sentinel;
    char key[];
} Entry;

typedef struct {
    Ptr next;
    u32 space;
    Entry data[];
} Bucket;

typedef struct {
    TieredAllocator *ta;
    Ptr *buckets;
    bool *visits;
    u32 hand;
    u32 size;
    u32 cap;
    u32 in_ram;
    u32 ram_buckets;
} HashMap;

typedef struct {
    HashMap *map;
    u64 i;
    Ptr bucket_ptr;
    Entry *entry;
} HashMapIter;

void hash_map_init(HashMap *map, TieredAllocator *ta, u32 cap, u32 ram_buckets);
void hash_map_deinit(HashMap *map);

bool hash_map_put(HashMap *map, const char *key, u64 value);
bool hash_map_get(HashMap *map, const char *key, u64 *value);

void hash_map_iter(HashMapIter *iter, HashMap *map);
Entry *hash_map_iter_next(HashMapIter *iter);

void hash_map_debug(HashMap *map, FILE *file);
u64 hash_map_mem_usage(HashMap *map);

#endif // HASH_MAP_H_
