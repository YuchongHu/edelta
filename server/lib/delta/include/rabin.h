/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef RABIN_H_
#define RABIN_H_

#define MSB64 0x8000000000000000LL
#define MAXBUF (128*1024)

#define FINGERPRINT_PT  0xbfe6b8a5bf378d83LL
#define BREAKMARK_VALUE 0x78

struct superF {
    uint64_t sf1;
    uint64_t sf2;
    uint64_t sf3;
};

int rabin_chunk_data(unsigned char *p, int n);
void chunkAlg_init();
void windows_reset();
void super_feature(const unsigned char*, int, struct superF*);
void finesse_super_feature(const unsigned char*, int, struct superF*);

#endif
