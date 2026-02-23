#include "proteus/playable/prompt_cache.hpp"

#include <stdexcept>

namespace proteus::playable {

std::optional<PromptCacheRecord> find_prompt_cache(persistence::SqliteDb& db, const std::string& prompt_hash) {
    auto stmt = db.prepare(
        "SELECT prompt_hash, domain, canonical_prompt, response_json, model_id, policy_version, created_at, last_used_at, hit_count "
        "FROM prompt_cache WHERE prompt_hash = ?1;"
    );
    stmt.bind_text(1, prompt_hash);
    if (!stmt.step()) {
        return std::nullopt;
    }

    PromptCacheRecord record;
    record.prompt_hash = stmt.column_text(0);
    record.domain = stmt.column_text(1);
    record.canonical_prompt = stmt.column_text(2);
    record.response_json = nlohmann::json::parse(stmt.column_text(3));
    record.model_id = stmt.column_is_null(4) ? std::string{} : stmt.column_text(4);
    record.policy_version = stmt.column_is_null(5) ? std::string{} : stmt.column_text(5);
    record.created_at = stmt.column_int64(6);
    record.last_used_at = stmt.column_int64(7);
    record.hit_count = stmt.column_int64(8);

    return record;
}

void insert_prompt_cache(persistence::SqliteDb& db, const PromptCacheRecord& record) {
    auto stmt = db.prepare(
        "INSERT INTO prompt_cache("
        "prompt_hash, domain, canonical_prompt, response_json, model_id, policy_version, created_at, last_used_at, hit_count"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);"
    );

    stmt.bind_text(1, record.prompt_hash);
    stmt.bind_text(2, record.domain);
    stmt.bind_text(3, record.canonical_prompt);
    stmt.bind_text(4, record.response_json.dump());
    if (record.model_id.empty()) {
        stmt.bind_null(5);
    } else {
        stmt.bind_text(5, record.model_id);
    }
    if (record.policy_version.empty()) {
        stmt.bind_null(6);
    } else {
        stmt.bind_text(6, record.policy_version);
    }
    stmt.bind_int64(7, record.created_at);
    stmt.bind_int64(8, record.last_used_at);
    stmt.bind_int64(9, record.hit_count);

    stmt.step();
}

void mark_prompt_cache_hit(persistence::SqliteDb& db, const std::string& prompt_hash, std::int64_t timestamp) {
    auto stmt = db.prepare(
        "UPDATE prompt_cache "
        "SET hit_count = hit_count + 1, last_used_at = ?1 "
        "WHERE prompt_hash = ?2;"
    );
    stmt.bind_int64(1, timestamp);
    stmt.bind_text(2, prompt_hash);
    stmt.step();
}

}  // namespace proteus::playable
