#pragma once

#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/proposal.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace proteus::playable {

struct PromptCacheRecord {
    std::string prompt_hash;
    std::string domain;
    std::string canonical_prompt;
    std::string model_id;
    std::string policy_version;
    std::int64_t created_at = 0;
    std::int64_t last_used_at = 0;
    std::int64_t hit_count = 0;
};

struct PromptCandidateRecord {
    std::string prompt_hash;
    std::string proposal_id;
    double weight = 1.0;
    std::int64_t created_at = 0;
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
    int reward_applied = 0;
    std::int64_t selection_seed = 0;
    std::string decision_features_json;
    std::string stable_player_id;
    double base_score = 0.0;
    double topology_modifier = 0.0;
    double final_score = 0.0;
    std::int64_t timestamp = 0;
};

struct ProposalStatsRecord {
    std::string proposal_id;
    std::int64_t shown_count = 0;
    double reward_sum = 0.0;
    std::int64_t reward_count = 0;
    std::int64_t last_shown_at = 0;
    bool last_shown_is_null = true;
};

std::optional<PromptCacheRecord> find_prompt_cache(persistence::SqliteDb& db, const std::string& prompt_hash);
void insert_prompt_cache(persistence::SqliteDb& db, const PromptCacheRecord& record);
void mark_prompt_cache_hit(persistence::SqliteDb& db, const std::string& prompt_hash, std::int64_t timestamp);

void insert_prompt_candidate(persistence::SqliteDb& db, const PromptCandidateRecord& record);
std::vector<std::string> list_prompt_candidate_ids(persistence::SqliteDb& db, const std::string& prompt_hash);
void remove_prompt_candidate(persistence::SqliteDb& db, const std::string& prompt_hash, const std::string& proposal_id);

int get_prompt_regen_count(persistence::SqliteDb& db, const std::string& prompt_hash);
int increment_prompt_regen_count(persistence::SqliteDb& db, const std::string& prompt_hash);

bool upsert_proposal_registry(
    persistence::SqliteDb& db,
    const std::string& proposal_id,
    const std::string& domain,
    const nlohmann::json& proposal_json,
    const std::string& source,
    std::int64_t created_at
);

Proposal load_proposal(persistence::SqliteDb& db, const std::string& proposal_id);

void insert_interaction_log(persistence::SqliteDb& db, const InteractionLogRecord& record);
bool log_reward_interaction_once(persistence::SqliteDb& db, const std::string& session_id, const std::string& proposal_id, double reward_value);

void update_proposal_stats_on_show(persistence::SqliteDb& db, const std::string& proposal_id, std::int64_t timestamp);
void update_proposal_stats_on_reward(persistence::SqliteDb& db, const std::string& proposal_id, double reward_value);
std::optional<ProposalStatsRecord> get_proposal_stats(persistence::SqliteDb& db, const std::string& proposal_id);

std::int64_t count_proposal_registry_rows(persistence::SqliteDb& db, const std::string& proposal_id);
std::optional<InteractionLogRecord> latest_interaction_for_session_and_arm(
    persistence::SqliteDb& db,
    const std::string& session_id,
    const std::string& proposal_id
);

}  // namespace proteus::playable
