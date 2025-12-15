#include "common.h"

u64 align_u64(u64 x) {
    return ((x >> 3) << 3) + (((x & 0b111) != 0) << 3);
}
