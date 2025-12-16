#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "hash_map.h"
#include "memory.h"

#define NS_PER_SEC 1000000000ULL
#define B_PER_MIB (1024 * 1024)

typedef struct {
    struct timespec start;
} Timer;

static inline void timer_start(Timer *t) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &t->start);
}

static inline u64 timer_elapsed(Timer *t) {
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    u64 dsec = end.tv_sec - t->start.tv_sec;
    u64 dnsec = end.tv_nsec - t->start.tv_nsec;
    return dsec * NS_PER_SEC + dnsec;
}

static u64 count_words(HashMap *counter, char *text) {
    u64 word_start = 0;
    u64 bytes_in = 0;

    for (u64 i = 0; text[i]; ++i) {
        if (!isalpha(text[i])) {
            u64 len = i - word_start;
            if (len > 0) {
                text[i] = 0;
                u64 count = 0;

                bytes_in += len + 9;
                hash_map_get(counter, text + word_start, &count);
                hash_map_put(counter, text + word_start, count + 1);
            }

            word_start = i + 1;
        }
    }

    return bytes_in;
}

char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    ASSERT(f);

    ASSERT(fseek(f, 0, SEEK_END) != -1);
    long n = ftell(f);
    ASSERT(n > 0);
    rewind(f);

    char *buffer = malloc(n + 1);
    ASSERT(buffer);
    ASSERT(fread(buffer, 1, n, f) == (unsigned long) n);
    ASSERT(fclose(f) == 0);

    buffer[n] = 0;
    return buffer;
}

#define NEXT_ARG(arr, n) (n == 0 ? abort() : 0, n--, *arr++)

void placement(int argc, char **argv) {
    TieredAllocator ta;
    ta_init(&ta, 256, 1 << 16);

    int buckets = atoi(NEXT_ARG(argv, argc));
    int ram_bucket_percent = atoi(NEXT_ARG(argv, argc));
    int ram_buckets = buckets * ram_bucket_percent / 100;

    HashMap counter;
    hash_map_init(&counter, &ta, buckets, ram_buckets);

    char *text = read_file("data/sample.txt");
    Timer timer;
    timer_start(&timer);
    u64 bytes_in = count_words(&counter, text);
    u64 elapsed = timer_elapsed(&timer);

    double throughput = (double) bytes_in / (double) elapsed;
    printf(
        "%d %f %f\n", ram_bucket_percent, throughput * NS_PER_SEC / B_PER_MIB,
        (double) hash_map_mem_usage(&counter) / B_PER_MIB
    );

    FILE *file = fopen("data/debug.txt", "w");
    ASSERT(file);
    hash_map_debug(&counter, file);
    ASSERT(fclose(file) == 0);

    free(text);
    hash_map_deinit(&counter);
    ta_deinit(&ta);
}

void memory(int argc, char **argv) {
    TieredAllocator ta;
    ta_init(&ta, 256, 1 << 16);

    Timer timer;

    int iters = atoi(NEXT_ARG(argv, argc));

    for (int t = 0; t < NUM_TIERS; ++t) {
        Ptr *ptrs = malloc(iters * sizeof(*ptrs));
        for (int i = 0; i < iters; ++i)
            ptrs[i] = ta_create(&ta, t);

        printf("%s READ |", TIER_STRS[t]);
        for (int i = 0; i < iters; ++i) {
            timer_start(&timer);
            ta_acquire(&ta, ptrs[i]);
            u64 elapsed = timer_elapsed(&timer);
            printf(" %lu ", elapsed);
        }
        printf("\n");

        printf("%s WRITE |", TIER_STRS[t]);
        for (int i = 0; i < iters; ++i) {
            timer_start(&timer);
            ta_flush(&ta, ptrs[i]);
            u64 elapsed = timer_elapsed(&timer);
            printf(" %lu", elapsed);
        }
        printf("\n");

        for (int i = 0; i < iters; ++i)
            ta_destroy(&ta, ptrs[i]);
        free(ptrs);
    }

    ta_deinit(&ta);
}

int main(int argc, char **argv) {
    NEXT_ARG(argv, argc);
    char *cmd = NEXT_ARG(argv, argc);

    if (strcmp(cmd, "placement") == 0)
        placement(argc, argv);
    else if (strcmp(cmd, "memory") == 0)
        memory(argc, argv);
}
