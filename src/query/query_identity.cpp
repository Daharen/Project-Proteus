#include "proteus/query/query_identity.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace proteus::query {
namespace {

bool is_ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool is_punctuation_to_strip(unsigned char c) {
    switch (c) {
        case '.': case ',': case '!': case '?': case ';': case ':':
        case '"': case '\'': case '(': case ')': case '[': case ']':
        case '{': case '}': case '<': case '>': case '/': case '\\':
        case '|': case '~': case '@': case '#': case '$': case '%':
        case '^': case '&': case '*': case '+': case '=': case '-':
        case '_':
            return true;
        default:
            return false;
    }
}

std::string fts_match_query_from_normalized(const std::string& normalized) {
    std::string out;
    bool in_token = false;
    for (char c : normalized) {
        if (c == ' ') {
            if (in_token) {
                out += "* ";
                in_token = false;
            }
            continue;
        }
        out.push_back(c);
        in_token = true;
    }
    if (in_token) {
        out += '*';
    } else if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::int64_t to_sqlite_hash64(std::uint64_t value) {
    return static_cast<std::int64_t>(value);
}

}  // namespace

std::string NormalizeQuery(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());

    bool pending_space = false;
    for (unsigned char c : raw) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c + ('a' - 'A'));
        }

        if (is_ascii_space(c)) {
            pending_space = !out.empty();
            continue;
        }

        if (is_punctuation_to_strip(c)) {
            pending_space = !out.empty();
            continue;
        }

        if (pending_space && !out.empty()) {
            out.push_back(' ');
        }
        out.push_back(static_cast<char>(c));
        pending_space = false;
    }

    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

std::uint64_t QueryHash64(std::string_view normalized) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    std::uint64_t hash = kOffsetBasis;
    for (unsigned char c : normalized) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::int64_t GetOrCreateQueryId(persistence::SqliteDb& db, const std::string& raw_text) {
    const std::string normalized = NormalizeQuery(raw_text);
    const std::uint64_t hash = QueryHash64(normalized);

    auto insert_stmt = db.prepare(
        "INSERT OR IGNORE INTO query_registry(normalized_text, raw_example, hash64, created_at_utc) "
        "VALUES(?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    insert_stmt.bind_text(1, normalized);
    insert_stmt.bind_text(2, raw_text);
    insert_stmt.bind_int64(3, to_sqlite_hash64(hash));
    insert_stmt.step();

    auto select_stmt = db.prepare("SELECT query_id FROM query_registry WHERE hash64 = ?1 LIMIT 1;");
    select_stmt.bind_int64(1, to_sqlite_hash64(hash));
    if (!select_stmt.step()) {
        throw std::runtime_error("Failed to load query_id after upsert");
    }
    return select_stmt.column_int64(0);
}

std::vector<SimilarQueryMatch> FindSimilarQueries(
    persistence::SqliteDb& db,
    const std::string& raw_text,
    int limit,
    double min_score
) {
    const int safe_limit = std::max(1, std::min(limit, 100));
    const std::string normalized = NormalizeQuery(raw_text);
    if (normalized.empty()) {
        return {};
    }

    const std::string fts_query = fts_match_query_from_normalized(normalized);
    if (fts_query.empty()) {
        return {};
    }

    auto stmt = db.prepare(
        "SELECT qr.query_id, bm25(query_fts) "
        "FROM query_fts "
        "JOIN query_registry qr ON qr.query_id = query_fts.rowid "
        "WHERE query_fts MATCH ?1 "
        "ORDER BY bm25(query_fts) ASC "
        "LIMIT ?2;"
    );
    stmt.bind_text(1, fts_query);
    stmt.bind_int64(2, safe_limit);

    std::vector<SimilarQueryMatch> out;
    while (stmt.step()) {
        const double bm25_score = stmt.column_double(1);
        const double similarity = 1.0 / (1.0 + std::max(0.0, bm25_score));
        if (similarity >= min_score) {
            out.push_back(SimilarQueryMatch{.query_id = stmt.column_int64(0), .score = similarity});
        }
    }
    return out;
}

QueryResolution ResolveQuery(
    persistence::SqliteDb& db,
    const std::string& raw_text,
    int limit,
    double min_score
) {
    const std::string normalized = NormalizeQuery(raw_text);
    const std::uint64_t hash64 = QueryHash64(normalized);
    const std::int64_t query_id = GetOrCreateQueryId(db, raw_text);
    auto similar = FindSimilarQueries(db, raw_text, limit, min_score);
    similar.erase(
        std::remove_if(
            similar.begin(),
            similar.end(),
            [query_id](const SimilarQueryMatch& m) { return m.query_id == query_id; }
        ),
        similar.end()
    );

    return QueryResolution{
        .query_id = query_id,
        .normalized = normalized,
        .hash64 = hash64,
        .similar = similar,
    };
}

}  // namespace proteus::query
