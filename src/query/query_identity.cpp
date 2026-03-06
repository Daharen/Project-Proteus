#include "proteus/query/query_identity.hpp"
#include "proteus/query/domain_synonyms_seed.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace proteus::query {
namespace {

constexpr int kFingerprintDims = 256;
constexpr int kFingerprintVersion = 2;
constexpr int kSynonymMapVersion = 1;
constexpr int kCandidateLimit = 32;
constexpr double kHardDuplicateThreshold = 0.92;
constexpr double kGreyBandThreshold = 0.78;
constexpr double kRecognitionStrongFloor = 0.70;
constexpr double kRecognitionWeakFloor = 0.45;

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

std::vector<std::string> split_tokens(const std::string& normalized) {
    std::vector<std::string> out;
    std::stringstream ss(normalized);
    std::string token;
    while (ss >> token) {
        out.push_back(token);
    }
    return out;
}

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string maybe_stem(std::string token) {
    const auto n = token.size();
    if (n > 4 && has_suffix(token, "ing")) {
        return token.substr(0, n - 3);
    }
    if (n > 3 && has_suffix(token, "ed")) {
        return token.substr(0, n - 2);
    }
    if (n > 3 && has_suffix(token, "s")) {
        return token.substr(0, n - 1);
    }
    return token;
}

std::string normalize_with_synonyms(
    persistence::SqliteDb& db,
    QueryDomain domain,
    const std::string& normalized
) {
    auto tokens = split_tokens(normalized);
    if (tokens.empty()) {
        return normalized;
    }

    auto stmt = db.prepare("SELECT term, canonical_term FROM domain_synonyms WHERE query_domain = ?1 AND mapping_version = ?2;");
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_int64(2, kSynonymMapVersion);
    std::unordered_map<std::string, std::string> synonyms;
    while (stmt.step()) {
        synonyms.emplace(stmt.column_text(0), stmt.column_text(1));
    }

    for (auto& token : tokens) {
        token = maybe_stem(token);
        auto it = synonyms.find(token);
        if (it != synonyms.end()) {
            token = it->second;
        }
    }

    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) out.push_back(' ');
        out += tokens[i];
    }
    return out;
}

std::vector<std::int16_t> fingerprint_dense_v2(std::string_view normalized_text) {
    std::vector<std::int32_t> dense(kFingerprintDims, 0);

    const std::string padded = "^" + std::string(normalized_text) + "$";
    for (int n = 3; n <= 5; ++n) {
        if (static_cast<int>(padded.size()) < n) continue;
        for (int i = 0; i + n <= static_cast<int>(padded.size()); ++i) {
            const std::string gram = padded.substr(static_cast<std::size_t>(i), static_cast<std::size_t>(n));
            const std::uint64_t h = fnv1a64("c:" + gram);
            const int dim = static_cast<int>(h % static_cast<std::uint64_t>(kFingerprintDims));
            dense[dim] += ((h >> 63U) & 1U) == 0U ? 2 : -2;
        }
    }

    const auto tokens = split_tokens(std::string(normalized_text));
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        const std::uint64_t hu = fnv1a64("u:" + t);
        dense[static_cast<int>(hu % static_cast<std::uint64_t>(kFingerprintDims))] += ((hu >> 63U) & 1U) == 0U ? 3 : -3;

        if (i + 1 < tokens.size()) {
            const std::string bigram = t + "_" + tokens[i + 1];
            const std::uint64_t hb = fnv1a64("b:" + bigram);
            dense[static_cast<int>(hb % static_cast<std::uint64_t>(kFingerprintDims))] += ((hb >> 63U) & 1U) == 0U ? 2 : -2;
        }
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

std::string fingerprint_to_blob_string(const std::vector<std::uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
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
    for (const auto& t : sa) if (sb.find(t) != sb.end()) ++intersection;
    const std::size_t union_count = sa.size() + sb.size() - intersection;
    if (union_count == 0) return 0.0;
    return static_cast<double>(intersection) / static_cast<double>(union_count);
}

std::string mint_cluster_id(QueryDomain domain, const std::string& normalized, int schema_version, int fingerprint_version) {
    const std::string seed = std::to_string(static_cast<std::int64_t>(domain)) + "|" + normalized + "|" + std::to_string(schema_version) + "|" + std::to_string(fingerprint_version);
    std::ostringstream oss;
    oss << "c" << std::hex << fnv1a64(seed);
    return oss.str();
}

void insert_alias(persistence::SqliteDb& db, QueryDomain domain, const std::string& normalized, const std::string& cluster_id) {
    auto ins_alias = db.prepare(
        "INSERT INTO concept_alias(query_domain, normalized_alias, cluster_id, created_at_utc) "
        "VALUES(?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
        "ON CONFLICT(query_domain, normalized_alias) DO UPDATE SET cluster_id=excluded.cluster_id;"
    );
    ins_alias.bind_int64(1, static_cast<std::int64_t>(domain));
    ins_alias.bind_text(2, normalized);
    ins_alias.bind_text(3, cluster_id);
    ins_alias.step();
}

bool is_single_token(const std::string& normalized) {
    return normalized.find(' ') == std::string::npos && !normalized.empty();
}

int upsert_domain_synonym(
    persistence::SqliteDb& db,
    QueryDomain domain,
    const std::string& term_token,
    const std::string& canonical_token,
    int mapping_version
) {
    auto stmt = db.prepare(
        "INSERT OR REPLACE INTO domain_synonyms(query_domain, term, canonical_term, mapping_version, created_at_utc) "
        "VALUES(?1, ?2, ?3, ?4, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_text(2, term_token);
    stmt.bind_text(3, canonical_token);
    stmt.bind_int64(4, static_cast<std::int64_t>(mapping_version));
    stmt.step();

    auto changes = db.prepare("SELECT changes();");
    if (!changes.step()) return 0;
    return static_cast<int>(changes.column_int64(0));
}

void upsert_alias_to_cluster(
    persistence::SqliteDb& db,
    QueryDomain domain,
    const std::string& normalized_alias,
    const std::string& cluster_id
) {
    auto stmt = db.prepare(
        "INSERT OR REPLACE INTO concept_alias(query_domain, normalized_alias, cluster_id, created_at_utc) "
        "VALUES(?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_text(2, normalized_alias);
    stmt.bind_text(3, cluster_id);
    stmt.step();
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

std::int64_t canonical_query_id_for_cluster(persistence::SqliteDb& db, QueryDomain domain, const std::string& cluster_id, std::int64_t fallback_query_id) {
    auto stmt = db.prepare("SELECT canonical_query_id FROM concept_cluster WHERE query_domain = ?1 AND cluster_id = ?2 LIMIT 1;");
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_text(2, cluster_id);
    if (stmt.step() && !stmt.column_is_null(0)) {
        const auto value = stmt.column_int64(0);
        if (value > 0) return value;
    }

    auto find_min = db.prepare(
        "SELECT MIN(query_id) FROM query_bootstrap_proposals WHERE query_domain = ?1 AND cluster_id = ?2;"
    );
    find_min.bind_int64(1, static_cast<std::int64_t>(domain));
    find_min.bind_text(2, cluster_id);
    if (find_min.step() && !find_min.column_is_null(0) && find_min.column_int64(0) > 0) {
        const auto canonical = find_min.column_int64(0);
        auto set_stmt = db.prepare("UPDATE concept_cluster SET canonical_query_id = ?1 WHERE query_domain = ?2 AND cluster_id = ?3;");
        set_stmt.bind_int64(1, canonical);
        set_stmt.bind_int64(2, static_cast<std::int64_t>(domain));
        set_stmt.bind_text(3, cluster_id);
        set_stmt.step();
        return canonical;
    }

    if (fallback_query_id > 0) {
        auto set_stmt = db.prepare("UPDATE concept_cluster SET canonical_query_id = COALESCE(canonical_query_id, ?1) WHERE query_domain = ?2 AND cluster_id = ?3;");
        set_stmt.bind_int64(1, fallback_query_id);
        set_stmt.bind_int64(2, static_cast<std::int64_t>(domain));
        set_stmt.bind_text(3, cluster_id);
        set_stmt.step();
        return fallback_query_id;
    }
    return 0;
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
    const std::string normalized = NormalizeQuery(raw_text);
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
    if (!select_stmt.step()) throw std::runtime_error("Failed to load query_id after upsert");
    return select_stmt.column_int64(0);
}

std::vector<SimilarQueryMatch> FindSimilarQueries(persistence::SqliteDb& db, const std::string& raw_text, int limit, double min_score, QueryDomain domain) {
    const int safe_limit = std::max(1, std::min(limit, 100));
    const std::string normalized = NormalizeQuery(raw_text);
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
    const std::string normalized = NormalizeQuery(raw_text);
    const std::uint64_t hash64 = QueryHash64(normalized, domain);
    const std::int64_t query_id = GetOrCreateQueryId(db, raw_text, domain);
    auto similar = FindSimilarQueries(db, raw_text, limit, min_score, domain);
    similar.erase(std::remove_if(similar.begin(), similar.end(), [query_id](const SimilarQueryMatch& m) { return m.query_id == query_id; }), similar.end());
    return QueryResolution{.query_id = query_id, .normalized = normalized, .hash64 = hash64, .similar = similar};
}

ClusterResolution ResolveOrAdmitClusterId(persistence::SqliteDb& db, QueryDomain query_domain, const std::string& raw_text, const std::string& thresholds_version) {
    const std::string normalized = NormalizeQuery(raw_text);

    (void)EnsureSeededDomainSynonyms(db, query_domain, kSynonymMapVersion);

    const std::string normalized_for_similarity = normalize_with_synonyms(db, query_domain, normalized);
    const auto fingerprint_blob = ComputeSemanticFingerprintV1(normalized_for_similarity);
    if (fingerprint_blob.size() != static_cast<std::size_t>(kFingerprintDims * 2)) {
        throw std::runtime_error("fingerprint blob size mismatch for query");
    }
    const auto fingerprint = fingerprint_dense_v2(normalized_for_similarity);
    const std::int64_t query_id = GetOrCreateQueryId(db, raw_text, query_domain);

    auto alias_stmt = db.prepare("SELECT cluster_id FROM concept_alias WHERE query_domain = ?1 AND normalized_alias = ?2 LIMIT 1;");
    alias_stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
    alias_stmt.bind_text(2, normalized);
    if (alias_stmt.step()) {
        const std::string cluster_id = alias_stmt.column_text(0);
        const std::int64_t canonical_query_id = canonical_query_id_for_cluster(db, query_domain, cluster_id, query_id);
        return ClusterResolution{.cluster_id = cluster_id, .decision_band = "alias_hit", .score = 1.0, .normalized = normalized, .query_id = canonical_query_id, .canonical_query_id = canonical_query_id};
    }

    struct Candidate {
        std::string cluster_id;
        std::string canonical_label;
        std::vector<std::int16_t> fingerprint;
        double score = 0.0;
        double token_score = 0.0;
        double synonym_score = 0.0;
    };

    std::vector<Candidate> candidates;
    auto cluster_stmt = db.prepare("SELECT cluster_id, canonical_label, fingerprint_blob FROM concept_cluster WHERE query_domain = ?1 ORDER BY cluster_id ASC;");
    cluster_stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
    while (cluster_stmt.step()) {
        Candidate c;
        c.cluster_id = cluster_stmt.column_text(0);
        c.canonical_label = cluster_stmt.column_text(1);
        const std::string blob = cluster_stmt.column_text(2);
        if (blob.size() != static_cast<std::size_t>(kFingerprintDims * 2)) {
            log_similarity_decision(db, query_domain, normalized, thresholds_version, c.cluster_id, 0.0, 0.0, 0.0, "fingerprint_invalid", "stored_blob_length_mismatch");
            continue;
        }
        c.fingerprint = fingerprint_from_blob(blob);
        c.score = cosine_similarity(fingerprint, c.fingerprint);
        const std::string canonical_normalized = NormalizeQuery(c.canonical_label);
        c.token_score = token_jaccard(normalized, canonical_normalized);
        c.synonym_score = token_jaccard(normalized_for_similarity, normalize_with_synonyms(db, query_domain, canonical_normalized));
        candidates.push_back(std::move(c));
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.synonym_score == b.synonym_score) {
            if (a.score == b.score) return a.cluster_id < b.cluster_id;
            return a.score > b.score;
        }
        return a.synonym_score > b.synonym_score;
    });
    if (candidates.size() > static_cast<std::size_t>(kCandidateLimit)) candidates.resize(kCandidateLimit);

    if (!candidates.empty() && candidates.front().synonym_score >= kHardDuplicateThreshold) {
        insert_alias(db, query_domain, normalized, candidates.front().cluster_id);
        const std::int64_t canonical_query_id = canonical_query_id_for_cluster(db, query_domain, candidates.front().cluster_id, query_id);
        return ClusterResolution{.cluster_id = candidates.front().cluster_id, .decision_band = "hard_duplicate", .score = candidates.front().synonym_score, .normalized = normalized, .query_id = canonical_query_id, .canonical_query_id = canonical_query_id};
    }

    if (!candidates.empty() && std::max(candidates.front().score, candidates.front().token_score) >= kGreyBandThreshold) {
        const Candidate& best = candidates.front();
        const std::string canonical_normalized = NormalizeQuery(best.canonical_label);
        const double jaccard = token_jaccard(normalized, canonical_normalized);
        const int lev = levenshtein_distance(normalized, canonical_normalized);
        const int max_len = std::max(normalized.size(), canonical_normalized.size());
        const double edit_similarity = max_len == 0 ? 1.0 : 1.0 - (static_cast<double>(lev) / static_cast<double>(max_len));

        if (jaccard >= 0.70 || edit_similarity >= 0.86) {
            insert_alias(db, query_domain, normalized, best.cluster_id);
            log_similarity_decision(db, query_domain, normalized, thresholds_version, best.cluster_id, best.score, jaccard, edit_similarity, "grey_duplicate", "deterministic_jaccard_or_edit");
            const std::int64_t canonical_query_id = canonical_query_id_for_cluster(db, query_domain, best.cluster_id, query_id);
            return ClusterResolution{.cluster_id = best.cluster_id, .decision_band = "grey_duplicate", .score = best.score, .normalized = normalized, .query_id = canonical_query_id, .canonical_query_id = canonical_query_id};
        }

        log_similarity_decision(db, query_domain, normalized, thresholds_version, best.cluster_id, best.score, jaccard, edit_similarity, "requires_server_adjudication", "grey_band_unresolved");
        const std::int64_t canonical_query_id = canonical_query_id_for_cluster(db, query_domain, best.cluster_id, query_id);
        return ClusterResolution{.cluster_id = best.cluster_id, .decision_band = "requires_server_adjudication", .score = best.score, .normalized = normalized, .query_id = canonical_query_id, .canonical_query_id = canonical_query_id};
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
    ins_cluster.bind_text(5, fingerprint_to_blob_string(fingerprint_blob));
    ins_cluster.bind_int64(6, query_id);
    ins_cluster.step();

    insert_alias(db, query_domain, normalized, cluster_id);
    return ClusterResolution{.cluster_id = cluster_id, .decision_band = "novel", .score = 0.0, .normalized = normalized, .query_id = query_id, .canonical_query_id = query_id};
}

bool UpsertClusterAlias(
    persistence::SqliteDb& db,
    QueryDomain domain,
    const std::string& alias_text,
    const std::string& cluster_id
) {
    const std::string normalized_alias = NormalizeQuery(alias_text);
    if (normalized_alias.empty() || cluster_id.empty()) {
        return false;
    }

    auto stmt = db.prepare(
        "INSERT INTO concept_alias(query_domain, normalized_alias, cluster_id, created_at_utc) "
        "VALUES(?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
        "ON CONFLICT(query_domain, normalized_alias) DO UPDATE SET cluster_id=excluded.cluster_id;"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_text(2, normalized_alias);
    stmt.bind_text(3, cluster_id);
    stmt.step();
    return true;
}

int UpsertDomainSynonym(persistence::SqliteDb& db, QueryDomain domain, const std::string& term, const std::string& canonical_term, int mapping_version) {
    const std::string normalized_term = NormalizeQuery(term);
    const std::string normalized_canonical_term = NormalizeQuery(canonical_term);

    if (normalized_term.empty() || normalized_canonical_term.empty()) {
        return 0;
    }

    auto stmt = db.prepare(
        "INSERT INTO domain_synonyms(query_domain, term, canonical_term, mapping_version, created_at_utc) "
        "VALUES(?1, ?2, ?3, ?4, strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
        "ON CONFLICT(query_domain, term) DO UPDATE SET "
        "canonical_term=excluded.canonical_term, mapping_version=excluded.mapping_version;"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_text(2, normalized_term);
    stmt.bind_text(3, normalized_canonical_term);
    stmt.bind_int64(4, static_cast<std::int64_t>(mapping_version));
    stmt.step();
    return 1;
}

ClusterAdjudicationResult AdjudicateClusterAliasAndSynonyms(
    persistence::SqliteDb& db,
    QueryDomain query_domain,
    const std::string& raw_text,
    const std::string& chosen_cluster_id,
    const std::vector<std::pair<std::string, std::string>>& synonym_upserts,
    int synonym_mapping_version
) {
    ClusterAdjudicationResult out;
    out.ok = false;

    if (chosen_cluster_id.empty()) {
        return out;
    }

    if (NormalizeQuery(raw_text).empty()) {
        return out;
    }

    out.alias_written = UpsertClusterAlias(db, query_domain, raw_text, chosen_cluster_id);

    int wrote = 0;
    for (const auto& kv : synonym_upserts) {
        wrote += UpsertDomainSynonym(db, query_domain, kv.first, kv.second, synonym_mapping_version);
    }
    out.synonyms_written = wrote;

    out.ok = true;
    out.cluster_id = chosen_cluster_id;
    out.decision_band = "adjudicated";
    return out;
}


std::vector<FacetTypeSearchHit> deduplicate_hits_by_cluster_id(const std::vector<FacetTypeSearchHit>& hits) {
    std::vector<FacetTypeSearchHit> out;
    out.reserve(hits.size());
    std::unordered_set<std::string> seen;
    for (const auto& hit : hits) {
        if (seen.insert(hit.cluster_id).second) {
            out.push_back(hit);
        }
    }
    return out;
}

std::string confidence_band_for_score(double score, bool exploratory_mode) {
    if (exploratory_mode) {
        return "exploratory";
    }
    if (score >= kRecognitionStrongFloor) {
        return "strong";
    }
    if (score >= kRecognitionWeakFloor) {
        return "weak";
    }
    return "exploratory";
}

std::vector<RecognitionCandidate> to_recognition_candidates(
    const std::vector<FacetTypeSearchHit>& hits,
    int limit,
    bool exploratory_mode
) {
    std::vector<RecognitionCandidate> out;
    const int safe_limit = std::max(1, std::min(25, limit));
    for (const auto& hit : hits) {
        if (static_cast<int>(out.size()) >= safe_limit) {
            break;
        }
        out.push_back(RecognitionCandidate{
            .cluster_id = hit.cluster_id,
            .canonical_label = hit.canonical_label,
            .score = hit.score,
            .prefix_match = hit.prefix_match,
            .confidence_band = confidence_band_for_score(hit.score, exploratory_mode),
        });
    }
    return out;
}

bool all_candidates_below_weak_floor(const std::vector<RecognitionCandidate>& candidates) {
    if (candidates.empty()) {
        return true;
    }
    for (const auto& candidate : candidates) {
        if (candidate.score >= kRecognitionWeakFloor) {
            return false;
        }
    }
    return true;
}

bool label_too_similar(const std::vector<FacetTypeSearchHit>& picks, const std::string& candidate_label) {
    const std::string normalized_candidate = NormalizeQuery(candidate_label);
    for (const auto& pick : picks) {
        const std::string normalized_pick = NormalizeQuery(pick.canonical_label);
        if (normalized_pick == normalized_candidate) {
            return true;
        }
        if (!normalized_pick.empty() && normalized_candidate.find(normalized_pick) != std::string::npos) {
            return true;
        }
        if (!normalized_candidate.empty() && normalized_pick.find(normalized_candidate) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<FacetTypeSearchHit> build_exploratory_hits(const std::vector<FacetTypeSearchHit>& nearest_hits, int limit) {
    std::vector<FacetTypeSearchHit> out;
    const int safe_limit = std::max(1, std::min(3, limit));
    for (const auto& hit : nearest_hits) {
        if (static_cast<int>(out.size()) >= safe_limit) {
            break;
        }
        if (label_too_similar(out, hit.canonical_label)) {
            continue;
        }
        out.push_back(hit);
    }
    for (const auto& hit : nearest_hits) {
        if (static_cast<int>(out.size()) >= safe_limit) {
            break;
        }
        const auto found = std::find_if(out.begin(), out.end(), [&](const FacetTypeSearchHit& existing) {
            return existing.cluster_id == hit.cluster_id;
        });
        if (found == out.end()) {
            out.push_back(hit);
        }
    }
    return out;
}

RecognitionPrompt build_deterministic_prompt(const std::vector<RecognitionCandidate>& candidates) {
    RecognitionPrompt prompt;
    if (candidates.empty()) {
        return prompt;
    }

    prompt.needed = true;
    prompt.kind = "discriminator";
    const std::size_t cap = std::min<std::size_t>(3, candidates.size());
    prompt.options.reserve(cap);
    for (std::size_t i = 0; i < cap; ++i) {
        if (!candidates[i].canonical_label.empty()) {
            prompt.options.push_back(candidates[i].canonical_label);
        } else {
            prompt.options.push_back(candidates[i].cluster_id);
        }
    }

    if (prompt.options.size() == 1) {
        prompt.prompt_text = "Is this concept closer to " + prompt.options[0] + "?";
    } else if (prompt.options.size() == 2) {
        prompt.prompt_text = "Is this concept closer to " + prompt.options[0] + " or " + prompt.options[1] + "?";
    } else {
        prompt.prompt_text = "Is this concept closer to " + prompt.options[0] + ", " + prompt.options[1] + ", or " + prompt.options[2] + "?";
    }
    return prompt;
}

ClusterGuess ResolveClusterGuess(
    persistence::SqliteDb& db,
    QueryDomain query_domain,
    const std::string& raw_text,
    const std::string& thresholds_version,
    int alternates_limit
) {
    (void)EnsureSeededDomainSynonyms(db, query_domain, kSynonymMapVersion);

    ClusterGuess out;
    out.resolution = ResolveOrAdmitClusterId(db, query_domain, raw_text, thresholds_version);

    const int lim = std::max(1, std::min(25, alternates_limit));
    auto nearest_hits = deduplicate_hits_by_cluster_id(SearchFacetTypes(db, query_domain, raw_text, lim));

    if (out.resolution.decision_band == "novel") {
        nearest_hits.erase(
            std::remove_if(
                nearest_hits.begin(),
                nearest_hits.end(),
                [&out](const FacetTypeSearchHit& hit) { return hit.cluster_id == out.resolution.cluster_id; }
            ),
            nearest_hits.end()
        );
    }

    out.candidates = to_recognition_candidates(nearest_hits, lim, false);

    if (out.candidates.empty() || all_candidates_below_weak_floor(out.candidates)) {
        const auto exploratory_hits = build_exploratory_hits(nearest_hits, lim);
        out.candidates = to_recognition_candidates(exploratory_hits, lim, true);
        out.prompt = build_deterministic_prompt(out.candidates);
    }

    out.force_novel_available = true;
    return out;
}

std::vector<FacetTypeSearchHit> SearchFacetTypes(persistence::SqliteDb& db, QueryDomain query_domain, const std::string& raw_text, int limit) {
    const int safe_limit = std::max(1, std::min(limit, 25));
    const std::string normalized = NormalizeQuery(raw_text);
    const auto qfp = fingerprint_dense_v2(normalize_with_synonyms(db, query_domain, normalized));

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
        const auto fp = fingerprint_from_blob(stmt.column_text(2));
        h.score = cosine_similarity(qfp, fp);
        h.prefix_match = stmt.column_int64(3) != 0;

        auto alias_stmt = db.prepare("SELECT normalized_alias FROM concept_alias WHERE query_domain = ?1 AND cluster_id = ?2 ORDER BY normalized_alias ASC;");
        alias_stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
        alias_stmt.bind_text(2, h.cluster_id);
        while (alias_stmt.step()) h.aliases.push_back(alias_stmt.column_text(0));
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

FingerprintDebugInfo DebugFingerprint(std::string_view raw_text, int top_k) {
    const std::string normalized = NormalizeQuery(raw_text);
    const auto dense = fingerprint_dense_v2(normalized);
    const auto blob = ComputeSemanticFingerprintV1(normalized);

    std::vector<std::pair<int, int>> ranked;
    int nonzero = 0;
    for (int i = 0; i < kFingerprintDims; ++i) {
        const int v = dense[i];
        if (v != 0) {
            ++nonzero;
            ranked.emplace_back(i, v);
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.second) == std::abs(b.second)) return a.first < b.first;
        return std::abs(a.second) > std::abs(b.second);
    });
    if (ranked.size() > static_cast<std::size_t>(std::max(1, top_k))) ranked.resize(static_cast<std::size_t>(std::max(1, top_k)));

    const std::uint64_t short_hash = fnv1a64(std::string_view(reinterpret_cast<const char*>(blob.data()), blob.size()));
    std::ostringstream hash_ss;
    hash_ss << std::hex << std::setw(16) << std::setfill('0') << short_hash;

    return FingerprintDebugInfo{
        .normalized_text = normalized,
        .fingerprint_version = kFingerprintVersion,
        .nonzero_bucket_count = nonzero,
        .short_hash = hash_ss.str(),
        .top_k_buckets = ranked,
    };
}

std::vector<SimilarityScanRow> SimilarityScan(persistence::SqliteDb& db, QueryDomain query_domain, const std::string& raw_text, int limit) {
    const int safe_limit = std::max(1, std::min(limit, 50));
    const std::string normalized = NormalizeQuery(raw_text);
    const std::string normalized_syn = normalize_with_synonyms(db, query_domain, normalized);
    const auto qfp = fingerprint_dense_v2(normalized_syn);

    std::vector<SimilarityScanRow> out;
    auto stmt = db.prepare("SELECT cluster_id, canonical_label, fingerprint_blob FROM concept_cluster WHERE query_domain = ?1 ORDER BY cluster_id ASC;");
    stmt.bind_int64(1, static_cast<std::int64_t>(query_domain));
    while (stmt.step()) {
        SimilarityScanRow row;
        row.cluster_id = stmt.column_text(0);
        row.canonical_label = stmt.column_text(1);
        row.chargram_score = cosine_similarity(qfp, fingerprint_from_blob(stmt.column_text(2)));
        const std::string canonical = NormalizeQuery(row.canonical_label);
        row.token_score = token_jaccard(normalized, canonical);
        row.synonym_normalized_score = token_jaccard(normalized_syn, normalize_with_synonyms(db, query_domain, canonical));
        if (row.synonym_normalized_score >= kHardDuplicateThreshold) row.decision_band = "hard_duplicate";
        else if (std::max(row.chargram_score, row.token_score) >= kGreyBandThreshold) row.decision_band = "grey_band";
        else row.decision_band = "novel";
        out.push_back(std::move(row));
    }

    std::sort(out.begin(), out.end(), [](const SimilarityScanRow& a, const SimilarityScanRow& b) {
        if (a.synonym_normalized_score == b.synonym_normalized_score) {
            if (a.chargram_score == b.chargram_score) return a.cluster_id < b.cluster_id;
            return a.chargram_score > b.chargram_score;
        }
        return a.synonym_normalized_score > b.synonym_normalized_score;
    });
    if (out.size() > static_cast<std::size_t>(safe_limit)) out.resize(static_cast<std::size_t>(safe_limit));
    return out;
}

}  // namespace proteus::query
