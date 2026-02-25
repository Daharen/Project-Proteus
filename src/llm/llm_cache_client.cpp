#include "proteus/llm/llm_cache_client.hpp"

#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#if defined(PROTEUS_ENABLE_OPENAI)
#include "proteus/llm/openai/openai_responses_client.hpp"
#endif

namespace proteus::llm {
namespace {

constexpr std::size_t kMaxStoredRawResponseTextLength = 32768;
constexpr std::size_t kMaxStoredRawResponseTextTruncLength = 8192;

std::string sha256_hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char b : hash) {
        out << std::setw(2) << static_cast<int>(b);
    }
    return out.str();
}

std::string truncate_det(const std::string& input, std::size_t cap) {
    if (input.size() <= cap) {
        return input;
    }
    return input.substr(0, cap);
}

}  // namespace

LlmCacheClient::LlmCacheClient() = default;

LlmRequest BuildDeterministicRequest(
    const std::string& provider,
    const std::string& model,
    const std::string& schema_name,
    std::int64_t schema_version,
    const std::string& prompt_text,
    LlmRequestKind request_kind,
    bootstrap::DimensionKind dimension_kind
) {
    nlohmann::json request = {
        {"request_kind", static_cast<double>(static_cast<std::int64_t>(request_kind))},
        {"dimension_kind", static_cast<double>(static_cast<std::int64_t>(dimension_kind))},
        {"provider", provider},
        {"model", model},
        {"schema_name", schema_name},
        {"schema_version", static_cast<double>(schema_version)},
        {"prompt_text", prompt_text}
    };
    return LlmRequest{
        .request_kind = request_kind,
        .dimension_kind = dimension_kind,
        .provider = provider,
        .model = model,
        .schema_name = schema_name,
        .schema_version = schema_version,
        .prompt_text = prompt_text,
        .prompt_hash_hex = sha256_hex(prompt_text),
        .request_json = request.dump()
    };
}

LlmArtifactResult LlmCacheClient::TryGetOrCaptureArtifact(
    persistence::SqliteDb& db,
    const LlmRequest& request,
    LlmMode mode
) {
    auto select = db.prepare(
        "SELECT cache_id, response_json, error_code "
        "FROM llm_response_cache "
        "WHERE provider = ?1 AND model = ?2 AND schema_name = ?3 AND schema_version = ?4 AND prompt_hash = ?5 "
        "LIMIT 1;"
    );
    select.bind_text(1, request.provider);
    select.bind_text(2, request.model);
    select.bind_text(3, request.schema_name);
    select.bind_int64(4, request.schema_version);
    select.bind_text(5, request.prompt_hash_hex);
    if (select.step()) {
        return LlmArtifactResult{
            .status = LlmArtifactStatus::CacheHit,
            .artifact_json = select.column_text(1),
            .provider_error_code = select.column_is_null(2) ? std::string{} : select.column_text(2),
            .cache_id = select.column_int64(0)
        };
    }

    if (mode == LlmMode::Offline) {
        return LlmArtifactResult{.status = LlmArtifactStatus::CacheMissOffline};
    }

    const auto maybe_capture = capture_from_provider(request);
    if (!maybe_capture.has_value() || !maybe_capture->ok) {
        return LlmArtifactResult{.status = LlmArtifactStatus::ProviderError, .provider_error_code = maybe_capture.has_value() ? maybe_capture->error_code : "PROVIDER_UNAVAILABLE"};
    }

    std::string error_code = maybe_capture->error_code;
    std::string raw_text = maybe_capture->raw_response_text;
    const std::string raw_trunc = truncate_det(raw_text, kMaxStoredRawResponseTextTruncLength);
    if (raw_text.size() > kMaxStoredRawResponseTextLength) {
        raw_text.clear();
        error_code = error_code.empty() ? "TRUNCATED" : error_code + ",TRUNCATED";
    }

    auto insert = db.prepare(
        "INSERT INTO llm_response_cache(created_at_utc, provider, model, schema_name, schema_version, prompt_hash, request_json, response_json, response_sha256, raw_response_text, raw_response_text_trunc, error_code) "
        "VALUES(strftime('%Y-%m-%dT%H:%M:%fZ','now'), ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11) "
        "ON CONFLICT(provider, model, schema_name, schema_version, prompt_hash) DO NOTHING;"
    );
    insert.bind_text(1, request.provider);
    insert.bind_text(2, request.model);
    insert.bind_text(3, request.schema_name);
    insert.bind_int64(4, request.schema_version);
    insert.bind_text(5, request.prompt_hash_hex);
    insert.bind_text(6, request.request_json);
    insert.bind_text(7, maybe_capture->response_json);
    insert.bind_text(8, sha256_hex(maybe_capture->response_json));
    insert.bind_text(9, raw_text);
    insert.bind_text(10, raw_trunc);
    if (error_code.empty()) {
        insert.bind_null(11);
    } else {
        insert.bind_text(11, error_code);
    }
    insert.step();

    auto id_stmt = db.prepare(
        "SELECT cache_id FROM llm_response_cache WHERE provider = ?1 AND model = ?2 AND schema_name = ?3 AND schema_version = ?4 AND prompt_hash = ?5 LIMIT 1;"
    );
    id_stmt.bind_text(1, request.provider);
    id_stmt.bind_text(2, request.model);
    id_stmt.bind_text(3, request.schema_name);
    id_stmt.bind_int64(4, request.schema_version);
    id_stmt.bind_text(5, request.prompt_hash_hex);
    if (!id_stmt.step()) {
        return LlmArtifactResult{.status = LlmArtifactStatus::ValidationFailed, .provider_error_code = "CACHE_WRITE_MISSING"};
    }

    return LlmArtifactResult{
        .status = LlmArtifactStatus::CapturedAndCached,
        .artifact_json = maybe_capture->response_json,
        .provider_error_code = error_code,
        .cache_id = id_stmt.column_int64(0)
    };
}

std::optional<ProviderCaptureResult> LlmCacheClient::capture_from_provider(const LlmRequest& request) const {
#if defined(PROTEUS_ENABLE_OPENAI)
    if (request.provider == "openai") {
        return openai::capture_openai_response(request);
    }
#endif
    (void)request;
    return ProviderCaptureResult{.ok = false, .error_code = "PROVIDER_DISABLED"};
}

}  // namespace proteus::llm
