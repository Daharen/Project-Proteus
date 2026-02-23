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

struct CliArgs {
    std::string domain;
    std::string prompt;
    std::string session_id;
    std::string proposal_id;
    std::string db_path = "./proteus.db";
    std::string host = "127.0.0.1";
    std::string static_dir = "./web";
    int port = 8080;
    double reward = -1.0;
    bool reward_mode = false;
    bool serve_mode = false;
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

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;

    for (int i = 1; i < argc; ++i) {
        const std::string current = argv[i];
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
            args.serve_mode = true;
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
        if (current == "--static_dir" && i + 1 < argc) {
            args.static_dir = argv[++i];
            continue;
        }
        throw std::runtime_error("Unknown or incomplete argument: " + current);
    }

    if (args.serve_mode) {
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

        if (args.serve_mode) {
            return proteus::playable::run_server(proteus::playable::HttpServerConfig{
                .host = args.host,
                .port = args.port,
                .db_path = args.db_path,
                .static_dir = args.static_dir,
            });
        }

        proteus::persistence::SqliteDb db;
        db.open(args.db_path);
        proteus::persistence::ensure_schema(db);
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
