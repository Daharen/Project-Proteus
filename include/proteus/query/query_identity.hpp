#pragma once

#include "proteus/persistence/sqlite_db.hpp"

#include <cstdint>
#include <string>
#include <string_view>
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

}  // namespace proteus::query
