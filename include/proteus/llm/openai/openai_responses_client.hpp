#pragma once

#include "proteus/llm/llm_cache_client.hpp"
#include "proteus/llm/llm_types.hpp"

namespace proteus::llm::openai {

ProviderCaptureResult capture_openai_response(const LlmRequest& request);

}
