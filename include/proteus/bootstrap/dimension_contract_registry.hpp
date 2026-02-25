#pragma once

#include "proteus/query/query_identity.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace proteus::bootstrap {

enum class DimensionKind : std::int64_t {
    Class = 0,
    Skill = 1,
    Dialogue = 2,
};

enum class UiRenderHint : std::int64_t {
    CandidateSet = 0,
    DialogueOptions = 1,
};

using JsonSchemaBuilderFn = nlohmann::json (*)();
using SemanticValidatorFn = bool (*)(const nlohmann::json&, std::vector<std::string>& issues);

struct DimensionContract {
    // Invariant: for bootstrap requests, payload naming is locked to kBootstrapSchemaName
    // in the OpenAI client; contract.schema_name is for non-bootstrap flows only.
    DimensionKind kind = DimensionKind::Class;
    const char* schema_name = "proteus_funnel_bootstrap_v1";
    JsonSchemaBuilderFn json_schema_builder = nullptr;
    SemanticValidatorFn semantic_validator = nullptr;
    UiRenderHint ui_render_hint = UiRenderHint::CandidateSet;
};

nlohmann::json BuildBootstrapSchema_ClassCandidateSet();
nlohmann::json BuildBootstrapSchema_SkillCandidateSet();
nlohmann::json BuildBootstrapSchema_DialogueOptions();

bool ValidateBootstrapArtifact_ClassCandidateSet(const nlohmann::json& artifact, std::vector<std::string>& issues);
bool ValidateBootstrapArtifact_SkillCandidateSet(const nlohmann::json& artifact, std::vector<std::string>& issues);
bool ValidateBootstrapArtifact_DialogueOptions(const nlohmann::json& artifact, std::vector<std::string>& issues);

const DimensionContract& GetDimensionContract(DimensionKind kind);
const DimensionContract& GetDimensionContractForDomain(query::QueryDomain domain);

std::string BuildBootstrapPromptForDimension(DimensionKind kind, const std::string& raw_prompt);

}  // namespace proteus::bootstrap
