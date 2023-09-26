//
// Created by lokyin on 22-10-13.
//

#ifndef DEDUP_SERVER_LOG_HPP
#define DEDUP_SERVER_LOG_HPP

#include <iostream>
#include <string>

namespace dedup::log {
constexpr inline std::string_view INFO{"[Info]: "};
constexpr inline std::string_view WARNING{"[Warning]: "};
constexpr inline std::string_view ERROR{"[Error]: "};
constexpr inline std::string_view Debug{"[Debug]: "};

inline std::string FormatLog(std::string msg, std::initializer_list<std::pair<std::string, std::string>> misc = {}) {
    std::string ret{std::move(msg) + '\n'};
    for (auto &item : misc) {
        ret += '\t' + item.first + ": " + item.second + '\n';
    }
    return ret;
}

inline std::string FormatLog(const boost::source_location &sourceLocation, std::string msg,
                             std::initializer_list<std::pair<std::string, std::string>> misc = {}) {
    auto ret = FormatLog(std::move(msg), misc);
    ret += "\tAt:\n"
           "\t\t" +
           sourceLocation.to_string() + '\n';
    return ret;
}

} // namespace dedup::log

#endif //DEDUP_SERVER_LOG_HPP
