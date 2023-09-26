//
// Created by Lokyin Zhao on 2022/10/13.
//

#ifndef DEDUP_SERVER_SERVER_HPP
#define DEDUP_SERVER_SERVER_HPP

#include "BS_thread_pool.hpp"
#include "sockpp/tcp_acceptor.h"

#include "comm/services.hpp"
#include "dedup/dedup_core.hpp"

namespace dedup {
class Server {
private:
    /// local dedup core
    DedupCore dedupObj_;
    /// server acceptor
    sockpp::tcp_acceptor acc_;
    /// thread pool for sessions
    BS::thread_pool threadPool_{boost::numeric_cast<BS::concurrency_t>(config::GetWorkThreadNum())};

public:
    Server() : acc_(config::GetAddress(), config::ACC_QUEUE_SIZE), dedupObj_() {
        // check acceptor's availability
        if (!acc_) {
            throw DedupException(BOOST_CURRENT_LOCATION, "fail to create a server socket",
                                 {
                                     {"error string", acc_.last_error_str()}
            });
        }

        //set socket options
        int opt = 1;
        if (!acc_.set_option(SOL_SOCKET, SO_REUSEADDR, opt) || !acc_.set_option(SOL_SOCKET, SO_KEEPALIVE, opt)) {
            throw DedupException(BOOST_CURRENT_LOCATION, "fail to set the server socket options",
                                 {
                                     {"error string", acc_.last_error_str()}
            });
        }
    }

    [[noreturn]] void run() {
        std::cout << log::INFO
                  << log::FormatLog("server running",
                                    {
                                        {"address",                 acc_.address().to_string()                    },
                                        {"thread count",            std::to_string(threadPool_.get_thread_count())},
                                        {"local",                   config::FORCE_LOCAL ? "true" : "false"        },
                                        {"db block cache size(MB)", std::to_string(config::BLOCK_CACHE_SIZE >> 20)},
                                        {"db mem table size(MB)",   std::to_string(config::MEM_TABLE_SIZE >> 20)  },
                                        {"db bloom filter bits",    std::to_string(config::BLOOM_FILTER_KEY_BITS) },
                                        {"delta depth",             std::to_string(config::MAX_DELTA_DEPTH)       }
        })
                  << std::flush;
        while (true) {
            auto sock = acc_.accept();
            if (!sock) {
                std::cerr << dedup::log::WARNING
                          << dedup::log::FormatLog(BOOST_CURRENT_LOCATION, "error on accepting incoming connection",
                                                   {
                                                       {"error string", acc_.last_error_str()}
                })
                          << std::flush;
            } else {
                // add the task to the thread pool
                threadPool_.push_task([this, sockHandler = sock.release()] {
                    try {
                        /// socket to the client
                        auto sock = sockpp::tcp_socket{sockHandler};
                        if (!sock) {
                            throw DedupException(BOOST_CURRENT_LOCATION, "error connecting to the client",
                                                 {
                                                     {"error string", sock.last_error_str()}
                            });
                        }
                        ServiceDispatcher::Submit(sock, this->dedupObj_);
                    } catch (DedupException &exception) {
                        std::cerr << exception.what();
                    } catch (std::exception &exception) {
                        std::cerr << log::ERROR << "exception caught " << exception.what()
                                  << "\n\tAt: " << BOOST_CURRENT_LOCATION.to_string() << std::endl;
                    }
                });
            }
        }
    }
};
} // namespace dedup

#endif //DEDUP_SERVER_SERVER_HPP
