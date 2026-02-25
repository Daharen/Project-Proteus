#pragma once

#include "proteus/bootstrap/dimension_contract_registry.hpp"

#include <cstdint>
#include <string>

namespace proteus::llm {

enum class LlmMode {
    Offline,
    OnlineCapture,
};

enum class LlmArtifactStatus {
    CacheHit,
    CapturedAndCached,
    CacheMissOffline,
    ProviderError,
    ValidationFailed,
};

enum class LlmRequestKind : std::int64_t {
    Generic = 0,
    BootstrapFunnel = 1,
};

struct LlmRequest {
    LlmRequestKind request_kind = LlmRequestKind::Generic;
    bootstrap::DimensionKind dimension_kind = static_cast<bootstrap::DimensionKind>(-1);
    std::string provider;
    std::string model;
    std::string schema_name;
    std::int64_t schema_version = 1;
    std::string prompt_text;
    std::string prompt_hash_hex;
    std::string request_json;
};

struct LlmArtifactResult {
    LlmArtifactStatus status = LlmArtifactStatus::ProviderError;
    std::string artifact_json;
    std::string provider_error_code;
    std::int64_t cache_id = 0;
};

}  // namespace proteus::llm
