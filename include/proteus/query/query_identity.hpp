#pragma once

#include "proteus/persistence/sqlite_db.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace proteus::query {

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

std::string NormalizeQuery(std::string_view raw);
std::uint64_t QueryHash64(std::string_view normalized);

std::int64_t GetOrCreateQueryId(persistence::SqliteDb& db, const std::string& raw_text);
std::vector<SimilarQueryMatch> FindSimilarQueries(
    persistence::SqliteDb& db,
    const std::string& raw_text,
    int limit,
    double min_score
);

QueryResolution ResolveQuery(
    persistence::SqliteDb& db,
    const std::string& raw_text,
    int limit,
    double min_score
);

}  // namespace proteus::query
