#ifndef MEMORY_H_
#define MEMORY_H_

#include "common.h"

typedef enum {
    TIER_RAM = 0,
    TIER_CXL = 1,
    TIER_SSD = 2,
    NUM_TIERS = 3,
} MemoryTier;

typedef struct {
    void **free_list;
} MemoryPool;

typedef struct {
    u64 cap;
    u64 chunk_size;
    void *buffers[3];
    MemoryPool pools[3];

    int backing_fd;
    void *cxl_scratch;

    u32 *cxl_usage;
    u64 memory_usage[3];
    bool *borrowed[3];
} TieredAllocator;

typedef u64 Ptr;

extern const char *TIER_STRS[3];

void ta_init(TieredAllocator *ta, u64 size, u64 chunk_size);
void ta_deinit(TieredAllocator *ta);

Ptr ta_create(TieredAllocator *ta, MemoryTier tier);
void ta_destroy(TieredAllocator *ta, Ptr ptr);
Ptr ta_migrate(TieredAllocator *ta, Ptr ptr, MemoryTier tier);

void *ta_acquire(TieredAllocator *ta, Ptr ptr);
void ta_flush(TieredAllocator *ta, Ptr ptr);
void *ta_acquire_raw(TieredAllocator *ta, Ptr ptr);

bool ta_ptr_valid(TieredAllocator *ta, Ptr ptr);

MemoryTier get_tier(Ptr ptr);
Ptr null_ptr();
bool is_null_ptr(Ptr ptr);

u64 chunk_bound(u64 chunk_size);

#endif // MEMORY_H_
