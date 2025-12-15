#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "common.h"
#include "hash_map.h"
#include "memory.h"
#include "murmur/murmur3.h"

#define SENTINEL_END 1

void hash_map_init(
    HashMap *map, TieredAllocator *ta, u32 cap, u32 ram_buckets
) {
    ASSERT(ta->chunk_size > 2 * sizeof(Entry) + sizeof(Bucket));

    map->ta = ta;
    map->in_ram = 0;
    map->ram_buckets = ram_buckets;
    map->cap = cap;
    map->size = 0;
    map->buckets = malloc(cap * sizeof(*map->buckets));
    map->visits = calloc(cap, sizeof(*map->visits));

    for (u32 i = 0; i < cap; ++i)
        map->buckets[i] = null_ptr();
}

void hash_map_deinit(HashMap *map) {
    for (u32 i = 0; i < map->cap; ++i) {
        Ptr ptr = map->buckets[i];

        while (!is_null_ptr(ptr)) {
            Bucket *bucket = ta_acquire(map->ta, ptr);
            Ptr to_free = ptr;
            ptr = bucket->next;
            ta_destroy(map->ta, to_free);
        }
    }

    free(map->visits);
    free(map->buckets);
}

static bool can_fit(u32 bucket_space, u32 entry_size) {
    return bucket_space >= entry_size + sizeof(Entry) + 8;
}

static u32 key_to_entry_size(u32 key_size) {
    return key_size + sizeof(Entry) + 1;
}

static u8 compute_sentinel(u32 hash) {
    return (hash >> 25) << 1;
}

static bool entry_eq(
    const Entry *entry_a, const char *key_b, u32 key_size_b, u32 key_hash_b
) {
    if (entry_a->size != key_to_entry_size(key_size_b))
        return false;
    if (entry_a->sentinel != compute_sentinel(key_hash_b))
        return false;
    return memcmp(entry_a->key, key_b, key_size_b) == 0;
}

static Entry *next_entry(Entry *entry) {
    return (Entry *) ((u8 *) entry + align_u64(entry->size));
}

static bool is_end(Entry *entry) {
    return entry->sentinel == SENTINEL_END;
}

static bool in_bucket(Bucket *bucket, void *addr, u32 bucket_size) {
    return (u64) addr - (u64) bucket < bucket_size;
}

static Entry *hash_map_find(
    HashMap *map, Ptr root, const char *key, u32 key_size, u32 hash,
    Ptr *out_bucket_ptr
) {
    Ptr bucket_ptr = root;

    while (!is_null_ptr(bucket_ptr)) {
        Bucket *bucket = ta_acquire(map->ta, bucket_ptr);

        for (Entry *entry = bucket->data; !is_end(entry);
             entry = next_entry(entry)) {
            if (entry_eq(entry, key, key_size, hash)) {
                *out_bucket_ptr = bucket_ptr;
                return entry;
            }
        }

        Ptr old = bucket_ptr;
        bucket_ptr = bucket->next;
        ta_flush(map->ta, old);
    }

    return NULL;
}

static void hash_map_evict(HashMap *map) {
    for (u64 i = 0; i < map->cap; ++i) {
        Ptr bucket_ptr = map->buckets[i];
        if (is_null_ptr(bucket_ptr) || get_tier(bucket_ptr) != TIER_RAM)
            continue;
        if (map->visits[i]) {
            map->visits[i] = false;
            continue;
        }

        map->buckets[i] = ta_migrate(map->ta, bucket_ptr, TIER_CXL);
        bucket_ptr = map->buckets[i];
        Bucket *bucket = ta_acquire_raw(map->ta, bucket_ptr);

        while (true) {
            Ptr next_ptr = bucket->next;
            if (is_null_ptr(next_ptr))
                break;
            Ptr new = ta_migrate(map->ta, next_ptr, TIER_CXL);
            bucket->next = new;
            ta_flush(map->ta, bucket_ptr);
            bucket_ptr = new;
            bucket = ta_acquire_raw(map->ta, new);
        }

        ta_flush(map->ta, bucket_ptr);
        break;
    }

    map->in_ram -= 1;
}

static void hash_map_check(HashMap *map) {
    /* u64 count = 0; */

    /* for (u64 i = 0; i < map->cap; ++i) { */
    /*     Ptr bucket_ptr = map->buckets[i]; */

    /*     while (!is_null_ptr(bucket_ptr)) { */
    /*         ASSERT(ta_ptr_valid(map->ta, bucket_ptr)); */
    /*         Bucket *bucket = ta_acquire_raw(map->ta, bucket_ptr); */

    /*         for (Entry *entry = bucket->data; !is_end(entry); */
    /*              entry = next_entry(entry)) */
    /*             count += 1; */

    /*         bucket_ptr = bucket->next; */
    /*     } */
    /* } */

    /* ASSERT(count == map->size); */
    (void) map;
}

bool hash_map_put(HashMap *map, const char *key, u64 value) {
    u32 key_size = strlen(key);
    u32 entry_size = key_to_entry_size(key_size);

    u32 hash;
    MurmurHash3_x86_32(key, key_size, 22, &hash);

    u32 i = hash % map->cap;
    Ptr bucket_ptr = map->buckets[i];
    Bucket *bucket = NULL;
    MemoryTier tier = TIER_RAM;

    if (!is_null_ptr(bucket_ptr)) {
        tier = get_tier(bucket_ptr);
        if (tier == TIER_RAM)
            map->visits[i] = true;
    }
    else {
        map->in_ram += 1;
    }

    Entry *entry =
        hash_map_find(map, bucket_ptr, key, key_size, hash, &bucket_ptr);
    if (entry) {
        entry->value = value;
        ta_flush(map->ta, bucket_ptr);

        hash_map_check(map);
        return false;
    }

    while (!is_null_ptr(bucket_ptr)) {
        bucket = ta_acquire(map->ta, bucket_ptr);
        if (can_fit(bucket->space, entry_size))
            break;
        Ptr old = bucket_ptr;
        bucket_ptr = bucket->next;
        ta_flush(map->ta, old);
    }

    if (is_null_ptr(bucket_ptr)) {
        Ptr new_ptr = ta_create(map->ta, tier);
        Bucket *new = ta_acquire(map->ta, new_ptr);

        new->data[0].sentinel = SENTINEL_END;
        new->space = map->ta->chunk_size - offsetof(Bucket, data);
        new->next = map->buckets[i];
        map->buckets[i] = new_ptr;

        ASSERT(entry_size + sizeof(Entry) + 8 <= new->space);

        bucket_ptr = new_ptr;
        bucket = new;
    }

    for (entry = bucket->data; !is_end(entry); entry = next_entry(entry)) {}

    entry->value = value;
    entry->sentinel = compute_sentinel(hash);
    entry->size = entry_size;
    memcpy(entry->key, key, key_size + 1);

    Entry *end = next_entry(entry);
    ASSERT(in_bucket(bucket, &end->sentinel, map->ta->chunk_size));
    end->sentinel = SENTINEL_END;
    map->size += 1;
    hash_map_check(map);
    bucket->space = map->ta->chunk_size - ((u64) &end->key - (u64) bucket);

    ta_flush(map->ta, bucket_ptr);

    if (map->in_ram > map->ram_buckets) {
        hash_map_evict(map);
        ASSERT(map->in_ram == map->ram_buckets);
    }

    hash_map_check(map);
    return true;
}

bool hash_map_get(HashMap *map, const char *key, u64 *value) {
    u32 key_size = strlen(key);

    u32 hash;
    MurmurHash3_x86_32(key, key_size, 22, &hash);

    u32 i = hash % map->cap;
    Ptr bucket_ptr = map->buckets[i];

    Entry *entry =
        hash_map_find(map, bucket_ptr, key, key_size, hash, &bucket_ptr);
    if (entry) {
        *value = entry->value;
        ta_flush(map->ta, bucket_ptr);
        if (get_tier(bucket_ptr) == TIER_RAM)
            map->visits[i] = true;
        hash_map_check(map);
        return true;
    }

    hash_map_check(map);
    return false;
}

void hash_map_iter(HashMapIter *iter, HashMap *map) {
    iter->map = map;
    iter->i = 0;
    iter->bucket_ptr = null_ptr();
    iter->entry = NULL;
}

Entry *hash_map_iter_next(HashMapIter *iter) {
    for (; iter->i < iter->map->cap; ++iter->i) {
        if (is_null_ptr(iter->map->buckets[iter->i]))
            continue;

        if (iter->entry == NULL) {
            if (!is_null_ptr(iter->bucket_ptr))
                ta_flush(iter->map->ta, iter->bucket_ptr);

            Bucket *bucket =
                ta_acquire(iter->map->ta, iter->map->buckets[iter->i]);

            iter->bucket_ptr = iter->map->buckets[iter->i];
            iter->entry = bucket->data;
        }

        Entry *entry = iter->entry;
        iter->entry = next_entry(entry);

        if (is_end(iter->entry)) {
            iter->entry = NULL;
            iter->i += 1;
        }

        return entry;
    }

    if (!is_null_ptr(iter->bucket_ptr))
        ta_flush(iter->map->ta, iter->bucket_ptr);

    return NULL;
}

void hash_map_debug(HashMap *map, FILE *file) {
    u64 count = 0;

    for (u64 i = 0; i < map->cap; ++i) {
        Ptr bucket_ptr = map->buckets[i];

        while (!is_null_ptr(bucket_ptr)) {
            Bucket *bucket = ta_acquire(map->ta, bucket_ptr);

            for (Entry *entry = bucket->data; !is_end(entry);
                 entry = next_entry(entry)) {
                count += 1;
                fprintf(file, "%s -> %lu\n", entry->key, entry->value);
            }

            Ptr old = bucket_ptr;
            bucket_ptr = bucket->next;
            ta_flush(map->ta, old);
        }
    }

    ASSERT(count == map->size);
}

u64 hash_map_mem_usage(HashMap *map) {
    u64 total = map->cap * (sizeof(*map->buckets) + sizeof(*map->visits));
    for (u8 t = 0; t < NUM_TIERS; ++t)
        total += map->ta->memory_usage[t];
    return total;
}
