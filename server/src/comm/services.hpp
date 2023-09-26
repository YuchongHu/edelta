//
// Created by Lokyin Zhao on 2022/10/13.
//

#ifndef DEDUP_SERVER_SERVICES_HPP
#define DEDUP_SERVER_SERVICES_HPP

#include <functional>
#include <memory>
#include <type_traits>

#include "sockpp/tcp_socket.h"

#include "dedup/dedup_core.hpp"
#include "def/exception.hpp"
#include "def/struct.hpp"

namespace dedup {
class ClientUpload {
private:
    const user_id_t userID_;
    sockpp::tcp_socket &sock_;
    ClientInterface &dedupObj_;

    /// size of the file share meta
    packet_size_t metaSize_{0};
    /// size of the share data
    packet_size_t dataSize_{0};
    /// number of shares in this file share
    uint32_t numOfTotalShares_{0};
    /// number of coming shares in this file share fragment
    std::size_t numOfComingShares_{0};
    /// buffer for the file share meta
    std::unique_ptr<std::byte[]> metaBuffer_{std::make_unique<std::byte[]>(config::META_BUFFER_LEN)};
    /// buffer for the response data
    std::unique_ptr<std::byte[]> responseBuffer_{std::make_unique<std::byte[]>(config::STAT_BUFFER_LEN)};
    /// buffer for the share data
    std::unique_ptr<std::byte[]> dataBuffer_{std::make_unique<std::byte[]>(config::DATA_BUFFER_LEN)};

    void firstStageReceive_() {
        // metadata format: [total packet size, number of total shares(uint32), metadata],
        // and the size of the 'number of total shares' is included in the packet size

        // read the packet size
        if (sock_.read_n(&metaSize_, sizeof(metaSize_)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }

        // read the number of shares
        if (sock_.read_n(&numOfTotalShares_, sizeof(numOfTotalShares_)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        metaSize_ -= sizeof(numOfTotalShares_);

        // read the metadata
        if (sock_.read_n(metaBuffer_.get(), metaSize_) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        // read the number of coming shares
        numOfComingShares_ = boost::numeric_cast<decltype(numOfComingShares_)>(
            reinterpret_cast<const fileShareMetaHead_t *>(metaBuffer_.get())->numOfComingSecrets);
    }

    void firstStageRespond_() {
        // set indicator and packet size
        *reinterpret_cast<indicator_e *>(responseBuffer_.get()) = indicator_e::STAT;
        *reinterpret_cast<packet_size_t *>(responseBuffer_.get() + INDICATOR_SIZE) =
            boost::numeric_cast<packet_size_t>(numOfComingShares_);
        if (sock_.write_n(responseBuffer_.get(), PACKET_HEADER_SIZE + numOfComingShares_) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

    void secondStageReceive_() {
        // receive the user id
        user_id_t userID; // NOLINT(cppcoreguidelines-init-variables)
        if (sock_.read_n(&userID, sizeof(userID)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        if constexpr (config::PARANOID_CHECK) {
            if (userID != userID_) {
                throw DedupException(BOOST_CURRENT_LOCATION, "user id not match");
            }
        }

        // receive the indicator
        indicator_e indicator; // NOLINT(cppcoreguidelines-init-variables)
        if (sock_.read_n(&indicator, sizeof(indicator)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        if constexpr (config::PARANOID_CHECK) {
            if (indicator != indicator_e::DATA) {
                throw DedupException(BOOST_CURRENT_LOCATION, "unexpected indicator");
            }
        }

        // receive the packet size
        if (sock_.read_n(&dataSize_, sizeof(dataSize_)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }

        // check whether the buffer can hold the data
        if constexpr (config::PARANOID_CHECK) {
            if (dataSize_ > config::DATA_BUFFER_LEN) {
                throw DedupException(BOOST_CURRENT_LOCATION, "buffer size is too small");
            }
        }

        // receive the share data
        if (sock_.read_n(dataBuffer_.get(), dataSize_) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

    bool unfinished_() {
        // receive the user id
        user_id_t userID; // NOLINT(cppcoreguidelines-init-variables)
        auto cnt = sock_.read_n(&userID, sizeof(userID));
        if (cnt == 0) {
            // socket is closed
            return false;
        }
        if (cnt == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        if constexpr (config::PARANOID_CHECK) {
            if (userID != userID_) {
                throw DedupException(BOOST_CURRENT_LOCATION, "user id not match");
            }
        }

        // receive the indicator
        indicator_e indicator; // NOLINT(cppcoreguidelines-init-variables)
        if (sock_.read_n(&indicator, sizeof(indicator)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        if constexpr (config::PARANOID_CHECK) {
            if (indicator != indicator_e::META) {
                throw DedupException(BOOST_CURRENT_LOCATION, "unexpected indicator");
            }
        }

        return true;
    }

public:
    ClientUpload(const user_id_t &userID, sockpp::tcp_socket &sock, ClientInterface &dedupObj)
        : userID_(userID), sock_(sock), dedupObj_(dedupObj) {
    }

    void operator()() {
        do {
            // perform first stage deduplication
            firstStageReceive_();
            dedupObj_.firstStageDedup(
                userID_, {metaBuffer_.get(), metaSize_},
                {reinterpret_cast<bool *>(responseBuffer_.get() + PACKET_HEADER_SIZE), numOfComingShares_});
            firstStageRespond_();

            // perform second stage deduplication
            secondStageReceive_();
            dedupObj_.secondStageDedup(
                userID_, {metaBuffer_.get(), metaSize_}, {dataBuffer_.get(), dataSize_},
                {reinterpret_cast<bool *>(responseBuffer_.get() + PACKET_HEADER_SIZE), numOfComingShares_},
                numOfTotalShares_);
        } while (unfinished_());
    }
};

class ClientDownload {
private:
    user_id_t userID;
    sockpp::tcp_socket &sock_;
    ClientInterface &dedupObj_;

    std::string fullFileName_{};
    std::unique_ptr<std::byte[]> shareFileBuffer_{std::make_unique<std::byte[]>(config::SHARE_FILE_BUFFER_LEN)};

    void receive_() {
        // read the size of the full file name
        packet_size_t fileNameSize; // NOLINT(cppcoreguidelines-init-variables)
        if (sock_.read_n(&fileNameSize, sizeof(fileNameSize)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        // resize the string for the full file name
        fullFileName_.resize(fileNameSize);
        // read the data
        if (sock_.read_n(fullFileName_.data(), fileNameSize) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

    void flush_(std::size_t dataSize) {
        *reinterpret_cast<indicator_e *>(shareFileBuffer_.get()) = indicator_e::RESP_DOWNLOAD;
        *reinterpret_cast<packet_size_t *>(shareFileBuffer_.get() + INDICATOR_SIZE) = dataSize;
        if (sock_.write_n(shareFileBuffer_.get(), PACKET_HEADER_SIZE + dataSize) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

public:
    ClientDownload(const user_id_t &userID, sockpp::tcp_socket &sock, ClientInterface &dedupObj)
        : userID(userID), sock_(sock), dedupObj_(dedupObj) {
    }

    void operator()() {
        receive_();
        dedupObj_.restoreShareFile(
            userID, fullFileName_,
            {shareFileBuffer_.get() + PACKET_HEADER_SIZE, config::SHARE_FILE_BUFFER_LEN - PACKET_HEADER_SIZE},
            [this](std::size_t dataSize) { this->flush_(dataSize); });
    }
};

class PeerIntraUserIndex {
    user_id_t userID;
    sockpp::tcp_socket &sock_;
    PeerInterface &dedupObj_;

    using status_t = decltype(std::declval<PeerInterface>().intraUserIndexUpdate(std::declval<fingerprint_t>(),
                                                                                 std::declval<user_id_t>()));
    fingerprint_t fp_{};
    std::array<std::byte, PACKET_HEADER_SIZE + sizeof(status_t)> responseData_{};

    void receive_() {
        // read the packet size, expected to be FP_SIZE
        packet_size_t packetSize; // NOLINT(cppcoreguidelines-init-variables)
        if (sock_.read_n(&packetSize, sizeof(packetSize)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        if constexpr (config::PARANOID_CHECK) {
            if (packetSize != FP_SIZE) {
                throw DedupException(BOOST_CURRENT_LOCATION, "packet size is invalid");
            }
        }
        // read the share fp
        if (sock_.read_n(fp_.data(), fp_.size()) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

    void respond_() {
        *reinterpret_cast<indicator_e *>(responseData_.data()) = indicator_e::RESP_INTRA_USER_SHARE_IDX_UPDATE;
        *reinterpret_cast<packet_size_t *>(responseData_.data() + INDICATOR_SIZE) = sizeof(status_t);
        if (sock_.write_n(responseData_.data(), responseData_.size()) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

public:
    PeerIntraUserIndex(const user_id_t &userID, sockpp::tcp_socket &sock, PeerInterface &dedupObj)
        : userID(userID), sock_(sock), dedupObj_(dedupObj) {
    }

    void operator()() {
        receive_();
        *reinterpret_cast<status_t *>(responseData_.data() + PACKET_HEADER_SIZE) =
            dedupObj_.intraUserIndexUpdate(fp_, userID);
        respond_();
    }
};

class PeerInterUserIndex {
    user_id_t userID;
    sockpp::tcp_socket &sock_;
    PeerInterface &dedupObj_;

    fingerprint_t fp_{};
    packet_size_t dataSize_{};
    std::unique_ptr<std::byte[]> shareData_{nullptr};

    void receive_() {
        // read the packet size
        if (sock_.read_n(&dataSize_, sizeof(dataSize_)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        // read the share fp
        if (sock_.read_n(fp_.data(), fp_.size()) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        dataSize_ -= fp_.size();
        // allocate a buffer and read the share data
        shareData_ = std::make_unique<std::byte[]>(dataSize_);
        if (sock_.read_n(shareData_.get(), dataSize_) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

public:
    PeerInterUserIndex(const user_id_t &userID, sockpp::tcp_socket &sock, PeerInterface &dedupObj)
        : userID(userID), sock_(sock), dedupObj_(dedupObj) {
    }

    void operator()() {
        receive_();
        dedupObj_.interUserIndexUpdate(fp_, userID, {shareData_.get(), dataSize_});
    }
};

class PeerRestoreShare {
    user_id_t userID;
    sockpp::tcp_socket &sock_;
    PeerInterface &dedupObj_;

    fingerprint_t fp_{};
    std::size_t shareSize_{};
    std::unique_ptr<std::byte[]> responseData_{nullptr};

    void receive_() {
        // packet format: [share size, share fp]

        // read the packet size, expected to be FP_SIZE + sizeof(shareSize_)
        packet_size_t packetSize; // NOLINT(cppcoreguidelines-init-variables)
        if (sock_.read_n(&packetSize, sizeof(packetSize)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        if constexpr (config::PARANOID_CHECK) {
            if (packetSize != sizeof(shareSize_) + FP_SIZE) {
                throw DedupException(BOOST_CURRENT_LOCATION, "packet size is invalid");
            }
        }

        // read the share size
        if (sock_.read_n(&shareSize_, sizeof(shareSize_)) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }

        // read the fp
        if (sock_.read_n(fp_.data(), fp_.size()) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
        // allocate a buffer for the share data
        responseData_ = std::make_unique<std::byte[]>(PACKET_HEADER_SIZE + shareSize_);
    }

    void respond_() {
        *reinterpret_cast<indicator_e *>(responseData_.get()) = indicator_e::RESP_RESTORE_SHARE;
        *reinterpret_cast<packet_size_t *>(responseData_.get() + INDICATOR_SIZE) = shareSize_;
        if (sock_.write_n(responseData_.get(), PACKET_HEADER_SIZE + shareSize_) == -1) {
            throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
        }
    }

public:
    PeerRestoreShare(const user_id_t &userID, sockpp::tcp_socket &sock, PeerInterface &dedupObj)
        : userID(userID), sock_(sock), dedupObj_(dedupObj) {
    }

    void operator()() {
        receive_();
        dedupObj_.restoreShare(fp_, {responseData_.get() + PACKET_HEADER_SIZE, shareSize_});
        respond_();
    }
};

class ServiceDispatcher {
    /**
 * @brief read the user id and indicator from the socket, and dispatch a callable object
 * @param sock socket to the peer
 * @param dedupObj dedup core to perform the deduplication
 * @return a pointer to the service, if the indicator received is valid, otherwise nullptr
 */
public:
    static void Submit(sockpp::tcp_socket &sock, DedupCore &dedupObj) {
        user_id_t userID{};
        indicator_e indicator{};
        ssize_t cnt{};

        while ((cnt = sock.read_n(&userID, sizeof(userID))) != 0) {
            if (cnt == -1) {
                throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
            }
            // getShareIndex indicator
            cnt = sock.read_n(&indicator, sizeof(indicator));
            if (cnt == -1) {
                throw DedupException(BOOST_CURRENT_LOCATION, "socket error");
            }
            // return a callable obj according to the indicator
            switch (indicator) {
            case indicator_e::META:
                ClientUpload{userID, sock, dedupObj}();
                return;
            case indicator_e::DOWNLOAD:
                ClientDownload{userID, sock, dedupObj}();
                return;
            case indicator_e::INTRA_USER_SHARE_IDX_UPDATE:
                PeerIntraUserIndex{userID, sock, dedupObj}();
                return;
            case indicator_e::INTER_USER_SHARE_IDX_UPDATE:
                PeerInterUserIndex(userID, sock, dedupObj)();
                return;
            case indicator_e::RESTORE_SHARE:
                PeerRestoreShare{userID, sock, dedupObj}();
                return;
            default:
                throw DedupException(
                    BOOST_CURRENT_LOCATION, "invalid indicator",
                    {
                        {"received indicator",
                         std::to_string(static_cast<std::underlying_type_t<decltype(indicator)>>(indicator))}
                });
            }
        }
    }
};
} // namespace dedup

#endif //DEDUP_SERVER_SERVICES_HPP
