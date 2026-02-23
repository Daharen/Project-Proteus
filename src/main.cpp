#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/http_server.hpp"
#include "proteus/playable/retrieval_engine.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {

enum class CliMode {
    Query,
    Serve,
    Migrate,
    Help,
};

struct CliArgs {
    CliMode mode = CliMode::Query;
    bool serve_help = false;
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
        << "  proteus --migrate [--db <path>]\n"
        << "  proteus serve [--host 127.0.0.1] [--port 8080] [--db <path>] [--static_root <path>] [--dev] [--smoke]\n"
        << "\n"
        << "Modes:\n"
        << "  query (default): use --domain/--prompt\n"
        << "  serve: start local web server and API\n"
        << "  migrate: apply DB migrations to latest schema version\n";
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
        throw std::runtime_error("Unknown or incomplete argument: " + current);
    }

    if (args.mode == CliMode::Help || args.serve_help) {
        return args;
    }

    if (args.mode == CliMode::Migrate) {
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
            proteus::persistence::open_and_migrate(db, args.db_path);
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
            });
        }

        proteus::persistence::SqliteDb db;
        proteus::persistence::open_and_migrate(db, args.db_path);
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
