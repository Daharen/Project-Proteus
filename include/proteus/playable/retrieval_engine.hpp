#pragma once

#include "proteus/bandits/contextual_bandit.hpp"
#include "proteus/persistence/sqlite_db.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace proteus::playable {

inline constexpr const char* kPlayableCorePolicyVersion = "playable_core_v1";

struct RetrievalRequest {
    std::string domain;
    std::string raw_prompt;
    bandits::PlayerContext player_context{};
    std::string policy_version = kPlayableCorePolicyVersion;
};

std::string compute_prompt_hash(
    const std::string& policy_version,
    const std::string& domain,
    const std::string& canonical_prompt
);

nlohmann::json retrieve_or_generate(persistence::SqliteDb& db, const RetrievalRequest& request);

}  // namespace proteus::playable
