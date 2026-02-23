#include "proteus/analytics/behavior_aggregation.h"
#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <sstream>
#include <string>

namespace {

std::string DumpTable(proteus::persistence::SqliteDb& db, const std::string& sql) {
    auto stmt = db.prepare(sql);
    std::ostringstream out;
    while (stmt.step()) {
        const int cols = sqlite3_column_count(stmt.native_handle());
        for (int i = 0; i < cols; ++i) {
            if (i > 0) out << '|';
            const int type = sqlite3_column_type(stmt.native_handle(), i);
            if (type == SQLITE_INTEGER) {
                out << stmt.column_int64(i);
            } else if (type == SQLITE_FLOAT) {
                out << stmt.column_double(i);
            } else if (type == SQLITE_TEXT) {
                out << stmt.column_text(i);
            } else if (type == SQLITE_BLOB) {
                const auto b = stmt.column_blob(i);
                out << "blob:" << b.size();
            } else {
                out << "NULL";
            }
        }
        out << '\n';
    }
    return out.str();
}

std::size_t AggregationHash(proteus::persistence::SqliteDb& db) {
    const std::string all =
        DumpTable(db, "SELECT query_id, stable_player_id, total_interactions, total_base_score, total_final_score, mean_final_score, variance_final_score, last_updated_timestamp FROM query_player_aggregate ORDER BY query_id, stable_player_id;") +
        DumpTable(db, "SELECT query_id, distinct_players, total_interactions, mean_final_score, entropy_score, divergence_index, last_recomputed_timestamp FROM query_cluster_stats ORDER BY query_id;") +
        DumpTable(db, "SELECT stable_player_id, vector_blob, dimensionality, last_recomputed_timestamp FROM player_preference_vector ORDER BY stable_player_id;");
    return std::hash<std::string>{}(all);
}

}  // namespace

TEST(BehaviorAggregationTest, RecomputeIsDeterministicAcrossRuns) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_behavior_aggregation.db";
    std::filesystem::remove(db_path);

    {
        proteus::persistence::SqliteDb db;
        db.open(db_path.string());
        proteus::persistence::ensure_schema(db);

        db.exec(
            "INSERT INTO interaction_log(query_id, stable_player_id, base_score, final_score, timestamp) VALUES "
            "(10, 'p1', 1.0, 1.2, 1),"
            "(10, 'p1', 2.0, 2.5, 2),"
            "(10, 'p2', 1.5, 1.7, 3),"
            "(11, 'p1', 0.5, 0.4, 4),"
            "(11, 'p3', 3.0, 2.8, 5);"
        );

        const proteus::analytics::RecomputeAggregatesOptions opt{
            .now_unix_seconds = 123456,
            .rounding_decimals = 9,
        };
        ASSERT_EQ(proteus::analytics::RecomputeBehaviorAggregatesDeterministic(db, opt), true);
        const auto h1 = AggregationHash(db);
        ASSERT_EQ(proteus::analytics::RecomputeBehaviorAggregatesDeterministic(db, opt), true);
        const auto h2 = AggregationHash(db);
        EXPECT_EQ(h1, h2);
    }

    std::filesystem::remove(db_path);
}

TEST(BehaviorAggregationTest, EntropyMatchesKnownDistribution) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_behavior_entropy.db";
    std::filesystem::remove(db_path);

    {
        proteus::persistence::SqliteDb db;
        db.open(db_path.string());
        proteus::persistence::ensure_schema(db);

        db.exec(
            "INSERT INTO interaction_log(query_id, stable_player_id, base_score, final_score, timestamp) VALUES "
            "(42, 'a', 1.0, 1.0, 1),"
            "(42, 'b', 1.0, 1.0, 2);"
        );

        const proteus::analytics::RecomputeAggregatesOptions opt{
            .now_unix_seconds = 7,
            .rounding_decimals = 9,
        };
        ASSERT_EQ(proteus::analytics::RecomputeBehaviorAggregatesDeterministic(db, opt), true);

        auto stmt = db.prepare("SELECT entropy_score FROM query_cluster_stats WHERE query_id=42;");
        ASSERT_EQ(stmt.step(), true);
        EXPECT_EQ(stmt.column_double(0), 1.0);
    }

    std::filesystem::remove(db_path);
}
