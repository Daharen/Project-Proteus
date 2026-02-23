#include "proteus/playable/http_server.hpp"

#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/retrieval_engine.hpp"
#include "proteus/playable/prompt_cache.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
#include <unordered_map>

namespace proteus::playable {

namespace {

constexpr std::size_t kMaxBodyBytes = 1024 * 1024;

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

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::string trim(const std::string& s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
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
        if (body.size() > kMaxBodyBytes) {
            throw std::runtime_error("Request body too large");
        }
    }
    if (body.size() > content_length) {
        body.resize(content_length);
    }

    req.body = std::move(body);
    return req;
}

void send_response(int client_fd, int status, const nlohmann::json& payload, const std::string& content_type = "application/json") {
    const auto body = payload.dump(2);
    std::ostringstream out;
    out << "HTTP/1.1 " << status << (status == 200 ? " OK" : status == 400 ? " Bad Request" : " Internal Server Error") << "\r\n";
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

void send_text(int client_fd, int status, const std::string& body, const std::string& content_type) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " OK\r\n";
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

nlohmann::json features_to_json(const std::vector<double>& v) {
    nlohmann::json arr = nlohmann::json::array({});
    for (double x : v) arr.push_back(x);
    return arr;
}

}  // namespace

int run_server(const HttpServerConfig& config) {
    const auto index_html = read_file_or_empty(config.static_dir + "/index.html");
    const auto app_js = read_file_or_empty(config.static_dir + "/app.js");
    const auto style_css = read_file_or_empty(config.static_dir + "/style.css");

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

    if (config.self_test_mode) {
        persistence::SqliteDb db;
        db.open(config.db_path);
        persistence::ensure_schema(db);
    }

    while (true) {
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

            if (req.method == "OPTIONS") {
                send_text(client_fd, 200, "", "text/plain");
                ::close(client_fd);
                continue;
            }

            if (req.method == "GET" && req.path == "/") {
                if (index_html.empty()) {
                    send_response(client_fd, 500, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Missing web/index.html"})}});
                } else {
                    send_text(client_fd, 200, index_html, "text/html; charset=utf-8");
                }
            } else if (req.method == "GET" && req.path == "/app.js") {
                send_text(client_fd, 200, app_js, "application/javascript");
            } else if (req.method == "GET" && req.path == "/style.css") {
                send_text(client_fd, 200, style_css, "text/css");
            } else if (req.method == "GET" && req.path == "/health") {
                send_response(client_fd, 200, nlohmann::json{{"ok", true}, {"version", "phase_4_1"}, {"policy_version", kPlayableCorePolicyVersion}});
            } else if (req.method == "POST" && req.path == "/query") {
                nlohmann::json body;
                try {
                    body = nlohmann::json::parse(req.body);
                } catch (const std::exception&) {
                    send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
                    ::close(client_fd);
                    continue;
                }

                if (!body.contains("domain") || !body.at("domain").is_string() ||
                    !body.contains("prompt") || !body.at("prompt").is_string()) {
                    send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"domain and prompt are required strings"})}});
                    ::close(client_fd);
                    continue;
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
                            {"selection_seed_hex", "0x" + ([](std::int64_t v){ std::ostringstream o; o<<std::hex<<static_cast<unsigned long long>(v); return o.str(); })(result.decision.selection_seed)},
                            {"decision_features", features_to_json(result.decision.decision_features)},
                        }},
                        {"proposal", result.proposal},
                        {"candidate_count", static_cast<double>(list_prompt_candidate_ids(db, result.prompt_hash).size())},
                    }
                );
            } else if (req.method == "POST" && req.path == "/dev/reset") {
                if (!config.dev_mode) {
                    send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"dev mode disabled"})}});
                    ::close(client_fd);
                    continue;
                }
                persistence::SqliteDb db;
                db.open(config.db_path);
                db.exec("DROP TABLE IF EXISTS prompt_candidates;");
                db.exec("DROP TABLE IF EXISTS interaction_log;");
                db.exec("DROP TABLE IF EXISTS prompt_cache;");
                db.exec("DROP TABLE IF EXISTS proposal_registry;");
                db.exec("DROP TABLE IF EXISTS proposal_stats;");
                db.exec("DROP TABLE IF EXISTS bandit_state;");
                db.exec("DROP TABLE IF EXISTS meta;");
                persistence::ensure_schema(db);
                send_response(client_fd, 200, nlohmann::json{{"ok", true}});
            } else if (req.method == "POST" && req.path == "/reward") {
                nlohmann::json body;
                try {
                    body = nlohmann::json::parse(req.body);
                } catch (const std::exception&) {
                    send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"Malformed JSON"})}});
                    ::close(client_fd);
                    continue;
                }

                if (!body.contains("session_id") || !body.at("session_id").is_string() ||
                    !body.contains("proposal_id") || !body.at("proposal_id").is_string() ||
                    !body.contains("reward") || !body.at("reward").is_number()) {
                    send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"session_id/proposal_id/reward required"})}});
                    ::close(client_fd);
                    continue;
                }

                double reward = body.at("reward").get<double>();
                if (reward < 0.0 || reward > 1.0) {
                    send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"updated", false}, {"errors", nlohmann::json::array({"reward must be in [0,1]"})}});
                    ::close(client_fd);
                    continue;
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
                    ::close(client_fd);
                    continue;
                }
                const bool already_applied = before->reward_applied != 0;
                log_reward(db, selector, sid, pid, reward);
                const auto after = latest_interaction_for_session_and_arm(db, sid, pid);
                const bool updated = !already_applied && after.has_value() && after->reward_applied != 0;
                send_response(client_fd, 200, nlohmann::json{{"ok", true}, {"updated", updated}});
            } else {
                send_response(client_fd, 400, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({"Unknown route"})}});
            }
        } catch (const std::exception& ex) {
            send_response(client_fd, 500, nlohmann::json{{"ok", false}, {"errors", nlohmann::json::array({ex.what()})}});
        }

        ::close(client_fd);
    }

    ::close(server_fd);
    return 0;
}

}  // namespace proteus::playable
