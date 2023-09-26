//
// Created by Lokyin Zhao on 2022/10/14.
//

#ifndef DEDUP_SERVER_INITIALIZER_HPP
#define DEDUP_SERVER_INITIALIZER_HPP

#include <string>

#include "sockpp/socket.h"

#include "backend/db_wrapper.hpp"
#include "def/benchmark.hpp"
#include "def/config.hpp"
#include "def/util.hpp"
#include "dedup/delta.hpp"
#include "third_party/crypto_primitive.hpp"

namespace dedup {
/**
 * @brief RAII class to initialize the dedup runtime environment
 * @note Before using any interface in the dedup namespace,
 * a Initializer object should be declared to perform initialization operations
 * and ensure that its lifetime spans the program runtime
 * (usually the best practice is to declare it in the main function)
 */
class Initializer {
private:
    sockpp::socket_initializer socketInitializer{};

public:
    Initializer() = delete;

    Initializer(const Initializer &) = delete;

    Initializer(Initializer &&) = delete;

    Initializer &operator=(const Initializer &) = delete;

    Initializer &operator=(Initializer &&) = delete;

    explicit Initializer(int index, const std::string &config = "./config.json") noexcept {
        // the config should be loaded first
        config::Load(config, index);

        dedup::DirInit(config::GetDirClear());
        DataBase::Init();
        Benchmark::Init();
        Delta::Init();

        try {
            crypto_primitive::CryptoPrimitive::OpensslLockSetup();
        } catch (std::exception &e) {
            std::cerr << log::ERROR << "exception occurs when setting up open ssl lock: " << e.what() << std::endl;
            exit(-1);
        }
    }

    ~Initializer() {
        crypto_primitive::CryptoPrimitive::OpensslLockCleanup();
    }
};
} // namespace dedup

#endif //DEDUP_SERVER_INITIALIZER_HPP
