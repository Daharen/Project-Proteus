#include "proteus/llm/openai/openai_responses_client.hpp"

#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include <cstdlib>
#include <sstream>
#include <string>

namespace proteus::llm::openai {
namespace {

ProviderCaptureResult fail(std::string code) {
    return ProviderCaptureResult{.ok = false, .error_code = std::move(code)};
}

ProviderCaptureResult post_openai_responses(const std::string& payload, const std::string& api_key) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == nullptr) {
        return fail("OPENAI_SSL_CONTEXT_ERROR");
    }

    BIO* bio = BIO_new_ssl_connect(ctx);
    if (bio == nullptr) {
        SSL_CTX_free(ctx);
        return fail("OPENAI_SSL_CONNECT_ERROR");
    }

    BIO_set_conn_hostname(bio, "api.openai.com:443");

    SSL* ssl = nullptr;
    BIO_get_ssl(bio, &ssl);
    if (ssl == nullptr) {
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return fail("OPENAI_SSL_INIT_ERROR");
    }
    SSL_set_tlsext_host_name(ssl, "api.openai.com");

    if (BIO_do_connect(bio) <= 0) {
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return fail("OPENAI_HTTP_ERROR");
    }

    std::ostringstream request;
    request << "POST /v1/responses HTTP/1.1\r\n";
    request << "Host: api.openai.com\r\n";
    request << "Authorization: Bearer " << api_key << "\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << payload.size() << "\r\n";
    request << "Connection: close\r\n\r\n";
    request << payload;

    const std::string request_text = request.str();
    if (BIO_write(bio, request_text.data(), static_cast<int>(request_text.size())) <= 0) {
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return fail("OPENAI_HTTP_ERROR");
    }

    std::string raw_response;
    char buffer[4096];
    while (true) {
        const int bytes = BIO_read(bio, buffer, static_cast<int>(sizeof(buffer)));
        if (bytes > 0) {
            raw_response.append(buffer, static_cast<size_t>(bytes));
            continue;
        }
        if (bytes == 0) {
            break;
        }
        if (!BIO_should_retry(bio)) {
            BIO_free_all(bio);
            SSL_CTX_free(ctx);
            return fail("OPENAI_HTTP_ERROR");
        }
    }

    BIO_free_all(bio);
    SSL_CTX_free(ctx);

    const auto header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return fail("OPENAI_INVALID_HTTP_RESPONSE");
    }

    const std::string header_blob = raw_response.substr(0, header_end);
    const std::string body = raw_response.substr(header_end + 4);

    std::istringstream status_stream(header_blob);
    std::string status_line;
    std::getline(status_stream, status_line);
    std::istringstream status_line_stream(status_line);
    std::string http_version;
    int status = 0;
    status_line_stream >> http_version >> status;
    if (status < 200 || status >= 300) {
        return fail("OPENAI_STATUS_" + std::to_string(status));
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(body);
    } catch (...) {
        return fail("OPENAI_INVALID_JSON");
    }

    if (!parsed.contains("output_text") || !parsed.at("output_text").is_string()) {
        return fail("OPENAI_MISSING_OUTPUT_TEXT");
    }

    return ProviderCaptureResult{
        .ok = true,
        .response_json = parsed.at("output_text").get<std::string>(),
        .raw_response_text = body,
        .error_code = ""
    };
}

}  // namespace

ProviderCaptureResult capture_openai_response(const LlmRequest& request) {
    const char* key = std::getenv("OPENAI_API_KEY");
    if (key == nullptr || std::string(key).empty()) {
        return fail("OPENAI_API_KEY_MISSING");
    }

    nlohmann::json schema = {
        {"name", request.schema_name},
        {"strict", true},
        {"schema", {
            {"type", "object"},
            {"properties", {
                {"normalized_query_text", {{"type", "string"}}},
                {"intent_tags", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                {"synopsis", {{"type", "string"}}},
                {"proposals", {{"type", "array"}}},
                {"safety_flags", {{"type", "array"}, {"items", {{"type", "string"}}}}}
            }},
            {"required", nlohmann::json::array({"normalized_query_text", "intent_tags", "synopsis", "proposals", "safety_flags"})}
        }}
    };

    nlohmann::json payload = {
        {"model", request.model},
        {"input", request.prompt_text},
        {"response_format", {{"type", "json_schema"}, {"json_schema", schema}}}
    };

    return post_openai_responses(payload.dump(), key);
}

}  // namespace proteus::llm::openai
