#include "proteus/bootstrap/dimension_contract_registry.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace proteus::bootstrap {
namespace {

nlohmann::json build_envelope_schema(const nlohmann::json& proposal_item_schema) {
    return nlohmann::json{
        {"name", "proteus_funnel_bootstrap_v1"},
        {"type", "object"},
        {"properties", {
            {"normalized_query_text", {{"type", "string"}}},
            {"intent_tags", {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"synopsis", {{"type", "string"}}},
            {"proposals", {{"type", "array"}, {"items", proposal_item_schema}}},
            {"safety_flags", {{"type", "array"}, {"items", {{"type", "string"}}}}}
        }},
        {"required", nlohmann::json::array({"normalized_query_text", "intent_tags", "synopsis", "proposals", "safety_flags"})}
    };
}

bool validate_common_envelope(const nlohmann::json& artifact, std::vector<std::string>& issues) {
    if (!artifact.is_object()) {
        issues.push_back("Artifact root must be an object");
        return false;
    }
    if (!artifact.contains("proposals") || !artifact.at("proposals").is_array()) {
        issues.push_back("Artifact proposals must be an array");
        return false;
    }
    const auto count = artifact.at("proposals").size();
    if (count < 3 || count > 5) {
        issues.push_back("Artifact proposals count must be between 3 and 5");
        return false;
    }
    return true;
}

bool validate_candidate_set(const nlohmann::json& artifact, std::vector<std::string>& issues) {
    if (!validate_common_envelope(artifact, issues)) {
        return false;
    }

    for (const auto& p : artifact.at("proposals")) {
        if (!p.is_object()) {
            issues.push_back("Each proposal must be an object");
            return false;
        }
        if (!p.contains("proposal_json") || !p.at("proposal_json").is_object()) {
            issues.push_back("proposal_json object is required");
            return false;
        }
        const auto& pj = p.at("proposal_json");
        if (!pj.contains("mode") || !pj.at("mode").is_string() || pj.at("mode").get<std::string>() != "candidate_set") {
            issues.push_back("proposal_json.mode must be candidate_set");
            return false;
        }
        if (!pj.contains("name") || !pj.at("name").is_string()) {
            issues.push_back("candidate_set requires proposal_json.name");
            return false;
        }
        const std::string name = pj.at("name").get<std::string>();
        if (name.empty()) {
            issues.push_back("candidate_set proposal_json.name is required");
            return false;
        }
        if (name.find(" is ") != std::string::npos || name.find(":") != std::string::npos) {
            issues.push_back("candidate_set names must be short labels, not definitions");
            return false;
        }
    }
    return true;
}

bool validate_dialogue_options(const nlohmann::json& artifact, std::vector<std::string>& issues) {
    if (!validate_common_envelope(artifact, issues)) {
        return false;
    }

    std::set<std::string> intent_tags;
    for (const auto& p : artifact.at("proposals")) {
        if (!p.is_object() || !p.contains("proposal_json") || !p.at("proposal_json").is_object()) {
            issues.push_back("dialogue proposal_json object is required");
            return false;
        }
        const auto& pj = p.at("proposal_json");
        if (!pj.contains("mode") || !pj.at("mode").is_string() || pj.at("mode").get<std::string>() != "dialogue_options") {
            issues.push_back("proposal_json.mode must be dialogue_options");
            return false;
        }
        if (!pj.contains("utterance") || !pj.at("utterance").is_string()) {
            issues.push_back("dialogue_options requires proposal_json.utterance");
            return false;
        }
        if (!pj.contains("intent_tag") || !pj.at("intent_tag").is_string()) {
            issues.push_back("dialogue_options requires proposal_json.intent_tag");
            return false;
        }
        const std::string utterance = pj.at("utterance").get<std::string>();
        if (utterance.empty() || utterance.size() > 160) {
            issues.push_back("dialogue utterance must be 1..160 chars");
            return false;
        }
        if (utterance.find(" is ") != std::string::npos || utterance.find("lore") != std::string::npos) {
            issues.push_back("dialogue utterances must avoid definitions/lore dumps");
            return false;
        }
        intent_tags.insert(pj.at("intent_tag").get<std::string>());
    }
    if (intent_tags.size() < 2) {
        issues.push_back("dialogue options require intent_tag variety");
        return false;
    }
    return true;
}

nlohmann::json candidate_proposal_schema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"proposal_id", {{"type", "string"}}},
            {"proposal_kind", {{"type", "integer"}}},
            {"proposal_title", {{"type", "string"}}},
            {"proposal_body", {{"type", "string"}}},
            {"proposal_json", {
                {"type", "object"},
                {"properties", {
                    {"mode", {{"type", "string"}, {"enum", nlohmann::json::array({"candidate_set"})}}},
                    {"name", {{"type", "string"}}}
                }},
                {"required", nlohmann::json::array({"mode", "name"})}
            }}
        }},
        {"required", nlohmann::json::array({"proposal_id", "proposal_kind", "proposal_title", "proposal_body", "proposal_json"})}
    };
}

nlohmann::json dialogue_proposal_schema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"proposal_id", {{"type", "string"}}},
            {"proposal_kind", {{"type", "integer"}}},
            {"proposal_title", {{"type", "string"}}},
            {"proposal_body", {{"type", "string"}}},
            {"proposal_json", {
                {"type", "object"},
                {"properties", {
                    {"mode", {{"type", "string"}, {"enum", nlohmann::json::array({"dialogue_options"})}}},
                    {"utterance", {{"type", "string"}}},
                    {"intent_tag", {{"type", "string"}}},
                    {"tone", {{"type", "string"}}}
                }},
                {"required", nlohmann::json::array({"mode", "utterance", "intent_tag", "tone"})}
            }}
        }},
        {"required", nlohmann::json::array({"proposal_id", "proposal_kind", "proposal_title", "proposal_body", "proposal_json"})}
    };
}

}  // namespace

nlohmann::json BuildBootstrapSchema_ClassCandidateSet() { return build_envelope_schema(candidate_proposal_schema()); }

nlohmann::json BuildBootstrapSchema_SkillCandidateSet() { return build_envelope_schema(candidate_proposal_schema()); }

nlohmann::json BuildBootstrapSchema_DialogueOptions() { return build_envelope_schema(dialogue_proposal_schema()); }

bool ValidateBootstrapArtifact_ClassCandidateSet(const nlohmann::json& artifact, std::vector<std::string>& issues) {
    return validate_candidate_set(artifact, issues);
}

bool ValidateBootstrapArtifact_SkillCandidateSet(const nlohmann::json& artifact, std::vector<std::string>& issues) {
    return validate_candidate_set(artifact, issues);
}

bool ValidateBootstrapArtifact_DialogueOptions(const nlohmann::json& artifact, std::vector<std::string>& issues) {
    return validate_dialogue_options(artifact, issues);
}

const DimensionContract& GetDimensionContract(DimensionKind kind) {
    static const std::array<DimensionContract, 3> kContracts = {{
        DimensionContract{DimensionKind::Class, "proteus_funnel_bootstrap_v1", &BuildBootstrapSchema_ClassCandidateSet, &ValidateBootstrapArtifact_ClassCandidateSet, UiRenderHint::CandidateSet},
        DimensionContract{DimensionKind::Skill, "proteus_funnel_bootstrap_v1", &BuildBootstrapSchema_SkillCandidateSet, &ValidateBootstrapArtifact_SkillCandidateSet, UiRenderHint::CandidateSet},
        DimensionContract{DimensionKind::Dialogue, "proteus_funnel_bootstrap_v1", &BuildBootstrapSchema_DialogueOptions, &ValidateBootstrapArtifact_DialogueOptions, UiRenderHint::DialogueOptions},
    }};
    return kContracts.at(static_cast<std::size_t>(kind));
}

const DimensionContract& GetDimensionContractForDomain(query::QueryDomain domain) {
    if (domain == query::QueryDomain::Skill) {
        return GetDimensionContract(DimensionKind::Skill);
    }
    if (domain == query::QueryDomain::DialogueLine || domain == query::QueryDomain::DialogueOption) {
        return GetDimensionContract(DimensionKind::Dialogue);
    }
    return GetDimensionContract(DimensionKind::Class);
}

DimensionKind DimensionKindFromSchemaName(const std::string& schema_name) {
    if (schema_name == "proteus_bootstrap_skill_v1") {
        return DimensionKind::Skill;
    }
    if (schema_name == "proteus_bootstrap_dialogue_options_v1") {
        return DimensionKind::Dialogue;
    }
    return DimensionKind::Class;
}

std::string BuildBootstrapPromptForDimension(DimensionKind kind, const std::string& raw_prompt) {
    if (kind == DimensionKind::Dialogue) {
        return "Dimension: dialogue. Generate 3-5 in-character dialogue options. Prohibit lore dumps and definitions. "
               "Each option must have a distinct intent_tag where possible. Keep each utterance concise and playable. "
               "Return strict JSON using proposal_json.mode=dialogue_options. User query: " + raw_prompt;
    }
    if (kind == DimensionKind::Skill) {
        return "Dimension: skill. Generate 3-5 candidate names only. Prohibit definitions and explanations. "
               "Use short names. Return strict JSON using proposal_json.mode=candidate_set. User query: " + raw_prompt;
    }
    return "Dimension: class. Generate 3-5 candidate names only. Prohibit definitions and explanations. "
           "Use short names. Return strict JSON using proposal_json.mode=candidate_set. User query: " + raw_prompt;
}

}  // namespace proteus::bootstrap
