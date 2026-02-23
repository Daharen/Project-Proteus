#pragma once

#include <string>

namespace proteus::playable {

struct HttpServerConfig {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string db_path = "./proteus.db";
    std::string static_dir = "./web";
    bool dev_mode = false;
    bool self_test_mode = false;
    bool smoke_mode = false;
    bool verbose = false;
};

int run_server(const HttpServerConfig& config);

}  // namespace proteus::playable
