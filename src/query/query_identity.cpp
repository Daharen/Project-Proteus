#include "proteus/query/query_identity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace proteus::query {
namespace {

constexpr int kFingerprintDims = 256;
constexpr int kFingerprintVersion = 2;
constexpr int kCandidateLimit = 32;
constexpr double kHardDuplicateThreshold = 0.92;
constexpr double kGreyBandThreshold = 0.78;

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

std::uint64_t fnv1a64(std::string_view v) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    for (unsigned char c : v) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::string stem_lite(std::string token) {
    if (token.size() > 4 && token.size() >= 3 && token.substr(token.size() - 3) == "ing") {
        token.resize(token.size() - 3);
    } else if (token.size() > 3 && token.size() >= 2 && token.substr(token.size() - 2) == "ed") {
        token.resize(token.size() - 2);
    } else if (token.size() > 3 && token.back() == 's') {
        token.pop_back();
    }
    return token;
}

std::vector<std::string> split_tokens(const std::string& normalized) {
    std::vector<std::string> out;
    std::stringstream ss(normalized);
    std::string token;
    while (ss >> token) {
        out.push_back(token);
    }
    return out;
}

std::string apply_domain_synonyms(persistence::SqliteDb& db, QueryDomain domain, const std::string& normalized) {
    if (domain != QueryDomain::Class) {
        return normalized;
    }
    auto tokens = split_tokens(normalized);
    if (tokens.empty()) {
        return normalized;
    }

    auto stmt = db.prepare("SELECT canonical_term FROM domain_synonyms WHERE query_domain = ?1 AND term = ?2 LIMIT 1;");
    for (auto& token : tokens) {
        stmt.bind_int64(1, static_cast<std::int64_t>(domain));
        stmt.bind_text(2, token);
        if (stmt.step()) {
            token = stmt.column_text(0);
        }
        stmt.reset();
    }

    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) out.push_back(' ');
        out += tokens[i];
    }
    return out;
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

std::vector<std::int16_t> fingerprint_dense_v2(std::string_view normalized_text) {
    std::vector<std::int32_t> dense(kFingerprintDims, 0);
    const std::string normalized(normalized_text);

    std::string padded = "^" + normalized + "$";
    for (int n = 3; n <= 5; ++n) {
        if (static_cast<int>(padded.size()) < n) continue;
        for (int i = 0; i + n <= static_cast<int>(padded.size()); ++i) {
            const std::string gram = padded.substr(static_cast<std::size_t>(i), static_cast<std::size_t>(n));
            const std::uint64_t h = fnv1a64(gram);
            const int dim = static_cast<int>(h % static_cast<std::uint64_t>(kFingerprintDims));
            const int sign = ((h >> 63U) & 1U) == 0U ? 1 : -1;
            dense[dim] += (3 * sign);
        }
    }

    auto tokens = split_tokens(normalized);
    for (auto& token : tokens) {
        token = stem_lite(token);
    }
    for (const auto& token : tokens) {
        const std::uint64_t h = fnv1a64("u:" + token);
        const int dim = static_cast<int>(h % static_cast<std::uint64_t>(kFingerprintDims));
        const int sign = ((h >> 63U) & 1U) == 0U ? 1 : -1;
        dense[dim] += (5 * sign);
    }
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string bg = tokens[i - 1] + "_" + tokens[i];
        const std::uint64_t h = fnv1a64("b:" + bg);
        const int dim = static_cast<int>(h % static_cast<std::uint64_t>(kFingerprintDims));
        const int sign = ((h >> 63U) & 1U) == 0U ? 1 : -1;
        dense[dim] += (4 * sign);
    }

    std::vector<std::int16_t> out(kFingerprintDims, 0);
    for (int i = 0; i < kFingerprintDims; ++i) {
        out[i] = static_cast<std::int16_t>(std::max(-32767, std::min(32767, dense[i])));
    }
    return out;
}

std::vector<std::int16_t> fingerprint_from_blob(const std::string& blob) {
    if (blob.size() != static_cast<std::size_t>(kFingerprintDims * static_cast<int>(sizeof(std::int16_t)))) {
        return std::vector<std::int16_t>(kFingerprintDims, 0);
    }
    std::vector<std::int16_t> out(kFingerprintDims, 0);
    for (int i = 0; i < kFingerprintDims; ++i) {
        const unsigned char lo = static_cast<unsigned char>(blob[static_cast<std::size_t>(2 * i)]);
        const unsigned char hi = static_cast<unsigned char>(blob[static_cast<std::size_t>(2 * i + 1)]);
        out[i] = static_cast<std::int16_t>((static_cast<std::uint16_t>(hi) << 8U) | static_cast<std::uint16_t>(lo));
    }
    return out;
}

double cosine_similarity(const std::vector<std::int16_t>& a, const std::vector<std::int16_t>& b) {
    long double dot = 0.0;
    long double an = 0.0;
    long double bn = 0.0;
    for (int i = 0; i < kFingerprintDims; ++i) {
        const long double av = static_cast<long double>(a[i]);
        const long double bv = static_cast<long double>(b[i]);
        dot += av * bv;
        an += av * av;
        bn += bv * bv;
    }
    if (an <= 0.0 || bn <= 0.0) return 0.0;
    return static_cast<double>(dot / std::sqrt(an * bn));
}

int levenshtein_distance(const std::string& a, const std::string& b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    std::vector<int> prev(m + 1);
    std::vector<int> cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

double token_jaccard(const std::string& a, const std::string& b) {
    const auto ta = split_tokens(a);
    const auto tb = split_tokens(b);
    if (ta.empty() && tb.empty()) return 1.0;
    std::unordered_set<std::string> sa(ta.begin(), ta.end());
    std::unordered_set<std::string> sb(tb.begin(), tb.end());
    std::size_t intersection = 0;
    for (const auto& t : sa) {
        if (sb.find(t) != sb.end()) ++intersection;
    }
    const std::size_t union_count = sa.size() + sb.size() - intersection;
    if (union_count == 0) return 0.0;
    return static_cast<double>(intersection) / static_cast<double>(union_count);
}

std::string mint_cluster_id(QueryDomain domain, const std::string& normalized, int schema_version, int fingerprint_version) {
    const std::string seed = std::to_string(static_cast<std::int64_t>(domain)) + "|" + normalized + "|" + std::to_string(schema_version) + "|" + std::to_string(fingerprint_version);
    const std::uint64_t h = fnv1a64(seed);
    std::ostringstream oss;
    oss << "c" << std::hex << h;
    return oss.str();
}

void log_similarity_decision(
    persistence::SqliteDb& db,
    QueryDomain domain,
    const std::string& normalized,
    const std::string& thresholds_version,
    const std::string& candidate_cluster_id,
    double similarity_score,
    double jaccard_score,
    double edit_similarity,
    const std::string& decision_band,
    const std::string& reason
) {
    auto stmt = db.prepare(
        "INSERT INTO concept_similarity_log(query_domain, normalized_text, thresholds_version, candidate_cluster_id, similarity_score, jaccard_score, edit_similarity, decision_band, decision_reason, created_at_utc) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_text(2, normalized);
    stmt.bind_text(3, thresholds_version);
    stmt.bind_text(4, candidate_cluster_id);
    stmt.bind_double(5, similarity_score);
    stmt.bind_double(6, jaccard_score);
    stmt.bind_double(7, edit_similarity);
    stmt.bind_text(8, decision_band);
    stmt.bind_text(9, reason);
    stmt.step();
}

void upsert_alias(persistence::SqliteDb& db, QueryDomain query_domain, const std::string& normalized_alias, const std::string& cluster_id) {
    auto ins_alias = db.prepare(
        "INSERT OR IGNORE INTO concept_alias(query_domain, normalized_alias, cluster_id, created_at_utc) "
        "VALUES(?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    ins_alias.bind_int64(1, static_cast<std::int64_t>(query_domain));
    ins_alias.bind_text(2, normalized_alias);
    ins_alias.bind_text(3, cluster_id);
    ins_alias.step();
}

}  // namespace

std::string NormalizeQuery(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    bool pending_space = false;
    for (unsigned char c : raw) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + ('a' - 'A'));
        if (is_ascii_space(c) || is_punctuation_to_strip(c)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space && !out.empty()) out.push_back(' ');
        out.push_back(static_cast<char>(c));
        pending_space = false;
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string NormalizeQueryForDomain(persistence::SqliteDb& db, std::string_view raw, QueryDomain domain) {
    return apply_domain_synonyms(db, domain, NormalizeQuery(raw));
}

std::uint64_t QueryHash64(std::string_view normalized, QueryDomain domain) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    hash ^= static_cast<std::uint64_t>(domain);
    hash *= kPrime;
    for (unsigned char c : normalized) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::vector<std::uint8_t> ComputeSemanticFingerprintV1(std::string_view normalized_text) {
    const auto dense = fingerprint_dense_v2(normalized_text);
    std::vector<std::uint8_t> blob(static_cast<std::size_t>(kFingerprintDims * 2), 0);
    for (int i = 0; i < kFingerprintDims; ++i) {
        const std::uint16_t v = static_cast<std::uint16_t>(dense[i]);
        blob[static_cast<std::size_t>(2 * i)] = static_cast<std::uint8_t>(v & 0xFFU);
        blob[static_cast<std::size_t>(2 * i + 1)] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
    }
    return blob;
}

std::int64_t GetOrCreateQueryId(persistence::SqliteDb& db, const std::string& raw_text, QueryDomain domain) {
    const std::string normalized = NormalizeQueryForDomain(db, raw_text, domain);
    const std::uint64_t hash = QueryHash64(normalized, domain);

    auto insert_stmt = db.prepare(
        "INSERT OR IGNORE INTO query_registry(query_domain, normalized_text, raw_example, hash64, normalized_text_hash64, created_at_utc) "
        "VALUES(?1, ?2, ?3, ?4, ?5, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    insert_stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    insert_stmt.bind_text(2, normalized);
    insert_stmt.bind_text(3, raw_text);
    insert_stmt.bind_int64(4, to_sqlite_hash64(hash));
    insert_stmt.bind_int64(5, to_sqlite_hash64(hash));
    insert_stmt.step();

    auto select_stmt = db.prepare("SELECT query_id FROM query_registry WHERE query_domain = ?1 AND normalized_text_hash64 = ?2 LIMIT 1;");
    select_stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    select_stmt.bind_int64(2, to_sqlite_hash64(hash));
    if (!select_stmt.step()) {
        throw std::runtime_error("Failed to load query_id after upsert");
    }
    return select_stmt.column_int64(0);
}

std::vector<SimilarQueryMatch> FindSimilarQueries(persistence::SqliteDb& db, const std::string& raw_text, int limit, double min_score, QueryDomain domain) {
    const int safe_limit = std::max(1, std::min(limit, 100));
    const std::string normalized = NormalizeQueryForDomain(db, raw_text, domain);
    if (normalized.empty()) return {};

    const std::string fts_query = fts_match_query_from_normalized(normalized);
    if (fts_query.empty()) return {};

    auto stmt = db.prepare(
        "SELECT qr.query_id, bm25(query_fts) "
        "FROM query_fts "
        "JOIN query_registry qr ON qr.query_id = query_fts.rowid "
        "WHERE query_fts MATCH ?1 AND qr.query_domain = ?2 "
        "ORDER BY bm25(query_fts) ASC "
        "LIMIT ?3;"
    );
    stmt.bind_text(1, fts_query);
    stmt.bind_int64(2, static_cast<std::int64_t>(domain));
    stmt.bind_int64(3, safe_limit);

    std::vector<SimilarQueryMatch> out;
    while (stmt.step()) {
        const double bm25_score = stmt.column_double(1);
        const double similarity = 1.0 / (1.0 + std::max(0.0, bm25_score));
        if (similarity >= min_score) out.push_back(SimilarQueryMatch{.query_id = stmt.column_int64(0), .score = similarity});
    }
    return out;
}

QueryResolution ResolveQuery(persistence::SqliteDb& db, const std::string& raw_text, int limit, double min_score, QueryDomain domain) {
    const std::string normalized = NormalizeQueryForDomain(db, raw_text, domain);
    const std::uint64_t hash64 = QueryHash64(normalized, domain);
    const std::int64_t query_id = GetOrCreateQueryId(db, raw_text, domain);
    auto similar = FindSimilarQueries(db, raw_text, limit, min_score, domain);
    similar.erase(std::remove_if(similar.begin(), similar.end(), [query_id](const SimilarQueryMatch& m) { return m.query_id == query_id; }), similar.end());

    return QueryResolution{.query_id = query_id, .normalized = normalized, .hash64 = hash64, .similar = similar};
}

std::int64_t ResolveCanonicalQueryIdForCluster(persistence::SqliteDb& db, QueryDomain query_domain, const std::string& cluster_id) {
    auto stmt = db.prepare("SELECT canonical_query_id FROM concept_cluster WHERE query_domain = ?1 AND cluster_id = ?2 LIMIT 1;");
    stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
    stmt.bind_text(2, cluster_id);
    if (!stmt.step()) return 0;
    return stmt.column_int64(0);
}

ClusterResolution ResolveOrAdmitClusterId(persistence::SqliteDb& db, QueryDomain query_domain, const std::string& raw_text, const std::string& thresholds_version) {
    const std::string normalized = NormalizeQueryForDomain(db, raw_text, query_domain);
    const auto fingerprint_blob = ComputeSemanticFingerprintV1(normalized);
    const auto fingerprint = fingerprint_dense_v2(normalized);
    const std::int64_t query_id = GetOrCreateQueryId(db, raw_text, query_domain);

    auto alias_stmt = db.prepare("SELECT cluster_id FROM concept_alias WHERE query_domain = ?1 AND normalized_alias = ?2 LIMIT 1;");
    alias_stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
    alias_stmt.bind_text(2, normalized);
    if (alias_stmt.step()) {
        const std::string cluster_id = alias_stmt.column_text(0);
        return ClusterResolution{.cluster_id = cluster_id, .decision_band = "hard_duplicate", .score = 1.0, .normalized = normalized, .query_id = query_id, .canonical_query_id = ResolveCanonicalQueryIdForCluster(db, query_domain, cluster_id)};
    }

    struct Candidate { std::string cluster_id; std::string canonical_label; std::vector<std::int16_t> fingerprint; std::int64_t canonical_query_id = 0; double score = 0.0; };
    std::vector<Candidate> candidates;
    auto cluster_stmt = db.prepare("SELECT cluster_id, canonical_label, fingerprint_blob, canonical_query_id FROM concept_cluster WHERE query_domain = ?1 ORDER BY cluster_id ASC;");
    cluster_stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
    while (cluster_stmt.step()) {
        Candidate c;
        c.cluster_id = cluster_stmt.column_text(0);
        c.canonical_label = cluster_stmt.column_text(1);
        c.fingerprint = fingerprint_from_blob(cluster_stmt.column_text(2));
        c.canonical_query_id = cluster_stmt.column_int64(3);
        c.score = cosine_similarity(fingerprint, c.fingerprint);
        candidates.push_back(std::move(c));
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score == b.score) return a.cluster_id < b.cluster_id;
        return a.score > b.score;
    });
    if (candidates.size() > static_cast<std::size_t>(kCandidateLimit)) candidates.resize(kCandidateLimit);

    if (!candidates.empty() && candidates.front().score >= kHardDuplicateThreshold) {
        upsert_alias(db, query_domain, normalized, candidates.front().cluster_id);
        return ClusterResolution{.cluster_id = candidates.front().cluster_id, .decision_band = "hard_duplicate", .score = candidates.front().score, .normalized = normalized, .query_id = query_id, .canonical_query_id = candidates.front().canonical_query_id};
    }

    if (!candidates.empty() && candidates.front().score >= kGreyBandThreshold) {
        const Candidate& best = candidates.front();
        const std::string canonical_normalized = NormalizeQueryForDomain(db, best.canonical_label, query_domain);
        const double jaccard = token_jaccard(normalized, canonical_normalized);
        const int lev = levenshtein_distance(normalized, canonical_normalized);
        const int max_len = std::max(normalized.size(), canonical_normalized.size());
        const double edit_similarity = max_len == 0 ? 1.0 : 1.0 - (static_cast<double>(lev) / static_cast<double>(max_len));

        if (jaccard >= 0.70 || edit_similarity >= 0.86) {
            upsert_alias(db, query_domain, normalized, best.cluster_id);
            log_similarity_decision(db, query_domain, normalized, thresholds_version, best.cluster_id, best.score, jaccard, edit_similarity, "grey_duplicate", "deterministic_jaccard_or_edit");
            return ClusterResolution{.cluster_id = best.cluster_id, .decision_band = "grey_duplicate", .score = best.score, .normalized = normalized, .query_id = query_id, .canonical_query_id = best.canonical_query_id};
        }

        log_similarity_decision(db, query_domain, normalized, thresholds_version, best.cluster_id, best.score, jaccard, edit_similarity, "grey_unresolved", "grey_band_unresolved");
        return ClusterResolution{.cluster_id = best.cluster_id, .decision_band = "grey_unresolved", .score = best.score, .normalized = normalized, .query_id = query_id, .canonical_query_id = best.canonical_query_id};
    }

    const std::string cluster_id = mint_cluster_id(query_domain, normalized, 12, kFingerprintVersion);
    auto ins_cluster = db.prepare(
        "INSERT OR IGNORE INTO concept_cluster(cluster_id, query_domain, canonical_label, fingerprint_version, fingerprint_blob, canonical_query_id, created_at_utc) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    ins_cluster.bind_text(1, cluster_id);
    ins_cluster.bind_int64(2, static_cast<std::int64_t>(query_domain));
    ins_cluster.bind_text(3, raw_text);
    ins_cluster.bind_int64(4, kFingerprintVersion);
    ins_cluster.bind_text(5, std::string(reinterpret_cast<const char*>(fingerprint_blob.data()), fingerprint_blob.size()));
    ins_cluster.bind_int64(6, query_id);
    ins_cluster.step();

    upsert_alias(db, query_domain, normalized, cluster_id);
    return ClusterResolution{.cluster_id = cluster_id, .decision_band = "novel", .score = 0.0, .normalized = normalized, .query_id = query_id, .canonical_query_id = query_id};
}

std::vector<FacetTypeSearchHit> SearchFacetTypes(persistence::SqliteDb& db, QueryDomain query_domain, const std::string& raw_text, int limit) {
    const int safe_limit = std::max(1, std::min(limit, 25));
    const std::string normalized = NormalizeQueryForDomain(db, raw_text, query_domain);
    const auto qfp = fingerprint_dense_v2(normalized);

    std::vector<FacetTypeSearchHit> out;
    auto stmt = db.prepare(
        "SELECT c.cluster_id, c.canonical_label, c.fingerprint_blob, "
        "EXISTS(SELECT 1 FROM concept_alias a WHERE a.cluster_id = c.cluster_id AND a.query_domain = c.query_domain AND a.normalized_alias LIKE ?2 || '%') AS prefix_match "
        "FROM concept_cluster c WHERE c.query_domain = ?1 ORDER BY c.cluster_id ASC;"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
    stmt.bind_text(2, normalized);
    while (stmt.step()) {
        FacetTypeSearchHit h;
        h.cluster_id = stmt.column_text(0);
        h.canonical_label = stmt.column_text(1);
        h.score = cosine_similarity(qfp, fingerprint_from_blob(stmt.column_text(2)));
        h.prefix_match = stmt.column_int64(3) != 0;

        auto alias_stmt = db.prepare("SELECT normalized_alias FROM concept_alias WHERE query_domain = ?1 AND cluster_id = ?2 ORDER BY normalized_alias ASC;");
        alias_stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
        alias_stmt.bind_text(2, h.cluster_id);
        while (alias_stmt.step()) {
            h.aliases.push_back(alias_stmt.column_text(0));
        }
        out.push_back(std::move(h));
    }

    std::sort(out.begin(), out.end(), [](const FacetTypeSearchHit& a, const FacetTypeSearchHit& b) {
        if (a.prefix_match != b.prefix_match) return a.prefix_match > b.prefix_match;
        if (a.score == b.score) return a.cluster_id < b.cluster_id;
        return a.score > b.score;
    });
    if (out.size() > static_cast<std::size_t>(safe_limit)) out.resize(static_cast<std::size_t>(safe_limit));
    return out;
}

}  // namespace proteus::query
