#include "proteus/llm/openai/openai_responses_client.hpp"

#include "proteus/bootstrap/dimension_contract_registry.hpp"

#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace proteus::llm::openai {
namespace {

static constexpr const char* kBootstrapSchemaName = "proteus_funnel_bootstrap_v1";

ProviderCaptureResult fail(std::string code, std::string raw_response_text = {}, std::string response_json = {}) {
    return ProviderCaptureResult{
        .ok = false,
        .response_json = std::move(response_json),
        .raw_response_text = std::move(raw_response_text),
        .error_code = std::move(code)
    };
}

void log_provider_error(const ProviderCaptureResult& result) {
    std::cerr << "OpenAI responses call failed: error_code=" << result.error_code;
    if (!result.response_json.empty()) {
        std::cerr << ", response_json=" << result.response_json;
    }
    if (!result.raw_response_text.empty()) {
        std::cerr << ", raw_response_text=" << result.raw_response_text;
    }
    std::cerr << "\n";
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
        std::string error_payload;
        try {
            const auto error_json = nlohmann::json::parse(body);
            error_payload = error_json.dump();
        } catch (...) {
            error_payload.clear();
        }
        return fail("OPENAI_STATUS_" + std::to_string(status), body, error_payload);
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(body);
    } catch (...) {
        return fail("OPENAI_INVALID_JSON");
    }

    const std::string text = extract_output_text_from_responses_json(parsed);
    if (text.empty()) {
        return fail("OPENAI_MISSING_OUTPUT_TEXT", body, parsed.dump());
    }

    return ProviderCaptureResult{
        .ok = true,
        .response_json = text,
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

    const auto payload = build_openai_responses_payload(request);

    const auto result = post_openai_responses(payload.dump(), key);
    if (!result.ok) {
        log_provider_error(result);
    }
    return result;
}

// Invariants:
// - Control-plane routing must never derive from request.prompt_text.
// - Bootstrap schema name is locked to kBootstrapSchemaName regardless of dimension.
nlohmann::json build_openai_responses_payload(const LlmRequest& request) {
    nlohmann::json schema;
    std::string format_name = request.schema_name;

    if (request.request_kind == LlmRequestKind::BootstrapFunnel) {
        if (request.dimension_kind != bootstrap::DimensionKind::Class &&
            request.dimension_kind != bootstrap::DimensionKind::Skill &&
            request.dimension_kind != bootstrap::DimensionKind::Dialogue) {
            throw std::runtime_error("OPENAI_BOOTSTRAP_DIMENSION_KIND_INVALID");
        }
        const auto& contract = bootstrap::GetDimensionContract(request.dimension_kind);
        schema = contract.json_schema_builder();
        format_name = kBootstrapSchemaName;
    } else {
        schema = nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"normalized_query_text", {{"type", "string"}}},
                {"intent_tags", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                {"synopsis", {{"type", "string"}}},
                {"proposals", {{"type", "array"}}},
                {"safety_flags", {{"type", "array"}, {"items", {{"type", "string"}}}}}
            }},
            {"required", nlohmann::json::array({"normalized_query_text", "intent_tags", "synopsis", "proposals", "safety_flags"})}
        };
    }

    return nlohmann::json{
        {"model", request.model},
        {"input", request.prompt_text},
        {"text", {{"format", {
            {"type", "json_schema"},
            {"name", format_name},
            {"strict", true},
            {"schema", schema}
        }}}}
    };
}

std::string extract_output_text_from_responses_json(const nlohmann::json& parsed) {
    if (parsed.contains("output") && parsed.at("output").is_array()) {
        std::string out;
        for (const auto& item : parsed.at("output")) {
            if (!item.contains("content") || !item.at("content").is_array()) {
                continue;
            }
            for (const auto& content : item.at("content")) {
                if (content.contains("type") && content.at("type").is_string() &&
                    content.at("type").get<std::string>() == "output_text" &&
                    content.contains("text") && content.at("text").is_string()) {
                    out += content.at("text").get<std::string>();
                }
            }
        }
        if (!out.empty()) {
            return out;
        }
    }

    if (parsed.contains("output_text") && parsed.at("output_text").is_string()) {
        return parsed.at("output_text").get<std::string>();
    }

    return {};
}

}  // namespace proteus::llm::openai
