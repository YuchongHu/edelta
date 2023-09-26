//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_DEDUP_CORE_HPP
#define DEDUP_SERVER_DEDUP_CORE_HPP

#include "backend/backend_facade.hpp"
#include "dedup/client_interface.hpp"
#include "dedup/delta.hpp"
#include "dedup/peer_mediator.hpp"
#include "def/benchmark.hpp"
#include "def/exception.hpp"
#include "def/span.hpp"
#include "def/struct.hpp"
#include "def/util.hpp"

namespace dedup {
/**
 * @brief core for deduplication
 */
class DedupCore : public PeerInterface, public ClientInterface {
private:
    /// remote peer mediator
    PeerMediator peerMediator_;
    /// backend interfaces
    BackendFacade backend_;
    /// delta obj
    Delta delta_;

public:
    DedupCore() : peerMediator_(*this) {
    }

    /**
     * @brief perform the first stage deduplication
     * @param userID the user id
     * @param shareMeta the share metadata
     * @param dupStat <u> return </u> deduplication status
     */
    void firstStageDedup(const user_id_t &userID, const bytes_view &shareMeta, const span<bool> &dupStat) override {
        // time benchmark
        benchmark::ScopedLap lap{Benchmark::FirstStageTimer()};

        // getShareIndex the file share meta head and share meta entries
        auto [kFileShareMDHead, fullFileNameView, shareMetaEntries] = ParseFileShareMeta(shareMeta);

        // check each subsequent share
        if constexpr (config::LOOP_PARALLEL) { // perform each intra-user index updating in parallel
            throw DedupException(BOOST_CURRENT_LOCATION, "unimplemented");
        } else { // perform each intra-user index updating serially
            for (int i = 0; i < kFileShareMDHead.numOfComingSecrets; i++) {
                // getShareIndex the share meta entry
                auto shareMetaEntry = shareMetaEntries.cbegin() + i;

                // check the intra-user duplicate status
                dupStat[i] = peerMediator_.intraUserIndexUpdate(shareMetaEntry->shareFP, userID);
            }
        }
    }

    /**
     * @brief perform intra-user share index updating
     * @param shareFP share fingerprint
     * @param userID user id
     * @return the share index result indicating whether this user has previously uploaded the same share,
     * true if this user owns the share, otherwise false
     */
    bool intraUserIndexUpdate(const fingerprint_t &shareFP, const user_id_t &userID) override {
        // getShareIndex the share index value according to the fp
        auto valueOpt = backend_.getShareIndex(
                BackendFacade::ToIndexKey(BackendFacade::IndexPrefix::SHARE_INDEX, shareFP));

        // check status
        if (!valueOpt) { // not found
            return false;
        } else { // found the share index
            // getShareIndex the head and reference entries
            auto &value = *valueOpt;
            auto [kShareIndexValueHead, userRefEntries] =
                ParseShareIndex({reinterpret_cast<const std::byte *>(value.data()), value.size()});

            return std::any_of(userRefEntries.cbegin(), userRefEntries.cend(),
                               [&userID](const shareUserRefEntry_t &entry) { return entry.userID == userID; });
        }
    }

    /**
     * @brief perform the second stage deduplication
     * @param userID user id
     * @param shareMeta span for the share meta
     * @param shareData span for the share data
     * @param dupStat span for the deduplication status on the first stage dedup
     * @param totalNumOfShares total number of shares for this share file
     */
    void secondStageDedup(const user_id_t &userID, const bytes_view &shareMeta, const bytes_view &shareData,
                          const span<const bool> &dupStat, const std::size_t &totalNumOfShares) override {
        // time benchmark
        benchmark::ScopedLap lap{Benchmark::SecondStageTimer()};
        // getShareIndex hte file share meta head, full file name and its share meta entries for the share meta buffer
        auto [kFileShareMDHead, fullFileNameView, shareMetaEntries] = ParseFileShareMeta(shareMeta);

        // generate the formatted full file name
        /// encrypted full file name of this file
        auto fullFileName = FormatFullFileName(std::string{fullFileNameView});

        // generate the inode index recipeKey
        /// fingerprint for this inode index
        auto recipeFP = ToRecipeFP(fullFileName, userID);
        /// recipeKey for this inode index
        auto recipeKey = BackendFacade::ToIndexKey(BackendFacade::IndexPrefix::RECIPE, recipeFP);

        // getShareIndex the file recipe buffer according to user id and full file name
        auto recipeFileEntries = backend_.putRecipeFile(userID, recipeKey, kFileShareMDHead, totalNumOfShares);

        // store each file recipe entry in the recipe file buffer
        if constexpr (config::LOOP_PARALLEL) { // perform each inter-user updating in parallel
            throw DedupException(BOOST_CURRENT_LOCATION, "unimplemented");
        } else { // perform each inter-user updating serially
            /// offset of the share data buffer
            std::size_t shareDataBufferOffset{0};
            for (int i = 0; i < kFileShareMDHead.numOfComingSecrets; i++) {
                // getShareIndex this share metadata entry
                auto shareMetaEntry = shareMetaEntries.cbegin() + i;
                if (!dupStat[i]) { // the share is not a duplicate, further perform share store
                    // perform inter-user index update
                    peerMediator_.interUserIndexUpdate(
                        shareMetaEntry->shareFP, userID,
                        {shareData.data() + shareDataBufferOffset,
                         boost::numeric_cast<bytes_view::size_type>(shareMetaEntry->shareSize)});
                    shareDataBufferOffset += shareMetaEntry->shareSize;
                } else { // this is a duplicate share
                    // log a duplicate
                    Benchmark::LogDuplicateShare(shareMetaEntry->shareSize);
                }
                // set this file recipe entry
                auto fileRecipeEntry = recipeFileEntries.begin() + i;
                fileRecipeEntry->shareFP = shareMetaEntry->shareFP;
                fileRecipeEntry->secretID = shareMetaEntry->secretID;
                fileRecipeEntry->secretSize = shareMetaEntry->secretSize;
                fileRecipeEntry->shareSize = shareMetaEntry->shareSize;
            }
        }

        // inform the backend that this file share fragment is finished
        backend_.finishRecipeFile(userID, kFileShareMDHead, recipeKey);
    }

    /**
     * @brief perform inter-user share index updating
     * @param shareFP share fingerprint
     * @param userID user id
     * @param shareData span for the share data
     */
    void interUserIndexUpdate(const fingerprint_t &shareFP, const user_id_t &userID,
                              const bytes_view &shareData) override {
        // getShareIndex the share index value according to the fp
        auto shareIndexKey = BackendFacade::ToIndexKey(BackendFacade::IndexPrefix::SHARE_INDEX, shareFP);
        auto indexValue = backend_.getShareIndex(shareIndexKey);

        if (!indexValue) { // this share does not exist, save it
            // super feature time benchmark
            benchmark::UniqueLap superFeatureLap{Benchmark::SuperFeatureTimer()};
            /// size for a new share index value
            static constexpr auto VALUE_SIZE = SHARE_INDEX_HEAD_SIZE + SHARE_USER_REF_ENTRY_SIZE;
            // check whether this share can be compressed by delta
            auto featuers = Delta::GenSuperFeature(shareData);
            auto baseFPOpt = delta_.superFeatureIndex(featuers);
            superFeatureLap.stop();
            if (baseFPOpt.has_value()) { // try to compress this share by delta
                auto &baseFP = baseFPOpt.value();
                auto baseIndexKey = BackendFacade::ToIndexKey(BackendFacade::IndexPrefix::SHARE_INDEX, baseFP);
                auto baseIndexValueOpt = backend_.getShareIndex(baseIndexKey);
                if (baseIndexValueOpt) {
                    auto &baseIndexValue = baseIndexValueOpt.value();
                    auto [kBaseShareIndexHead, baseShareUserRefEntries] = ParseShareIndex(
                        {reinterpret_cast<const std::byte *>(baseIndexValue.data()), baseIndexValue.size()});
                    if (kBaseShareIndexHead.deltaDepth < config::MAX_DELTA_DEPTH) { // this share can be compressed
                        std::vector<std::byte> base(kBaseShareIndexHead.shareSize);
                        if (kBaseShareIndexHead.deltaDepth == 0) { // this base is a regular share
                            backend_.getShareData(kBaseShareIndexHead.containerName, kBaseShareIndexHead.offset, base);
                        } else { // this is a delta compressed base share
                            benchmark::ScopedLap lap{Benchmark::RestoreFromDeltaTimer()};
                            restoreDeltaShare(kBaseShareIndexHead, base);
                        }
                        // compute the delta
                        superFeatureLap.start();
                        auto delta = Delta::ComputeDelta(
                            {reinterpret_cast<const std::byte *>(base.data()), base.size()}, shareData);
                        superFeatureLap.stop();
                        if (!delta.empty()) { // this share can be compressed by delta
                            // allocate a new share index buffer
                            std::array<std::byte, VALUE_SIZE> value; // NOLINT(cppcoreguidelines-pro-type-member-init)
                            auto [shareIndexHead, shareUserRefEntry] = ParseNewShareIndex(value);
                            // write the share data (delta from base)
                            std::tie(shareIndexHead.containerName, shareIndexHead.offset) =
                                backend_.putShareData(delta);
                            // set the share index value head
                            shareIndexHead.shareSize =
                                boost::numeric_cast<decltype(shareIndexHead.shareSize)>(shareData.size());
                            shareIndexHead.numOfUsers = 1;
                            shareIndexHead.deltaDepth = kBaseShareIndexHead.deltaDepth + 1;
                            shareIndexHead.baseFP = baseFP;
                            shareIndexHead.deltaSize = delta.size();
                            // set the share index user reference entry
                            shareUserRefEntry.userID = userID;
                            // write the value into share index
                            backend_.putShareIndex(shareIndexKey, value);
                            // update the feature index
                            delta_.superFeatureIndexUpdate(featuers, shareFP);
                            // log a delta compressed share
                            Benchmark::LogDeltaCompressed(shareData.size(), delta.size());
                            return;
                        }
                    }
                }
            }

            // this is a unique share which cannot be compressed by delta
            // allocate the share index value
            std::array<std::byte, VALUE_SIZE> value; // NOLINT(cppcoreguidelines-pro-type-member-init)
            // getShareIndex the head and user reference entry
            auto [shareIndexHead, shareUserRefEntry] = ParseNewShareIndex(value);
            // write the share data (delta from base)
            std::tie(shareIndexHead.containerName, shareIndexHead.offset) = backend_.putShareData(shareData);
            // set the share index value head
            shareIndexHead.shareSize = boost::numeric_cast<decltype(shareIndexHead.shareSize)>(shareData.size());
            shareIndexHead.numOfUsers = 1;
            shareIndexHead.deltaDepth = 0;
            shareIndexHead.baseFP = {};
            shareIndexHead.deltaSize = 0;
            // set the share index user reference entry
            shareUserRefEntry.userID = userID;
            // write the value into share index
            backend_.putShareIndex(shareIndexKey, value);
            // update the feature index
            delta_.superFeatureIndexUpdate(featuers, shareFP);
            // log a unique share
            Benchmark::LogUniqueShare(shareData.size());
        } else { // this share already exists, just update the user count
            // getShareIndex the value head
            auto &value = *indexValue;
            auto [kShareIndexHead, shareUserRefEntries] =
                ParseShareIndex({reinterpret_cast<const std::byte *>(value.data()), value.size()});

            /*
             * check if the user owns the share corresponding to the shareIndexKey,
             * and if not, then append a user reference entry.
             * note:
             * we here still check if the user owns the share, in consideration of the
             * special case where the received package of shares contains repeated
             * shares. The underlying reason is that in the first stage of deduplication,
             * we do not check if there is any repeated share contained in the coming
             * package of shares
             */
            if (std::none_of(shareUserRefEntries.cbegin(), shareUserRefEntries.cend(),
                             [&userID](const shareUserRefEntry_t &entry) { return entry.userID == userID; })) {
                backend_.updateShareIndex(userID, shareIndexKey, value);
            }

            // log a duplicate share
            Benchmark::LogDuplicateShare(shareData.size());
        }
    }

    /**
     * @brief perform restoring a share file
     * @param userID user id
     * @param fullFileName full file name for this share file
     * @param shareFileData a modifiable span for the share file data buffer
     * @param flushCallBack a callable object for flushing the shareFileData when the buffer is full
     */
    void restoreShareFile(const user_id_t &userID, const std::string &fullFileName,
                          const mutable_bytes_view &shareFileData,
                          const std::function<void(std::size_t)> &flushCallBack) override {
        // time benchmark
        benchmark::UniqueLap lap{Benchmark::RestoreTimer()};
        benchmark::UniqueLap restoreRecipeLap{Benchmark::RestoreRecipeTimer()};

        // format the full file name
        /// (formatted) full file name
        auto formattedFullFileName = FormatFullFileName(fullFileName);

        // generate the recipe file fingerprint and key, and getShareIndex the recipe index
        /// fingerprint for this recipe file
        auto recipeFP = ToRecipeFP(formattedFullFileName, userID);
        /// key for this recipe file
        auto recipeKey = BackendFacade::ToIndexKey(BackendFacade::IndexPrefix::RECIPE, recipeFP);
        auto recipeValueOpt = backend_.getRecipeData(recipeKey);

        if (recipeValueOpt) { // if such a recipe for full file name exists
            /// span for the file recipe
            auto &recipeValue = *recipeValueOpt;

            // read the file recipe head and entries
            /// head of the file recipe (constant)
            auto [kFileRecipeHead, kFileRecipeEntries] =
                ParseFileRecipe({reinterpret_cast<const std::byte *>(recipeValue.data()), recipeValue.size()});
            restoreRecipeLap.stop();

            // set the share file head in the share file data buffer
            /// offset of the share file buffer
            std::size_t shareFileBufferOffset{0};
            /// share file head
            auto &shareFileHead = *reinterpret_cast<shareFileHead_t *>(shareFileData.data() + shareFileBufferOffset);
            shareFileBufferOffset += SHARE_FILE_HEAD_SIZE;
            shareFileHead.fileSize = kFileRecipeHead.fileSize;
            shareFileHead.numOfShares = kFileRecipeHead.numOfShares;

            // 6. restore each share
            if constexpr (config::LOOP_PARALLEL) { // perform each share restoring in parallel
                throw DedupException(BOOST_CURRENT_LOCATION, "unimplemented");
            } else { // perform each share restoring serially
                for (const auto &kFileRecipeEntry : kFileRecipeEntries) {
                    // if the share file buffer cannot contain the coming data, flush the buffer
                    /// size of the file share(share entry and share data)
                    const std::size_t kFileShareSize = SHARE_ENTRY_SIZE + kFileRecipeEntry.shareSize;
                    if (shareFileBufferOffset + kFileShareSize >= shareFileData.size()) {
                        lap.stop();
                        flushCallBack(shareFileBufferOffset);
                        shareFileBufferOffset = 0;
                        lap.start();
                    }

                    // set the share entry
                    auto &shareEntry = *reinterpret_cast<shareEntry_t *>(shareFileData.data() + shareFileBufferOffset);
                    shareEntry.secretID = kFileRecipeEntry.secretID;
                    shareEntry.secretSize = kFileRecipeEntry.secretSize;
                    shareEntry.shareSize = kFileRecipeEntry.shareSize;
                    shareFileBufferOffset += SHARE_ENTRY_SIZE;

                    // perform share restore
                    peerMediator_.restoreShare(kFileRecipeEntry.shareFP,
                                               {shareFileData.data() + shareFileBufferOffset,
                                                boost::numeric_cast<std::size_t>(kFileRecipeEntry.shareSize)});
                    shareFileBufferOffset += kFileRecipeEntry.shareSize;
                }
            }

            // send the rest data
            lap.stop();
            if (shareFileBufferOffset > 0) {
                flushCallBack(shareFileBufferOffset);
            }
        } else { // there is no such inode for this full file name
            throw DedupException(BOOST_CURRENT_LOCATION, "there is no such inode index",
                                 {
                                     {"file name",      fullFileName           },
                                     {"file name dump", ToHexDump(fullFileName)},
                                     {"inode FP",       ToHexDump(recipeFP)    },
                                     {"key",            ToHexDump(recipeKey)   }
            });
        }
    }

    /**
     * @brief perform restoring a share
     * @param shareFP share fingerprint
     * @param shareData a writable span for the share data buffer
     */
    void restoreShare(const fingerprint_t &shareFP, const mutable_bytes_view &shareData) override {
        // getShareIndex the share index value
        benchmark::UniqueLap indexLap{Benchmark::RestoreShareIndexTimer()};
        auto indexKey = BackendFacade::ToIndexKey(BackendFacade::IndexPrefix::SHARE_INDEX, shareFP);
        auto shareIndexValueOpt = backend_.getShareIndex(indexKey);
        if (!shareIndexValueOpt.has_value()) {
            throw DedupException(BOOST_CURRENT_LOCATION, "no such share index");
        }
        auto &shareIndexValue = shareIndexValueOpt.value();
        auto [kShareIndexHead, shareUserRefEntries] =
            ParseShareIndex({reinterpret_cast<const std::byte *>(shareIndexValue.data()), shareIndexValue.size()});
        indexLap.stop();

        if (kShareIndexHead.deltaDepth > 0) { // this is a delta compressed share
            benchmark::ScopedLap lap{Benchmark::RestoreFromDeltaTimer()};
            restoreDeltaShare(kShareIndexHead, shareData);
        } else { // this is a unique share
            // check the share data span size is valid
            if constexpr (config::PARANOID_CHECK) {
                if (kShareIndexHead.shareSize != shareData.size()) {
                    throw DedupException(BOOST_CURRENT_LOCATION, "share data span size is invalid");
                }
            }
            // set the share data
            benchmark::ScopedLap lap{Benchmark::RestoreCommonShareTimer()};
            backend_.getShareData(kShareIndexHead.containerName, kShareIndexHead.offset, shareData);
        }
    }

    /**
     * @brief restore a delta compressed share
     * @param shareIndexHead index head of the share index
     * @param shareData writable span for the share data buffer
     */
    void restoreDeltaShare(const shareIndexHead_t &shareIndexHead, mutable_bytes_view shareData) {
        // getShareIndex the base index data
        benchmark::UniqueLap deltaBaseIndexLap{Benchmark::RestoreDeltaBaseIndexTimer()};
        auto baseIndexOpt =
                backend_.getShareIndex(
                        BackendFacade::ToIndexKey(BackendFacade::IndexPrefix::SHARE_INDEX, shareIndexHead.baseFP));
        if (!baseIndexOpt.has_value()) {
            throw DedupException(BOOST_CURRENT_LOCATION, "base index not exists");
        }
        auto &baseIndexValue = baseIndexOpt.value();
        auto [kBaseShareIndexHead, baseShareUserRefEntries] =
            ParseShareIndex({reinterpret_cast<const std::byte *>(baseIndexValue.data()), baseIndexValue.size()});
        std::vector<std::byte> base(kBaseShareIndexHead.shareSize);
        deltaBaseIndexLap.stop();

        if (kBaseShareIndexHead.deltaDepth == 0) {
            benchmark::UniqueLap deltaBaseDataLap{Benchmark::RestoreDeltaBaseShareDataTimer()};
            backend_.getShareData(kBaseShareIndexHead.containerName, kBaseShareIndexHead.offset, base);
            deltaBaseDataLap.stop();
        } else {
            // this base is a delta compressed share
            restoreDeltaShare(kBaseShareIndexHead, base);
        }

        // getShareIndex the delta data
        benchmark::UniqueLap deltaDataLap{Benchmark::RestoreDeltaShareDataTimer()};
        std::vector<std::byte> delta(shareIndexHead.deltaSize);
        backend_.getShareData(shareIndexHead.containerName, shareIndexHead.offset, delta);
        deltaDataLap.stop();
        
        // restore the share with base and delta
        benchmark::UniqueLap computeDeltaLap{Benchmark::DeltaRestoreComputeTimer()};
        auto restoreData = delta_.RestoreSrc(base, delta, shareIndexHead.shareSize);
        if constexpr (config::PARANOID_CHECK) {
            if (restoreData.size() != shareData.size()) {
                throw DedupException(BOOST_CURRENT_LOCATION, "size not match");
            }
        }
        computeDeltaLap.stop();

        //copy restored data
        std::copy(restoreData.cbegin(), restoreData.cend(), shareData.begin());
    }
};
} // namespace dedup

#endif //DEDUP_SERVER_DEDUP_CORE_HPP
