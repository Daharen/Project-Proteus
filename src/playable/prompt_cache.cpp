#include "proteus/playable/prompt_cache.hpp"

#include <stdexcept>

namespace proteus::playable {

std::optional<PromptCacheRecord> find_prompt_cache(persistence::SqliteDb& db, const std::string& prompt_hash) {
    auto stmt = db.prepare(
        "SELECT prompt_hash, domain, canonical_prompt, model_id, policy_version, created_at, last_used_at, hit_count "
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
    record.model_id = stmt.column_is_null(3) ? std::string{} : stmt.column_text(3);
    record.policy_version = stmt.column_is_null(4) ? std::string{} : stmt.column_text(4);
    record.created_at = stmt.column_int64(5);
    record.last_used_at = stmt.column_int64(6);
    record.hit_count = stmt.column_int64(7);
    return record;
}

void insert_prompt_cache(persistence::SqliteDb& db, const PromptCacheRecord& record) {
    auto stmt = db.prepare(
        "INSERT INTO prompt_cache("
        "prompt_hash, domain, canonical_prompt, model_id, policy_version, created_at, last_used_at, hit_count"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);"
    );

    stmt.bind_text(1, record.prompt_hash);
    stmt.bind_text(2, record.domain);
    stmt.bind_text(3, record.canonical_prompt);
    if (record.model_id.empty()) {
        stmt.bind_null(4);
    } else {
        stmt.bind_text(4, record.model_id);
    }
    if (record.policy_version.empty()) {
        stmt.bind_null(5);
    } else {
        stmt.bind_text(5, record.policy_version);
    }
    stmt.bind_int64(6, record.created_at);
    stmt.bind_int64(7, record.last_used_at);
    stmt.bind_int64(8, record.hit_count);
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

void insert_prompt_candidate(persistence::SqliteDb& db, const PromptCandidateRecord& record) {
    auto stmt = db.prepare(
        "INSERT INTO prompt_candidates(prompt_hash, proposal_id, weight, created_at) "
        "VALUES(?1, ?2, ?3, ?4) ON CONFLICT(prompt_hash, proposal_id) DO NOTHING;"
    );
    stmt.bind_text(1, record.prompt_hash);
    stmt.bind_text(2, record.proposal_id);
    stmt.bind_double(3, record.weight);
    stmt.bind_int64(4, record.created_at);
    stmt.step();
}

std::vector<std::string> list_prompt_candidate_ids(persistence::SqliteDb& db, const std::string& prompt_hash) {
    auto stmt = db.prepare(
        "SELECT proposal_id FROM prompt_candidates WHERE prompt_hash = ?1 ORDER BY proposal_id ASC;"
    );
    stmt.bind_text(1, prompt_hash);

    std::vector<std::string> out;
    while (stmt.step()) {
        out.push_back(stmt.column_text(0));
    }
    return out;
}

void remove_prompt_candidate(persistence::SqliteDb& db, const std::string& prompt_hash, const std::string& proposal_id) {
    auto stmt = db.prepare(
        "DELETE FROM prompt_candidates WHERE prompt_hash = ?1 AND proposal_id = ?2;"
    );
    stmt.bind_text(1, prompt_hash);
    stmt.bind_text(2, proposal_id);
    stmt.step();
}

int get_prompt_regen_count(persistence::SqliteDb& db, const std::string& prompt_hash) {
    auto stmt = db.prepare("SELECT regen_count FROM prompt_meta WHERE prompt_hash = ?1;");
    stmt.bind_text(1, prompt_hash);
    if (!stmt.step()) {
        return 0;
    }
    return static_cast<int>(stmt.column_int64(0));
}

int increment_prompt_regen_count(persistence::SqliteDb& db, const std::string& prompt_hash) {
    auto upsert = db.prepare(
        "INSERT INTO prompt_meta(prompt_hash, regen_count) VALUES(?1, 1) "
        "ON CONFLICT(prompt_hash) DO UPDATE SET regen_count = regen_count + 1;"
    );
    upsert.bind_text(1, prompt_hash);
    upsert.step();
    return get_prompt_regen_count(db, prompt_hash);
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

Proposal load_proposal(persistence::SqliteDb& db, const std::string& proposal_id) {
    auto stmt = db.prepare("SELECT domain, proposal_json, source FROM proposal_registry WHERE proposal_id = ?1;");
    stmt.bind_text(1, proposal_id);
    if (!stmt.step()) {
        throw std::runtime_error("proposal_id missing from proposal_registry: " + proposal_id);
    }

    return Proposal{
        .proposal_id = proposal_id,
        .domain = stmt.column_text(0),
        .source = stmt.column_is_null(2) ? std::string{} : stmt.column_text(2),
        .payload = nlohmann::json::parse(stmt.column_text(1)),
    };
}

void insert_interaction_log(persistence::SqliteDb& db, const InteractionLogRecord& record) {
    auto stmt = db.prepare(
        "INSERT INTO interaction_log("
        "session_id, prompt_hash, player_context_json, chosen_arm, novelty_flag, reward_signal, reward_applied, selection_seed, decision_features_json, timestamp"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
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
    stmt.bind_int64(7, record.reward_applied);
    stmt.bind_int64(8, record.selection_seed);
    stmt.bind_text(9, record.decision_features_json);
    stmt.bind_int64(10, record.timestamp);
    stmt.step();
}

bool log_reward_interaction_once(persistence::SqliteDb& db, const std::string& session_id, const std::string& proposal_id, double reward_value) {
    auto stmt = db.prepare(
        "UPDATE interaction_log "
        "SET reward_signal = ?1, reward_applied = 1 "
        "WHERE id = ("
        "SELECT id FROM interaction_log WHERE session_id = ?2 AND chosen_arm = ?3 AND reward_applied = 0 ORDER BY id DESC LIMIT 1"
        ");"
    );
    stmt.bind_double(1, reward_value);
    stmt.bind_text(2, session_id);
    stmt.bind_text(3, proposal_id);
    stmt.step();
    return sqlite3_changes(db.native_handle()) > 0;
}

void update_proposal_stats_on_show(persistence::SqliteDb& db, const std::string& proposal_id, std::int64_t timestamp) {
    auto stmt = db.prepare(
        "INSERT INTO proposal_stats(proposal_id, shown_count, reward_sum, reward_count, last_shown_at) "
        "VALUES(?1, 1, 0.0, 0, ?2) "
        "ON CONFLICT(proposal_id) DO UPDATE SET shown_count = shown_count + 1, last_shown_at = excluded.last_shown_at;"
    );
    stmt.bind_text(1, proposal_id);
    stmt.bind_int64(2, timestamp);
    stmt.step();
}

void update_proposal_stats_on_reward(persistence::SqliteDb& db, const std::string& proposal_id, double reward_value) {
    auto stmt = db.prepare(
        "INSERT INTO proposal_stats(proposal_id, shown_count, reward_sum, reward_count, last_shown_at) "
        "VALUES(?1, 0, ?2, 1, NULL) "
        "ON CONFLICT(proposal_id) DO UPDATE SET reward_sum = reward_sum + excluded.reward_sum, reward_count = reward_count + 1;"
    );
    stmt.bind_text(1, proposal_id);
    stmt.bind_double(2, reward_value);
    stmt.step();
}

std::optional<ProposalStatsRecord> get_proposal_stats(persistence::SqliteDb& db, const std::string& proposal_id) {
    auto stmt = db.prepare(
        "SELECT proposal_id, shown_count, reward_sum, reward_count, last_shown_at "
        "FROM proposal_stats WHERE proposal_id = ?1;"
    );
    stmt.bind_text(1, proposal_id);
    if (!stmt.step()) {
        return std::nullopt;
    }

    ProposalStatsRecord record;
    record.proposal_id = stmt.column_text(0);
    record.shown_count = stmt.column_int64(1);
    record.reward_sum = stmt.column_double(2);
    record.reward_count = stmt.column_int64(3);
    record.last_shown_is_null = stmt.column_is_null(4);
    record.last_shown_at = record.last_shown_is_null ? 0 : stmt.column_int64(4);
    return record;
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
        "SELECT id, session_id, prompt_hash, player_context_json, chosen_arm, novelty_flag, reward_signal, reward_applied, selection_seed, decision_features_json, timestamp "
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
    out.reward_applied = static_cast<int>(stmt.column_int64(7));
    out.selection_seed = stmt.column_int64(8);
    out.decision_features_json = stmt.column_is_null(9) ? std::string{} : stmt.column_text(9);
    out.timestamp = stmt.column_int64(10);
    return out;
}

}  // namespace proteus::playable
