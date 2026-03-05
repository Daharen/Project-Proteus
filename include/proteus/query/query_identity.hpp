#pragma once

#include "proteus/persistence/sqlite_db.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace proteus::query {

enum class QueryDomain : std::int64_t {
    Generic = 0,
    Class = 1,
    Skill = 2,
    NpcIntent = 3,
    DialogueLine = 4,
    DialogueOption = 5,
};

struct SimilarQueryMatch {
    std::int64_t query_id = 0;
    double score = 0.0;
};

struct QueryResolution {
    std::int64_t query_id = 0;
    std::string normalized;
    std::uint64_t hash64 = 0;
    std::vector<SimilarQueryMatch> similar;
};


struct ClusterResolution {
    std::string cluster_id;
    std::string decision_band;
    double score = 0.0;
    std::string normalized;
    std::int64_t query_id = 0;
    std::int64_t canonical_query_id = 0;
};

struct FacetTypeSearchHit {
    std::string cluster_id;
    std::string canonical_label;
    std::vector<std::string> aliases;
    double score = 0.0;
    bool prefix_match = false;
};

std::string NormalizeQuery(std::string_view raw);
std::uint64_t QueryHash64(std::string_view normalized, QueryDomain domain = QueryDomain::Generic);

std::int64_t GetOrCreateQueryId(
    persistence::SqliteDb& db,
    const std::string& raw_text,
    QueryDomain domain = QueryDomain::Generic
);
std::vector<SimilarQueryMatch> FindSimilarQueries(
    persistence::SqliteDb& db,
    const std::string& raw_text,
    int limit,
    double min_score,
    QueryDomain domain = QueryDomain::Generic
);

QueryResolution ResolveQuery(
    persistence::SqliteDb& db,
    const std::string& raw_text,
    int limit,
    double min_score,
    QueryDomain domain = QueryDomain::Generic
);

std::vector<std::uint8_t> ComputeSemanticFingerprintV1(std::string_view normalized_text);

struct FingerprintDebugInfo {
    std::string normalized_text;
    int fingerprint_version = 0;
    int nonzero_bucket_count = 0;
    std::string short_hash;
    std::vector<std::pair<int, int>> top_k_buckets;
};

struct SimilarityScanRow {
    std::string cluster_id;
    std::string canonical_label;
    double chargram_score = 0.0;
    double token_score = 0.0;
    double synonym_normalized_score = 0.0;
    std::string decision_band;
};

ClusterResolution ResolveOrAdmitClusterId(
    persistence::SqliteDb& db,
    QueryDomain query_domain,
    const std::string& raw_text,
    const std::string& thresholds_version
);

std::vector<FacetTypeSearchHit> SearchFacetTypes(
    persistence::SqliteDb& db,
    QueryDomain query_domain,
    const std::string& raw_text,
    int limit
);

FingerprintDebugInfo DebugFingerprint(std::string_view raw_text, int top_k);
std::vector<SimilarityScanRow> SimilarityScan(
    persistence::SqliteDb& db,
    QueryDomain query_domain,
    const std::string& raw_text,
    int limit
);

struct ClusterGuess {
    ClusterResolution best;
    std::vector<FacetTypeSearchHit> alternates;
    bool force_novel_available = true;
};

struct ClusterAdjudicationResult {
    bool ok = false;
    std::string cluster_id;
    std::string decision_band;
    bool alias_written = false;
    int synonyms_written = 0;
};

ClusterGuess ResolveClusterGuess(
    persistence::SqliteDb& db,
    QueryDomain query_domain,
    const std::string& raw_text,
    const std::string& thresholds_version,
    int alternates_limit
);

ClusterAdjudicationResult AdjudicateClusterAliasAndSynonyms(
    persistence::SqliteDb& db,
    QueryDomain query_domain,
    const std::string& raw_text,
    const std::string& chosen_cluster_id,
    const std::vector<std::pair<std::string, std::string>>& synonym_upserts,
    int synonym_mapping_version
);

}  // namespace proteus::query
