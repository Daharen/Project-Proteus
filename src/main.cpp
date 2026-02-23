#include "proteus/analytics/behavior_aggregation.h"
#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/http_server.hpp"
#include "proteus/playable/retrieval_engine.hpp"
#include "proteus/query/query_identity.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <random>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

enum class CliMode {
    Query,
    Serve,
    Migrate,
    QueryResolve,
    RecomputeAggregates,
    InspectCluster,
    InspectPlayer,
    Help,
};

struct CliArgs {
    CliMode mode = CliMode::Query;
    bool serve_help = false;
    std::string query_text;
    int query_top = 5;
    double min_score = 0.2;
    std::string domain;
    std::string prompt;
    std::string session_id;
    std::string proposal_id;
    std::string db_path = "./proteus.db";
    std::string host = "127.0.0.1";
    std::string static_dir;
    int port = 8080;
    double reward = -1.0;
    bool reward_mode = false;
    bool dev_mode = false;
    bool self_test_mode = false;
    bool smoke_mode = false;
    bool verbose = false;
    std::int64_t now_unix_seconds = -1;
    int rounding_decimals = 9;
    std::int64_t inspect_query_id = -1;
    std::string inspect_player_id;
};

std::string generate_session_uuid() {
    std::array<unsigned int, 8> blocks{};
    std::random_device rd;
    for (auto& b : blocks) {
        b = rd();
    }

    char out[37] = {0};
    std::snprintf(
        out,
        sizeof(out),
        "%08x-%04x-%04x-%04x-%04x%08x",
        blocks[0],
        blocks[1] & 0xFFFFU,
        (blocks[2] & 0x0FFFU) | 0x4000U,
        (blocks[3] & 0x3FFFU) | 0x8000U,
        blocks[4] & 0xFFFFU,
        blocks[5]
    );
    return std::string(out);
}

void print_help() {
    std::cout
        << "Usage:\n"
        << "  proteus --domain <domain> --prompt <prompt> [--db <path>] [--session <id>]\n"
        << "  proteus --reward <value> --proposal_id <id> [--session <id>] [--db <path>]\n"
        << "  proteus --migrate [--db <path>] [--verbose]\n"
        << "  proteus serve [--host 127.0.0.1] [--port 8080] [--db <path>] [--static_root <path>] [--dev] [--smoke] [--verbose]\n"
        << "  proteus query --db <path> --text \"<raw>\" [--top 5] [--min_score 0.2]\n"
        << "\n"
        << "Modes:\n"
        << "  query (default): use --domain/--prompt\n"
        << "  serve: start local web server and API\n"
        << "  migrate: apply DB migrations to latest schema version\n"
        << "  query: resolve canonical query identity + similar matches\n";
}

void print_serve_help() {
    std::cout
        << "Usage: proteus serve [options]\n"
        << "\n"
        << "Options:\n"
        << "  --host <host>           Bind host (default 127.0.0.1)\n"
        << "  --port <port>           Bind port (default 8080, use 0 with --smoke for ephemeral)\n"
        << "  --db <path>             SQLite DB path (default ./proteus.db)\n"
        << "  --static_root <path>    Static web root (default auto-detected repo web/)\n"
        << "  --dev                   Enable dev endpoints\n"
        << "  --smoke                 Bind/listen probe, print SMOKE_OK, then shutdown\n"
        << "  --verbose               Log SQLite compile options at startup\n"
        << "  --help                  Show serve help\n";
}

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;
    int start_index = 1;

    if (argc >= 2) {
        const std::string first = argv[1];
        if (first == "serve") {
            args.mode = CliMode::Serve;
            start_index = 2;
        } else if (first == "query") {
            args.mode = CliMode::QueryResolve;
            start_index = 2;
        }
    }

    for (int i = start_index; i < argc; ++i) {
        const std::string current = argv[i];
        if (current == "--help" || current == "-h") {
            if (args.mode == CliMode::Serve) {
                args.serve_help = true;
            } else {
                args.mode = CliMode::Help;
            }
            continue;
        }
        if (current == "--migrate") {
            args.mode = CliMode::Migrate;
            continue;
        }
        if (current == "--domain" && i + 1 < argc) {
            args.domain = argv[++i];
            continue;
        }
        if (current == "--prompt" && i + 1 < argc) {
            args.prompt = argv[++i];
            continue;
        }
        if (current == "--db" && i + 1 < argc) {
            args.db_path = argv[++i];
            continue;
        }
        if (current == "--text" && i + 1 < argc) {
            args.query_text = argv[++i];
            continue;
        }
        if (current == "--top" && i + 1 < argc) {
            args.query_top = std::stoi(argv[++i]);
            continue;
        }
        if (current == "--min_score" && i + 1 < argc) {
            args.min_score = std::stod(argv[++i]);
            continue;
        }
        if (current == "--session" && i + 1 < argc) {
            args.session_id = argv[++i];
            continue;
        }
        if (current == "--proposal_id" && i + 1 < argc) {
            args.proposal_id = argv[++i];
            continue;
        }
        if (current == "--reward" && i + 1 < argc) {
            args.reward = std::stod(argv[++i]);
            args.reward_mode = true;
            continue;
        }
        if (current == "--serve") {
            args.mode = CliMode::Serve;
            continue;
        }
        if (current == "--dev") {
            args.dev_mode = true;
            continue;
        }
        if (current == "--self_test") {
            args.self_test_mode = true;
            args.mode = CliMode::Serve;
            continue;
        }
        if (current == "--smoke") {
            args.smoke_mode = true;
            args.mode = CliMode::Serve;
            continue;
        }
        if (current == "--verbose") {
            args.verbose = true;
            continue;
        }
        if (current == "--host" && i + 1 < argc) {
            args.host = argv[++i];
            continue;
        }
        if (current == "--port" && i + 1 < argc) {
            args.port = std::stoi(argv[++i]);
            continue;
        }
        if ((current == "--static_root" || current == "--static_dir") && i + 1 < argc) {
            args.static_dir = argv[++i];
            continue;
        }
        if (current == "--recompute-aggregates") {
            args.mode = CliMode::RecomputeAggregates;
            continue;
        }
        if (current == "--inspect-cluster" && i + 1 < argc) {
            args.mode = CliMode::InspectCluster;
            args.inspect_query_id = std::stoll(argv[++i]);
            continue;
        }
        if (current == "--inspect-player" && i + 1 < argc) {
            args.mode = CliMode::InspectPlayer;
            args.inspect_player_id = argv[++i];
            continue;
        }
        if (current == "--now" && i + 1 < argc) {
            args.now_unix_seconds = std::stoll(argv[++i]);
            continue;
        }
        if (current == "--rounding_decimals" && i + 1 < argc) {
            args.rounding_decimals = std::stoi(argv[++i]);
            continue;
        }
        throw std::runtime_error("Unknown or incomplete argument: " + current);
    }

    if (args.mode == CliMode::Help || args.serve_help) {
        return args;
    }

    if (args.mode == CliMode::Migrate) {
        return args;
    }

    if (args.mode == CliMode::RecomputeAggregates) {
        return args;
    }

    if (args.mode == CliMode::InspectCluster) {
        if (args.inspect_query_id < 0) {
            throw std::runtime_error("--inspect-cluster requires non-negative query_id");
        }
        return args;
    }

    if (args.mode == CliMode::InspectPlayer) {
        if (args.inspect_player_id.empty()) {
            throw std::runtime_error("--inspect-player requires stable_player_id");
        }
        args.query_top = std::max(1, std::min(args.query_top, 100));
        return args;
    }

    if (args.mode == CliMode::QueryResolve) {
        if (args.query_text.empty()) {
            throw std::runtime_error("query mode requires --text");
        }
        args.query_top = std::max(1, std::min(args.query_top, 25));
        if (args.min_score < 0.0) args.min_score = 0.0;
        if (args.min_score > 1.0) args.min_score = 1.0;
        return args;
    }

    if (args.mode == CliMode::Serve) {
        if (args.smoke_mode && args.port == 8080) {
            args.port = 0;
        }
        if (args.self_test_mode && args.port == 8080) {
            args.port = 0;
        }
        return args;
    }

    if (args.session_id.empty()) {
        args.session_id = generate_session_uuid();
    }

    if (args.reward_mode) {
        if (args.proposal_id.empty()) {
            throw std::runtime_error("Reward mode requires --proposal_id");
        }
        if (args.reward < 0.0 || args.reward > 5.0) {
            throw std::runtime_error("Reward must be in [0,1] or [1,5]");
        }
        if (args.reward > 1.0) {
            args.reward = (args.reward - 1.0) / 4.0;
        }
        return args;
    }

    if (args.domain.empty() || args.prompt.empty()) {
        throw std::runtime_error("Usage: proteus --domain <domain> --prompt <prompt> [--db <path>] [--session <id>]");
    }

    return args;
}


std::int64_t unix_timestamp_now() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::vector<double> DecodeLittleEndianDoubles(const std::vector<std::uint8_t>& blob, std::size_t expected_dims) {
    if (blob.size() != expected_dims * sizeof(double)) {
        throw std::runtime_error("player_preference_vector blob size mismatch");
    }

    std::vector<double> out(expected_dims, 0.0);
    for (std::size_t i = 0; i < expected_dims; ++i) {
        std::array<std::uint8_t, sizeof(double)> bytes{};
        std::memcpy(bytes.data(), blob.data() + i * sizeof(double), sizeof(double));
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
        std::reverse(bytes.begin(), bytes.end());
#endif
        std::memcpy(&out[i], bytes.data(), sizeof(double));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliArgs args = parse_args(argc, argv);

        if (args.mode == CliMode::Help) {
            print_help();
            return 0;
        }

        if (args.serve_help) {
            print_serve_help();
            return 0;
        }

        if (args.mode == CliMode::Migrate) {
            proteus::persistence::SqliteDb db;
            proteus::persistence::open_and_migrate(db, args.db_path, args.verbose);
            std::cout << "MIGRATE_OK version=" << proteus::persistence::kSchemaVersion << "\n";
            return 0;
        }

        if (args.mode == CliMode::Serve) {
            return proteus::playable::run_server(proteus::playable::HttpServerConfig{
                .host = args.host,
                .port = args.port,
                .db_path = args.db_path,
                .static_dir = args.static_dir,
                .dev_mode = args.dev_mode,
                .self_test_mode = args.self_test_mode,
                .smoke_mode = args.smoke_mode,
                .verbose = args.verbose,
            });
        }

        proteus::persistence::SqliteDb db;
        proteus::persistence::open_and_migrate(db, args.db_path, args.verbose);

        if (args.mode == CliMode::RecomputeAggregates) {
            const std::int64_t now = args.now_unix_seconds >= 0 ? args.now_unix_seconds : unix_timestamp_now();
            const proteus::analytics::RecomputeAggregatesOptions opt{
                .now_unix_seconds = now,
                .rounding_decimals = args.rounding_decimals,
            };
            const bool ok = proteus::analytics::RecomputeBehaviorAggregatesDeterministic(db, opt);
            if (!ok) {
                throw std::runtime_error("recompute aggregates failed");
            }
            std::cout << "RECOMPUTE_OK rounding_decimals=" << opt.rounding_decimals << " now=" << opt.now_unix_seconds << "\n";
            return 0;
        }

        if (args.mode == CliMode::InspectCluster) {
            auto stats_stmt = db.prepare(
                "SELECT distinct_players, total_interactions, mean_final_score, entropy_score, divergence_index "
                "FROM query_cluster_stats WHERE query_id=?1;"
            );
            stats_stmt.bind_int64(1, args.inspect_query_id);
            if (!stats_stmt.step()) {
                throw std::runtime_error("cluster not found");
            }
            const auto distinct_players = stats_stmt.column_int64(0);
            const auto total_interactions = stats_stmt.column_int64(1);
            const double mean_final = stats_stmt.column_double(2);
            const double entropy = stats_stmt.column_double(3);
            const double divergence = stats_stmt.column_double(4);

            std::cout << "cluster query_id=" << args.inspect_query_id
                      << " distinct_players=" << distinct_players
                      << " total_interactions=" << total_interactions
                      << " mean_final_score=" << mean_final
                      << " entropy_score=" << entropy
                      << " divergence_index=" << divergence << "\n";

            auto top_stmt = db.prepare(
                "SELECT stable_player_id, mean_final_score FROM query_player_aggregate "
                "WHERE query_id=?1 ORDER BY ABS(mean_final_score - ?2) DESC, stable_player_id ASC LIMIT 10;"
            );
            top_stmt.bind_int64(1, args.inspect_query_id);
            top_stmt.bind_double(2, mean_final);
            while (top_stmt.step()) {
                std::cout << "  player=" << top_stmt.column_text(0) << " mean_final_score=" << top_stmt.column_double(1) << "\n";
            }
            return 0;
        }

        if (args.mode == CliMode::InspectPlayer) {
            auto query_ids_stmt = db.prepare("SELECT query_id FROM query_cluster_stats ORDER BY query_id ASC;");
            std::vector<std::int64_t> query_ids;
            while (query_ids_stmt.step()) {
                query_ids.push_back(query_ids_stmt.column_int64(0));
            }

            auto player_stmt = db.prepare(
                "SELECT vector_blob, dimensionality FROM player_preference_vector WHERE stable_player_id=?1;"
            );
            player_stmt.bind_text(1, args.inspect_player_id);
            if (!player_stmt.step()) {
                throw std::runtime_error("player preference vector not found");
            }

            const auto blob = player_stmt.column_blob(0);
            const auto dims = static_cast<std::size_t>(player_stmt.column_int64(1));
            auto decoded = DecodeLittleEndianDoubles(blob, dims);

            struct Item { std::int64_t query_id; double value; };
            std::vector<Item> values;
            const std::size_t count = std::min(query_ids.size(), decoded.size());
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                values.push_back(Item{query_ids[i], decoded[i]});
            }

            std::stable_sort(values.begin(), values.end(), [](const Item& a, const Item& b) {
                const double aa = std::abs(a.value);
                const double bb = std::abs(b.value);
                if (aa != bb) {
                    return aa > bb;
                }
                return a.query_id < b.query_id;
            });

            const std::size_t top_n = std::min<std::size_t>(static_cast<std::size_t>(args.query_top), values.size());
            std::cout << "player=" << args.inspect_player_id << " top_dimensions=" << top_n << "\n";
            for (std::size_t i = 0; i < top_n; ++i) {
                std::cout << "  query_id=" << values[i].query_id << " value=" << values[i].value << "\n";
            }
            return 0;
        }

        if (args.mode == CliMode::QueryResolve) {
            const auto resolved = proteus::query::ResolveQuery(db, args.query_text, args.query_top, args.min_score);
            nlohmann::json similar = nlohmann::json::array({});
            for (const auto& item : resolved.similar) {
                similar.push_back(nlohmann::json{{"query_id", static_cast<double>(item.query_id)}, {"score", item.score}});
            }
            std::cout << nlohmann::json{{"normalized", resolved.normalized}, {"hash64", std::to_string(resolved.hash64)}, {"query_id", static_cast<double>(resolved.query_id)}, {"similar", similar}}.dump(2) << '\n';
            return 0;
        }

        proteus::playable::BanditSelector selector(db, proteus::playable::kPlayableCorePolicyVersion);

        if (args.reward_mode) {
            proteus::playable::log_reward(db, selector, args.session_id, args.proposal_id, args.reward);
            std::cout << "{\"ok\":true}\n";
            return 0;
        }

        auto response = proteus::playable::run_retrieval(
            db,
            proteus::playable::RetrievalRequest{
                .domain = args.domain,
                .raw_prompt = args.prompt,
                .session_id = args.session_id,
            },
            selector
        );
        response["session_id"] = args.session_id;

        std::cout << response.dump(2) << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
