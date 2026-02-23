#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace proteus::playable {

struct Proposal {
    std::string proposal_id;
    std::string domain;
    std::string source;
    nlohmann::json payload;
};

}  // namespace proteus::playable
