#pragma once

#include "proteus/persistence/sqlite_db.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace proteus::playable {

struct PromptCacheRecord {
    std::string prompt_hash;
    std::string domain;
    std::string canonical_prompt;
    std::string proposal_id;
    std::string model_id;
    std::string policy_version;
    std::int64_t created_at = 0;
    std::int64_t last_used_at = 0;
    std::int64_t hit_count = 0;
};

struct InteractionLogRecord {
    std::int64_t id = 0;
    std::string session_id;
    std::string prompt_hash;
    std::string player_context_json;
    std::string chosen_arm;
    int novelty_flag = 0;
    double reward_signal = 0.0;
    bool reward_is_null = true;
    std::int64_t timestamp = 0;
};

std::optional<PromptCacheRecord> find_prompt_cache(persistence::SqliteDb& db, const std::string& prompt_hash);
void insert_prompt_cache(persistence::SqliteDb& db, const PromptCacheRecord& record);
void mark_prompt_cache_hit(persistence::SqliteDb& db, const std::string& prompt_hash, std::int64_t timestamp);

bool upsert_proposal_registry(
    persistence::SqliteDb& db,
    const std::string& proposal_id,
    const std::string& domain,
    const nlohmann::json& proposal_json,
    const std::string& source,
    std::int64_t created_at
);

nlohmann::json load_proposal_json(persistence::SqliteDb& db, const std::string& proposal_id);

void insert_interaction_log(persistence::SqliteDb& db, const InteractionLogRecord& record);
void log_reward_interaction(persistence::SqliteDb& db, const std::string& session_id, const std::string& proposal_id, double reward_value);

std::int64_t count_proposal_registry_rows(persistence::SqliteDb& db, const std::string& proposal_id);
std::optional<InteractionLogRecord> latest_interaction_for_session_and_arm(
    persistence::SqliteDb& db,
    const std::string& session_id,
    const std::string& proposal_id
);

}  // namespace proteus::playable
