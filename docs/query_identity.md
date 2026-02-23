# Query identity and similarity

This project uses deterministic query identity to avoid unbounded raw-query duplication in SQLite.

## Normalization rules

`NormalizeQuery(std::string_view raw)` applies the following steps in order:

1. UTF-8 pass-through: bytes are preserved unless they are ASCII characters covered below.
2. ASCII lowercase: only `A`-`Z` are lowercased.
3. Whitespace collapse: runs of ASCII whitespace are converted to a single ASCII space.
4. Trim: leading and trailing spaces are removed.
5. Punctuation collapse: runs from `.,!?;:"'()[]{}<>/\\|~@#$%^&*+=-_` are treated as separators and replaced by a single space.
6. Whitespace collapse (again): any spaces introduced by punctuation stripping are collapsed and trimmed.

The final normalized string is hashed with FNV-1a 64 (`QueryHash64`). The hash is persisted in SQLite as an `INTEGER` (`int64` two's-complement bit pattern of the unsigned hash).

## Storage model

Schema v7 introduces:

- `query_registry(query_id INTEGER PRIMARY KEY, normalized_text TEXT NOT NULL, raw_example TEXT NOT NULL, hash64 INTEGER NOT NULL UNIQUE, created_at_utc TEXT NOT NULL)`.
- `query_fts` (FTS5) with external content from `query_registry` (`content='query_registry', content_rowid='query_id'`).
- `interaction_log.query_id` as the canonical foreign-key-like link for query identity; `raw_query_text` remains debug-only.

`query_fts` is kept synchronized with deterministic insert/update/delete triggers on `query_registry`.

## Similarity scoring

`FindSimilarQueries` tokenizes the normalized query into an FTS5 MATCH string with per-token prefix matching (`token*`).

SQLite BM25 rank (`bm25(query_fts)`) is converted to similarity in `[0,1]` by:

`similarity = 1 / (1 + max(0, bm25))`

Results are sorted by BM25 ascending in SQLite and filtered by caller-provided `min_score`.
