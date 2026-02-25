#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace proteus::semantic {

constexpr std::int64_t kCandidateNormalizationVersion = 1;

struct CandidateSemanticItem {
    std::string label;
    std::string short_rationale;
};

enum class CandidateSemanticRejectCode : std::int64_t {
    SEMANTIC_DUPLICATE_V1 = 0,
    SEMANTIC_STEM_COLLISION_V1 = 1,
    SEMANTIC_ECHO_V1 = 2,
    SEMANTIC_OVERLAP_V1 = 3,
};

struct CandidateSemanticRejection {
    CandidateSemanticRejectCode code = CandidateSemanticRejectCode::SEMANTIC_DUPLICATE_V1;
    std::size_t first_index = 0;
    std::size_t second_index = 0;
};

struct CandidateSemanticValidationResult {
    bool ok = true;
    std::vector<std::string> normalized_labels;
    std::vector<CandidateSemanticRejection> rejections;
};

std::string NormalizeCandidateLabel(const std::string& input);

std::string SerializeRejectCode(CandidateSemanticRejectCode code);

CandidateSemanticValidationResult ValidateCandidateSetDeterministic(
    const std::vector<CandidateSemanticItem>& candidates,
    const std::string& query_intent
);

}  // namespace proteus::semantic
