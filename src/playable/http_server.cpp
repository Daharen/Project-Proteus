#include "proteus/playable/http_server.hpp"

#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/retrieval_engine.hpp"

#include <nlohmann/json.hpp>

#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace proteus::playable {

namespace {

constexpr std::size_t kMaxBodyBytes = 1024 * 1024;

struct HttpClientResponse {
    int status = 0;
    nlohmann::json json;
};

std::string generate_session_id() {
    std::mt19937_64 rng(std::random_device{}());
    std::ostringstream out;
    out << std::hex << rng();
    return out.str();
}

void add_cors_headers(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
}

void send_json(httplib::Response& res, int status, const nlohmann::json& payload) {
    res.status = status;
    res.set_content(payload.dump(2), "application/json");
    add_cors_headers(res);
}

std::string read_file_or_empty(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bandits::PlayerContext parse_player_context(const nlohmann::json& body) {
    bandits::PlayerContext ctx;
    if (!body.contains("player_context") || !body.at("player_context").is_object()) {
        return ctx;
    }
    const auto& pc = body.at("player_context");
    if (pc.contains("identity_confidence") && pc.at("identity_confidence").is_number()) {
        ctx.identity_confidence = static_cast<float>(pc.at("identity_confidence").get<double>());
    }
    if (pc.contains("identity_entropy") && pc.at("identity_entropy").is_number()) {
        ctx.identity_entropy = static_cast<float>(pc.at("identity_entropy").get<double>());
    }
    if (pc.contains("idk_rate") && pc.at("idk_rate").is_number()) {
        ctx.idk_rate = static_cast<float>(pc.at("idk_rate").get<double>());
    }
    if (pc.contains("questions_answered") && pc.at("questions_answered").is_number()) {
        ctx.questions_answered = static_cast<std::uint32_t>(pc.at("questions_answered").get<double>());
    }
    if (pc.contains("stable_player_id") && pc.at("stable_player_id").is_string()) {
        ctx.stable_player_id = pc.at("stable_player_id").get<std::string>();
    }
    if (pc.contains("identity_axes") && pc.at("identity_axes").is_array()) {
        const auto& axes = pc.at("identity_axes");
        std::size_t i = 0;
        for (const auto& axis : axes) {
            if (i >= ctx.identity_axes.size()) {
                break;
            }
            if (axis.is_number()) {
                ctx.identity_axes[i] = static_cast<float>(axis.get<double>());
            }
            ++i;
        }
    }
    return ctx;
}

bool json_bool(const nlohmann::json& obj, const char* key, bool fallback) {
    if (!obj.is_object() || !obj.contains(key)) {
        return fallback;
    }
    try {
        return obj.at(key).get<bool>();
    } catch (const std::exception&) {
        return fallback;
    }
}

bool almost_equal(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) <= tol;
}

nlohmann::json candidate_scores_to_json(const std::vector<CandidateScoreDebug>& scores) {
    nlohmann::json arr = nlohmann::json::array({});
    for (const auto& score : scores) {
        arr.push_back(nlohmann::json{{"proposal_id", score.proposal_id}, {"base", score.base_score}, {"modifier", score.modifier}, {"topology", score.topology_score}, {"governor_factor", score.governor_factor}, {"final", score.final_score}});
    }
    return arr;
}

nlohmann::json features_to_json(const std::vector<double>& v) {
    nlohmann::json arr = nlohmann::json::array({});
    for (double x : v) {
        arr.push_back(x);
    }
    return arr;
}

void register_routes(httplib::Server& svr, const HttpServerConfig& config) {
    const std::filesystem::path static_dir(config.static_dir);

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content("", "text/plain");
        add_cors_headers(res);
    });

    svr.Get("/", [static_dir](const httplib::Request&, httplib::Response& res) {
        const auto index_html = read_file_or_empty(static_dir / "index.html");
        if (index_html.empty()) {
            send_json(res, 500, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Missing web/index.html"})}});
            return;
        }
        res.status = 200;
        res.set_content(index_html, "text/html; charset=utf-8");
        add_cors_headers(res);
    });

    svr.Get("/app.js", [static_dir](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(read_file_or_empty(static_dir / "app.js"), "application/javascript");
        add_cors_headers(res);
    });

    svr.Get("/style.css", [static_dir](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(read_file_or_empty(static_dir / "style.css"), "text/css");
        add_cors_headers(res);
    });

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, 200, nlohmann::json{{"ok", true}, {"version", "phase_4_2"}, {"policy_version", kPlayableCorePolicyVersion}});
    });

    svr.Post("/dev/reset", [config](const httplib::Request&, httplib::Response& res) {
        if (!config.dev_mode) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"dev mode disabled"})}});
            return;
        }
        persistence::SqliteDb db;
        db.open(config.db_path);
        db.exec("DROP TABLE IF EXISTS prompt_meta;");
        db.exec("DROP TABLE IF EXISTS prompt_candidates;");
        db.exec("DROP TABLE IF EXISTS interaction_log;");
        db.exec("DROP TABLE IF EXISTS prompt_cache;");
        db.exec("DROP TABLE IF EXISTS proposal_registry;");
        db.exec("DROP TABLE IF EXISTS proposal_stats;");
        db.exec("DROP TABLE IF EXISTS bandit_state;");
        db.exec("DROP TABLE IF EXISTS meta;");
        persistence::ensure_schema(db);
        send_json(res, 200, nlohmann::json{{"ok", true}});
    });

    svr.Post("/dev/stats", [config](const httplib::Request& req, httplib::Response& res) {
        if (!config.dev_mode) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"dev mode disabled"})}});
            return;
        }
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
            return;
        }
        if (!body.contains("proposal_id") || !body.at("proposal_id").is_string()) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"proposal_id required"})}});
            return;
        }

        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);
        const auto stats = get_proposal_stats(db, body.at("proposal_id").get<std::string>());
        if (!stats.has_value()) {
            send_json(res, 200, nlohmann::json{{"ok", true}, {"found", false}});
            return;
        }

        send_json(res, 200, nlohmann::json{{"ok", true}, {"found", true}, {"stats", {
            {"proposal_id", stats->proposal_id},
            {"shown_count", static_cast<double>(stats->shown_count)},
            {"reward_sum", stats->reward_sum},
            {"reward_count", static_cast<double>(stats->reward_count)}
        }}});
    });

    svr.Post("/query", [config](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
            return;
        }

        if (!body.contains("domain") || !body.at("domain").is_string() ||
            !body.contains("prompt") || !body.at("prompt").is_string()) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"domain and prompt are required strings"})}});
            return;
        }

        const std::string session_id = (body.contains("session_id") && body.at("session_id").is_string())
            ? body.at("session_id").get<std::string>()
            : generate_session_id();

        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);
        BanditSelector selector(db, kPlayableCorePolicyVersion);

        const auto result = run_retrieval_detailed(
            db,
            RetrievalRequest{
                .domain = body.at("domain").get<std::string>(),
                .raw_prompt = body.at("prompt").get<std::string>(),
                .session_id = session_id,
                .player_context = parse_player_context(body),
            },
            selector
        );

        send_json(res, 200, nlohmann::json{
            {"ok", true},
            {"session_id", result.session_id},
            {"prompt_hash", result.prompt_hash},
            {"decision", nlohmann::json{
                {"proposal_id", result.decision.proposal_id},
                {"explored", result.decision.explored},
                {"epsilon", result.decision.epsilon_used},
                {"selection_seed", std::to_string(result.decision.selection_seed)},
                {"selection_seed_hex", "0x" + ([](std::int64_t v){ std::ostringstream o; o << std::hex << static_cast<unsigned long long>(v); return o.str(); })(result.decision.selection_seed)},
                {"decision_features", features_to_json(result.decision.decision_features)},
                {"topology_seed", result.decision.topology_seed},
                {"topology_adjustments", nlohmann::json{{"bandit_scoring_weight", nlohmann::json{{"base", result.decision.base_score}, {"modifier", result.decision.topology_modifier}, {"final", result.decision.base_score * (1.0 + result.decision.topology_modifier)}}}}},
                {"governor", nlohmann::json{{"factor", result.decision.governor_factor}, {"reason", result.decision.governor_reason}}},
                {"final_score", result.decision.final_score},
                {"candidate_scores", candidate_scores_to_json(result.decision.candidate_scores)},
            }},
            {"proposal", result.proposal},
            {"candidate_count", static_cast<double>(list_prompt_candidate_ids(db, result.prompt_hash).size())},
        });
    });

    svr.Post("/reward", [config](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
            return;
        }

        if (!body.contains("session_id") || !body.at("session_id").is_string() ||
            !body.contains("proposal_id") || !body.at("proposal_id").is_string() ||
            !body.contains("reward") || !body.at("reward").is_number()) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"session_id/proposal_id/reward required"})}});
            return;
        }

        const double reward = body.at("reward").get<double>();
        if (reward < 0.0 || reward > 1.0) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"reward must be in [0,1]"})}});
            return;
        }

        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);
        BanditSelector selector(db, kPlayableCorePolicyVersion);
        const auto sid = body.at("session_id").get<std::string>();
        const auto pid = body.at("proposal_id").get<std::string>();
        const auto before = latest_interaction_for_session_and_arm(db, sid, pid);
        if (!before.has_value()) {
            send_json(res, 200, nlohmann::json{{"ok", true}, {"updated", false}, {"errors", nlohmann::json::array({"No matching interaction"})}});
            return;
        }
        const bool already_applied = before->reward_applied != 0;
        log_reward(db, selector, sid, pid, reward);
        const auto after = latest_interaction_for_session_and_arm(db, sid, pid);
        const bool updated = !already_applied && after.has_value() && after->reward_applied != 0;
        send_json(res, 200, nlohmann::json{{"ok", true}, {"updated", updated}});
    });
}

HttpClientResponse client_request(int port, const std::string& method, const std::string& path, const nlohmann::json& body = nlohmann::json{}) {
    httplib::Client cli("127.0.0.1", port);
    std::shared_ptr<httplib::Result> result;

    if (method == "POST") {
        result = cli.Post(path, body.dump(), "application/json");
    } else {
        result = cli.Get(path);
    }

    if (!result) {
        throw std::runtime_error("self_test HTTP request failed");
    }

    HttpClientResponse out;
    out.status = result->status;
    if (!result->body.empty()) {
        out.json = nlohmann::json::parse(result->body);
    }
    return out;
}

int run_self_test(const HttpServerConfig& config) {
    const int port = config.port;
    const std::string session = "self-test-session";

    auto health = client_request(port, "GET", "/health");
    if (health.status != 200 || !json_bool(health.json, "ok", false)) {
        throw std::runtime_error("self_test health failed");
    }

    auto query = client_request(port, "POST", "/query", nlohmann::json{
        {"domain", "rpg"},
        {"prompt", "find me a quest"},
        {"session_id", session}
    });
    if (query.status != 200 || !json_bool(query.json, "ok", false)) {
        throw std::runtime_error("self_test query failed");
    }

    const std::string proposal_id = query.json.at("decision").at("proposal_id").get<std::string>();

    auto reward1 = client_request(port, "POST", "/reward", nlohmann::json{
        {"session_id", session},
        {"proposal_id", proposal_id},
        {"reward", 1.0}
    });
    if (reward1.status != 200 || !json_bool(reward1.json, "updated", false)) {
        throw std::runtime_error("self_test first reward update failed");
    }

    auto reward2 = client_request(port, "POST", "/reward", nlohmann::json{
        {"session_id", session},
        {"proposal_id", proposal_id},
        {"reward", 1.0}
    });
    if (reward2.status != 200 || json_bool(reward2.json, "updated", true)) {
        throw std::runtime_error("self_test reward dedupe failed");
    }

    auto stats = client_request(port, "POST", "/dev/stats", nlohmann::json{{"proposal_id", proposal_id}});
    if (stats.status != 200 || !json_bool(stats.json, "found", false)) {
        throw std::runtime_error("self_test stats lookup failed");
    }
    if (stats.json.at("stats").at("shown_count").get<double>() < 1.0 || stats.json.at("stats").at("reward_count").get<double>() < 1.0) {
        throw std::runtime_error("self_test stats assertions failed");
    }

    const nlohmann::json player_a_ctx = {
        {"identity_axes", nlohmann::json::array({0.80, -0.55, 0.65, 0.25, -0.20, 0.40, -0.35, 0.15})},
        {"stable_player_id", "player-A"}
    };
    const nlohmann::json player_b_ctx = {
        {"identity_axes", nlohmann::json::array({-0.70, 0.45, -0.30, 0.75, 0.55, -0.40, 0.20, -0.65})},
        {"stable_player_id", "player-B"}
    };

    auto topo_a = client_request(port, "POST", "/query", nlohmann::json{
        {"domain", "rpg"},
        {"prompt", "topology divergence proof prompt"},
        {"session_id", "topology-proof-session"},
        {"player_context", player_a_ctx}
    });
    auto topo_b = client_request(port, "POST", "/query", nlohmann::json{
        {"domain", "rpg"},
        {"prompt", "topology divergence proof prompt"},
        {"session_id", "topology-proof-session"},
        {"player_context", player_b_ctx}
    });

    if (topo_a.status != 200 || topo_b.status != 200 || !json_bool(topo_a.json, "ok", false) || !json_bool(topo_b.json, "ok", false)) {
        throw std::runtime_error("self_test topology proof query failed");
    }

    const auto& dec_a = topo_a.json.at("decision");
    const auto& dec_b = topo_b.json.at("decision");
    const std::string seed_a = dec_a.at("topology_seed").get<std::string>();
    const std::string seed_b = dec_b.at("topology_seed").get<std::string>();
    const auto& adj_a = dec_a.at("topology_adjustments").at("bandit_scoring_weight");
    const auto& adj_b = dec_b.at("topology_adjustments").at("bandit_scoring_weight");

    const double base_a = adj_a.at("base").get<double>();
    const double mod_a = adj_a.at("modifier").get<double>();
    const double final_a = adj_a.at("final").get<double>();
    const double base_b = adj_b.at("base").get<double>();
    const double mod_b = adj_b.at("modifier").get<double>();
    const double final_b = adj_b.at("final").get<double>();

    if (seed_a == seed_b) {
        throw std::runtime_error("self_test topology proof failed: identical topology seeds");
    }
    if (mod_a == mod_b) {
        throw std::runtime_error("self_test topology proof failed: identical modifiers");
    }
    if (std::abs(mod_a) > 0.08 || std::abs(mod_b) > 0.08) {
        throw std::runtime_error("self_test topology proof failed: modifier out of bounds");
    }
    if (!almost_equal(final_a, base_a * (1.0 + mod_a)) || !almost_equal(final_b, base_b * (1.0 + mod_b))) {
        throw std::runtime_error("self_test topology proof failed: final score equation mismatch");
    }

    bool divergence_observed = false;
    const auto& scores_a = dec_a.at("candidate_scores");
    const auto& scores_b = dec_b.at("candidate_scores");
    for (const auto& ca : scores_a) {
        const auto proposal = ca.at("proposal_id").get<std::string>();
        for (const auto& cb : scores_b) {
            if (cb.at("proposal_id").get<std::string>() != proposal) {
                continue;
            }
            if (!almost_equal(ca.at("final").get<double>(), cb.at("final").get<double>())) {
                divergence_observed = true;
                break;
            }
        }
        if (divergence_observed) {
            break;
        }
    }
    if (!divergence_observed) {
        throw std::runtime_error("self_test topology proof failed: no per-candidate divergence");
    }

    std::cout << "[topology_proof] A seed=" << seed_a
              << " modifier=" << mod_a
              << " base=" << base_a
              << " final=" << final_a
              << " proposal=" << dec_a.at("proposal_id").get<std::string>()
              << "\n";
    std::cout << "[topology_proof] B seed=" << seed_b
              << " modifier=" << mod_b
              << " base=" << base_b
              << " final=" << final_b
              << " proposal=" << dec_b.at("proposal_id").get<std::string>()
              << "\n";

    return 0;
}

}  // namespace

int run_server(const HttpServerConfig& input_config) {
    HttpServerConfig config = input_config;
    if (config.self_test_mode && !config.dev_mode) {
        config.dev_mode = true;
    }

    persistence::SqliteDb db;
    db.open(config.db_path);
    persistence::ensure_schema(db);

    httplib::Server svr;
    svr.set_payload_max_length(kMaxBodyBytes);
    register_routes(svr, config);

    if (config.self_test_mode) {
        std::thread server_thread([&]() {
            svr.listen(config.host.c_str(), config.port);
        });

        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const int rc = run_self_test(config);
            svr.stop();
            server_thread.join();
            return rc;
        } catch (...) {
            svr.stop();
            server_thread.join();
            throw;
        }
    }

    svr.listen(config.host.c_str(), config.port);
    return 0;
}

}  // namespace proteus::playable
