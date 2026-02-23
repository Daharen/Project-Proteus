#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace httplib {

using Headers = std::unordered_map<std::string, std::string>;

namespace detail {
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
inline int close_socket(socket_t s) { return closesocket(s); }
struct WsaInit {
  WsaInit() {
    WSADATA d{};
    WSAStartup(MAKEWORD(2, 2), &d);
  }
  ~WsaInit() { WSACleanup(); }
};
inline WsaInit &wsa_init() {
  static WsaInit inst;
  return inst;
}
#else
using socket_t = int;
constexpr socket_t invalid_socket = -1;
inline int close_socket(socket_t s) { return ::close(s); }
#endif

inline std::string trim(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
  return s.substr(b, e - b);
}

inline bool recv_http_message(socket_t fd, std::string &out, size_t max_body) {
  char buffer[4096];
  out.clear();

  while (out.find("\r\n\r\n") == std::string::npos) {
    const auto n = recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0) return false;
    out.append(buffer, static_cast<size_t>(n));
    if (out.size() > max_body + 8192) return false;
  }

  const auto header_end = out.find("\r\n\r\n");
  const auto header_blob = out.substr(0, header_end);
  std::istringstream hs(header_blob);
  std::string line;
  size_t content_length = 0;
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto pos = line.find(':');
    if (pos == std::string::npos) continue;
    auto name = trim(line.substr(0, pos));
    if (name == "Content-Length") {
      content_length = static_cast<size_t>(std::stoull(trim(line.substr(pos + 1))));
    }
  }
  if (content_length > max_body) return false;

  while (out.size() < header_end + 4 + content_length) {
    const auto n = recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0) return false;
    out.append(buffer, static_cast<size_t>(n));
  }
  return true;
}

inline bool parse_request(const std::string &raw, std::string &method, std::string &path,
                          Headers &headers, std::string &body) {
  const auto split = raw.find("\r\n\r\n");
  if (split == std::string::npos) return false;
  std::istringstream hs(raw.substr(0, split));
  std::string line;
  if (!std::getline(hs, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  std::istringstream ls(line);
  std::string version;
  ls >> method >> path >> version;
  if (method.empty() || path.empty()) return false;

  headers.clear();
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto pos = line.find(':');
    if (pos == std::string::npos) continue;
    headers[trim(line.substr(0, pos))] = trim(line.substr(pos + 1));
  }

  body = raw.substr(split + 4);
  auto it = headers.find("Content-Length");
  if (it != headers.end()) {
    const auto need = static_cast<size_t>(std::stoull(it->second));
    if (body.size() > need) body.resize(need);
  }
  return true;
}

inline std::string reason_phrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    default: return "Internal Server Error";
  }
}

}  // namespace detail

struct Request {
  std::string method;
  std::string path;
  std::string body;
  Headers headers;
};

struct Response {
  int status = 200;
  std::string body;
  Headers headers;

  void set_content(const std::string &b, const std::string &content_type) {
    body = b;
    headers["Content-Type"] = content_type;
  }

  void set_header(const std::string &key, const std::string &value) {
    headers[key] = value;
  }
};

using Handler = std::function<void(const Request &, Response &)>;

class Server {
 public:
  Server() = default;

  void set_payload_max_length(size_t bytes) { max_payload_ = bytes; }
  void Get(const std::string &pattern, Handler h) { routes_.push_back({"GET", pattern, std::move(h)}); }
  void Post(const std::string &pattern, Handler h) { routes_.push_back({"POST", pattern, std::move(h)}); }
  void Options(const std::string &pattern, Handler h) { routes_.push_back({"OPTIONS", pattern, std::move(h)}); }

  bool listen(const char *host, int port) {
#ifdef _WIN32
    detail::wsa_init();
#endif
    stop_.store(false);
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == detail::invalid_socket) return false;

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) return false;

    if (::bind(server_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) return false;
    if (::listen(server_fd_, 16) < 0) return false;

    while (!stop_.load()) {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(server_fd_, &read_fds);
      timeval timeout{};
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
      if (::select(static_cast<int>(server_fd_) + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) continue;

      sockaddr_in client_addr{};
#ifdef _WIN32
      int client_len = sizeof(client_addr);
#else
      socklen_t client_len = sizeof(client_addr);
#endif
      auto client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
      if (client_fd == detail::invalid_socket) continue;

      std::string raw;
      if (!detail::recv_http_message(client_fd, raw, max_payload_)) {
        detail::close_socket(client_fd);
        continue;
      }

      Request req;
      if (!detail::parse_request(raw, req.method, req.path, req.headers, req.body)) {
        detail::close_socket(client_fd);
        continue;
      }

      Response res;
      bool matched = false;
      for (const auto &r : routes_) {
        if (r.method != req.method) continue;
        if (r.pattern == req.path || std::regex_match(req.path, std::regex(r.pattern))) {
          matched = true;
          r.handler(req, res);
          break;
        }
      }
      if (!matched) {
        res.status = 404;
        res.set_content("Not Found", "text/plain");
      }

      std::ostringstream out;
      out << "HTTP/1.1 " << res.status << " " << detail::reason_phrase(res.status) << "\r\n";
      if (!res.headers.count("Content-Type")) {
        res.headers["Content-Type"] = "text/plain";
      }
      for (const auto &h : res.headers) {
        out << h.first << ": " << h.second << "\r\n";
      }
      out << "Content-Length: " << res.body.size() << "\r\n";
      out << "Connection: close\r\n\r\n";
      out << res.body;
      const auto msg = out.str();
      send(client_fd, msg.c_str(), static_cast<int>(msg.size()), 0);
      detail::close_socket(client_fd);
    }

    detail::close_socket(server_fd_);
    server_fd_ = detail::invalid_socket;
    return true;
  }

  void stop() {
    stop_.store(true);
    if (server_fd_ != detail::invalid_socket) {
      detail::close_socket(server_fd_);
      server_fd_ = detail::invalid_socket;
    }
  }

 private:
  struct Route { std::string method; std::string pattern; Handler handler; };
  std::vector<Route> routes_;
  size_t max_payload_ = 1024 * 1024;
  std::atomic<bool> stop_{false};
  detail::socket_t server_fd_ = detail::invalid_socket;
};

struct Result {
  int status = 0;
  std::string body;
  Headers headers;
};

class Client {
 public:
  Client(std::string host, int port) : host_(std::move(host)), port_(port) {}

  std::shared_ptr<Result> Get(const std::string &path) {
    return request("GET", path, "", "application/json");
  }

  std::shared_ptr<Result> Post(const std::string &path, const std::string &body, const std::string &content_type) {
    return request("POST", path, body, content_type);
  }

 private:
  std::shared_ptr<Result> request(const std::string &method, const std::string &path,
                                  const std::string &body, const std::string &content_type) {
#ifdef _WIN32
    detail::wsa_init();
#endif
    auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == detail::invalid_socket) return nullptr;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
      detail::close_socket(fd);
      return nullptr;
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      detail::close_socket(fd);
      return nullptr;
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host_ << "\r\n";
    req << "Content-Type: " << content_type << "\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n\r\n";
    req << body;

    const auto text = req.str();
    send(fd, text.c_str(), static_cast<int>(text.size()), 0);

    std::string response;
    char buf[4096];
    while (true) {
      const auto n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) break;
      response.append(buf, static_cast<size_t>(n));
    }
    detail::close_socket(fd);

    const auto split = response.find("\r\n\r\n");
    if (split == std::string::npos) return nullptr;

    std::istringstream hs(response.substr(0, split));
    std::string status_line;
    std::getline(hs, status_line);
    std::istringstream sl(status_line);
    std::string http;
    int status = 0;
    sl >> http >> status;

    auto out = std::make_shared<Result>();
    out->status = status;
    out->body = response.substr(split + 4);
    return out;
  }

  std::string host_;
  int port_;
};

}  // namespace httplib
