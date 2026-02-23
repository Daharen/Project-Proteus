#include "proteus/playable/prompt_cache.hpp"

#include <stdexcept>

namespace proteus::playable {

std::optional<PromptCacheRecord> find_prompt_cache(persistence::SqliteDb& db, const std::string& prompt_hash) {
    auto stmt = db.prepare(
        "SELECT prompt_hash, domain, canonical_prompt, proposal_id, model_id, policy_version, created_at, last_used_at, hit_count "
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
    record.proposal_id = stmt.column_text(3);
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
        "prompt_hash, domain, canonical_prompt, proposal_id, model_id, policy_version, created_at, last_used_at, hit_count"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);"
    );

    stmt.bind_text(1, record.prompt_hash);
    stmt.bind_text(2, record.domain);
    stmt.bind_text(3, record.canonical_prompt);
    stmt.bind_text(4, record.proposal_id);
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

bool upsert_proposal_registry(
    persistence::SqliteDb& db,
    const std::string& proposal_id,
    const std::string& domain,
    const nlohmann::json& proposal_json,
    const std::string& source,
    std::int64_t created_at
) {
    auto check = db.prepare("SELECT proposal_json FROM proposal_registry WHERE proposal_id = ?1;");
    check.bind_text(1, proposal_id);
    if (check.step()) {
        return false;
    }

    auto insert = db.prepare(
        "INSERT INTO proposal_registry(proposal_id, domain, proposal_json, source, created_at) "
        "VALUES(?1, ?2, ?3, ?4, ?5);"
    );
    insert.bind_text(1, proposal_id);
    insert.bind_text(2, domain);
    insert.bind_text(3, proposal_json.dump());
    insert.bind_text(4, source);
    insert.bind_int64(5, created_at);
    insert.step();
    return true;
}

nlohmann::json load_proposal_json(persistence::SqliteDb& db, const std::string& proposal_id) {
    auto stmt = db.prepare("SELECT proposal_json FROM proposal_registry WHERE proposal_id = ?1;");
    stmt.bind_text(1, proposal_id);
    if (!stmt.step()) {
        throw std::runtime_error("proposal_id missing from proposal_registry: " + proposal_id);
    }
    return nlohmann::json::parse(stmt.column_text(0));
}

void insert_interaction_log(persistence::SqliteDb& db, const InteractionLogRecord& record) {
    auto stmt = db.prepare(
        "INSERT INTO interaction_log("
        "session_id, prompt_hash, player_context_json, chosen_arm, novelty_flag, reward_signal, timestamp"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);"
    );
    stmt.bind_text(1, record.session_id);
    stmt.bind_text(2, record.prompt_hash);
    stmt.bind_text(3, record.player_context_json);
    stmt.bind_text(4, record.chosen_arm);
    stmt.bind_int64(5, record.novelty_flag);
    if (record.reward_is_null) {
        stmt.bind_null(6);
    } else {
        stmt.bind_double(6, record.reward_signal);
    }
    stmt.bind_int64(7, record.timestamp);
    stmt.step();
}

void log_reward_interaction(persistence::SqliteDb& db, const std::string& session_id, const std::string& proposal_id, double reward_value) {
    auto stmt = db.prepare(
        "UPDATE interaction_log "
        "SET reward_signal = ?1 "
        "WHERE id = ("
        "SELECT id FROM interaction_log WHERE session_id = ?2 AND chosen_arm = ?3 ORDER BY id DESC LIMIT 1"
        ");"
    );
    stmt.bind_double(1, reward_value);
    stmt.bind_text(2, session_id);
    stmt.bind_text(3, proposal_id);
    stmt.step();
}

std::int64_t count_proposal_registry_rows(persistence::SqliteDb& db, const std::string& proposal_id) {
    auto stmt = db.prepare("SELECT COUNT(*) FROM proposal_registry WHERE proposal_id = ?1;");
    stmt.bind_text(1, proposal_id);
    stmt.step();
    return stmt.column_int64(0);
}

std::optional<InteractionLogRecord> latest_interaction_for_session_and_arm(
    persistence::SqliteDb& db,
    const std::string& session_id,
    const std::string& proposal_id
) {
    auto stmt = db.prepare(
        "SELECT id, session_id, prompt_hash, player_context_json, chosen_arm, novelty_flag, reward_signal, timestamp "
        "FROM interaction_log WHERE session_id = ?1 AND chosen_arm = ?2 ORDER BY id DESC LIMIT 1;"
    );
    stmt.bind_text(1, session_id);
    stmt.bind_text(2, proposal_id);
    if (!stmt.step()) {
        return std::nullopt;
    }

    InteractionLogRecord out;
    out.id = stmt.column_int64(0);
    out.session_id = stmt.column_text(1);
    out.prompt_hash = stmt.column_text(2);
    out.player_context_json = stmt.column_text(3);
    out.chosen_arm = stmt.column_text(4);
    out.novelty_flag = static_cast<int>(stmt.column_int64(5));
    out.reward_is_null = stmt.column_is_null(6);
    out.reward_signal = out.reward_is_null ? 0.0 : stmt.column_double(6);
    out.timestamp = stmt.column_int64(7);
    return out;
}

}  // namespace proteus::playable
