#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "memory.h"

#define NS_PER_SEC 1000000000ULL

typedef struct {
    struct timespec start;
} Timer;

static inline void timer_start(Timer *t) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &t->start);
}

static inline uint64_t timer_elapsed(Timer *t) {
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    uint64_t dsec = end.tv_sec - t->start.tv_sec;
    uint64_t dnsec = end.tv_nsec - t->start.tv_nsec;
    return dsec * NS_PER_SEC + dnsec;
}

#define ITERS 100

int main() {
    TieredAllocator ta;
    ta_init(&ta, 64, 1024);

    Timer timer;

    for (int t = 0; t < NUM_TIERS; ++t) {
        uint64_t total = 0;

        for (int i = 0; i < ITERS; ++i) {
            Ptr a = ta_create(&ta, t);
            timer_start(&timer);
            strcpy(ta_acquire(&ta, a), "Hello, World!");
            ta_flush(&ta, a);
            total += timer_elapsed(&timer);
        }

        printf("%s (%lu ns)\n", TIER_STRS[t], total / ITERS);
    }
}
