#include <lz4.h>

#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "memory.h"

const char *TIER_STRS[3] = {"RAM", "CXL", "SSD"};

static void *mp_create(MemoryPool *mp) {
    ASSERT(mp->free_list != NULL);
    void *ptr = mp->free_list;
    mp->free_list = *mp->free_list;
    return ptr;
}

static void mp_destroy(MemoryPool *mp, void *ptr) {
    void *next = mp->free_list;
    mp->free_list = ptr;
    *mp->free_list = next;
}

static void mp_init(MemoryPool *mp, u64 size, u64 chunk_size, void *pool) {
    mp->free_list = NULL;
    for (u64 offset = chunk_size; offset < size; offset += chunk_size)
        mp_destroy(mp, pool + offset - chunk_size);
}

u64 chunk_bound(u64 chunk_size) {
    return align_u64(LZ4_compressBound(chunk_size));
}

void ta_init(TieredAllocator *ta, u64 chunk_size, u64 num_chunks) {
    u64 chunk_cap = chunk_bound(chunk_size);
    u64 cap = chunk_cap * num_chunks;

    ta->cap = cap;
    ta->chunk_size = chunk_size;
    ta->backing_fd = open("/var/tmp/ssd", O_CREAT | O_RDWR | O_TRUNC, 0666);
    ASSERT(ta->backing_fd != -1);
    ASSERT(ftruncate(ta->backing_fd, cap) == 0);

    ta->cxl_scratch = malloc(chunk_cap);
    ta->buffers[TIER_RAM] = malloc(cap);
    ta->buffers[TIER_CXL] = malloc(cap);
    ta->buffers[TIER_SSD] =
        mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_SHARED, ta->backing_fd, 0);

    ASSERT(ta->cxl_scratch);
    ASSERT(ta->buffers[TIER_RAM]);
    ASSERT(ta->buffers[TIER_CXL]);
    ASSERT(ta->buffers[TIER_SSD] != MAP_FAILED);

    for (u8 t = 0; t < NUM_TIERS; ++t) {
        ta->borrowed[t] = calloc(num_chunks, sizeof(*ta->borrowed));
        ASSERT(ta->borrowed[t]);
        mp_init(ta->pools + t, cap, chunk_cap, ta->buffers[t]);
    }

    ta->cxl_usage = calloc(num_chunks, sizeof (*ta->cxl_usage));
    ASSERT(ta->cxl_usage);
    memset(ta->memory_usage, 0, sizeof (ta->memory_usage));
}

void ta_deinit(TieredAllocator *ta) {
    u64 chunk_cap = chunk_bound(ta->chunk_size);
    u64 num_chunks = ta->cap / chunk_cap;

    for (u8 t = 0; t < NUM_TIERS; ++t) {
        for (u64 i = 0; i < num_chunks; ++i)
            ASSERT(!ta->borrowed[t][i]);

        free(ta->borrowed[t]);
    }

    free(ta->cxl_usage);
    free(ta->cxl_scratch);
    free(ta->buffers[TIER_RAM]);
    free(ta->buffers[TIER_CXL]);
    ASSERT(munmap(ta->buffers[TIER_SSD], ta->cap) != -1);
    ASSERT(close(ta->backing_fd) != -1);
}

Ptr ta_create(TieredAllocator *ta, MemoryTier tier) {
    void *buf = mp_create(ta->pools + tier);
    u64 offset = buf - ta->buffers[tier];
    u64 chunk_cap = chunk_bound(ta->chunk_size);
    u64 chunk_num = offset / chunk_cap;
    Ptr ptr = ((u64) tier << 62) | offset;

    if (tier == TIER_CXL) {
        ta->borrowed[tier][chunk_num] = true;
        memset(buf, 0, ta->chunk_size);
        ta_flush(ta, ptr);
    }
    else {
        ta->memory_usage[tier] += ta->chunk_size;
    }

    return ptr;
}

void ta_destroy(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = get_tier(ptr);
    u64 offset = (ptr << 2) >> 2;
    u64 chunk_cap = chunk_bound(ta->chunk_size);
    u64 chunk_num = offset / chunk_cap;

    ASSERT(tier < NUM_TIERS);

    ta->borrowed[tier][chunk_num] = false;

    if (tier == TIER_CXL) {
        ta->memory_usage[tier] -= ta->cxl_usage[chunk_num];
        ta->cxl_usage[chunk_num] = 0;
    }
    else {
        ta->memory_usage[tier] -= ta->chunk_size;
    }

    mp_destroy(ta->pools + tier, ta->buffers[tier] + offset);
}

Ptr ta_migrate(TieredAllocator *ta, Ptr src_ptr, MemoryTier tier) {
    Ptr dst_ptr = ta_create(ta, tier);
    void *src = ta_acquire(ta, src_ptr);
    void *dst = ta_acquire(ta, dst_ptr);
    memcpy(dst, src, ta->chunk_size);
    ta_destroy(ta, src_ptr);
    return dst_ptr;
}

void *ta_acquire(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = get_tier(ptr);
    u64 offset = (ptr << 2) >> 2;
    u64 chunk_cap = chunk_bound(ta->chunk_size);
    u64 chunk_num = offset / chunk_cap;
    void *p = ta->buffers[tier] + offset;

    ASSERT(tier < NUM_TIERS);
    ASSERT(!ta->borrowed[tier][chunk_num]);
    ta->borrowed[tier][chunk_num] = true;

    switch (tier) {
    case TIER_RAM:
        break;
    case TIER_CXL:
        LZ4_decompress_safe(p, ta->cxl_scratch, chunk_cap, chunk_cap);
        memcpy(p, ta->cxl_scratch, ta->chunk_size);
        break;
    case TIER_SSD:
        madvise(p, chunk_cap, MADV_DONTNEED);
        break;
    default:
        break;
    }

    return p;
}

void *ta_acquire_raw(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = get_tier(ptr);
    u64 offset = (ptr << 2) >> 2;
    void *p = ta->buffers[tier] + offset;

    return p;
}

void ta_flush(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = get_tier(ptr);
    u64 offset = (ptr << 2) >> 2;
    u64 chunk_cap = chunk_bound(ta->chunk_size);
    u64 chunk_num = offset / chunk_cap;
    void *p = ta->buffers[tier] + offset;

    ASSERT(tier < NUM_TIERS);
    ASSERT(ta->borrowed[tier][chunk_num]);
    ta->borrowed[tier][chunk_num] = false;

    switch (tier) {
    case TIER_RAM:
        break;
    case TIER_CXL:
        ta->memory_usage[tier] -= ta->cxl_usage[chunk_num];
        ta->cxl_usage[chunk_num] =
            LZ4_compress_default(p, ta->cxl_scratch, ta->chunk_size, chunk_cap);
        memcpy(p, ta->cxl_scratch, chunk_cap);
        ta->memory_usage[tier] += ta->cxl_usage[chunk_num];
        break;
    case TIER_SSD:
        msync(p, chunk_cap, MS_SYNC);
        break;
    default:
        break;
    }
}

bool ta_ptr_valid(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = get_tier(ptr);
    u64 offset = (ptr << 2) >> 2;
    u64 chunk_cap = chunk_bound(ta->chunk_size);
    u64 chunk_num = offset / chunk_cap;
    u64 num_chunks = ta->cap / chunk_cap;

    return tier < NUM_TIERS && chunk_num < num_chunks;
}

MemoryTier get_tier(Ptr ptr) {
    return ptr >> 62;
}

Ptr null_ptr() {
    return (u64) NUM_TIERS << 62;
}

bool is_null_ptr(Ptr ptr) {
    return get_tier(ptr) == NUM_TIERS;
}
