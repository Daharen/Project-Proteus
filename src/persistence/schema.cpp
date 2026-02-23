#include "proteus/persistence/schema.hpp"

#include <stdexcept>
#include <string>

namespace proteus::persistence {

namespace {

void create_tables_v4(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS proposal_registry ("
        "proposal_id TEXT PRIMARY KEY,"
        "domain TEXT,"
        "proposal_json TEXT,"
        "source TEXT,"
        "created_at INTEGER"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS prompt_cache ("
        "prompt_hash TEXT PRIMARY KEY,"
        "domain TEXT NOT NULL,"
        "canonical_prompt TEXT NOT NULL,"
        "model_id TEXT,"
        "policy_version TEXT,"
        "created_at INTEGER NOT NULL,"
        "last_used_at INTEGER NOT NULL,"
        "hit_count INTEGER NOT NULL DEFAULT 0"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS prompt_candidates("
        "prompt_hash TEXT NOT NULL,"
        "proposal_id TEXT NOT NULL,"
        "weight REAL DEFAULT 1.0,"
        "created_at INTEGER NOT NULL,"
        "PRIMARY KEY(prompt_hash, proposal_id),"
        "FOREIGN KEY(prompt_hash) REFERENCES prompt_cache(prompt_hash) ON DELETE CASCADE,"
        "FOREIGN KEY(proposal_id) REFERENCES proposal_registry(proposal_id) ON DELETE CASCADE"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS interaction_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "session_id TEXT,"
        "prompt_hash TEXT,"
        "player_context_json TEXT,"
        "chosen_arm TEXT,"
        "novelty_flag INTEGER,"
        "reward_signal REAL,"
        "reward_applied INTEGER NOT NULL DEFAULT 0,"
        "selection_seed INTEGER,"
        "decision_features_json TEXT,"
        "timestamp INTEGER"
        ");"
    );


    db.exec(
        "CREATE TABLE IF NOT EXISTS proposal_stats("
        "proposal_id TEXT PRIMARY KEY,"
        "shown_count INTEGER NOT NULL DEFAULT 0,"
        "reward_sum REAL NOT NULL DEFAULT 0.0,"
        "reward_count INTEGER NOT NULL DEFAULT 0,"
        "last_shown_at INTEGER"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS bandit_state("
        "key TEXT PRIMARY KEY,"
        "value_json TEXT NOT NULL,"
        "updated_at INTEGER NOT NULL"
        ");"
    );

    db.exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
}

void rebuild_to_v4(SqliteDb& db) {
    db.exec("DROP TABLE IF EXISTS prompt_candidates;");
    db.exec("DROP TABLE IF EXISTS interaction_log;");
    db.exec("DROP TABLE IF EXISTS prompt_cache;");
    db.exec("DROP TABLE IF EXISTS proposal_registry;");
    db.exec("DROP TABLE IF EXISTS proposal_stats;");
    db.exec("DROP TABLE IF EXISTS bandit_state;");
    db.exec("DROP TABLE IF EXISTS meta;");
    create_tables_v4(db);
    auto insert_stmt = db.prepare("INSERT INTO meta(key, value) VALUES(?1, ?2);");
    insert_stmt.bind_text(1, "schema_version");
    insert_stmt.bind_text(2, std::to_string(kSchemaVersion));
    insert_stmt.step();
}

}  // namespace

void ensure_schema(SqliteDb& db) {
    create_tables_v4(db);

    auto stmt = db.prepare("SELECT value FROM meta WHERE key = ?1;");
    stmt.bind_text(1, "schema_version");

    std::string schema_value;
    if (stmt.step()) {
        schema_value = stmt.column_text(0);
    }

    if (schema_value.empty()) {
        auto insert_stmt = db.prepare("INSERT INTO meta(key, value) VALUES(?1, ?2);");
        insert_stmt.bind_text(1, "schema_version");
        insert_stmt.bind_text(2, std::to_string(kSchemaVersion));
        insert_stmt.step();
        return;
    }

    const auto actual = std::stoi(schema_value);
    if (actual < kSchemaVersion) {
        rebuild_to_v4(db);
        return;
    }

    if (actual != kSchemaVersion) {
        throw std::runtime_error("Schema version mismatch. Migrations are not implemented yet.");
    }
}

}  // namespace proteus::persistence
