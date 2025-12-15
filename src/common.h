#ifndef COMMON_H_
#define COMMON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint8_t u8;
typedef int8_t i8;

#define ASSERT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(                                                           \
                stderr, __FILE__ ":%d: Assertion error, " #condition "\n",     \
                __LINE__                                                       \
            );                                                                 \
            abort();                                                           \
        }                                                                      \
    } while (0)

#endif // COMMON_H_
