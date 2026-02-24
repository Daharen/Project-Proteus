#include "proteus/llm/openai/openai_responses_client.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

namespace proteus::llm::openai {

ProviderCaptureResult capture_openai_response(const LlmRequest& request) {
    const char* key = std::getenv("OPENAI_API_KEY");
    if (key == nullptr || std::string(key).empty()) {
        return ProviderCaptureResult{.ok = false, .error_code = "OPENAI_API_KEY_MISSING"};
    }

    httplib::SSLClient cli("api.openai.com", 443);
    cli.set_read_timeout(30, 0);

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

    httplib::Headers headers = {
        {"Authorization", std::string("Bearer ") + key},
        {"Content-Type", "application/json"}
    };

    const auto response = cli.Post("/v1/responses", headers, payload.dump(), "application/json");
    if (!response) {
        return ProviderCaptureResult{.ok = false, .error_code = "OPENAI_HTTP_ERROR"};
    }
    if (response->status < 200 || response->status >= 300) {
        return ProviderCaptureResult{.ok = false, .error_code = "OPENAI_STATUS_" + std::to_string(response->status)};
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(response->body);
    } catch (...) {
        return ProviderCaptureResult{.ok = false, .error_code = "OPENAI_INVALID_JSON"};
    }

    if (!parsed.contains("output_text") || !parsed.at("output_text").is_string()) {
        return ProviderCaptureResult{.ok = false, .error_code = "OPENAI_MISSING_OUTPUT_TEXT"};
    }

    return ProviderCaptureResult{
        .ok = true,
        .response_json = parsed.at("output_text").get<std::string>(),
        .raw_response_text = response->body,
        .error_code = ""
    };
}

}  // namespace proteus::llm::openai
