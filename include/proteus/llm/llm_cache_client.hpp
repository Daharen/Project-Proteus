#pragma once

#include "proteus/llm/illm_client.hpp"

#include <optional>

namespace proteus::llm {

struct ProviderCaptureResult {
    bool ok = false;
    std::string response_json;
    std::string raw_response_text;
    std::string error_code;
};

class LlmCacheClient final : public ILLMClient {
public:
    LlmCacheClient();

    LlmArtifactResult TryGetOrCaptureArtifact(
        persistence::SqliteDb& db,
        const LlmRequest& request,
        LlmMode mode
    ) override;

private:
    std::optional<ProviderCaptureResult> capture_from_provider(const LlmRequest& request) const;
};

LlmRequest BuildDeterministicRequest(
    const std::string& provider,
    const std::string& model,
    const std::string& schema_name,
    std::int64_t schema_version,
    const std::string& prompt_text
);

}  // namespace proteus::llm
