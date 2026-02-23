#include "proteus/playable/proposal_schema.hpp"

namespace proteus::playable {

ValidationReport validate_proposal_json(const nlohmann::json& proposal_json) {
    ValidationReport report;

    if (!proposal_json.is_object()) {
        report.ok = false;
        report.issues.push_back("Proposal root must be an object");
        return report;
    }

    auto validate_required_non_empty = [&report, &proposal_json](const char* key) {
        if (!proposal_json.contains(key)) {
            report.ok = false;
            report.issues.emplace_back(std::string{"Missing required field: "} + key);
            return;
        }
        if (!proposal_json.at(key).is_string() || proposal_json.at(key).get<std::string>().empty()) {
            report.ok = false;
            report.issues.emplace_back(std::string{"Field must be a non-empty string: "} + key);
        }
    };

    validate_required_non_empty("proposal_id");
    validate_required_non_empty("domain");
    validate_required_non_empty("type");
    validate_required_non_empty("text");

    if (proposal_json.contains("tags")) {
        if (!proposal_json.at("tags").is_array()) {
            report.ok = false;
            report.issues.push_back("Optional field tags must be an array");
        } else {
            for (const auto& tag : proposal_json.at("tags")) {
                if (!tag.is_string()) {
                    report.ok = false;
                    report.issues.push_back("tags must contain only strings");
                    break;
                }
            }
        }
    }

    if (proposal_json.contains("axis_bias")) {
        if (!proposal_json.at("axis_bias").is_object()) {
            report.ok = false;
            report.issues.push_back("Optional field axis_bias must be an object");
        } else {
            for (const auto& entry : proposal_json.at("axis_bias").items()) {
                const auto& value = entry.value();
                if (!value.is_number()) {
                    report.ok = false;
                    report.issues.push_back("axis_bias values must be numeric");
                    break;
                }
                if (value.get<double>() < -1.0 || value.get<double>() > 1.0) {
                    report.ok = false;
                    report.issues.push_back("axis_bias values must be within [-1, 1]");
                    break;
                }
            }
        }
    }

    return report;
}

}  // namespace proteus::playable
