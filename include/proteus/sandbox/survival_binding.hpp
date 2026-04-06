#pragma once

#include "proteus/sandbox/sandbox_types.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace proteus::sandbox {

struct SurvivalBinding {
    std::unordered_map<std::string, std::string> state_to_seek_action;
    std::unordered_map<std::string, std::string> seek_to_terminal_action;
    std::unordered_map<std::string, std::string> action_to_resource_kind;
    std::unordered_map<std::string, std::string> need_to_state_variable;
    std::unordered_map<std::string, std::string> hazard_to_response_action;

    nlohmann::json to_json() const;
};

struct SurvivalBindingLoadResult {
    bool ok = false;
    std::string source_path;
    SurvivalBinding binding;
    std::vector<std::string> errors;
};

struct SurvivalPreview {
    std::string highest_state_pressure;
    std::string recommended_seek_action;
    std::string recommended_terminal_action;
    std::string recommended_resource_kind;
    double highest_pressure_normalized = 0.0;
};

SurvivalBindingLoadResult load_survival_binding_from_file(const std::string& path);
SurvivalBindingLoadResult parse_survival_binding_json(const nlohmann::json& root, const std::string& source_path);
std::optional<SurvivalPreview> compute_survival_preview(const AgentSurvivalState& survival, const SurvivalBinding& binding);

}  // namespace proteus::sandbox
