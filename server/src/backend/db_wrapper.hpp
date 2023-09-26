//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_DB_WRAPPER_HPP
#define DEDUP_SERVER_DB_WRAPPER_HPP

#include <mutex>
#include <optional>

#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/filter_policy.h"
#include "leveldb/write_batch.h"

#include "def/config.hpp"
#include "def/exception.hpp"
#include "def/log.hpp"
#include "def/span.hpp"
#include "def/util.hpp"
#include "def/benchmark.hpp"

namespace dedup {
/**
 * @brief Singleton for backend database
 */
class DataBase {
private:
    /// pointer to the db instance
    inline static leveldb::DB *db_{nullptr};
    /// default read options for db
    inline static const leveldb::ReadOptions readOptions_{};
    /// default write options for db
    inline static const leveldb::WriteOptions writeOptions_{};
    /// write batch for accelerating db writing
    inline static leveldb::WriteBatch writeBatch_{};
    inline static std::recursive_mutex writeBatchMtx_{};
    inline static int32_t batchCnt_{0};
    /// default db options
    inline static leveldb::Options dbOptions_{};

    static leveldb::DB &DB_() {
        return *db_;
    }

public:
    static void Init() noexcept {
        static std::once_flag onceFlag{};
        std::call_once(onceFlag, []() {
            // open/create the key-value database
            dbOptions_.create_if_missing = true;
            dbOptions_.write_buffer_size = config::MEM_TABLE_SIZE;
            dbOptions_.block_cache = leveldb::NewLRUCache(config::BLOCK_CACHE_SIZE);
            dbOptions_.filter_policy = leveldb::NewBloomFilterPolicy(config::BLOOM_FILTER_KEY_BITS);
            auto openStat = leveldb::DB::Open(dbOptions_, config::GetDBDir(), &db_);
            if (!openStat.ok()) {
                std::cerr << log::ERROR << log::FormatLog(BOOST_CURRENT_LOCATION, "error on initializing db");
                exit(-1);
            }
        });
    }

    /**
   * @brief getShareIndex the value of the entry according to a key
   * @param key key for the entry
   * @return option for the value if the corresponding entry found, and nullopt
   * if not found
   * @throw DedupException if an error occurs on db_
   */
    [[nodiscard]] static std::optional<std::string> Get(const bytes_view &key) {
        // do db_ read
        std::string value{};
        auto status = DB_().Get(readOptions_, {reinterpret_cast<const char *>(key.data()), key.size()}, &value);
        // check result
        if (status.ok()) {
            return {std::move(value)};
        } else if (status.IsNotFound()) {
            return {};
        } else {
            throw DedupException(BOOST_CURRENT_LOCATION, "error on getting share index from db",
                                 {
                                     {"share fingerprint", ToHexDump(bytes_view{key.cbegin() + 1, key.cend()})},
                                     {"db status",         status.ToString()                                  }
            });
        }
    }

    static void BatchFlush() {
        if constexpr (config::BATCH_SIZE > 0) {
            std::lock_guard<decltype(writeBatchMtx_)> lockGuard{writeBatchMtx_};
            auto status = DB_().Write(writeOptions_, &writeBatch_);
            writeBatch_ = leveldb::WriteBatch{};
            batchCnt_ = 0;
            if (!status.ok()) {
                throw DedupException(BOOST_CURRENT_LOCATION, "error on putting share index to db",
                                     {
                                         {"db status", status.ToString()}
                });
            }
        }
    }

    /**
   * @brief put a key-value entry to the db_
   * @param key key for the entry
   * @param value value for the entry
   * @throw DedupException if an error occurs on db_
   */
    static void Put(const bytes_view &key, const bytes_view &value) {
        if constexpr (config::BATCH_SIZE > 0) {
            // do db write in batch manner
            std::lock_guard<decltype(writeBatchMtx_)> lockGuard{writeBatchMtx_};
            // time benchmark
            benchmark::ScopedLap lap{Benchmark::DiskWriteTimer()};
            
            writeBatch_.Put({reinterpret_cast<const char *>(key.data()), key.size()},
                            {reinterpret_cast<const char *>(value.data()), value.size()});
            if (++batchCnt_ > config::BATCH_SIZE) {
                BatchFlush();
            }
        } else {
            // do db_ write
            auto status = DataBase::DB_().Put(writeOptions_, {reinterpret_cast<const char *>(key.data()), key.size()},
                                              {reinterpret_cast<const char *>(value.data()), value.size()});
            if (!status.ok()) {
                throw DedupException(BOOST_CURRENT_LOCATION, "error on putting share index to db",
                                     {
                                         {"share fingerprint", ToHexDump(bytes_view{key.cbegin() + 1, key.cend()})},
                                         {"db status",         status.ToString()                                  }
                });
            }
        }
    }
    static void Put(const key_t &key, const bytes_view &value) {
        Put(bytes_view{key.data(), key.size()}, value);
    }
};
} // namespace dedup

#endif // DEDUP_SERVER_DB_WRAPPER_HPP
