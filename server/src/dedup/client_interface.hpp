//
// Created by Lokyin Zhao on 2022/10/14.
//

#ifndef DEDUP_SERVER_CLIENT_INTERFACE_HPP
#define DEDUP_SERVER_CLIENT_INTERFACE_HPP

#include "def/span.hpp"
#include "def/struct.hpp"
#include <functional>

namespace dedup {
struct ClientInterface { // NOLINT(cppcoreguidelines-special-member-functions)
    virtual void firstStageDedup(const user_id_t &userID, const bytes_view &shareMeta, const span<bool> &dupStat) = 0;

    virtual void secondStageDedup(const user_id_t &userID, const bytes_view &shareMeta, const bytes_view &shareData,
                                  const span<const bool> &dupStat, const std::size_t &totalNumOfShares) = 0;

    virtual void restoreShareFile(const user_id_t &userID, const std::string &fullFileName,
                                  const mutable_bytes_view &shareFileData,
                                  const std::function<void(std::size_t)> &flushCallBack) = 0;

    virtual ~ClientInterface() = default;
};
} // namespace dedup

#endif //DEDUP_SERVER_CLIENT_INTERFACE_HPP
