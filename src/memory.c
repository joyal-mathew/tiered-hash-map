#include <lz4.h>

#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "memory.h"

const char *TIER_STRS[3] = {"RAM", "CXL", "SSD"};

void *mp_create(MemoryPool *mp) {
    ASSERT(mp->free_list != NULL);
    void *ptr = mp->free_list;
    mp->free_list = *mp->free_list;
    return ptr;
}

void mp_destroy(MemoryPool *mp, void *ptr) {
    void *next = mp->free_list;
    mp->free_list = ptr;
    *mp->free_list = next;
}

void mp_init(MemoryPool *mp, u64 size, u64 chunk_size, void *pool) {
    mp->free_list = NULL;
    for (u64 offset = chunk_size; offset < size; offset += chunk_size)
        mp_destroy(mp, pool + offset - chunk_size);
}

void ta_init(TieredAllocator *ta, u64 chunk_size, u64 num_chunks) {
    u64 chunk_cap = LZ4_compressBound(chunk_size);
    u64 cap = chunk_cap * num_chunks;

    ta->cap = cap;
    ta->chunk_size = chunk_size;
    ta->backing_fd = open("/var/tmp/ssd", O_CREAT | O_RDWR | O_TRUNC, 0644);
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

    for (u8 t = 0; t < NUM_TIERS; ++t)
        mp_init(ta->pools + t, cap, chunk_cap, ta->buffers[t]);
}

void ta_deinit(TieredAllocator *ta) {
    free(ta->cxl_scratch);
    free(ta->buffers[TIER_RAM]);
    free(ta->buffers[TIER_CXL]);
    ASSERT(munmap(ta->buffers[TIER_SSD], ta->cap) != -1);
    ASSERT(close(ta->backing_fd) != -1);
}

Ptr ta_create(TieredAllocator *ta, MemoryTier tier) {
    void *ptr = mp_create(ta->pools + tier);
    u64 offset = ptr - ta->buffers[tier];
    return ((u64) tier << 62) | offset;
}

void ta_destroy(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = ptr >> 62;
    u64 offset = (ptr << 2) >> 2;
    mp_destroy(ta->pools + tier, ta->buffers[tier] + offset);
}

void *ta_acquire(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = ptr >> 62;
    u64 offset = (ptr << 2) >> 2;
    u64 chunk_cap = LZ4_compressBound(ta->chunk_size);
    void *p = ta->buffers[tier] + offset;

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

const void *ta_peek(TieredAllocator *ta, Ptr ptr) {
    return ta_acquire(ta, ptr);
}

void ta_flush(TieredAllocator *ta, Ptr ptr) {
    MemoryTier tier = ptr >> 62;
    u64 offset = (ptr << 2) >> 2;
    u64 chunk_cap = LZ4_compressBound(ta->chunk_size);
    void *p = ta->buffers[tier] + offset;

    switch (tier) {
    case TIER_RAM:
        break;
    case TIER_CXL:
        LZ4_compress_default(p, ta->cxl_scratch, ta->chunk_size, chunk_cap);
        memcpy(p, ta->cxl_scratch, chunk_cap);
        break;
    case TIER_SSD:
        msync(p, chunk_cap, MS_SYNC);
        break;
    default:
        break;
    }
}
