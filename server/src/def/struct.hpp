//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_STRUCT_HPP
#define DEDUP_SERVER_STRUCT_HPP

#include "def/config.hpp"

namespace dedup {
/// type definition for user id, whose underlying type is int
using user_id_t = int32_t;
/// size of user_id_t
inline constexpr std::size_t USER_ID_SIZE{sizeof(user_id_t)};
/// type definition for packet size, whose underlying type is uint32_t
using packet_size_t = uint32_t;
/// size of packet_size_t (its name is so weird ;)
inline constexpr std::size_t PACKET_SIZE_SIZE{sizeof(packet_size_t)};
/// type definition for FP, whose underlying type is std::array<std::byte,
/// config::FP_SIZE>
using fingerprint_t = std::array<std::byte, config::FP_SIZE>;
/// size of fingerprint_t
inline constexpr std::size_t FP_SIZE{sizeof(fingerprint_t)};
/// type definition for Key, whose underlying type is std::array
using key_t = std::array<std::byte, config::KEY_SIZE>;
/// size of key_t
inline constexpr std::size_t KEY_SIZE{sizeof(key_t)};
/// enumerations for indicator
enum class indicator_e : int32_t {
    /// client sends file share metadata to login server, or server send file
    /// share metadata to a peer server
    META = -1,
    /// client sends share data to login server
    DATA = -2,
    /// login server sends dedup status list to client
    STAT = -3,
    /// client requests download
    DOWNLOAD = -7,
    /// the server sends part of the chunk to the client on downloading file
    RESP_DOWNLOAD = -5,
    /// server sends a share fp to perform the intra-user share index update to a
    /// peer node
    INTRA_USER_SHARE_IDX_UPDATE = -10,
    /// a peer server returns the intra-user share index update result to server
    RESP_INTRA_USER_SHARE_IDX_UPDATE = -11,
    /// server sends a user share data to perform the inter-user share index
    /// update to a peer node
    INTER_USER_SHARE_IDX_UPDATE = -15,
    /// a peer server returns the inter-user share index update result to server
    RESP_INTER_USER_SHARE_IDX_UPDATE = -16,
    /// request share from peer node
    RESTORE_SHARE = -17,
    /// Respond to share requests from peer node
    RESP_RESTORE_SHARE = -18,
};
/// size of indicator_e
inline constexpr std::size_t INDICATOR_SIZE{sizeof(indicator_e)};
/// size of the packet header (indicator + packet size)
inline constexpr std::size_t PACKET_HEADER_SIZE{INDICATOR_SIZE + PACKET_SIZE_SIZE};

/// structure for internal file name
using internal_file_name_t  = std::array<char, config::INTERNAL_FILE_NAME_SIZE>;
inline constexpr std::size_t INTERNAL_FILE_NAME_SIZE{sizeof(internal_file_name_t)};

/**
 * @brief the head structure of the file share metadata
 * @note the shareMetaBuffer format: [fileShareMetaHead_t + full file name(prefix
 * path included) + shareMetaEntry_t ... shareMetaEntry_t]
 */
struct fileShareMetaHead_t {
    int fullNameSize;
    long fileSize;
    int numOfPastSecrets; // number of the secrets remains on the previous file
    long sizeOfPastSecrets;
    int numOfComingSecrets; // number of the secrets in the file share to be processed
    long sizeOfComingSecrets;
};
/// size of the head structure of the file share metadata
inline constexpr int FILE_SHARE_META_HEAD_SIZE{sizeof(fileShareMetaHead_t)};

/**
 * @brief the entry structure of the file share metadata
 * @note the shareMetaBuffer format: [fileShareMetaHead_t + full file name +
 * shareMetaEntry_t ... shareMetaEntry_t]
 */
struct shareMetaEntry_t {
    fingerprint_t shareFP;
    int secretID;
    int secretSize;
    int shareSize;
};
/// size of the entry structure of the file share metadata
inline constexpr int SHARE_META_ENTRY_SIZE{sizeof(shareMetaEntry_t)};

/**
 * @brief the head structure of the value of the share index
 * @note share index value format: [shareIndexHead_t + shareUserRefEntry_t
 * ... shareUserRefEntry_t]
 */
struct shareIndexHead_t {
    int shareSize;
    int numOfUsers;
    uint8_t deltaDepth;
    std::size_t deltaSize;
    fingerprint_t baseFP;
    internal_file_name_t containerName;
    std::size_t offset;
};
/// size of the head structure of the value of the share index
inline constexpr int SHARE_INDEX_HEAD_SIZE{sizeof(shareIndexHead_t)};

/// the user reference entry structure of the value of the share index
struct shareUserRefEntry_t {
    user_id_t userID;
};
/// size of the user reference entry structure of the value of the share index
inline constexpr int SHARE_USER_REF_ENTRY_SIZE{sizeof(shareUserRefEntry_t)};

/**
 * @brief the head structure of the recipes of a file
 * @note file recipe format: [fileRecipeHead_t + fileRecipeEntry_t ...
 * fileRecipeEntry_t]
 */
struct fileRecipeHead_t {
    user_id_t userID;
    long fileSize;
    int numOfShares;
};
/// size of the head structure of the recipes of a file
inline constexpr int FILE_RECIPE_HEAD_SIZE{sizeof(fileRecipeHead_t)};

/// the entry structure of the recipes of a file
struct fileRecipeEntry_t {
    fingerprint_t shareFP;
    int secretID;
    int secretSize;
    int shareSize;
};
/// size of the entry structure of the recipes of a file
inline constexpr int FILE_RECIPE_ENTRY_SIZE{sizeof(fileRecipeEntry_t)};

/**
 * @brief the head structure of the restored share file
 * @note restored share file format: [shareFileHead_t + shareEntry_t + share
 * data + ... + shareEntry_t + share data]
 */
struct shareFileHead_t {
    long fileSize;
    int numOfShares;
};
/// size of the head structure of the restored share file
inline constexpr int SHARE_FILE_HEAD_SIZE{sizeof(shareFileHead_t)};

/// the entry structure of the restored share file
struct shareEntry_t {
    int secretID;
    int secretSize;
    int shareSize;
};
/// size of the entry structure of the restored share file
inline constexpr int SHARE_ENTRY_SIZE{sizeof(shareEntry_t)};

template <typename TrivialType>
struct trivial_hash {
    auto operator()(const TrivialType &obj) const noexcept {
        static_assert(std::is_trivial_v<TrivialType>);
        using result_type = std::result_of_t<std::hash<uint8_t>(uint8_t)>;
        using argument_type = TrivialType;
        using fold_t = uint64_t;
        using remain_t = uint8_t;
        constexpr fold_t FOLD_MAGIC{0xF18A467BC1E9AD3F};
        constexpr remain_t REMAIN_MAGIC{0xE2};

        constexpr auto FOLD_TIME = argument_type{}.size() / sizeof(fold_t);
        constexpr auto REMAIN_TIME = argument_type{}.size() % sizeof(fold_t);

        fold_t fold{FOLD_MAGIC};
        auto pFold = reinterpret_cast<const fold_t *>(obj.data());
        for (int i = 0; i < FOLD_TIME; ++i) {
            fold ^= *(pFold + i);
        }

        remain_t remain{REMAIN_MAGIC};
        auto pRemain = reinterpret_cast<const remain_t *>(pFold + FOLD_TIME);
        for (int i = 0; i < REMAIN_TIME; ++i) {
            remain ^= *(pRemain + i);
        }

        return std::hash<fold_t>{}(fold) ^ std::hash<remain_t>{}(remain);
    }
};
} // namespace dedup

namespace std {
/// specialization for dedup::fingerprint_t
template <>
struct hash<dedup::fingerprint_t> {
    auto operator()(const dedup::fingerprint_t &arg) const noexcept {
        return dedup::trivial_hash<dedup::fingerprint_t>{}(arg);
    }
};

/// specialization for dedup::key_t
template <>
struct hash<dedup::key_t> {
    auto operator()(const dedup::key_t &arg) const noexcept {
        return dedup::trivial_hash<dedup::key_t>{}(arg);
    }
};
} // namespace std
#endif // DEDUP_SERVER_STRUCT_HPP
