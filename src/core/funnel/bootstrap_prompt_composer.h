#pragma once

#include "proteus/bootstrap/bootstrap_category.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace proteus::funnel {

constexpr std::int64_t kBootstrapPromptComposerVersion = 1;
constexpr std::int64_t kBootstrapPromptCandidateCount = 5;

struct BootstrapPromptTypedContext {
    bootstrap::BootstrapCategory bootstrap_category = bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_UNSPECIFIED_V1;
    std::int64_t schema_version = 1;
    std::int64_t candidate_count = kBootstrapPromptCandidateCount;
    std::vector<std::string> context_tokens;
};

const char* GlobalBootstrapInstructionBlockV1();
const char* CategoryInstructionBlockV1(bootstrap::BootstrapCategory category);

std::string ComposeBootstrapPrompt(const BootstrapPromptTypedContext& context);
std::string BuildSemanticRepairInstruction(
    bootstrap::BootstrapCategory category,
    const std::vector<std::string>& reject_codes
);

}  // namespace proteus::funnel
