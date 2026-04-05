#pragma once

#include <string>

namespace proteus::sandbox {

struct AgentSemanticState {
    double motivation = 0.5;
    double affect = 0.5;
    double temperament = 0.5;
    double trust = 0.5;
    double fear = 0.5;
    double loyalty = 0.5;
};

struct AgentBehaviorWeights {
    double seek_interest_weight = 1.0;
    double avoid_threat_weight = 1.0;
    double ally_pull_weight = 1.0;
    double rival_repulsion_weight = 1.0;
    double idle_wander_weight = 0.2;
};

struct SandboxAgent {
    int agent_id = 0;
    std::string label;
    double x = 0.0;
    double y = 0.0;
    double facing_radians = 0.0;
    double radius = 12.0;
    std::string color_hex = "#ffffff";
    bool selected = false;
    bool is_player_controlled = false;
    double move_speed = 3.0;
    double interaction_radius = 52.0;
    std::string last_action = "idle";
    std::string influence_summary;
    std::string last_interaction;
    std::string last_interaction_result;
    int target_object_id = -1;
    int target_agent_id = -1;
    AgentSemanticState semantic;
    AgentBehaviorWeights behavior;
};

struct SandboxObject {
    int object_id = 0;
    std::string label;
    std::string kind;
    double x = 0.0;
    double y = 0.0;
    double radius = 10.0;
    double interaction_radius = 35.0;
    std::string color_hex = "#bbbbbb";
    double interest_tag = 0.0;
    double threat_tag = 0.0;
    double social_tag = 0.0;
    double resource_tag = 0.0;
};

struct SandboxObstacle {
    int obstacle_id = 0;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

}  // namespace proteus::sandbox
