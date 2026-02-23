#pragma once

#include <cstdint>

namespace proteus::persistence {
class SqliteDb;
}

namespace proteus::analytics {

struct RecomputeAggregatesOptions {
    std::int64_t now_unix_seconds = 0;
    int rounding_decimals = 9;
};

bool RecomputeBehaviorAggregatesDeterministic(
    persistence::SqliteDb& db,
    const RecomputeAggregatesOptions& opt
);

}  // namespace proteus::analytics
