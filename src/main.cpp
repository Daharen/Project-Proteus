#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
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
    std::string db_path = "./proteus.db";
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
        throw std::runtime_error("Unknown or incomplete argument: " + current);
    }

    if (args.domain.empty() || args.prompt.empty()) {
        throw std::runtime_error("Usage: proteus --domain <domain> --prompt <prompt> [--db <path>] [--session <id>]");
    }

    if (args.session_id.empty()) {
        args.session_id = generate_session_uuid();
    }

    return args;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliArgs args = parse_args(argc, argv);

        proteus::persistence::SqliteDb db;
        db.open(args.db_path);
        proteus::persistence::ensure_schema(db);

        const proteus::playable::DeterministicSelector selector;
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
