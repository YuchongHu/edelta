//
// Created by Lokyin Zhao on 2022/11/2.
//

#ifndef DEDUP_SERVER_DELTA_HPP
#define DEDUP_SERVER_DELTA_HPP

#include <array>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "def/span.hpp"
#include "def/struct.hpp"

extern "C" {
#include "delta.h"
#include "rabin.h"
};

namespace dedup {

class Delta {
public:
    using super_feature_t = decltype(superF{}.sf1);
    static constexpr std::size_t SUPER_FEATURE_NUM{3};
    using super_features_t = superF;

private:
    using map_type = std::unordered_map<super_feature_t, fingerprint_t>;

    map_type superFeature1Map_{};
    map_type superFeature2Map_{};
    map_type superFeature3Map_{};

public:
    /**
     * @brief initialize the delta lib
     */
    static void Init() {
        static std::once_flag initFlag{};
        std::call_once(initFlag, []() { chunkAlg_init(); });
    }

    /**
     * @brief generate super features according to the data
     * @param data source data
     * @return super features for the data
     */
    static super_features_t GenSuperFeature(bytes_view data) {
        superF value{};
        // super_feature(reinterpret_cast<const unsigned char *> (data.data()), data.size(), &value);
        finesse_super_feature(reinterpret_cast<const unsigned char *> (data.data()), data.size(), &value);
        return value;
    }

    /**
     * @brief compute delta from source data and it's base data, and the source data can be restored from delta and base
     * @param base base data
     * @param src source data
     * @return delta data
     */
    static std::vector<std::byte> ComputeDelta(bytes_view base, bytes_view src) {
        auto delta = compute_delta(reinterpret_cast<const uint8_t *>(base.data()), base.size(),
                                   reinterpret_cast<const uint8_t *>(src.data()), src.size());
        if (delta != nullptr) {
            auto ret = std::vector<std::byte>(delta->deltaSize);
            std::copy_n(delta->data, delta->deltaSize, reinterpret_cast<uint8_t *>(ret.data()));
            free(delta->data);
            free(delta);
            return ret;
        } else {
            return {};
        }
    }

    /**
     * @brief restore source data from base data and delta data
     * @param base base data
     * @param delta delta data
     * @param srcSize size of the source data
     * @return source data
     */
    std::vector<std::byte> RestoreSrc(bytes_view base, bytes_view delta, std::size_t srcSize) {
        std::size_t restoreDataSize{0};
        std::vector<std::byte> ret(srcSize);
        struct delta deltaStruct {
            .srcSize = srcSize, .deltaSize = delta.size(),
            .data = const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(delta.data()))
        };
        auto status = restore_delta(reinterpret_cast<const uint8_t *>(base.data()), base.size(), &deltaStruct,
                                    reinterpret_cast<uint8_t *>(ret.data()), &restoreDataSize);
        if (status == 0) {
            ret.resize(restoreDataSize);
            return ret;
        } else {
            return {};
        }
    }

    /**
     * @brief update the super feature index with a entry of super feature and it's corresponding fingerprint
     * @param features super features
     * @param fp fingerprint for this super feature
     */
    void superFeatureIndexUpdate(const super_features_t &features, const fingerprint_t &fp) {
        superFeature1Map_[features.sf1] = fp;
        superFeature2Map_[features.sf2] = fp;
        superFeature3Map_[features.sf3] = fp;
    }

    /**
     * @brief index a fingerprint with super features
     * @param features super features for the index key
     * @return optional for the corresponding fingerprint, or nullopt if there is no such an entry for the super feature
     */
    std::optional<fingerprint_t> superFeatureIndex(const super_features_t &features) {
        auto iter = superFeature1Map_.find(features.sf1);
        if (iter != superFeature1Map_.end()) {
            return std::optional<fingerprint_t>{std::in_place, iter->second};
        }

        iter = superFeature2Map_.find(features.sf2);
        if (iter != superFeature2Map_.end()) {
            return std::optional<fingerprint_t>{std::in_place, iter->second};
        }

        iter = superFeature3Map_.find(features.sf3);
        if (iter != superFeature3Map_.end()) {
            return std::optional<fingerprint_t>{std::in_place, iter->second};
        }

        // not found
        return {};
    }
};

} // namespace dedup

#endif //DEDUP_SERVER_DELTA_HPP
