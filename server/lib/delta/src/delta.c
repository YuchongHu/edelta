//
// Created by Lokyin Zhao on 2022/11/1.
//
#include "delta.h"

struct delta *compute_delta(const uint8_t *base, usize_t baseSize, const uint8_t *src, usize_t srcSize) {
    if (baseSize < 1024 || srcSize < 512) {
        return NULL;
    }
    
    uint8_t deltaData[128 * 1024];//[128*1024];
    usize_t dSize;
    
    int ret = xd3_encode_memory(src, srcSize, base, baseSize, deltaData, &dSize, srcSize, 0);
    if (ret != 0) {
        return NULL;
    }
    if (dSize > srcSize) {
        return NULL;
    }
    
    struct delta *deltaVal = (struct delta *) malloc(sizeof(struct delta));
    deltaVal->deltaSize = dSize;
    deltaVal->data = (unsigned char *) malloc(dSize);
    deltaVal->srcSize = srcSize;
    memcpy(deltaVal->data, deltaData, dSize);
    
    return deltaVal;
}

int restore_delta(const uint8_t *base, usize_t baseSize, const struct delta *deltaData, uint8_t *restoreData,
                  usize_t *restoreSize) {
    return xd3_decode_memory(deltaData->data,
                             deltaData->deltaSize,
                             base,
                             baseSize,
                             restoreData,
                             restoreSize,
                             deltaData->srcSize,
                             0);
}
