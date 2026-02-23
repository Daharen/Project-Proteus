#include "proteus/playable/http_server.hpp"

#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/retrieval_engine.hpp"
#include "proteus/query/query_identity.hpp"

#include <nlohmann/json.hpp>

#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#endif

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

int last_socket_error_code() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

std::filesystem::path resolve_static_root(const std::string& configured_static_root) {
    if (!configured_static_root.empty()) {
        return std::filesystem::absolute(std::filesystem::path(configured_static_root));
    }

    std::filesystem::path probe = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        const auto candidate = probe / "web";
        if (std::filesystem::exists(candidate / "index.html")) {
            return std::filesystem::absolute(candidate);
        }
        if (!probe.has_parent_path()) {
            break;
        }
        probe = probe.parent_path();
    }

    return std::filesystem::absolute(std::filesystem::current_path() / "web");
}

bool wait_for_server_ready(const std::string& host, int port, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        httplib::Client client(host, port);
        if (auto res = client.Get("/health")) {
            if (res->status == 200) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

int pick_random_port() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(20000, 45000);
    return dist(rng);
}


std::string query_param(const std::string& path, const std::string& key) {
    const auto qpos = path.find('?');
    if (qpos == std::string::npos) {
        return {};
    }
    const std::string query = path.substr(qpos + 1);
    const std::string needle = key + "=";
    std::size_t start = 0;
    while (start < query.size()) {
        const auto end = query.find('&', start);
        const std::string token = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token.rfind(needle, 0) == 0) {
            return token.substr(needle.size());
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

std::string request_player_id(const httplib::Request& req) {
    return query_param(req.path, "player_id");
}

int request_limit(const httplib::Request& req, int fallback, int min_v, int max_v) {
    const std::string raw = query_param(req.path, "limit");
    if (raw.empty()) {
        return fallback;
    }
    try {
        const int v = std::stoi(raw);
        return std::max(min_v, std::min(max_v, v));
    } catch (const std::exception&) {
        return fallback;
    }
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
        db.exec("DROP TABLE IF EXISTS query_fts;");
        db.exec("DROP TABLE IF EXISTS query_registry;");
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

    svr.Get(R"(/dev/player_state.*)", [config](const httplib::Request& req, httplib::Response& res) {
        if (!config.dev_mode) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"dev mode disabled"})}});
            return;
        }
        const std::string player_id = request_player_id(req);
        if (player_id.empty()) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"player_id required"})}});
            return;
        }

        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);

        std::int64_t query_count = 0;
        std::int64_t reward_count = 0;
        std::int64_t like_count = 0;
        std::int64_t dislike_count = 0;
        double reward_sum = 0.0;
        {
            auto query_count_stmt = db.prepare("SELECT COUNT(*) FROM interaction_log WHERE stable_player_id = ?1;");
            query_count_stmt.bind_text(1, player_id);
            if (query_count_stmt.step()) {
                query_count = query_count_stmt.column_int64(0);
            }
        }
        {
            auto stmt = db.prepare(
                "SELECT COUNT(*) AS reward_count, "
                "SUM(CASE WHEN reward_signal > 0 THEN 1 ELSE 0 END) AS like_count, "
                "SUM(CASE WHEN reward_signal <= 0 THEN 1 ELSE 0 END) AS dislike_count, "
                "COALESCE(SUM(reward_signal), 0) AS reward_sum "
                "FROM interaction_log WHERE stable_player_id = ?1 AND reward_applied = 1;"
            );
            stmt.bind_text(1, player_id);
            if (stmt.step()) {
                reward_count = stmt.column_int64(0);
                like_count = stmt.column_int64(1);
                dislike_count = stmt.column_int64(2);
                reward_sum = stmt.column_double(3);
            }
        }

        std::int64_t last_timestamp = 0;
        std::string last_prompt_hash;
        std::string last_proposal_id;
        {
            auto stmt = db.prepare(
                "SELECT timestamp, prompt_hash, chosen_arm FROM interaction_log "
                "WHERE stable_player_id = ?1 ORDER BY id DESC LIMIT 1;"
            );
            stmt.bind_text(1, player_id);
            if (stmt.step()) {
                last_timestamp = stmt.column_int64(0);
                last_prompt_hash = stmt.column_is_null(1) ? std::string{} : stmt.column_text(1);
                last_proposal_id = stmt.column_is_null(2) ? std::string{} : stmt.column_text(2);
            }
        }

        send_json(res, 200, nlohmann::json{
            {"ok", true},
            {"player_id", player_id},
            {"totals", nlohmann::json{
                {"query_count", static_cast<double>(query_count)},
                {"reward_count", static_cast<double>(reward_count)},
                {"like_count", static_cast<double>(like_count)},
                {"dislike_count", static_cast<double>(dislike_count)},
                {"reward_sum", reward_sum},
            }},
            {"last_seen", nlohmann::json{
                {"timestamp", static_cast<double>(last_timestamp)},
                {"prompt_hash", last_prompt_hash.empty() ? nlohmann::json(nullptr) : nlohmann::json(last_prompt_hash)},
                {"proposal_id", last_proposal_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(last_proposal_id)},
            }},
        });
    });

    svr.Get(R"(/dev/recent_interactions.*)", [config](const httplib::Request& req, httplib::Response& res) {
        if (!config.dev_mode) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"dev mode disabled"})}});
            return;
        }
        const std::string player_id = request_player_id(req);
        if (player_id.empty()) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"player_id required"})}});
            return;
        }
        const int limit = request_limit(req, 25, 1, 100);

        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);

        auto stmt = db.prepare(
            "SELECT id, timestamp, prompt_hash, chosen_arm, base_score, topology_modifier, final_score, reward_signal "
            "FROM interaction_log WHERE stable_player_id = ?1 ORDER BY id DESC LIMIT ?2;"
        );
        stmt.bind_text(1, player_id);
        stmt.bind_int64(2, limit);

        nlohmann::json rows = nlohmann::json::array({});
        while (stmt.step()) {
            rows.push_back(nlohmann::json{
                {"id", static_cast<double>(stmt.column_int64(0))},
                {"created_at", static_cast<double>(stmt.column_int64(1))},
                {"prompt_hash", stmt.column_is_null(2) ? nlohmann::json(nullptr) : nlohmann::json(stmt.column_text(2))},
                {"proposal_id", stmt.column_is_null(3) ? nlohmann::json(nullptr) : nlohmann::json(stmt.column_text(3))},
                {"base_score", stmt.column_is_null(4) ? 0.0 : stmt.column_double(4)},
                {"topology_modifier", stmt.column_is_null(5) ? 0.0 : stmt.column_double(5)},
                {"final_score", stmt.column_is_null(6) ? 0.0 : stmt.column_double(6)},
                {"reward", stmt.column_is_null(7) ? nlohmann::json(nullptr) : nlohmann::json(stmt.column_double(7))},
            });
        }

        send_json(res, 200, nlohmann::json{{"ok", true}, {"player_id", player_id}, {"limit", static_cast<double>(limit)}, {"rows", rows}});
    });


    svr.Post("/api/query/resolve", [config](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
            return;
        }
        if (!body.contains("text") || !body.at("text").is_string()) {
            send_json(res, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"text is required"})}});
            return;
        }

        const int limit = body.contains("limit") && body.at("limit").is_number()
            ? std::max(1, std::min(25, static_cast<int>(body.at("limit").get<double>())))
            : 5;
        const double min_score = body.contains("min_score") && body.at("min_score").is_number()
            ? std::max(0.0, std::min(1.0, body.at("min_score").get<double>()))
            : 0.2;

        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);

        const auto resolved = query::ResolveQuery(db, body.at("text").get<std::string>(), limit, min_score);
        nlohmann::json similar = nlohmann::json::array({});
        for (const auto& item : resolved.similar) {
            similar.push_back(nlohmann::json{{"query_id", static_cast<double>(item.query_id)}, {"score", item.score}});
        }

        send_json(res, 200, nlohmann::json{{"ok", true}, {"query_id", static_cast<double>(resolved.query_id)}, {"normalized", resolved.normalized}, {"hash64", std::to_string(resolved.hash64)}, {"similar", similar}});
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

        const auto context = parse_player_context(body);
        const auto result = run_retrieval_detailed(
            db,
            RetrievalRequest{
                .domain = body.at("domain").get<std::string>(),
                .raw_prompt = body.at("prompt").get<std::string>(),
                .session_id = session_id,
                .player_context = context,
            },
            selector
        );
        const std::string stable_player_id = context.stable_player_id.empty() ? session_id : context.stable_player_id;

        send_json(res, 200, nlohmann::json{
            {"ok", true},
            {"session_id", result.session_id},
            {"prompt_hash", result.prompt_hash},
            {"decision", nlohmann::json{
                {"proposal_id", result.decision.proposal_id},
                {"stable_player_id", stable_player_id},
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

    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path resolved_static_root = resolve_static_root(config.static_dir);
    config.static_dir = resolved_static_root.string();
    if ((config.smoke_mode || config.self_test_mode) && config.port == 0) {
        config.port = pick_random_port();
    }

    std::cout << "Server startup diagnostics:\n"
              << "  host=" << config.host << "\n"
              << "  port=" << config.port << "\n"
              << "  static_root=" << resolved_static_root.string() << "\n"
              << "  cwd=" << cwd.string() << "\n";

    persistence::SqliteDb db;
    persistence::open_and_migrate(db, config.db_path);

    httplib::Server svr;
    svr.set_payload_max_length(kMaxBodyBytes);
    register_routes(svr, config);

    std::atomic<bool> listen_success{false};
    std::atomic<int> listen_error{0};

    std::thread server_thread([&]() {
        const bool ok = svr.listen(config.host.c_str(), config.port);
        listen_success.store(ok);
        if (!ok) {
            listen_error.store(last_socket_error_code());
        }
    });

    if (!wait_for_server_ready(config.host, config.port, 3000)) {
        const int os_error = listen_error.load() != 0 ? listen_error.load() : last_socket_error_code();
        std::cerr << "Server bind/listen failed: os_error=" << os_error << "\n";
        svr.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
        return 2;
    }

    std::cout << "Listening on http://127.0.0.1:" << config.port << "/\n";

    if (config.smoke_mode) {
        std::cout << "SMOKE_OK\n";
        svr.stop();
        server_thread.join();
        return 0;
    }

    if (config.self_test_mode) {
        try {
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

    server_thread.join();
    if (!listen_success.load()) {
        const int os_error = listen_error.load() != 0 ? listen_error.load() : last_socket_error_code();
        std::cerr << "Server listen loop exited with failure: os_error=" << os_error << "\n";
        return 3;
    }
    return 0;
}

}  // namespace proteus::playable
