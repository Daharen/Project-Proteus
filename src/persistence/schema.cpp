#include "proteus/persistence/schema.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace proteus::persistence {

namespace {

bool table_exists(SqliteDb& db, const std::string& table_name) {
    auto stmt = db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1;");
    stmt.bind_text(1, table_name);
    return stmt.step();
}

bool column_exists(SqliteDb& db, const std::string& table_name, const std::string& column_name) {
    auto stmt = db.prepare("PRAGMA table_info(" + table_name + ");");
    while (stmt.step()) {
        if (stmt.column_text(1) == column_name) {
            return true;
        }
    }
    return false;
}

int current_schema_version(SqliteDb& db) {
    if (!table_exists(db, "meta")) {
        return 0;
    }

    auto stmt = db.prepare("SELECT value FROM meta WHERE key = ?1;");
    stmt.bind_text(1, "schema_version");
    if (!stmt.step()) {
        return 0;
    }

    const std::string value = stmt.column_text(0);
    if (value.empty()) {
        return 0;
    }
    return std::stoi(value);
}

void set_schema_version(SqliteDb& db, int version) {
    db.exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    auto stmt = db.prepare(
        "INSERT INTO meta(key, value) VALUES(?1, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;"
    );
    stmt.bind_text(1, "schema_version");
    stmt.bind_text(2, std::to_string(version));
    stmt.step();
}

void migrate_0_to_1(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS proposal_registry ("
        "proposal_id TEXT PRIMARY KEY,"
        "domain TEXT,"
        "proposal_json TEXT,"
        "source TEXT,"
        "created_at INTEGER"
        ");"
    );
}

void migrate_1_to_2(SqliteDb& db) {
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
        "CREATE TABLE IF NOT EXISTS prompt_meta("
        "prompt_hash TEXT PRIMARY KEY,"
        "regen_count INTEGER NOT NULL DEFAULT 0,"
        "FOREIGN KEY(prompt_hash) REFERENCES prompt_cache(prompt_hash) ON DELETE CASCADE"
        ");"
    );
}

void migrate_2_to_3(SqliteDb& db) {
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
}

void migrate_3_to_4(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS proposal_stats("
        "proposal_id TEXT PRIMARY KEY,"
        "shown_count INTEGER NOT NULL DEFAULT 0,"
        "reward_sum REAL NOT NULL DEFAULT 0.0,"
        "reward_count INTEGER NOT NULL DEFAULT 0,"
        "last_shown_at INTEGER"
        ");"
    );
}

void migrate_4_to_5(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS bandit_state("
        "key TEXT PRIMARY KEY,"
        "value_json TEXT NOT NULL,"
        "updated_at INTEGER NOT NULL"
        ");"
    );
}

void migrate_5_to_6(SqliteDb& db) {
    if (!table_exists(db, "interaction_log")) {
        migrate_2_to_3(db);
    }

    if (!column_exists(db, "interaction_log", "stable_player_id")) {
        db.exec("ALTER TABLE interaction_log ADD COLUMN stable_player_id TEXT;");
    }
    if (!column_exists(db, "interaction_log", "base_score")) {
        db.exec("ALTER TABLE interaction_log ADD COLUMN base_score REAL;");
    }
    if (!column_exists(db, "interaction_log", "topology_modifier")) {
        db.exec("ALTER TABLE interaction_log ADD COLUMN topology_modifier REAL;");
    }
    if (!column_exists(db, "interaction_log", "final_score")) {
        db.exec("ALTER TABLE interaction_log ADD COLUMN final_score REAL;");
    }

    db.exec("CREATE INDEX IF NOT EXISTS idx_interaction_log_stable_player_id ON interaction_log(stable_player_id);");
}

void apply_migration(SqliteDb& db, int from_version) {
    switch (from_version) {
        case 0: migrate_0_to_1(db); return;
        case 1: migrate_1_to_2(db); return;
        case 2: migrate_2_to_3(db); return;
        case 3: migrate_3_to_4(db); return;
        case 4: migrate_4_to_5(db); return;
        case 5: migrate_5_to_6(db); return;
        default: throw std::runtime_error("Unsupported schema version: " + std::to_string(from_version));
    }
}

}  // namespace

void ensure_schema(SqliteDb& db) {
    SqliteTransaction tx(db);

    int version = current_schema_version(db);
    if (version > kSchemaVersion) {
        throw std::runtime_error("Database schema version is newer than supported binary");
    }

    while (version < kSchemaVersion) {
        apply_migration(db, version);
        ++version;
        set_schema_version(db, version);
    }

    tx.commit();
}

void open_and_migrate(SqliteDb& db, const std::string& db_path) {
    db.open(db_path);
    ensure_schema(db);
}

}  // namespace proteus::persistence
