//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_BACKEND_FACADE_HPP
#define DEDUP_SERVER_BACKEND_FACADE_HPP

#include <memory>
#include <mutex>
#include <unordered_map>
#include <fstream>

#include <boost/compute/detail/lru_cache.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "backend/container.hpp"
#include "backend/db_wrapper.hpp"
#include "backend/name_dispenser.hpp"
#include "def/exception.hpp"
#include "def/span.hpp"
#include "def/struct.hpp"
#include "def/util.hpp"

namespace dedup {
class BackendFacade {
private:
    /**
     * @brief Cache for the unfinished recipe file.
     * The key is the index key for the recipe file.
     * The value is a pair of the cache buffer and the total number of shares for this recipe, which also indicates the buffer size
     */
    std::unordered_map<key_t, std::pair<std::unique_ptr<std::byte[]>, std::size_t>> unfinishedRecipeFileCache_;
    std::mutex unfinishedRecipeFileCacheMtx_{};

    NameDispenser containerNameDispenser_{};
    MutableContainer shareContainer_{};
    internal_file_name_t shareContainerName_{};
    std::size_t shareContainerOffset_{};
    std::mutex shareContainerMtx_{};

    using caontainer_cache_t = boost::compute::detail::lru_cache<internal_file_name_t, std::shared_ptr<Container>>;
    caontainer_cache_t readContainerCache_{config::CONTAINER_CACHE_SIZE};
    std::mutex readContainerCacheMtx_{};

    using recipe_cache_t =
        boost::compute::detail::lru_cache<key_t, std::pair<std::shared_ptr<std::byte[]>, std::size_t>>;
    recipe_cache_t recipeCache_{config::RECIPE_CACHE_SIZE};
    std::mutex recipeCacheMtx_{};

    void createShareContainer_() {
        shareContainerName_ = containerNameDispenser_.get();
        shareContainer_.create(config::GetContianerDir(), shareContainerName_);
        shareContainerOffset_ = 0;
    }

    std::string formatRecipeFileName(const key_t &key){
        auto recipeName = ToHexDump(key);
        auto recipeFileName = config::GetContianerDir() + recipeName + ".rf";
        return recipeFileName;
    }

public:
    BackendFacade() {
        createShareContainer_();
    }

    enum class IndexPrefix : uint8_t {
        RECIPE = 0,
        SHARE_INDEX = 1,
    };

    /**
     * transform a fingerprint to an index key
     * @param FP the fingerprint to be transformed
     * @param key - <u>return</u> the resulting index key
     */
    static key_t ToIndexKey(IndexPrefix pre, const fingerprint_t &inodeFP) {
        key_t key;
        key[0] = static_cast<std::byte>(pre);
        std::copy(inodeFP.cbegin(), inodeFP.cend(), key.begin() + 1);
        return key;
    }

    /**
     * @brief create/update a recipe file data
     * @param userID user id of the recipe file
     * @param key index key for this recipe file
     * @param fileShareMetaHead share share meta head
     * @param totalNumOfShares total number of shares for this recipe file
     * @return writable span for file recipe entries
     * @throw DedupException if the argument is invalid
     */
    span<fileRecipeEntry_t> putRecipeFile(const user_id_t &userID, const key_t &key,
                                          const fileShareMetaHead_t &fileShareMetaHead,
                                          const std::size_t &totalNumOfShares) {
        std::lock_guard<decltype(unfinishedRecipeFileCacheMtx_)> lockGuard{unfinishedRecipeFileCacheMtx_};
        if (fileShareMetaHead.numOfPastSecrets == 0) { // this is a new file
            // allocate a buffer for the recipe file
            const std::size_t kRecipFileBufferSize = FILE_RECIPE_HEAD_SIZE + FILE_RECIPE_ENTRY_SIZE * totalNumOfShares;
            /// buffer for the recipe file in the cache
            auto &recipeFileBuffer =
                unfinishedRecipeFileCache_
                    .emplace(key, std::make_pair(std::make_unique<std::byte[]>(kRecipFileBufferSize), totalNumOfShares))
                    .first->second.first;

            // set the file recipe head
            auto &fileRecipeHead = *reinterpret_cast<fileRecipeHead_t *>(recipeFileBuffer.get());
            fileRecipeHead.userID = userID;
            fileRecipeHead.fileSize = fileShareMetaHead.fileSize;
            fileRecipeHead.numOfShares = 0;

            // return the buffer span for writing recipe file entries
            return {reinterpret_cast<fileRecipeEntry_t *>(recipeFileBuffer.get() + FILE_RECIPE_HEAD_SIZE),
                    boost::numeric_cast<std::size_t>(fileShareMetaHead.numOfComingSecrets)};
        } else { // this is a remains of a previous file
            auto findIter = unfinishedRecipeFileCache_.find(key);
            if (findIter == unfinishedRecipeFileCache_.end()) {
                throw DedupException(BOOST_CURRENT_LOCATION, "fail to find the recipe file in cache",
                                     {
                                         {"user id", std::to_string(userID)},
                                         {"key",     ToHexDump(key)        }
                });
            }
            auto &recipeFileBuffer = findIter->second.first;
            if constexpr (config::PARANOID_CHECK) {
                auto &fileRecipeHead = *reinterpret_cast<const fileRecipeHead_t *>(recipeFileBuffer.get());
                if (fileRecipeHead.userID != userID || findIter->second.second != totalNumOfShares) {
                    throw DedupException(BOOST_CURRENT_LOCATION, "the file recipe head is invalid");
                }
            }

            return {reinterpret_cast<fileRecipeEntry_t *>(recipeFileBuffer.get() + FILE_RECIPE_HEAD_SIZE +
                                                          FILE_RECIPE_ENTRY_SIZE * fileShareMetaHead.numOfPastSecrets),
                    boost::numeric_cast<std::size_t>(fileShareMetaHead.numOfComingSecrets)};
        }
    }

    /**
     * @brief indicate the backend that all the recipe entries for a file share are set,
     *  and the recipe file data can be written to the disk if possible
     * @param userID user id
     * @param fileShareMetaHead head of the file share
     * @param key key for this recipe file
     */
    void finishRecipeFile(const user_id_t &userID, const fileShareMetaHead_t &fileShareMetaHead, const key_t &key) {
        std::lock_guard<decltype(unfinishedRecipeFileCacheMtx_)> lockGuard{unfinishedRecipeFileCacheMtx_};
        // update the recipe file head
        auto findIter = unfinishedRecipeFileCache_.find(key);
        if (findIter == unfinishedRecipeFileCache_.end()) {
            throw DedupException(BOOST_CURRENT_LOCATION, "fail to find the recipe file in cache");
        }
        auto &recipeFileBuffer = findIter->second.first;
        auto &fileRecipeHead = *reinterpret_cast<fileRecipeHead_t *>(recipeFileBuffer.get());
        fileRecipeHead.numOfShares += fileShareMetaHead.numOfComingSecrets;
        // log secret size
        Benchmark::LogSecretSize(fileShareMetaHead.sizeOfComingSecrets);

        // if all the entries of this recipe file is set, write it to the disk
        if (findIter->second.second == fileRecipeHead.numOfShares) {
            auto recipeFileName = formatRecipeFileName(key);
            const auto kRecipeSize = boost::numeric_cast<std::size_t>(
                    FILE_RECIPE_HEAD_SIZE + FILE_RECIPE_ENTRY_SIZE * fileRecipeHead.numOfShares);
            std::ofstream{recipeFileName, std::ios::binary | std::ios::trunc}.write(
                    reinterpret_cast<const char *>(recipeFileBuffer.get()), kRecipeSize);
            {
                std::lock_guard<decltype(recipeCacheMtx_)> recipeLockGuard{recipeCacheMtx_};
                recipeCache_.insert(
                    key, std::make_pair(std::shared_ptr<std::byte[]>(std::move(recipeFileBuffer)), kRecipeSize));
            }
            unfinishedRecipeFileCache_.erase(findIter);
            // log recipe size
            Benchmark::LogRecipe(kRecipeSize);
        }
        // do db write after this recipe file was finished
        DataBase::BatchFlush();
    }

    /**
     * @brief create a new share index
     * @param key key for the index
     * @param value index value, which has only one user reference entry
     */
    void putShareIndex(const key_t &key,
                       const std::array<std::byte, SHARE_INDEX_HEAD_SIZE + SHARE_USER_REF_ENTRY_SIZE> &value) {
        DataBase::Put(key, {value.data(), value.size()});
    }

    /**
   * @brief append a share reference entry for the user to the end of the value,
   * and update the share index
   * @param userID user id
   * @param key key for this share index
   * @param value <u> modify </u> share index value for the key, and a share
   * reference entry for the user will be appended
   */
    void updateShareIndex(const user_id_t &userID, const key_t &key, std::string &value) {
        // make sure the buffer can hold the coming one reference entry
        value.resize(value.size() + SHARE_USER_REF_ENTRY_SIZE);

        // getShareIndex the head and the appending entry
        auto &shareIndexHead = *reinterpret_cast<shareIndexHead_t *>(value.data());
        auto &userRefEntry = *reinterpret_cast<shareUserRefEntry_t *>(
            value.data() + SHARE_INDEX_HEAD_SIZE + shareIndexHead.numOfUsers * SHARE_USER_REF_ENTRY_SIZE);

        // set the last user reference entry, which is the appended one after buffer resize
        userRefEntry.userID = userID;

        // update the share index value head
        shareIndexHead.numOfUsers++;

        // write the update to db_
        DataBase::Put(key, {reinterpret_cast<const std::byte *>(value.data()), value.size()});
    }

    /**
     * @brief put the data of a share
     * @param shareData data of the share
     * @return container name and data offset
     */
    std::pair<internal_file_name_t, std::size_t> putShareData(bytes_view shareData) {
        std::lock_guard<decltype(shareContainerMtx_)> lockGuard{shareContainerMtx_};
        benchmark::ScopedLap lap{Benchmark::DiskWriteTimer()};
        if (shareContainerOffset_ + shareData.size() > shareContainer_.size()) {
            // this container file is full, open a new container
            createShareContainer_();
        }
        std::copy(shareData.cbegin(), shareData.cend(), shareContainer_.region().begin() + shareContainerOffset_);
        auto off = shareContainerOffset_;
        shareContainerOffset_ += shareData.size();
        return {shareContainerName_, off};
    };

    /**
      * @brief getShareIndex the data corresponding to the given key, and the data type depends on the prefix of the key
      * @param key key for the data
      * @return option for the value corresponding to this key if found, and nullopt if not found
      * @throw DedupException if an error occurs on db_
      */
    std::optional<std::string> getShareIndex(const key_t &key) {
        return DataBase::Get(key);
    }

    std::optional<std::string> getRecipeData(const key_t &key) {
        // try to find in the recipe cache
        {
            std::lock_guard<decltype(recipeCacheMtx_)> lockGuard{recipeCacheMtx_};
            auto recipeOpt = recipeCache_.get(key);
            if (recipeOpt) {
                auto &recipe = recipeOpt.value().first;
                auto size = recipeOpt.value().second;
                return std::optional<std::string>{
                        std::in_place, std::string{reinterpret_cast<const char *>(recipe.get()), size}
                };
            }
        }
        std::ifstream recipeFile{formatRecipeFileName(key)};
        if (not recipeFile.is_open()){
            throw DedupException(BOOST_CURRENT_LOCATION, "fail to open recipe file",
                                 {{"recipe key", ToHexDump(key)}});
        }
        std::string recipeData{std::istreambuf_iterator<char>{recipeFile}, std::istreambuf_iterator<char>{}};
        return {recipeData};
    }

    /**
     * @brief getShareIndex the share data from container
     * @param containerName container name
     * @param off container offset for the share data
     * @param shareData a span to write the share data
     */
    void getShareData(const internal_file_name_t &containerName, std::size_t off, mutable_bytes_view shareData) {
        std::lock_guard<decltype(readContainerCacheMtx_)> lockGuard{readContainerCacheMtx_};
        auto containerOpt = readContainerCache_.get(containerName);
        if (containerOpt) {
            auto &container = *(containerOpt.value());
            if constexpr (config::PARANOID_CHECK) {
                if (off + shareData.size() > container.size()) {
                    throw DedupException(BOOST_CURRENT_LOCATION, "share data size is invalid");
                }
            }
            std::copy_n(container.region().cbegin() + off, shareData.size(), shareData.begin());
        } else {
            auto containerPtr = std::make_shared<Container>(containerName);
            auto &container = *containerPtr;
            if constexpr (config::PARANOID_CHECK) {
                if (off + shareData.size() > container.size()) {
                    throw DedupException(BOOST_CURRENT_LOCATION, "share data size is invalid");
                }
            }
            std::copy_n(container.region().cbegin() + off, shareData.size(), shareData.begin());
            readContainerCache_.insert(containerName, containerPtr);
        }
    }
};
} // namespace dedup

#endif // DEDUP_SERVER_BACKEND_FACADE_HPP
