#pragma once

#include "proteus/llm/llm_cache_client.hpp"
#include "proteus/llm/llm_types.hpp"

#include <nlohmann/json.hpp>

namespace proteus::llm::openai {

ProviderCaptureResult capture_openai_response(const LlmRequest& request);

std::string extract_output_text_from_responses_json(const nlohmann::json& parsed);

}
