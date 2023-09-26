//
// Created by 86182 on 2022/7/15.
//

#include <cstdint>
#include <cstdlib>
#include "iostream"

/*
 * rabin calculate the hash value of the chunk using the rabin algorithm
 *
 * @param buffer - a buffer to be calculated
 * @param bufferSize - the size of the buffer
 * @return maxWinFp - the rabin hash value
 */
int rabin(unsigned char *buffer, int chunkSize){
//    std::cout<<"rabin buffer"<<std::endl;
//    for(int i=0;i<chunkSize;i++){
//        std::cout<<(int)buffer[i]<<" ";
//        if(i!=0&&i%10==0)
//            std::cout<<std::endl;
//    }

    int winFp = 0, i, slidingWinSize_ = 48;
    int maxWinFp = 0;
    if(chunkSize<48){
        slidingWinSize_=chunkSize;
    }
    /*initialize the base and modulus for calculating the fingerprint of a window*/
    /*these two values were employed in open-vcdiff: "http://code.google.com/p/open-vcdiff/"*/
    uint32_t polyBase_ = 257; /*a prime larger than 255, the max value of "unsigned char"*/
    uint32_t polyMOD_ = (1 << 23); /*polyMOD_ - 1 = 0x7fffff: use the last 23 bits of a polynomial as its hash*/

    /*initialize the lookup table for accelerating the power calculation in rolling hash*/
    uint32_t *powerLUT_ = (uint32_t *) malloc(sizeof(uint32_t) * slidingWinSize_);
    /*powerLUT_[i] = power(polyBase_, i) mod polyMOD_*/
    powerLUT_[0] = 1;
    for (i = 1; i < slidingWinSize_; i++) {
        /*powerLUT_[i] = (powerLUT_[i-1] * polyBase_) mod polyMOD_*/
        powerLUT_[i] = (powerLUT_[i-1] * polyBase_) & (polyMOD_ - 1);
    }

    /*initialize the lookup table for accelerating the byte remove in rolling hash*/
    uint32_t *removeLUT_ = (uint32_t *) malloc(sizeof(uint32_t) * 256); /*256 for unsigned char*/
    for (i = 0; i < 256; i++) {
        /*removeLUT_[i] = (- i * powerLUT_[slidingWinSize_-1]) mod polyMOD_*/
        removeLUT_[i] = (i * powerLUT_[slidingWinSize_-1]) & (polyMOD_ - 1);
        if (removeLUT_[i] != 0) removeLUT_[i] = polyMOD_ - removeLUT_[i];
        /*note: % is a remainder (rather than modulus) operator*/
        /*      if a < 0,  -polyMOD_ < a % polyMOD_ <= 0       */
    }

    /*initialize the mask for depolytermining an anchor*/
    /*note: power(2, numOfMaskBits) = avgChunkSize_*/
    int numOfMaskBits = 1;
    int avgChunkSize_ = 8<<10, minChunkSize = 2<<10, maxChunkSize = 8<<10; //??? the size of chunk is 8KB
//    while ((avgChunkSize_ >> numOfMaskBits) != 1) numOfMaskBits++;
//    uint32_t anchorMask_ = (1 << numOfMaskBits) - 1;

    /*initialize the value for depolytermining an anchor*/
    uint32_t anchorValue_ = 0;

    //上面代码都是为了简化运算

//    int *numOfChunks;
//    (*numOfChunks) = 0;
    int chunkEndIndex = -1 + slidingWinSize_;
    int chunkEndIndexLimit = -1 + chunkSize;  //即为块大小

//    if (chunkEndIndexLimit >= bufferSize) chunkEndIndexLimit = bufferSize - 1;

    /*calculate the fingerprint of the first window*/
    for (i = 0; i < slidingWinSize_; i++) {
        /*winFp = winFp + ((buffer[chunkEndIndex-i] * powerLUT_[i]) mod polyMOD_)*/
        winFp = winFp + ((buffer[chunkEndIndex-i] * powerLUT_[i]) & (polyMOD_ - 1));
    }
    /*winFp = winFp mod polyMOD_*/
    winFp = winFp & (polyMOD_ - 1);
    maxWinFp = (winFp > maxWinFp) ? winFp : maxWinFp;

    while (/*((winFp & anchorMask_) != anchorValue_) && */(chunkEndIndex < chunkEndIndexLimit)) {
        /*move the window forward by 1 byte*/
        chunkEndIndex++;

        /*update the fingerprint based on rolling hash*/
        /*winFp = ((winFp + removeLUT_[buffer[chunkEndIndex-slidingWinSize_]]) * polyBase_ + buffer[chunkEndIndex]) mod polyMOD_*/
        winFp = ((winFp + removeLUT_[buffer[chunkEndIndex-slidingWinSize_]]) * polyBase_ + buffer[chunkEndIndex]) & (polyMOD_ - 1);
        maxWinFp = (winFp > maxWinFp) ? winFp : maxWinFp;
    }

    /*record the end index of a chunk*/
//    chunkEndIndexList[(*numOfChunks)] = chunkEndIndex;

    /*go on for the next chunk*/
//    chunkEndIndex = chunkEndIndexList[(*numOfChunks)] + minChunkSize_;
//    chunkEndIndexLimit = chunkEndIndexList[(*numOfChunks)] + maxChunkSize_;
//    (*numOfChunks)++;
//    printf("maxWinFp : %d\n",maxWinFp);
    return maxWinFp;
}