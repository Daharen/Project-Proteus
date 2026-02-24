#include "proteus/persistence/schema.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
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
    if (table_exists(db, "schema_meta")) {
        auto stmt = db.prepare("SELECT value FROM schema_meta WHERE key = ?1;");
        stmt.bind_text(1, "schema_version");
        if (stmt.step()) {
            const std::string value = stmt.column_text(0);
            if (!value.empty()) {
                return std::stoi(value);
            }
        }
    }

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
    db.exec("CREATE TABLE IF NOT EXISTS schema_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    auto schema_stmt = db.prepare(
        "INSERT INTO schema_meta(key, value) VALUES(?1, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;"
    );
    schema_stmt.bind_text(1, "schema_version");
    schema_stmt.bind_text(2, std::to_string(version));
    schema_stmt.step();

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

void migrate_6_to_7(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS query_registry("
        "query_id INTEGER PRIMARY KEY,"
        "normalized_text TEXT NOT NULL,"
        "raw_example TEXT NOT NULL,"
        "hash64 INTEGER NOT NULL UNIQUE,"
        "created_at_utc TEXT NOT NULL"
        ");"
    );
    db.exec("CREATE INDEX IF NOT EXISTS idx_query_registry_hash64 ON query_registry(hash64);");

    db.exec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS query_fts USING fts5("
        "normalized_text,"
        "content='query_registry',"
        "content_rowid='query_id'"
        ");"
    );

    db.exec(
        "CREATE TRIGGER IF NOT EXISTS query_registry_ai AFTER INSERT ON query_registry BEGIN "
        "INSERT INTO query_fts(rowid, normalized_text) VALUES (new.query_id, new.normalized_text);"
        "END;"
    );
    db.exec(
        "CREATE TRIGGER IF NOT EXISTS query_registry_ad AFTER DELETE ON query_registry BEGIN "
        "INSERT INTO query_fts(query_fts, rowid, normalized_text) VALUES('delete', old.query_id, old.normalized_text);"
        "END;"
    );
    db.exec(
        "CREATE TRIGGER IF NOT EXISTS query_registry_au AFTER UPDATE ON query_registry BEGIN "
        "INSERT INTO query_fts(query_fts, rowid, normalized_text) VALUES('delete', old.query_id, old.normalized_text);"
        "INSERT INTO query_fts(rowid, normalized_text) VALUES (new.query_id, new.normalized_text);"
        "END;"
    );
    db.exec(
        "INSERT INTO query_fts(rowid, normalized_text) "
        "SELECT query_id, normalized_text FROM query_registry "
        "WHERE query_id NOT IN (SELECT rowid FROM query_fts);"
    );

    if (!table_exists(db, "interaction_log")) {
        migrate_2_to_3(db);
    }
    if (!column_exists(db, "interaction_log", "raw_query_text")) {
        db.exec("ALTER TABLE interaction_log ADD COLUMN raw_query_text TEXT;");
    }
    if (!column_exists(db, "interaction_log", "query_id")) {
        db.exec("ALTER TABLE interaction_log ADD COLUMN query_id INTEGER;");
    }
    db.exec("CREATE INDEX IF NOT EXISTS idx_interaction_log_query_id ON interaction_log(query_id);");
}


void migrate_7_to_8(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS schema_meta ("
        "key TEXT PRIMARY KEY,"
        "value TEXT NOT NULL"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS query_player_aggregate ("
        "query_id INTEGER NOT NULL,"
        "stable_player_id TEXT NOT NULL,"
        "total_interactions INTEGER NOT NULL,"
        "total_base_score REAL NOT NULL,"
        "total_final_score REAL NOT NULL,"
        "mean_final_score REAL NOT NULL,"
        "variance_final_score REAL NOT NULL,"
        "last_updated_timestamp INTEGER NOT NULL,"
        "PRIMARY KEY (query_id, stable_player_id)"
        ");"
    );
    db.exec("CREATE INDEX IF NOT EXISTS idx_qpa_player ON query_player_aggregate(stable_player_id);");
    db.exec("CREATE INDEX IF NOT EXISTS idx_qpa_query ON query_player_aggregate(query_id);");

    db.exec(
        "CREATE TABLE IF NOT EXISTS query_cluster_stats ("
        "query_id INTEGER PRIMARY KEY,"
        "distinct_players INTEGER NOT NULL,"
        "total_interactions INTEGER NOT NULL,"
        "mean_final_score REAL NOT NULL,"
        "entropy_score REAL NOT NULL,"
        "divergence_index REAL NOT NULL,"
        "last_recomputed_timestamp INTEGER NOT NULL"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS player_preference_vector ("
        "stable_player_id TEXT PRIMARY KEY,"
        "vector_blob BLOB NOT NULL,"
        "dimensionality INTEGER NOT NULL,"
        "last_recomputed_timestamp INTEGER NOT NULL"
        ");"
    );
}



void migrate_8_to_9(SqliteDb& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS llm_response_cache ("
        "cache_id INTEGER PRIMARY KEY,"
        "created_at_utc TEXT NOT NULL,"
        "provider TEXT NOT NULL,"
        "model TEXT NOT NULL,"
        "schema_name TEXT NOT NULL,"
        "schema_version INTEGER NOT NULL,"
        "prompt_hash BLOB NOT NULL,"
        "request_json TEXT NOT NULL,"
        "response_json TEXT NOT NULL,"
        "response_sha256 BLOB NOT NULL,"
        "raw_response_text TEXT NOT NULL,"
        "raw_response_text_trunc TEXT NOT NULL,"
        "error_code TEXT NULL,"
        "UNIQUE(provider, model, schema_name, schema_version, prompt_hash)"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS query_metadata ("
        "query_id INTEGER PRIMARY KEY,"
        "normalized_query_text TEXT NOT NULL,"
        "synopsis TEXT NOT NULL,"
        "intent_tags_json TEXT NOT NULL,"
        "schema_version INTEGER NOT NULL"
        ");"
    );

    db.exec(
        "CREATE TABLE IF NOT EXISTS query_bootstrap_proposals ("
        "query_id INTEGER NOT NULL,"
        "schema_version INTEGER NOT NULL,"
        "proposal_index INTEGER NOT NULL,"
        "proposal_id TEXT NOT NULL,"
        "proposal_title TEXT NOT NULL,"
        "proposal_body TEXT NOT NULL,"
        "choice_seed_hint TEXT NOT NULL,"
        "risk_profile TEXT NOT NULL,"
        "PRIMARY KEY(query_id, schema_version, proposal_index)"
        ");"
    );
}

void migrate_9_to_10(SqliteDb& db) {
    if (!column_exists(db, "query_registry", "query_domain")) {
        db.exec("ALTER TABLE query_registry ADD COLUMN query_domain INTEGER NOT NULL DEFAULT 0;");
    }
    if (!column_exists(db, "query_registry", "normalized_text_hash64")) {
        db.exec("ALTER TABLE query_registry ADD COLUMN normalized_text_hash64 INTEGER;");
    }
    db.exec("UPDATE query_registry SET normalized_text_hash64 = hash64 WHERE normalized_text_hash64 IS NULL;");
    db.exec("CREATE UNIQUE INDEX IF NOT EXISTS uq_query_registry_domain_hash ON query_registry(query_domain, normalized_text_hash64);");

    if (!column_exists(db, "query_metadata", "query_domain")) {
        db.exec("ALTER TABLE query_metadata ADD COLUMN query_domain INTEGER NOT NULL DEFAULT 0;");
    }
    if (!column_exists(db, "query_metadata", "title")) {
        db.exec("ALTER TABLE query_metadata ADD COLUMN title TEXT NOT NULL DEFAULT ''; ");
    }
    if (!column_exists(db, "query_metadata", "tags_json")) {
        db.exec("ALTER TABLE query_metadata ADD COLUMN tags_json TEXT NOT NULL DEFAULT '[]';");
    }
    db.exec("CREATE UNIQUE INDEX IF NOT EXISTS uq_query_metadata_id_domain ON query_metadata(query_id, query_domain);");

    if (!column_exists(db, "query_bootstrap_proposals", "query_domain")) {
        db.exec("ALTER TABLE query_bootstrap_proposals ADD COLUMN query_domain INTEGER NOT NULL DEFAULT 0;");
    }
    if (!column_exists(db, "query_bootstrap_proposals", "proposal_kind")) {
        db.exec("ALTER TABLE query_bootstrap_proposals ADD COLUMN proposal_kind INTEGER NOT NULL DEFAULT 0;");
    }
    if (!column_exists(db, "query_bootstrap_proposals", "proposal_json")) {
        db.exec("ALTER TABLE query_bootstrap_proposals ADD COLUMN proposal_json TEXT NOT NULL DEFAULT '{}';");
    }

    db.exec(
        "CREATE TABLE IF NOT EXISTS npc_registry ("
        "npc_id BLOB PRIMARY KEY,"
        "npc_name TEXT NOT NULL,"
        "npc_role TEXT NOT NULL,"
        "npc_seed_material TEXT NOT NULL,"
        "created_from_query_id INTEGER NOT NULL"
        ");"
    );
}

void verify_sqlite_capabilities(SqliteDb& db, bool verbose) {
    auto fts_stmt = db.prepare("SELECT sqlite_compileoption_used('ENABLE_FTS5');");
    if (!fts_stmt.step() || fts_stmt.column_int64(0) == 0) {
        throw std::runtime_error("SQLite build missing ENABLE_FTS5 (required)");
    }

    const bool log_opts = verbose || (std::getenv("PROTEUS_LOG_SQLITE_OPTS") != nullptr);
    if (!log_opts) {
        return;
    }

    auto opt_stmt = db.prepare("PRAGMA compile_options;");
    std::cerr << "[sqlite] compile_options:" << std::endl;
    while (opt_stmt.step()) {
        std::cerr << "  - " << opt_stmt.column_text(0) << std::endl;
    }
}
void apply_migration(SqliteDb& db, int from_version) {
    switch (from_version) {
        case 0: migrate_0_to_1(db); return;
        case 1: migrate_1_to_2(db); return;
        case 2: migrate_2_to_3(db); return;
        case 3: migrate_3_to_4(db); return;
        case 4: migrate_4_to_5(db); return;
        case 5: migrate_5_to_6(db); return;
        case 6: migrate_6_to_7(db); return;
        case 7: migrate_7_to_8(db); return;
        case 8: migrate_8_to_9(db); return;
        case 9: migrate_9_to_10(db); return;
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

void open_and_migrate(SqliteDb& db, const std::string& db_path, bool verbose) {
    db.open(db_path);
    verify_sqlite_capabilities(db, verbose);
    ensure_schema(db);
}

}  // namespace proteus::persistence
