#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace proteus::playable {

struct ValidationReport {
    bool ok = true;
    std::vector<std::string> issues;
};

ValidationReport validate_proposal_json(const nlohmann::json& proposal_json);

}  // namespace proteus::playable
