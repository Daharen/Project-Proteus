#pragma once

#include "proteus/bandits/contextual_bandit.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/proposal_selector.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace proteus::playable {

inline constexpr const char* kPlayableCorePolicyVersion = "playable_core_v1";

struct RetrievalRequest {
    std::string domain;
    std::string raw_prompt;
    std::string session_id;
    bandits::PlayerContext player_context{};
    std::string policy_version = kPlayableCorePolicyVersion;
};

std::string compute_prompt_hash(
    const std::string& policy_version,
    const std::string& domain,
    const std::string& canonical_prompt
);

nlohmann::json run_retrieval(
    persistence::SqliteDb& db,
    const RetrievalRequest& request,
    const ProposalSelector& selector
);

void log_reward(
    persistence::SqliteDb& db,
    const std::string& session_id,
    const std::string& proposal_id,
    double reward_value
);

}  // namespace proteus::playable
