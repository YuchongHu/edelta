//
// Created by Lokyin Zhao on 2022/11/2.
//

#ifndef DEDUP_SERVER_CONTAINER_HPP
#define DEDUP_SERVER_CONTAINER_HPP

#include <filesystem>
#include <string>

#include <boost/iostreams/device/mapped_file.hpp>

#include "def/exception.hpp"
#include "def/span.hpp"
#include "def/struct.hpp"

namespace dedup {

/**
 * @brief convert a @e internal_file_name_t to @e std::string
 */
std::string to_string(const internal_file_name_t &obj) {
    return std::string{obj.data(), obj.size()};
}

/**
 * @brief a @e MutableContainer is a memory mapped file with read and write access
 */
class MutableContainer {
private:
    using bio_mapped_file_t = boost::iostreams::mapped_file;
    using bio_mapped_file_param_t = boost::iostreams::mapped_file_params;
    bio_mapped_file_t mappedFile_{};

public:
    /**
     * @brief create a new container file
     * @param fileName container file name
     */
    void create(const std::string &dir, const internal_file_name_t &fileName) {
        using namespace std::filesystem;
        path filePath = dir + to_string(fileName);
        if (exists(filePath)) {
            throw DedupException(BOOST_CURRENT_LOCATION, "container file already exists");
        }
        auto params = bio_mapped_file_param_t{filePath};
        params.new_file_size = config::CONTAINER_SIZE;
        params.flags = bio_mapped_file_t::mapmode::readwrite;
        if (mappedFile_.is_open()) {
            mappedFile_.close();
        }
        mappedFile_.open(params);
        if (!mappedFile_.is_open()) {
            throw DedupException(BOOST_CURRENT_LOCATION, "fail to create the mapped file");
        }
    }

    /**
     * @brief Create an empty container instance which associated to nothing
     */
    explicit MutableContainer() = default;

    /**
     * @brief getShareIndex the memory region
     */
    mutable_bytes_view region() {
        return {reinterpret_cast<std::byte *>(mappedFile_.data()), mappedFile_.size()};
    }
    bytes_view region() const {
        return {reinterpret_cast<const std::byte *>(mappedFile_.const_data()), mappedFile_.size()};
    }

    /**
     * @brief getShareIndex the size of the memory region, with is also the size of the mapped file
     */
    auto size() const {
        return mappedFile_.size();
    }
};

/**
 * @brief a @e Container is a memory mapped file with read only access
 */
class Container {
private:
    using bio_mapped_file_t = boost::iostreams::mapped_file_source;
    using bio_mapped_file_param_t = boost::iostreams::mapped_file_params;
    bio_mapped_file_t mappedFile_{};

public:
    /**
     * @brief open an existing container
     * @param fileName container name
     */
    explicit Container(const internal_file_name_t &fileName) {
        using namespace std::filesystem;
        path filePath = config::GetContianerDir() + to_string(fileName);
        if (!exists(filePath)) {
            throw DedupException(BOOST_CURRENT_LOCATION, "container file not exists");
        }
        auto params = bio_mapped_file_param_t{filePath};
        params.flags = bio_mapped_file_t::mapmode::readonly;
        try {
            mappedFile_.open(params);
        } catch (std::exception &e) {
            throw DedupException(BOOST_CURRENT_LOCATION, e.what());
        }
        if (!mappedFile_.is_open()) {
            throw DedupException(BOOST_CURRENT_LOCATION, "fail to open the mapped file");
        }
    };

    /**
     * @brief getShareIndex the memory region
     */
    bytes_view region() const {
        return {reinterpret_cast<const std::byte *>(mappedFile_.data()), mappedFile_.size()};
    }

    /**
     * @brief getShareIndex the size of the memory region, with is also the size of the mapped file
     */
    auto size() const {
        return mappedFile_.size();
    }
};

} // namespace dedup

#endif //DEDUP_SERVER_CONTAINER_HPP
