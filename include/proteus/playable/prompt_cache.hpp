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
    nlohmann::json response_json;
    std::string model_id;
    std::string policy_version;
    std::int64_t created_at = 0;
    std::int64_t last_used_at = 0;
    std::int64_t hit_count = 0;
};

std::optional<PromptCacheRecord> find_prompt_cache(persistence::SqliteDb& db, const std::string& prompt_hash);

void insert_prompt_cache(persistence::SqliteDb& db, const PromptCacheRecord& record);

void mark_prompt_cache_hit(persistence::SqliteDb& db, const std::string& prompt_hash, std::int64_t timestamp);

}  // namespace proteus::playable
