#include "proteus/playable/http_server.hpp"

#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/retrieval_engine.hpp"
#include "proteus/playable/prompt_cache.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace proteus::playable {

namespace {

constexpr std::size_t kMaxBodyBytes = 1024 * 1024;

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

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

std::string read_file_or_empty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        default: return "Internal Server Error";
    }
}

void send_raw_response(int client_fd, int status, const std::string& body, const std::string& content_type) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reason_phrase(status) << "\r\n";
    out << "Content-Type: " << content_type << "\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Access-Control-Allow-Headers: Content-Type\r\n";
    out << "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n\r\n";
    out << body;
    const auto text = out.str();
    ::send(client_fd, text.c_str(), text.size(), 0);
}

void send_response(int client_fd, int status, const nlohmann::json& payload) {
    send_raw_response(client_fd, status, payload.dump(2), "application/json");
}

void send_text(int client_fd, int status, const std::string& body, const std::string& content_type) {
    send_raw_response(client_fd, status, body, content_type);
}

HttpRequest parse_request(int client_fd) {
    std::string raw;
    char buffer[4096];

    while (raw.find("\r\n\r\n") == std::string::npos) {
        const auto n = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            throw std::runtime_error("Failed reading request headers");
        }
        raw.append(buffer, static_cast<std::size_t>(n));
        if (raw.size() > kMaxBodyBytes + 8192) {
            throw std::runtime_error("Request too large");
        }
    }

    const auto header_end = raw.find("\r\n\r\n");
    const std::string header_blob = raw.substr(0, header_end);
    std::string body = raw.substr(header_end + 4);

    std::istringstream headers_stream(header_blob);
    std::string line;
    HttpRequest req;

    if (!std::getline(headers_stream, line)) {
        throw std::runtime_error("Malformed request line");
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        std::istringstream ls(line);
        std::string version;
        ls >> req.method >> req.path >> version;
        if (req.method.empty() || req.path.empty() || version.rfind("HTTP/", 0) != 0) {
            throw std::runtime_error("Malformed request line");
        }
    }

    while (std::getline(headers_stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        req.headers[trim(line.substr(0, pos))] = trim(line.substr(pos + 1));
    }

    if (req.headers.count("Transfer-Encoding") > 0) {
        throw std::runtime_error("chunked encoding unsupported");
    }

    std::size_t content_length = 0;
    if (req.headers.count("Content-Length") > 0) {
        content_length = static_cast<std::size_t>(std::stoull(req.headers["Content-Length"]));
    }
    if (content_length > kMaxBodyBytes) {
        throw std::runtime_error("Request body too large");
    }

    while (body.size() < content_length) {
        const auto n = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            throw std::runtime_error("Failed reading request body");
        }
        body.append(buffer, static_cast<std::size_t>(n));
    }
    if (body.size() > content_length) {
        body.resize(content_length);
    }

    req.body = std::move(body);
    return req;
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

nlohmann::json features_to_json(const std::vector<double>& v) {
    nlohmann::json arr = nlohmann::json::array({});
    for (double x : v) arr.push_back(x);
    return arr;
}

void handle_route(
    int client_fd,
    const HttpRequest& req,
    const HttpServerConfig& config,
    const std::string& index_html,
    const std::string& app_js,
    const std::string& style_css
) {
    if (req.method == "OPTIONS") {
        send_text(client_fd, 200, "", "text/plain");
        return;
    }

    if (req.method == "GET" && req.path == "/") {
        if (index_html.empty()) {
            send_response(client_fd, 500, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Missing web/index.html"})}});
        } else {
            send_text(client_fd, 200, index_html, "text/html; charset=utf-8");
        }
        return;
    }
    if (req.method == "GET" && req.path == "/app.js") {
        send_text(client_fd, 200, app_js, "application/javascript");
        return;
    }
    if (req.method == "GET" && req.path == "/style.css") {
        send_text(client_fd, 200, style_css, "text/css");
        return;
    }
    if (req.method == "GET" && req.path == "/health") {
        send_response(client_fd, 200, nlohmann::json{{"ok", true}, {"version", "phase_4_2"}, {"policy_version", kPlayableCorePolicyVersion}});
        return;
    }

    if (req.method == "POST" && req.path == "/dev/reset") {
        if (!config.dev_mode) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"dev mode disabled"})}});
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
        send_response(client_fd, 200, nlohmann::json{{"ok", true}});
        return;
    }

    if (req.method == "POST" && req.path == "/dev/stats") {
        if (!config.dev_mode) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"dev mode disabled"})}});
            return;
        }
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
            return;
        }
        if (!body.contains("proposal_id") || !body.at("proposal_id").is_string()) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"proposal_id required"})}});
            return;
        }
        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);
        const auto stats = get_proposal_stats(db, body.at("proposal_id").get<std::string>());
        if (!stats.has_value()) {
            send_response(client_fd, 200, nlohmann::json{{"ok", true}, {"found", false}});
            return;
        }
        send_response(client_fd, 200, nlohmann::json{{"ok", true}, {"found", true}, {"stats", {
            {"proposal_id", stats->proposal_id},
            {"shown_count", static_cast<double>(stats->shown_count)},
            {"reward_sum", stats->reward_sum},
            {"reward_count", static_cast<double>(stats->reward_count)}
        }}});
        return;
    }

    if (req.method == "POST" && req.path == "/query") {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
            return;
        }

        if (!body.contains("domain") || !body.at("domain").is_string() ||
            !body.contains("prompt") || !body.at("prompt").is_string()) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"domain and prompt are required strings"})}});
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

        send_response(
            client_fd,
            200,
            nlohmann::json{
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
                }},
                {"proposal", result.proposal},
                {"candidate_count", static_cast<double>(list_prompt_candidate_ids(db, result.prompt_hash).size())},
            }
        );
        return;
    }

    if (req.method == "POST" && req.path == "/reward") {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
            return;
        }

        if (!body.contains("session_id") || !body.at("session_id").is_string() ||
            !body.contains("proposal_id") || !body.at("proposal_id").is_string() ||
            !body.contains("reward") || !body.at("reward").is_number()) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"session_id/proposal_id/reward required"})}});
            return;
        }

        double reward = body.at("reward").get<double>();
        if (reward < 0.0 || reward > 1.0) {
            send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"reward must be in [0,1]"})}});
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
            send_response(client_fd, 200, nlohmann::json{{"ok", true}, {"updated", false}, {"errors", nlohmann::json::array({"No matching interaction"})}});
            return;
        }
        const bool already_applied = before->reward_applied != 0;
        log_reward(db, selector, sid, pid, reward);
        const auto after = latest_interaction_for_session_and_arm(db, sid, pid);
        const bool updated = !already_applied && after.has_value() && after->reward_applied != 0;
        send_response(client_fd, 200, nlohmann::json{{"ok", true}, {"updated", updated}});
        return;
    }

    send_response(client_fd, 404, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Unknown route"})}});
}

int start_server_socket(const HttpServerConfig& config) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw std::runtime_error("socket() failed");
    }

    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.port));
    if (::inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) <= 0) {
        ::close(server_fd);
        throw std::runtime_error("Invalid host for bind");
    }

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(server_fd);
        throw std::runtime_error("bind() failed");
    }
    if (::listen(server_fd, 16) < 0) {
        ::close(server_fd);
        throw std::runtime_error("listen() failed");
    }
    return server_fd;
}

void server_loop(int server_fd, const HttpServerConfig& config, std::atomic<bool>* stop_flag) {
    const auto index_html = read_file_or_empty(config.static_dir + "/index.html");
    const auto app_js = read_file_or_empty(config.static_dir + "/app.js");
    const auto style_css = read_file_or_empty(config.static_dir + "/style.css");

    while (!stop_flag || !stop_flag->load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        const int ready = ::select(server_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            continue;
        }

        timeval tv{};
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        try {
            const auto req = parse_request(client_fd);
            handle_route(client_fd, req, config, index_html, app_js, style_css);
        } catch (const std::exception& ex) {
            send_response(client_fd, 500, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({ex.what()})}});
        }

        ::close(client_fd);
    }
}

HttpClientResponse client_request(int port, const std::string& method, const std::string& path, const nlohmann::json& body = nlohmann::json{}) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("self_test client socket failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        ::close(fd);
        throw std::runtime_error("self_test inet_pton failed");
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("self_test connect failed");
    }

    std::string payload;
    if (method == "POST") {
        payload = body.dump();
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: 127.0.0.1\r\n";
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << payload.size() << "\r\n";
    req << "Connection: close\r\n\r\n";
    req << payload;

    const auto text = req.str();
    ::send(fd, text.c_str(), text.size(), 0);

    std::string response;
    char buf[4096];
    while (true) {
        const auto n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        response.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);

    const auto split = response.find("\r\n\r\n");
    if (split == std::string::npos) {
        throw std::runtime_error("self_test invalid HTTP response");
    }
    const auto head = response.substr(0, split);
    const auto body_text = response.substr(split + 4);

    std::istringstream hs(head);
    std::string status_line;
    std::getline(hs, status_line);
    std::istringstream sl(status_line);
    std::string http;
    int status = 0;
    sl >> http >> status;

    HttpClientResponse out;
    out.status = status;
    if (!body_text.empty()) {
        out.json = nlohmann::json::parse(body_text);
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

    const int server_fd = start_server_socket(config);
    std::atomic<bool> stop_flag{false};
    std::thread thread([&]() {
        server_loop(server_fd, config, &stop_flag);
    });

    if (config.self_test_mode) {
        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const int rc = run_self_test(config);
            stop_flag.store(true);
            ::close(server_fd);
            thread.join();
            return rc;
        } catch (...) {
            stop_flag.store(true);
            ::close(server_fd);
            thread.join();
            throw;
        }
    }

    thread.join();
    ::close(server_fd);
    return 0;
}

}  // namespace proteus::playable
