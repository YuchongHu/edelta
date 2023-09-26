
#include <iostream>

#include "comm/server.hpp"
#include "def/config.hpp"
#include "def/initializer.hpp"

void usage() {
    std::cout << "usage: server <index> [config_file]\n"
                 "\tindex_num: the addresses index of this server node in the config file\n"
                 "\tconfig_file: (optional) config file, default to './config.json'\n"
                 "config file format:\n"
              << dedup::config::GetDefaultConfigStr() << std::endl;
    exit(-1);
}

int main(int argc, char *argv[]) {
    try {
        if (argc == 2) {
            int index = std::stoi(argv[1]);
            dedup::Initializer initializer{index};
            dedup::Server{}.run();
        } else if (argc == 3) {
            int index = std::stoi(argv[1]);
            dedup::Initializer initializer{index, argv[2]};
            dedup::Server{}.run();
        } else {
            usage();
        }
    } catch (std::invalid_argument &e) {
        usage();
    }
}
