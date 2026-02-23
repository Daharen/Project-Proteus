#include "proteus/analytics/behavior_aggregation.h"

#include "proteus/math/deterministic_math.h"
#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace proteus::analytics {
namespace {

constexpr double kDivergenceEpsilon = 1e-9;
constexpr double kDivergenceMax = 1e6;

std::vector<std::uint8_t> SerializeLittleEndianDoubles(const std::vector<double>& values) {
    std::vector<std::uint8_t> blob(values.size() * sizeof(double));
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::array<std::uint8_t, sizeof(double)> bytes{};
        std::memcpy(bytes.data(), &values[i], sizeof(double));
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
        std::reverse(bytes.begin(), bytes.end());
#endif
        std::memcpy(blob.data() + i * sizeof(double), bytes.data(), sizeof(double));
    }
    return blob;
}

}  // namespace

bool RecomputeBehaviorAggregatesDeterministic(
    persistence::SqliteDb& db,
    const RecomputeAggregatesOptions& opt
) {
    try {
        db.exec("BEGIN IMMEDIATE TRANSACTION;");

        db.exec("DELETE FROM query_player_aggregate;");
        db.exec("DELETE FROM query_cluster_stats;");
        db.exec("DELETE FROM player_preference_vector;");

        int inserted_qpa = 0;
        {
            auto raw_stmt = db.prepare(
                "SELECT query_id, stable_player_id, COUNT(*) AS n, "
                "SUM(base_score) AS sum_base, SUM(final_score) AS sum_final, "
                "SUM(final_score * final_score) AS sum_final_sq "
                "FROM interaction_log "
                "WHERE query_id IS NOT NULL AND stable_player_id IS NOT NULL "
                "AND base_score IS NOT NULL AND final_score IS NOT NULL "
                "GROUP BY query_id, stable_player_id "
                "ORDER BY query_id ASC, stable_player_id ASC;"
            );

            auto insert_stmt = db.prepare(
                "INSERT INTO query_player_aggregate ("
                "query_id, stable_player_id, total_interactions, total_base_score, total_final_score,"
                "mean_final_score, variance_final_score, last_updated_timestamp"
                ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);"
            );

            while (raw_stmt.step()) {
                const auto query_id = raw_stmt.column_int64(0);
                const auto stable_player_id = raw_stmt.column_text(1);
                const auto n = raw_stmt.column_int64(2);
                const double sum_base = raw_stmt.column_double(3);
                const double sum_final = raw_stmt.column_double(4);
                const double sum_final_sq = raw_stmt.column_double(5);

                const double mean_final = (n > 0) ? (sum_final / static_cast<double>(n)) : 0.0;
                double variance_final = 0.0;
                if (n > 0) {
                    variance_final = (sum_final_sq / static_cast<double>(n)) - (mean_final * mean_final);
                    if (variance_final < 0.0 && variance_final > -1e-12) {
                        variance_final = 0.0;
                    }
                }

                insert_stmt.bind_int64(1, query_id);
                insert_stmt.bind_text(2, stable_player_id);
                insert_stmt.bind_int64(3, n);
                insert_stmt.bind_double(4, math::RoundFixed(sum_base, opt.rounding_decimals));
                insert_stmt.bind_double(5, math::RoundFixed(sum_final, opt.rounding_decimals));
                insert_stmt.bind_double(6, math::RoundFixed(mean_final, opt.rounding_decimals));
                insert_stmt.bind_double(7, math::RoundFixed(variance_final, opt.rounding_decimals));
                insert_stmt.bind_int64(8, opt.now_unix_seconds);
                insert_stmt.step();
                insert_stmt.reset();
                ++inserted_qpa;
            }
        }

        int inserted_qcs = 0;
        {
            auto cluster_stmt = db.prepare(
                "SELECT query_id, SUM(total_interactions) AS total_n, COUNT(*) AS distinct_players, "
                "SUM(total_final_score) AS sum_final "
                "FROM query_player_aggregate "
                "GROUP BY query_id "
                "ORDER BY query_id ASC;"
            );

            auto player_counts_stmt = db.prepare(
                "SELECT total_interactions FROM query_player_aggregate WHERE query_id=?1 ORDER BY stable_player_id ASC;"
            );
            auto player_means_stmt = db.prepare(
                "SELECT mean_final_score FROM query_player_aggregate WHERE query_id=?1 ORDER BY stable_player_id ASC;"
            );

            auto insert_stmt = db.prepare(
                "INSERT INTO query_cluster_stats ("
                "query_id, distinct_players, total_interactions, mean_final_score, "
                "entropy_score, divergence_index, last_recomputed_timestamp"
                ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);"
            );

            while (cluster_stmt.step()) {
                const auto query_id = cluster_stmt.column_int64(0);
                const auto total_n = cluster_stmt.column_int64(1);
                const auto distinct_players = cluster_stmt.column_int64(2);
                const double sum_final = cluster_stmt.column_double(3);

                const double cluster_mean = total_n > 0 ? (sum_final / static_cast<double>(total_n)) : 0.0;

                player_counts_stmt.bind_int64(1, query_id);
                std::vector<double> probs;
                probs.reserve(static_cast<std::size_t>(std::max<std::int64_t>(distinct_players, 0)));
                while (player_counts_stmt.step()) {
                    const auto count = player_counts_stmt.column_int64(0);
                    const double p = total_n > 0 ? static_cast<double>(count) / static_cast<double>(total_n) : 0.0;
                    probs.push_back(p);
                }
                player_counts_stmt.reset();
                const double entropy = math::ShannonEntropy(probs, opt.rounding_decimals);

                player_means_stmt.bind_int64(1, query_id);
                std::vector<double> means;
                means.reserve(static_cast<std::size_t>(std::max<std::int64_t>(distinct_players, 0)));
                while (player_means_stmt.step()) {
                    means.push_back(player_means_stmt.column_double(0));
                }
                player_means_stmt.reset();

                double means_var = 0.0;
                if (!means.empty()) {
                    double mean_of_means = 0.0;
                    for (double v : means) {
                        mean_of_means += v;
                    }
                    mean_of_means /= static_cast<double>(means.size());
                    for (double v : means) {
                        const double d = v - mean_of_means;
                        means_var += d * d;
                    }
                    means_var /= static_cast<double>(means.size());
                }

                double divergence = means_var / (std::abs(cluster_mean) + kDivergenceEpsilon);
                divergence = std::clamp(divergence, 0.0, kDivergenceMax);

                insert_stmt.bind_int64(1, query_id);
                insert_stmt.bind_int64(2, distinct_players);
                insert_stmt.bind_int64(3, total_n);
                insert_stmt.bind_double(4, math::RoundFixed(cluster_mean, opt.rounding_decimals));
                insert_stmt.bind_double(5, entropy);
                insert_stmt.bind_double(6, math::RoundFixed(divergence, opt.rounding_decimals));
                insert_stmt.bind_int64(7, opt.now_unix_seconds);
                insert_stmt.step();
                insert_stmt.reset();
                ++inserted_qcs;
            }
        }

        int inserted_ppv = 0;
        {
            std::vector<std::int64_t> query_ids;
            auto query_ids_stmt = db.prepare("SELECT query_id FROM query_cluster_stats ORDER BY query_id ASC;");
            while (query_ids_stmt.step()) {
                query_ids.push_back(query_ids_stmt.column_int64(0));
            }

            std::unordered_map<std::int64_t, std::size_t> query_to_dim;
            for (std::size_t i = 0; i < query_ids.size(); ++i) {
                query_to_dim.emplace(query_ids[i], i);
            }

            auto players_stmt = db.prepare(
                "SELECT DISTINCT stable_player_id FROM query_player_aggregate ORDER BY stable_player_id ASC;"
            );
            auto player_vec_stmt = db.prepare(
                "SELECT query_id, mean_final_score FROM query_player_aggregate "
                "WHERE stable_player_id=?1 ORDER BY query_id ASC;"
            );
            auto insert_stmt = db.prepare(
                "INSERT INTO player_preference_vector ("
                "stable_player_id, vector_blob, dimensionality, last_recomputed_timestamp"
                ") VALUES (?1, ?2, ?3, ?4);"
            );

            while (players_stmt.step()) {
                const std::string stable_player_id = players_stmt.column_text(0);
                std::vector<double> vec(query_ids.size(), 0.0);

                player_vec_stmt.bind_text(1, stable_player_id);
                while (player_vec_stmt.step()) {
                    const auto query_id = player_vec_stmt.column_int64(0);
                    const double mean = player_vec_stmt.column_double(1);
                    const auto it = query_to_dim.find(query_id);
                    if (it != query_to_dim.end()) {
                        vec[it->second] = math::RoundFixed(mean, opt.rounding_decimals);
                    }
                }
                player_vec_stmt.reset();

                const std::vector<std::uint8_t> blob = SerializeLittleEndianDoubles(vec);
                insert_stmt.bind_text(1, stable_player_id);
                insert_stmt.bind_blob(2, blob);
                insert_stmt.bind_int64(3, static_cast<std::int64_t>(query_ids.size()));
                insert_stmt.bind_int64(4, opt.now_unix_seconds);
                insert_stmt.step();
                insert_stmt.reset();
                ++inserted_ppv;
            }
        }

        db.exec("COMMIT;");

        std::cerr << "[aggregation] recompute_complete schema_version=" << persistence::kSchemaVersion
                  << " rounding_decimals=" << opt.rounding_decimals
                  << " now_unix_seconds=" << opt.now_unix_seconds
                  << " inserted_qpa=" << inserted_qpa
                  << " inserted_qcs=" << inserted_qcs
                  << " inserted_ppv=" << inserted_ppv
                  << "\n";

        return true;
    } catch (const std::exception&) {
        try {
            db.exec("ROLLBACK;");
        } catch (...) {
        }
        return false;
    }
}

}  // namespace proteus::analytics
