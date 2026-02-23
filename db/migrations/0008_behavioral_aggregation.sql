-- Schema Version: 8
-- Purpose: Deterministic behavioral aggregation layer tables

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS query_player_aggregate (
  query_id INTEGER NOT NULL,
  stable_player_id TEXT NOT NULL,

  total_interactions INTEGER NOT NULL,
  total_base_score REAL NOT NULL,
  total_final_score REAL NOT NULL,

  mean_final_score REAL NOT NULL,
  variance_final_score REAL NOT NULL,

  last_updated_timestamp INTEGER NOT NULL,

  PRIMARY KEY (query_id, stable_player_id)
);

CREATE INDEX IF NOT EXISTS idx_qpa_player ON query_player_aggregate (stable_player_id);
CREATE INDEX IF NOT EXISTS idx_qpa_query  ON query_player_aggregate (query_id);

CREATE TABLE IF NOT EXISTS query_cluster_stats (
  query_id INTEGER PRIMARY KEY,

  distinct_players INTEGER NOT NULL,
  total_interactions INTEGER NOT NULL,

  mean_final_score REAL NOT NULL,

  entropy_score REAL NOT NULL,
  divergence_index REAL NOT NULL,

  last_recomputed_timestamp INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS player_preference_vector (
  stable_player_id TEXT PRIMARY KEY,

  vector_blob BLOB NOT NULL,
  dimensionality INTEGER NOT NULL,

  last_recomputed_timestamp INTEGER NOT NULL
);
