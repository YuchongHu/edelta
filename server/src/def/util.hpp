//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_UTIL_HPP
#define DEDUP_SERVER_UTIL_HPP

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <boost/numeric/conversion/cast.hpp>

#include "leveldb/slice.h"

#include "def/exception.hpp"
#include "def/log.hpp"
#include "def/span.hpp"
#include "def/struct.hpp"
#include "third_party/crypto_primitive.hpp"

namespace dedup {
/**
 * @brief dump a byte array to string in hexadecimal representation.
 * @example "ABC" will be dumped to "414243"
 * @param str original byte array
 * @return dumped string
 */
inline std::string ToHexDump(const std::string &str) {
    std::stringstream sstr;
    for (auto &item : str) {
        sstr << std::hex << std::setfill('0') << std::setw(2)
             << static_cast<unsigned>(static_cast<unsigned char>(item));
    }
    return sstr.str();
}

template <typename T, std::size_t N>
inline std::string ToHexDump(const std::array<T, N> &arr) {
    return ToHexDump({reinterpret_cast<const char *>(arr.data()), arr.size()});
}

template <typename T, std::size_t N>
inline std::string ToHexDump(const span<T, N> &bv) {
    return ToHexDump({reinterpret_cast<const char *>(bv.data()), bv.size()});
}

/**
 * @brief converts an enumeration to its underlying type.
 */
template <typename Enum>
constexpr inline std::underlying_type_t<Enum> ToUnderlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(e);
}

/**
 * format a full file name (including the path) into '/.../.../shortName'
 * @param fullFileName the full file name to be formated
 * @return formatted full file name
 * @throw DedupException if fullFileName begins with './' or '../'
 */
inline std::string FormatFullFileName(const std::string &fullFileName) {
    std::string ret{fullFileName};
    // check if it is empty
    if (ret.empty()) {
        throw DedupException(BOOST_CURRENT_LOCATION, "the full file name is empty",
                             {
                                 {"file name",      fullFileName           },
                                 {"file name dump", ToHexDump(fullFileName)}
        });
    }
    // make sure that fullFileName starts with '/'
    if (ret[0] != '/') {
        if ((ret.substr(0, 2) == "./") || (ret.substr(0, 3) == "../")) {
            throw DedupException(BOOST_CURRENT_LOCATION, "the full file name should not begin with './' or '../'",
                                 {
                                     {"file name",      fullFileName           },
                                     {"file name dump", ToHexDump(fullFileName)}
            });
        } else {
            ret = '/' + ret;
        }
    }
    return ret;
}

/**
 * format a directory name into '.../.../shortName/'
 * @param dirName <u>return</u> the directory name to be formatted
 * @throw DedupException if dirName is empty
 */
inline void FormatDirName(std::string &dirName) {
    // check if it is empty
    if (dirName.empty()) {
        throw DedupException(BOOST_CURRENT_LOCATION, "the name of the directory to format is empty");
    }

    // make sure that dirName ends with '/'
    if (dirName[dirName.size() - 1] != '/') {
        dirName += '/';
    }
}

/**
 * recursively create a directory
 * @param dirName the name (which ends with '/') of the directory to be created
 * @param clear whether to remove the existing directory
 * @throw DedupException if the creation of the directory fails or an underlying
 * operating system error occurs
 */
inline void CreateDir(const std::string &dirName, bool clear) {
    using namespace std::filesystem;
    try {
        path path{dirName};
        if (clear) {
            // the directory needs to be clear
            remove_all(path);
        }
        if (!exists(path)) {
            // the directory does not exist, then create it
            if (!create_directories(path)) {
                throw DedupException(BOOST_CURRENT_LOCATION, "fail to create the directory",
                                     {
                                         {"directory name", dirName}
                });
            }
        }
    } catch (const filesystem_error &err) {
        throw DedupException(BOOST_CURRENT_LOCATION, "a file system error occurs",
                             {
                                 {"directory name", dirName   },
                                 {"error string",   err.what()}
        });
    }
}

/**
 * @brief initialize related deduplication directories at startup
 * @param clear whether to remove the existing files
 */
inline void DirInit(bool clear) noexcept {
    // create the DB dir
    try {
        CreateDir(config::GetDBDir(), clear);
        CreateDir(config::GetContianerDir(), clear);
    } catch (DedupException &e) {
        std::cerr << e.what() << std::endl;
        exit(-1);
    } catch (std::exception &e) {
        std::cerr << log::ERROR << "exception occurs when initializing directories: " << e.what() << std::endl;
        exit(-1);
    }

    if (clear) {
        std::cout << log::INFO << log::FormatLog("the directories has been cleared and recreated");
    }
}

/**
 * @brief generate a fingerprint with the given data
 * @param rawData the data span
 * @return the generated fingerprint
 */
inline fingerprint_t ToFP(const bytes_view &rawData) {
    thread_local crypto_primitive::CryptoPrimitive cryptoPrimitive{};

    fingerprint_t fp;
    if (!cryptoPrimitive.generateHash(reinterpret_cast<const unsigned char *>(rawData.data()),
                                      boost::numeric_cast<int>(rawData.size()),
                                      reinterpret_cast<unsigned char *>(fp.data()))) {
        throw DedupException(BOOST_CURRENT_LOCATION,
                             "the size of the generated hash does not match with the expected one");
    }
    return fp;
}

/**
 * transform a file name to a recipe file's fingerprint
 * @param fullFileName - the full file name to be transformed
 * @param userID - the user id
 * @throw DedupException if the hash generation fails
 */
inline fingerprint_t ToRecipeFP(const std::string &fullFileName, const user_id_t &userID) {
    // generate the inode's fingerprint from both full file name and user id
    auto hashInputBuffer = std::make_unique<std::byte[]>(fullFileName.size() + sizeof(int));
    std::copy(fullFileName.begin(), fullFileName.end(), reinterpret_cast<char *>(hashInputBuffer.get()));
    *(reinterpret_cast<user_id_t *>(hashInputBuffer.get() + fullFileName.size())) = userID;

    return ToFP({hashInputBuffer.get(), fullFileName.size() + sizeof(int)});
}

/**
 * @brief parse the content of a file share meta buffer
 * @param fileShareMeta buffer for the file share meta to parse
 * @return a tuple for the file share meta head, full file name and its share
 * meta entries
 * @throw DedupException if the size of the fileShareMeta is invalid, that is,
 * it's inconsistent with the size that the head indicating
 * @note file share meta format: [fileShareMetaHead_t + full file name(prefix path included) + shareMetaEntry_t ... shareMetaEntry_t]
 */
inline std::tuple<const fileShareMetaHead_t &, std::string_view, span<const shareMetaEntry_t>>
ParseFileShareMeta(const bytes_view &fileShareMeta) {
    /// offset of the file share data buffer
    std::size_t offset{0};

    // getShareIndex the head
    auto &kfileShareMetaHead = *reinterpret_cast<const fileShareMetaHead_t *>(fileShareMeta.data() + offset);
    offset += FILE_SHARE_META_HEAD_SIZE;

    // check the buffer length is valid
    if constexpr (config::PARANOID_CHECK) {
        if (fileShareMeta.size() != FILE_SHARE_META_HEAD_SIZE + kfileShareMetaHead.fullNameSize +
                                        SHARE_META_ENTRY_SIZE * kfileShareMetaHead.numOfComingSecrets) {
            throw DedupException(BOOST_CURRENT_LOCATION, "file share meta is invalid");
        }
    }

    // getShareIndex the file name
    auto fullFileName = std::string_view{reinterpret_cast<const char *>(fileShareMeta.data() + offset),
                                         boost::numeric_cast<std::size_t>(kfileShareMetaHead.fullNameSize)};
    offset += kfileShareMetaHead.fullNameSize;

    // getShareIndex the share meta entries
    auto shareMetaEntries =
        span<const shareMetaEntry_t>{reinterpret_cast<const shareMetaEntry_t *>(fileShareMeta.data() + offset),
                                     boost::numeric_cast<std::size_t>(kfileShareMetaHead.numOfComingSecrets)};

    return {kfileShareMetaHead, fullFileName, shareMetaEntries};
}

/**
 * @brief parse the content of a file recipe buffer
 * @param fileRecipeData buffer for the file recipe to parse
 * @return a pair for the file recipe head and its file recipe entries
 * @note file recipe format: [fileRecipeHead_t + fileRecipeEntry_t ... fileRecipeEntry_t]
 */
inline std::pair<const fileRecipeHead_t &, span<const fileRecipeEntry_t>>
ParseFileRecipe(const bytes_view &fileRecipeData) {
    // getShareIndex the head
    auto &kFileRecipeHead = *reinterpret_cast<const fileRecipeHead_t *>(fileRecipeData.data());

    // check the buffer length is valid
    if constexpr (config::PARANOID_CHECK) {
        if (fileRecipeData.size() != FILE_RECIPE_HEAD_SIZE + kFileRecipeHead.numOfShares * FILE_RECIPE_ENTRY_SIZE) {
            throw DedupException(BOOST_CURRENT_LOCATION, "file recipe data is invalid");
        }
    }

    // getShareIndex the recipe entries
    auto fileRecipeEntries = span<const fileRecipeEntry_t>{
        reinterpret_cast<const fileRecipeEntry_t *>(fileRecipeData.data() + FILE_RECIPE_HEAD_SIZE),
        boost::numeric_cast<std::size_t>(kFileRecipeHead.numOfShares)};

    return {kFileRecipeHead, fileRecipeEntries};
}

/**
 * @brief parse the content of a share index buffer
 * @param shareIndexData buffer for the share index data
 * @return a pair for the share index head and share user reference entries
 * @note share index format: [shareIndexHead_t + shareUserRefEntry_t ... shareUserRefEntry_t]
 */
inline std::pair<const shareIndexHead_t &, span<const shareUserRefEntry_t>>
ParseShareIndex(const bytes_view &shareIndexData) {
    // getShareIndex the head
    auto &kShareIndexHead = *reinterpret_cast<const shareIndexHead_t *>(shareIndexData.data());

    // check the buffer length is valid
    if constexpr (config::PARANOID_CHECK) {
        if (shareIndexData.size() != SHARE_INDEX_HEAD_SIZE + kShareIndexHead.numOfUsers * SHARE_USER_REF_ENTRY_SIZE) {
            throw DedupException(BOOST_CURRENT_LOCATION, "share index data is invalid");
        }
    }

    // getShareIndex the user reference entries
    auto userRefEntries = span<const shareUserRefEntry_t>{
        reinterpret_cast<const shareUserRefEntry_t *>(shareIndexData.data() + SHARE_INDEX_HEAD_SIZE),
        boost::numeric_cast<std::size_t>(kShareIndexHead.numOfUsers)};

    return {kShareIndexHead, userRefEntries};
}

/**
 * @brief parse the content of a new share index buffer
 * @param shareIndexData buffer for the share index data, which has only one share user reference entry
 * @return a pair for the share index head and a share user reference entry
 * @note share index format: [shareIndexHead_t + shareUserRefEntry_t ... shareUserRefEntry_t]
 */
inline std::pair<shareIndexHead_t &, shareUserRefEntry_t &>
ParseNewShareIndex(std::array<std::byte, SHARE_INDEX_HEAD_SIZE + SHARE_USER_REF_ENTRY_SIZE> &shareIndexData) {
    return {*reinterpret_cast<shareIndexHead_t *>(shareIndexData.data()),
            *reinterpret_cast<shareUserRefEntry_t *>(shareIndexData.data() + SHARE_INDEX_HEAD_SIZE)};
}

} // namespace dedup

#endif // DEDUP_SERVER_UTIL_HPP
