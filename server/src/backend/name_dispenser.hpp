//
// Created by Lokyin Zhao on 2022/11/5.
//

#ifndef DEDUP_SERVER_NAME_DISPENSER_HPP
#define DEDUP_SERVER_NAME_DISPENSER_HPP

#include <algorithm>
#include <array>
#include <mutex>

#include "def/config.hpp"
#include "def/exception.hpp"
#include "def/struct.hpp"

namespace dedup {
/**
 * @brief a file name dispenser manages a string sequence in lexicographical order, from which user can getShareIndex a globally unique file name
 */
class NameDispenser {
    using element_type = internal_file_name_t;

private:
    element_type name_;
    std::mutex mtx_{};

public:
    explicit NameDispenser() : name_() {
        std::fill(name_.begin(), name_.end(), 'a');
    }

    element_type get() {
        std::lock_guard<decltype(mtx_)> lockGuard{mtx_};
        auto res = name_;
        auto cur = std::find_if_not(name_.begin(), name_.end(), [](char c) { return c == 'z'; });
        if (cur != name_.cend()) {
            (*cur)++;
            std::fill(name_.begin(), cur, 'a');
        } else {
            throw DedupException(BOOST_CURRENT_LOCATION, "the global filee nae reaches maximum lexicographically");
        }
        return res;
    }
};
} // namespace dedup

#endif //DEDUP_SERVER_NAME_DISPENSER_HPP
