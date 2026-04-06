#include "proteus/sandbox/survival_binding.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace proteus::sandbox {

namespace {


std::optional<std::string> read_non_empty_string(
    const nlohmann::json& object,
    const std::string& section,
    const std::string& key,
    std::vector<std::string>& errors) {
    if (!object.at(key).is_string()) {
        errors.push_back(section + "." + key + " must be a string");
        return std::nullopt;
    }
    const std::string value = object.at(key).get<std::string>();
    if (value.empty()) {
        errors.push_back(section + "." + key + " must not be empty");
        return std::nullopt;
    }
    return value;
}

using Mapping = std::unordered_map<std::string, std::string>;

void parse_mapping_section(const nlohmann::json& root,
    const char* section,
    Mapping& out,
    std::vector<std::string>& errors) {
    if (!root.contains(section)) {
        errors.push_back(std::string("missing required section: ") + section);
        return;
    }
    const auto& value = root.at(section);
    if (!value.is_object()) {
        errors.push_back(std::string(section) + " must be an object");
        return;
    }

    for (const auto& item : value.items()) {
        const std::string key = item.key();
        if (key.empty()) {
            errors.push_back(std::string(section) + " contains empty key");
            continue;
        }
        const auto parsed = read_non_empty_string(value, section, key, errors);
        if (!parsed.has_value()) {
            continue;
        }

        const auto existing = out.find(key);
        if (existing != out.end() && existing->second != parsed.value()) {
            errors.push_back(std::string(section) + " contains contradictory mapping for key: " + key);
            continue;
        }
        out[key] = parsed.value();
    }
}

double safe_normalized_pressure(double current, double max_v) {
    if (max_v <= 0.0) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, current / max_v));
}

nlohmann::json map_to_json(const std::unordered_map<std::string, std::string>& mapping) {
    nlohmann::json out = nlohmann::json::parse("{}");
    for (const auto& [k, v] : mapping) {
        out[k] = v;
    }
    return out;
}

std::string lookup_or_empty(const std::unordered_map<std::string, std::string>& mapping, const std::string& key) {
    const auto it = mapping.find(key);
    if (it == mapping.end()) {
        return {};
    }
    return it->second;
}

}  // namespace

nlohmann::json SurvivalBinding::to_json() const {
    nlohmann::json out = nlohmann::json::parse("{}");
    out["state_to_seek_action"] = map_to_json(state_to_seek_action);
    out["seek_to_terminal_action"] = map_to_json(seek_to_terminal_action);
    out["action_to_resource_kind"] = map_to_json(action_to_resource_kind);
    out["need_to_state_variable"] = map_to_json(need_to_state_variable);
    out["hazard_to_response_action"] = map_to_json(hazard_to_response_action);
    return out;
}


SurvivalBindingLoadResult load_survival_binding_from_file(const std::string& path) {
    SurvivalBindingLoadResult result;
    result.source_path = path;

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        result.errors.push_back("unable to open file: " + path);
        return result;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& ex) {
        result.errors.push_back(std::string("invalid json: ") + ex.what());
        return result;
    }

    return parse_survival_binding_json(parsed, path);
}

SurvivalBindingLoadResult parse_survival_binding_json(const nlohmann::json& root, const std::string& source_path) {
    SurvivalBindingLoadResult result;
    result.source_path = source_path;

    if (!root.is_object()) {
        result.errors.push_back("root must be a JSON object");
        return result;
    }

    parse_mapping_section(root, "state_to_seek_action", result.binding.state_to_seek_action, result.errors);
    parse_mapping_section(root, "seek_to_terminal_action", result.binding.seek_to_terminal_action, result.errors);
    parse_mapping_section(root, "action_to_resource_kind", result.binding.action_to_resource_kind, result.errors);
    parse_mapping_section(root, "need_to_state_variable", result.binding.need_to_state_variable, result.errors);
    parse_mapping_section(root, "hazard_to_response_action", result.binding.hazard_to_response_action, result.errors);

    result.ok = result.errors.empty();
    return result;
}

std::optional<SurvivalPreview> compute_survival_preview(const AgentSurvivalState& survival, const SurvivalBinding& binding) {
    struct StatePressure {
        const char* state;
        double pressure;
    };

    // Stable tie-break order is explicit and deterministic: thirst > hunger > fatigue.
    const std::array<StatePressure, 3> pressures = {{
        {"thirst", safe_normalized_pressure(survival.state_thirst_current, survival.state_thirst_max)},
        {"hunger", safe_normalized_pressure(survival.state_hunger_current, survival.state_hunger_max)},
        {"fatigue", safe_normalized_pressure(survival.state_fatigue_current, survival.state_fatigue_max)},
    }};

    const StatePressure* best = &pressures[0];
    for (const auto& candidate : pressures) {
        if (candidate.pressure > best->pressure) {
            best = &candidate;
        }
    }

    SurvivalPreview preview;
    preview.highest_state_pressure = best->state;
    preview.highest_pressure_normalized = best->pressure;
    preview.recommended_seek_action = lookup_or_empty(binding.state_to_seek_action, preview.highest_state_pressure);
    preview.recommended_terminal_action = lookup_or_empty(binding.seek_to_terminal_action, preview.recommended_seek_action);
    preview.recommended_resource_kind = lookup_or_empty(binding.action_to_resource_kind, preview.recommended_terminal_action);
    return preview;
}

}  // namespace proteus::sandbox
