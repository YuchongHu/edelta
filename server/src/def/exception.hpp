//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_EXCEPTION_HPP
#define DEDUP_SERVER_EXCEPTION_HPP

#include <string>
#include <vector>

#include "third_party/boost_source_location.hpp"

namespace dedup {
/**
 * @brief DedupException reports runtime errors that are related to dedup system
 */
class DedupException : public std::exception {
private:
    std::string msg_{};
    std::vector<std::pair<std::string, std::string>> description_{};
    std::vector<std::string> stackTrace_{};
    mutable std::string what_{};

public:
    DedupException() = delete;

    DedupException(const boost::source_location &sourceLocation, std::string msg,
                   std::initializer_list<std::pair<std::string, std::string>> desc = {})
        : std::exception(), msg_(std::move(msg)) {
        descriptionRegister(desc);
        stackRegister(sourceLocation);
    }

    void stackRegister(const boost::source_location &sourceLocation) {
        stackTrace_.emplace_back(sourceLocation.to_string());
    }

    void descriptionRegister(std::pair<std::string, std::string> desc) {
        description_.push_back(std::move(desc));
    }

    void descriptionRegister(std::initializer_list<std::pair<std::string, std::string>> desc) {
        std::copy(
            desc.begin(), desc.end(),
            std::insert_iterator<std::vector<std::pair<std::string, std::string>>>(description_, description_.end()));
    }

    [[nodiscard]] const char *what() const noexcept override {
        what_ = {"[DedupException] " + msg_ + '\n'};
        for (const auto &item : description_) {
            what_ += '\t' + item.first + ": " + item.second + '\n';
        }
        what_ += "\tAt\n";
        for (const auto &item : stackTrace_) {
            what_ += "\t\t" + item + '\n';
        }
        return what_.c_str();
    }
};
} // namespace dedup

#endif //DEDUP_SERVER_EXCEPTION_HPP
