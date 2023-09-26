//
// Created by Lokyin Zhao on 2022/10/13.
//

#ifndef DEDUP_SERVER_PEER_MEDIATOR_HPP
#define DEDUP_SERVER_PEER_MEDIATOR_HPP

#include "dedup/peer_interface.hpp"
#include "def/config.hpp"
#include "def/exception.hpp"
#include "def/struct.hpp"

namespace dedup {
/**
 * @brief PeerMediator is an intermediary for communication with peer nodes and is also a PeerInterface proxy.
 */
class PeerMediator : public PeerInterface {
private:
    /// local dedup core
    PeerInterface &self_;

public:
    explicit PeerMediator(PeerInterface &self) : self_(self) {
    }

    bool intraUserIndexUpdate(const fingerprint_t &shareFP, const user_id_t &userID) override {
        if constexpr (config::FORCE_LOCAL) {
            return self_.intraUserIndexUpdate(shareFP, userID);
        } else {
            throw DedupException(BOOST_CURRENT_LOCATION, "unimplemented");
        }
    }
    void interUserIndexUpdate(const fingerprint_t &shareFP, const user_id_t &userID,
                              const bytes_view &shareData) override {
        if constexpr (config::FORCE_LOCAL) {
            self_.interUserIndexUpdate(shareFP, userID, shareData);
        } else {
            throw DedupException(BOOST_CURRENT_LOCATION, "unimplemented");
        }
    }
    void restoreShare(const fingerprint_t &shareFP, const mutable_bytes_view &shareData) override {
        if constexpr (config::FORCE_LOCAL) {
            self_.restoreShare(shareFP, shareData);
        } else {
            throw DedupException(BOOST_CURRENT_LOCATION, "unimplemented");
        }
    }
};
} // namespace dedup

#endif //DEDUP_SERVER_PEER_MEDIATOR_HPP
