#ifndef DELTA_TEST_DELTA_H
#define DELTA_TEST_DELTA_H

#include "xdelta3.h"

typedef uint8_t fingerprint[32];

struct delta {
    usize_t srcSize; /* the size of source chunk */
    usize_t deltaSize;       /* the size of delta */
    uint8_t *data;/* delta content */
};

struct delta *compute_delta(const uint8_t *base, size_t baseSize, const uint8_t *src, size_t srcSize);

int restore_delta(const uint8_t *base, size_t baseSize, const struct delta *deltaData, uint8_t *restoreData,
                  size_t *restoreSize);

#endif //DELTA_TEST_DELTA_H
