#ifndef DEDUP_SERVER_CRYPTO_PRIMITIVE_HPP
#define DEDUP_SERVER_CRYPTO_PRIMITIVE_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/*for the use of OpenSSL*/
#include <openssl/crypto.h>
#include <openssl/evp.h>

#define OPENSSL_THREAD_DEFINES

#include <openssl/opensslconf.h>
/*macro for OpenSSL debug*/
#define OPENSSL_DEBUG 0
/*for the use of mutex lock*/
#include <cstddef>
#include <pthread.h>

namespace crypto_primitive {
/*macro for the type of a high secure pair of hash generation and encryption*/
inline constexpr int HIGH_SEC_PAIR_TYPE{0};
/*macro for the type of a low secure pair of hash generation and encryption*/
inline constexpr int LOW_SEC_PAIR_TYPE{1};
/*macro for the type of a SHA-256 hash generation*/
inline constexpr int SHA256_TYPE{2};
/*macro for the type of a SHA-1 hash generation*/
inline constexpr int SHA1_TYPE{3};

typedef struct {
    pthread_mutex_t *lockList;
    long *cntList;
} opensslLock_t;

class CryptoPrimitive {
private:
    /*the type of CryptoPrimitive*/
    int cryptoType_;

    /*variables used in hash calculation*/
    EVP_MD_CTX mdctx_;
    const EVP_MD *md_;
    /*the size of the generated hash*/
    int hashSize_;

    /*variables used in encryption*/
    EVP_CIPHER_CTX cipherctx_;
    const EVP_CIPHER *cipher_;
    unsigned char *iv_;

    /*the size of the key for encryption*/
    int keySize_;
    /*the size of the encryption block unit*/
    int blockSize_;

    /*OpenSSL lock*/
    inline static opensslLock_t *opensslLock_;

    /*
     * OpenSSL locking callback function
     */
    static void opensslLockingCallback_(int mode, int type, const char *file, int line) {
        if (mode & CRYPTO_LOCK) {
            pthread_mutex_lock(&(opensslLock_->lockList[type]));
            CryptoPrimitive::opensslLock_->cntList[type]++;
        } else {
            pthread_mutex_unlock(&(opensslLock_->lockList[type]));
        }
    }

    /**
     * getShareIndex the id of the current thread
     *
     * @param id - the thread id <return>
     */
    static void opensslThreadID_(CRYPTO_THREADID *id) {
        CRYPTO_THREADID_set_numeric(id, pthread_self());
    }

public:
    /**
     * constructor of CryptoPrimitive
     *
     * @param cryptoType - the type of CryptoPrimitive
     */
    explicit CryptoPrimitive(int cryptoType = SHA256_TYPE) : cryptoType_(cryptoType) {
#if defined(OPENSSL_THREADS)
        /*check if OpensslLockSetup() has been called to set up OpenSSL locks*/
        if (opensslLock_ == nullptr) {
            fprintf(stderr, "Error: OpensslLockSetup() was not called before initializing CryptoPrimitive instances\n");
            exit(1);
        }

        if (cryptoType_ == HIGH_SEC_PAIR_TYPE) {
            /*allocate, initialize and return the digest context mdctx_*/
            EVP_MD_CTX_init(&mdctx_);

            /*getShareIndex the EVP_MD structure for SHA-256*/
            md_ = EVP_sha256();
            hashSize_ = 32;

            /*initializes cipher contex cipherctx_*/
            EVP_CIPHER_CTX_init(&cipherctx_);

            /*getShareIndex the EVP_CIPHER structure for AES-256*/
            cipher_ = EVP_aes_256_cbc();
            keySize_ = 32;
            blockSize_ = 16;

            /*allocate a constant IV*/
            iv_ = (unsigned char *)malloc(sizeof(unsigned char) * blockSize_);
            memset(iv_, 0, blockSize_);
        }

        if (cryptoType_ == LOW_SEC_PAIR_TYPE) {
            /*allocate, initialize and return the digest context mdctx_*/
            EVP_MD_CTX_init(&mdctx_);

            /*getShareIndex the EVP_MD structure for MD5*/
            md_ = EVP_md5();
            hashSize_ = 16;

            /*initializes cipher contex cipherctx_*/
            EVP_CIPHER_CTX_init(&cipherctx_);

            /*getShareIndex the EVP_CIPHER structure for AES-128*/
            cipher_ = EVP_aes_128_cbc();
            keySize_ = 16;
            blockSize_ = 16;

            /*allocate a constant IV*/
            iv_ = (unsigned char *)malloc(sizeof(unsigned char) * blockSize_);
            memset(iv_, 0, blockSize_);
        }

        if (cryptoType_ == SHA256_TYPE) {
            /*allocate, initialize and return the digest context mdctx_*/
            EVP_MD_CTX_init(&mdctx_);

            /*getShareIndex the EVP_MD structure for SHA-256*/
            md_ = EVP_sha256();
            hashSize_ = 32;

            keySize_ = -1;
            blockSize_ = -1;
        }

        if (cryptoType_ == SHA1_TYPE) {
            /*allocate, initialize and return the digest context mdctx_*/
            EVP_MD_CTX_init(&mdctx_);

            /*getShareIndex the EVP_MD structure for SHA-1*/
            md_ = EVP_sha1();
            hashSize_ = 20;

            keySize_ = -1;
            blockSize_ = -1;
        }

#else
        fprintf(stderr, "Error: OpenSSL was not configured with thread support!\n");
        exit(1);
#endif
    }
    /**
     * destructor of CryptoPrimitive
     */
    ~CryptoPrimitive() {
        if ((cryptoType_ == HIGH_SEC_PAIR_TYPE) || (cryptoType_ == LOW_SEC_PAIR_TYPE)) {
            /*clean up the digest context mdctx_ and free up the space allocated to it*/
            EVP_MD_CTX_cleanup(&mdctx_);

            /*clean up the cipher context cipherctx_ and free up the space allocated to it*/
            EVP_CIPHER_CTX_cleanup(&cipherctx_);
            free(iv_);
        }

        if ((cryptoType_ == SHA256_TYPE) || (cryptoType_ == SHA1_TYPE)) {
            /*clean up the digest context mdctx_ and free up the space allocated to it*/
            EVP_MD_CTX_cleanup(&mdctx_);
        }
    }

    /**
     * set up OpenSSL locks
     *
     * @return - a boolean value that indicates if the setup succeeds
     */
    static bool OpensslLockSetup() {
#if defined(OPENSSL_THREADS)
        opensslLock_ = (opensslLock_t *)malloc(sizeof(opensslLock_t));

        opensslLock_->lockList = (pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
        opensslLock_->cntList = (long *)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(long));

        for (int i = 0; i < CRYPTO_num_locks(); i++) {
            pthread_mutex_init(&(opensslLock_->lockList[i]), nullptr);
            opensslLock_->cntList[i] = 0;
        }

        CRYPTO_THREADID_set_callback(&opensslThreadID_);
        CRYPTO_set_locking_callback(&opensslLockingCallback_);

        return true;
#else
        fprintf(stderr, "Error: OpenSSL was not configured with thread support!\n");

        return 0;
#endif
    }

    /**
     * clean up OpenSSL locks
     *
     * @return - a boolean value that indicates if the cleanup succeeds
     */
    static bool OpensslLockCleanup() {
#if defined(OPENSSL_THREADS)
        CRYPTO_set_locking_callback(nullptr);

        for (int i = 0; i < CRYPTO_num_locks(); i++) {
            pthread_mutex_destroy(&(opensslLock_->lockList[i]));
        }

        OPENSSL_free(opensslLock_->lockList);
        OPENSSL_free(opensslLock_->cntList);
        free(opensslLock_);

        return true;
#else
        fprintf(stderr, "Error: OpenSSL was not configured with thread support!\n");

        return 0;
#endif
    }

    /**
     * generate the hash for the data stored in a buffer
     *
     * @param dataBuffer - the buffer that stores the data
     * @param dataSize - the size of the data
     * @param hash - the generated hash <return>
     *
     * @return - true if the hash generation succeeds, otherwise false when the size of the generated hash does not match with the expected one
     */
    bool generateHash(const unsigned char *dataBuffer, const int &dataSize, unsigned char *hash) {
        int hashSize;

        EVP_DigestInit_ex(&mdctx_, md_, NULL);
        EVP_DigestUpdate(&mdctx_, dataBuffer, dataSize);
        EVP_DigestFinal_ex(&mdctx_, hash, (unsigned int *)&hashSize);

        if (hashSize != hashSize_) {
            return 0;
        }

        return 1;
    }
};
} // namespace crypto_primitive

#endif
