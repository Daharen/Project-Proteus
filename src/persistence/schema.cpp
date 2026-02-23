#include "proteus/persistence/schema.hpp"

#include <stdexcept>
#include <string>

namespace proteus::persistence {

namespace {

void create_tables(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS prompt_cache ("
        "prompt_hash TEXT PRIMARY KEY,"
        "domain TEXT NOT NULL,"
        "canonical_prompt TEXT NOT NULL,"
        "response_json TEXT NOT NULL,"
        "model_id TEXT,"
        "policy_version TEXT,"
        "created_at INTEGER NOT NULL,"
        "last_used_at INTEGER NOT NULL,"
        "hit_count INTEGER NOT NULL DEFAULT 0"
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
        "timestamp INTEGER"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS proposal_registry ("
        "proposal_id TEXT PRIMARY KEY,"
        "domain TEXT,"
        "proposal_json TEXT,"
        "source TEXT,"
        "created_at INTEGER"
        ");"
    );

    db.exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
}

void upsert_schema_version(SqliteDb& db) {
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
    if (actual != kSchemaVersion) {
        throw std::runtime_error("Schema version mismatch. Migrations are not implemented yet.");
    }
}

}  // namespace

void ensure_schema(SqliteDb& db) {
    create_tables(db);
    upsert_schema_version(db);
}

}  // namespace proteus::persistence
