#pragma once

#include "proteus/llm/llm_types.hpp"
#include "proteus/persistence/sqlite_db.hpp"

namespace proteus::llm {

class ILLMClient {
public:
    virtual ~ILLMClient() = default;
    virtual LlmArtifactResult TryGetOrCaptureArtifact(
        persistence::SqliteDb& db,
        const LlmRequest& request,
        LlmMode mode
    ) = 0;
};

}  // namespace proteus::llm
