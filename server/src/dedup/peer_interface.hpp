//
// Created by Lokyin Zhao on 2022/10/13.
//

#ifndef DEDUP_SERVER_PEER_INTERFACE_HPP
#define DEDUP_SERVER_PEER_INTERFACE_HPP

#include "def/span.hpp"
#include "def/struct.hpp"

namespace dedup {
/**
 * @brief interface for inter-peer remote call procedure
 * @note In fact, there is no need for an abstract interface,
 * and it is introduced here only to solve the circular dependency problem between DedupCore and PeerMediator :)
 */
struct PeerInterface { // NOLINT(cppcoreguidelines-special-member-functions)
    virtual bool intraUserIndexUpdate(const fingerprint_t &shareFP, const user_id_t &userID) = 0;

    virtual void interUserIndexUpdate(const fingerprint_t &shareFP, const user_id_t &userID,
                                      const bytes_view &shareData) = 0;

    virtual void restoreShare(const fingerprint_t &shareFP, const mutable_bytes_view &shareData) = 0;

    virtual ~PeerInterface() = default;
};
} // namespace dedup

#endif //DEDUP_SERVER_PEER_INTERFACE_HPP
