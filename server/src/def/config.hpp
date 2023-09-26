//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_CONFIG_HPP
#define DEDUP_SERVER_CONFIG_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <netinet/in.h>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <chrono>
#include <vector>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "sockpp/inet_address.h"

#include "def/exception.hpp"
#include "def/log.hpp"

namespace dedup {
class config {
private:
    /**
     * @brief default config in json format. \n
     * {\n
     * "cluster": [\n
     * {"ip": "0.0.0.0", "port": "6000"},\n
     * {"ip": "0.0.0.0", "port": "6001"},\n
     * {"ip": "0.0.0.0", "port": "6002"},\n
     * {"ip": "0.0.0.0", "port": "6003"}\n
     * ],\n
     * "database dir": "./meta/DedupDB/",\n
     * "container dir": "./meta/Container/",\n
     * "clean": true\n
     * }\n
     */
    static constexpr std::string_view DEFAULT_CONFIG = {"{\n"
                                                        "  \"cluster\": [\n"
                                                        "    {\"ip\": \"0.0.0.0\", \"port\": \"6000\"},\n"
                                                        "    {\"ip\": \"0.0.0.0\", \"port\": \"6001\"},\n"
                                                        "    {\"ip\": \"0.0.0.0\", \"port\": \"6002\"},\n"
                                                        "    {\"ip\": \"0.0.0.0\", \"port\": \"6003\"}\n"
                                                        "  ],\n"
                                                        "  \"database dir\": \"./meta/DedupDB/\",\n"
                                                        "  \"container dir\": \"./meta/Container/\",\n"
                                                        "  \"clean\": true\n"
                                                        "}"};
    /*
     * The default configuration used when the relevant configuration is not available
     * (e.g. when the configuration file is not specified or the configuration parameters are wrong)
     */
    static constexpr int DEFAULT_WORK_THREAD_NUM_{6};
    static constexpr bool DEFAULT_CLEAR_DIR_{true};
    static constexpr std::string_view DEFAULT_DB_DIR_{"./meta/DedupDB/"};
    static constexpr std::string_view DEFAULT_CONTAINER_DIR_{"./meta/Container/"};

    /* dynamic switch options, defined at run time */
    /// whether to clear the directory if it exists, default to true
    inline static bool clearDir_;
    /// database file directory, default to './meta/DedupDB/'
    inline static std::string dbDir_;
    /// container file directory
    inline static std::string containerDir_;
    /// recipe file directory
    inline static std::string recipeDir_;
    /// number of the working thread, default to DEFAULT_WORK_THREAD_NUM_
    inline static int workThreadNum_;
    /// addresses of server clusters
    inline static std::vector<sockpp::inet_address> clusterAddress_;
    /// the addresses index of this server node in the config file
    inline static std::size_t selfIndex_;

    /**
     * @brief parse the configuration form ptree
     * @param ptree configuration ptree, read from json
     * @param index the addresses index of this server node in the config file
     */
    static void ParseConfig(const boost::property_tree::ptree &ptree, int index) noexcept {
        try {
            // read the address
            std::for_each(ptree.get_child("cluster").begin(), ptree.get_child("cluster").end(),
                          [](const boost::property_tree::ptree::value_type &value) {
                              auto ip = value.second.get<std::string>("ip");
                              auto port = value.second.get<in_port_t>("port");
                              clusterAddress_.emplace_back(ip, port);
                          });
            if (index <= 0 || index > clusterAddress_.size()) {
                std::cerr << log::ERROR
                          << log::FormatLog("the addresses index of this server node in the config file is invalid",
                                            {
                                                {"index",            std::to_string(index)                 },
                                                {"server nodes num", std::to_string(clusterAddress_.size())}
                })
                          << std::endl;
                exit(-1);
            }
            selfIndex_ = index - 1;

            // read directory options
            clearDir_ = ptree.get<bool>("clean", DEFAULT_CLEAR_DIR_);
            dbDir_ = ptree.get<std::string>("database dir", std::string{DEFAULT_DB_DIR_});
            containerDir_ = ptree.get<std::string>("container dir", std::string{DEFAULT_CONTAINER_DIR_});

            // load the working thread number, default to hardware concurrency,
            // or DEFAULT_WORK_THREAD_NUM_(6) if hardware concurrency is not available,
            workThreadNum_ =
                std::thread::hardware_concurrency() == 0
                    ? DEFAULT_WORK_THREAD_NUM_
                    : boost::numeric_cast<decltype(workThreadNum_)>(
                          std::thread::hardware_concurrency()); // NOLINT(cppcoreguidelines-narrowing-conversions)
        } catch (boost::property_tree::ptree_error &exception) {
            std::cerr << log::ERROR << "exception occurs when loading config:" << exception.what() << '\n'
                      << "proper config format:\n"
                      << DEFAULT_CONFIG << std::endl;
            exit(-1);
        } catch (std::exception &exception) {
            std::cerr << log::ERROR << "exception occurs when loading config: " << exception.what() << '\n'
                      << "proper config format:\n"
                      << DEFAULT_CONFIG << std::endl;
            exit(-1);
        }
    }

public:
    static void Load(const std::string &configFileName, int index) noexcept {
        // make sure the load can be invoked only once
        static std::once_flag onceFlag;
        try {
            std::call_once(onceFlag, [&configFileName, index]() {
                using namespace boost::property_tree;
                ptree ptree{};
                std::filesystem::path configPath{configFileName};

                // try to read the config file, and if fail to read, load the default config
                try {
                    read_json(configFileName, ptree);
                    ParseConfig(ptree, index);
                } catch (json_parser_error &e) {
                    // fail to read the config file, loading the default config
                    std::cout << log::WARNING << "error on parsing config json: " << e.what() << '\n'
                              << log::INFO << "loading the default config:\n"
                              << DEFAULT_CONFIG << std::endl;
                    // read the default config
                    std::stringstream defaultConfigStream{std::string{DEFAULT_CONFIG}};
                    read_json(defaultConfigStream, ptree);
                    ParseConfig(ptree, index);
                    return;
                }
            });
        } catch (std::exception &exception) {
            std::cerr << log::ERROR << "exception occurs when loading config: " << exception.what() << std::endl;
            exit(-1);
        }
    }

    static const std::string_view &GetDefaultConfigStr() {
        return DEFAULT_CONFIG;
    }

    static sockpp::inet_address GetAddress() {
        return GetAddress(selfIndex_);
    }

    static sockpp::inet_address GetAddress(std::size_t index) {
        try {
            return clusterAddress_.at(index);
        } catch (std::out_of_range &e) {
            throw DedupException(BOOST_CURRENT_LOCATION, "peer index is out of range");
        }
    }

    static std::size_t GetPeerNum() {
        return clusterAddress_.size() - 1;
    }

    static int GetWorkThreadNum() {
        return workThreadNum_;
    }

    static bool GetDirClear() {
        return clearDir_;
    }

    static const std::string &GetDBDir() {
        return dbDir_;
    }
    
    static const std::string &GetContianerDir(){
        return containerDir_;
    }

    /* static switch options, defined at compile time */
    /// debug option: force DedupCore to execute PeerInterface locally
    static constexpr bool FORCE_LOCAL{true};
    /// do some aggressive checking
    static constexpr bool PARANOID_CHECK{false};
    /// perform operations on shares in parallel
    static constexpr bool LOOP_PARALLEL{false};

    /// total number of connections to peers in the peer connection pool
    static constexpr int MAX_CONN_NUM{FORCE_LOCAL ? 0 : 200};
    /// queue size for server acceptor
    static constexpr int ACC_QUEUE_SIZE{20};
    /// default size for the data buffer
    static constexpr int32_t DATA_BUFFER_LEN{4 << 20};
    /// default size fot the meta data buffer
    static constexpr int32_t META_BUFFER_LEN{2 << 20};
    /// default size for the status list buffer
    static constexpr int32_t STAT_BUFFER_LEN{2 << 20};
    /// default size for the share file buffer
    static constexpr int32_t SHARE_FILE_BUFFER_LEN{4 << 20};
    /// size of the fingerprint with the use of SHA-256 CryptoPrimitive instance
    static constexpr int32_t FP_SIZE{32};
    /// size of the key
    static constexpr int32_t KEY_SIZE{FP_SIZE + 1};

    /*config for LevelDB option settings*/
    static constexpr int32_t MEM_TABLE_SIZE{512 << 20};
    static constexpr int32_t BLOCK_CACHE_SIZE{1 << 30};
    static constexpr int32_t BLOOM_FILTER_KEY_BITS{20};
    static constexpr int32_t BATCH_SIZE{512};

    /* config for container */
    static constexpr std::size_t CONTAINER_SIZE{256 << 10};
    static constexpr std::size_t INTERNAL_FILE_NAME_SIZE{16};
    static constexpr std::size_t CONTAINER_CACHE_SIZE{1024 * 32};

    /* config for recipe cache */
    static constexpr std::size_t RECIPE_CACHE_SIZE{3};
    
    /* config for delta compress */
    static constexpr std::uint8_t MAX_DELTA_DEPTH{1};
    
    /* config for benchmark */
    /// file name for benchmark log
    static constexpr std::string_view BENCHMARK_LOG_NAME{"benchmark-log"};
    /// interval for benchmark log
    static constexpr std::chrono::hours BENCHMARK_LOG_INTERVAL{1};
};
} // namespace dedup

#endif // DEDUP_SERVER_CONFIG_HPP
