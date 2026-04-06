#include "proteus/sandbox/sandbox_world.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>

namespace proteus::sandbox {

namespace {

constexpr double kStepSize = 3.0;
constexpr double kMinDistance = 1e-5;
constexpr double kPi = 3.14159265358979323846;
constexpr double kFrontArcRadians = kPi * 0.66;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct InteractionCandidate {
    bool is_agent = false;
    int id = -1;
    std::string label;
    double distance = std::numeric_limits<double>::max();
    double dot = -1.0;
};

double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}

double clamp_non_negative(double v) {
    return std::max(0.0, v);
}

Vec2 add(const Vec2& a, const Vec2& b) {
    return Vec2{a.x + b.x, a.y + b.y};
}

Vec2 scale(const Vec2& v, double s) {
    return Vec2{v.x * s, v.y * s};
}

double length(const Vec2& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

std::pair<Vec2, double> direction_and_distance(double from_x, double from_y, double to_x, double to_y) {
    const Vec2 d{to_x - from_x, to_y - from_y};
    const double dist = std::max(length(d), kMinDistance);
    return {Vec2{d.x / dist, d.y / dist}, dist};
}

std::string survival_summary_text(const AgentSurvivalState& survival) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << "thirst=" << survival.state_thirst_current << "/" << survival.state_thirst_max
        << " hunger=" << survival.state_hunger_current << "/" << survival.state_hunger_max
        << " fatigue=" << survival.state_fatigue_current << "/" << survival.state_fatigue_max;
    return out.str();
}

nlohmann::json semantic_to_json(const AgentSemanticState& semantic) {
    return nlohmann::json{
        {"motivation", semantic.motivation},
        {"affect", semantic.affect},
        {"temperament", semantic.temperament},
        {"trust", semantic.trust},
        {"fear", semantic.fear},
        {"loyalty", semantic.loyalty},
    };
}

nlohmann::json behavior_to_json(const AgentBehaviorWeights& behavior) {
    return nlohmann::json{
        {"seek_interest_weight", behavior.seek_interest_weight},
        {"avoid_threat_weight", behavior.avoid_threat_weight},
        {"ally_pull_weight", behavior.ally_pull_weight},
        {"rival_repulsion_weight", behavior.rival_repulsion_weight},
        {"idle_wander_weight", behavior.idle_wander_weight},
    };
}

nlohmann::json survival_to_json(const AgentSurvivalState& survival) {
    return nlohmann::json{
        {"need_survival_weight", survival.need_survival_weight},
        {"need_hydration_weight", survival.need_hydration_weight},
        {"need_nutrition_weight", survival.need_nutrition_weight},
        {"need_rest_weight", survival.need_rest_weight},
        {"state_thirst_current", survival.state_thirst_current},
        {"state_thirst_max", survival.state_thirst_max},
        {"state_hunger_current", survival.state_hunger_current},
        {"state_hunger_max", survival.state_hunger_max},
        {"state_fatigue_current", survival.state_fatigue_current},
        {"state_fatigue_max", survival.state_fatigue_max},
        {"thirst_increase_per_tick", survival.thirst_increase_per_tick},
        {"hunger_increase_per_tick", survival.hunger_increase_per_tick},
        {"fatigue_increase_per_tick", survival.fatigue_increase_per_tick},
        {"drink_thirst_reduction", survival.drink_thirst_reduction},
        {"eat_hunger_reduction", survival.eat_hunger_reduction},
        {"sleep_fatigue_reduction", survival.sleep_fatigue_reduction},
        {"survival_summary", survival.survival_summary},
    };
}

bool is_ally(const SandboxAgent& lhs, const SandboxAgent& rhs) {
    return (lhs.agent_id % 2) == (rhs.agent_id % 2);
}

std::optional<InteractionCandidate> pick_interaction_target(const SandboxAgent& player,
    const std::vector<SandboxAgent>& agents,
    const std::vector<SandboxObject>& objects) {
    const double front_arc_cos = std::cos(kFrontArcRadians * 0.5);
    const Vec2 forward{std::cos(player.facing_radians), std::sin(player.facing_radians)};

    std::optional<InteractionCandidate> best;

    auto consider = [&](bool is_agent, int id, const std::string& label, double x, double y) {
        const auto [dir, dist] = direction_and_distance(player.x, player.y, x, y);
        if (dist > player.interaction_radius) {
            return;
        }
        const double dot = dir.x * forward.x + dir.y * forward.y;
        if (dot < front_arc_cos) {
            return;
        }

        InteractionCandidate candidate{is_agent, id, label, dist, dot};
        if (!best.has_value()) {
            best = candidate;
            return;
        }

        if (candidate.distance + 1e-8 < best->distance) {
            best = candidate;
            return;
        }
        if (std::abs(candidate.distance - best->distance) <= 1e-8) {
            if (candidate.id < best->id) {
                best = candidate;
                return;
            }
            if (candidate.id == best->id && candidate.is_agent && !best->is_agent) {
                best = candidate;
            }
        }
    };

    for (const auto& other : agents) {
        if (other.agent_id == player.agent_id) {
            continue;
        }
        consider(true, other.agent_id, other.label, other.x, other.y);
    }

    for (const auto& object : objects) {
        consider(false, object.object_id, object.label, object.x, object.y);
    }

    return best;
}

}  // namespace

SandboxWorld::SandboxWorld() {
    reset();
}

void SandboxWorld::reset() {
    tick_counter_ = 0;
    seed_world();
    update_survival_summaries();
}

void SandboxWorld::seed_world() {
    agents_.clear();
    objects_.clear();
    obstacles_.clear();

    agents_ = {
        SandboxAgent{1, "agent_red", 110.0, 90.0, 0.0, 12.0, "#ef4444", false, true, 4.0, 55.0, "idle", "seeded player",
            "", "", -1, -1,
            AgentSemanticState{0.85, 0.6, 0.4, 0.7, 0.25, 0.8},
            AgentBehaviorWeights{1.2, 0.9, 1.0, 0.5, 0.15},
            AgentSurvivalState{1.0, 1.25, 0.9, 0.8, 62.0, 100.0, 55.0, 100.0, 70.0, 100.0, 1.0, 0.7, 0.5, 25.0, 20.0, 30.0, ""}},
        SandboxAgent{2, "agent_blue", 250.0, 85.0, 0.2, 12.0, "#3b82f6", false, false, 3.0, 52.0, "idle", "seeded", "", "", -1, -1,
            AgentSemanticState{0.65, 0.5, 0.6, 0.55, 0.45, 0.55},
            AgentBehaviorWeights{1.0, 1.0, 0.9, 0.7, 0.2},
            AgentSurvivalState{0.9, 0.8, 1.1, 0.7, 20.0, 100.0, 80.0, 100.0, 35.0, 100.0, 0.8, 1.2, 0.4, 18.0, 24.0, 22.0, ""}},
        SandboxAgent{3, "agent_green", 420.0, 120.0, 0.6, 12.0, "#22c55e", false, false, 3.0, 52.0, "idle", "seeded", "", "", -1, -1,
            AgentSemanticState{0.6, 0.45, 0.55, 0.4, 0.6, 0.35},
            AgentBehaviorWeights{0.8, 1.2, 0.7, 1.1, 0.22},
            AgentSurvivalState{1.1, 1.0, 1.0, 1.2, 48.0, 100.0, 40.0, 100.0, 88.0, 100.0, 0.6, 0.6, 0.9, 14.0, 15.0, 35.0, ""}},
        SandboxAgent{4, "agent_yellow", 150.0, 300.0, 1.0, 12.0, "#facc15", false, false, 3.0, 52.0, "idle", "seeded", "", "", -1, -1,
            AgentSemanticState{0.5, 0.65, 0.35, 0.8, 0.2, 0.9},
            AgentBehaviorWeights{1.1, 0.8, 1.2, 0.5, 0.12},
            AgentSurvivalState{0.8, 0.7, 0.9, 1.0, 30.0, 100.0, 22.0, 100.0, 45.0, 100.0, 0.7, 0.5, 0.7, 16.0, 18.0, 28.0, ""}},
        SandboxAgent{5, "agent_purple", 360.0, 280.0, 2.0, 12.0, "#a855f7", false, false, 3.0, 52.0, "idle", "seeded", "", "", -1, -1,
            AgentSemanticState{0.55, 0.5, 0.7, 0.45, 0.55, 0.45},
            AgentBehaviorWeights{0.9, 1.1, 0.8, 1.0, 0.25},
            AgentSurvivalState{1.0, 1.0, 1.0, 1.0, 75.0, 100.0, 64.0, 100.0, 20.0, 100.0, 1.4, 0.8, 0.2, 30.0, 26.0, 18.0, ""}},
    };

    for (auto& agent : agents_) {
        clamp_semantic_state(agent.semantic);
        clamp_behavior_weights(agent.behavior);
        clamp_survival_state(agent.survival);
    }

    objects_ = {
        SandboxObject{101, "water_source_a", "resource", 90.0, 210.0, 10.0, 48.0, "#38bdf8", 0.85, 0.0, 0.2, 0.9,
            "water_source", "water", 10.0, 10.0, 0.25, "drink", 1.0},
        SandboxObject{102, "danger_node_a", "danger", 305.0, 195.0, 11.0, 55.0, "#dc2626", 0.0, 0.9, 0.0, 0.0,
            "hazard", "", 0.0, 0.0, 0.0, "inspect", 0.0},
        SandboxObject{103, "food_source_a", "resource", 520.0, 90.0, 9.0, 38.0, "#f59e0b", 0.35, 0.0, 0.1, 0.1,
            "food_source", "food", 8.0, 8.0, 0.2, "eat", 1.0},
        SandboxObject{104, "ally_beacon_a", "ally_beacon", 520.0, 300.0, 12.0, 52.0, "#2563eb", 0.25, 0.0, 0.95, 0.0,
            "beacon", "", 0.0, 0.0, 0.0, "inspect", 0.0},
        SandboxObject{105, "rival_beacon_a", "rival_beacon", 240.0, 335.0, 12.0, 52.0, "#7c3aed", 0.1, 0.5, 0.65, 0.0,
            "beacon", "", 0.0, 0.0, 0.0, "inspect", 0.0},
        SandboxObject{106, "shelter_a", "rest", 72.0, 360.0, 10.0, 40.0, "#fde68a", 0.3, 0.0, 0.2, 0.6,
            "shelter", "rest", 1.0, 1.0, 0.0, "sleep", 0.0},
        SandboxObject{107, "resource_node_b", "resource", 600.0, 220.0, 10.0, 48.0, "#10b981", 0.8, 0.0, 0.1, 0.95,
            "resource", "", 0.0, 0.0, 0.0, "inspect", 0.0},
        SandboxObject{108, "danger_node_b", "danger", 430.0, 340.0, 11.0, 55.0, "#b91c1c", 0.0, 0.85, 0.0, 0.0,
            "hazard", "", 0.0, 0.0, 0.0, "inspect", 0.0},
    };

    for (auto& object : objects_) {
        clamp_object_resource_state(object);
    }

    obstacles_ = {
        SandboxObstacle{201, 285.0, 75.0, 70.0, 95.0},
        SandboxObstacle{202, 390.0, 225.0, 85.0, 75.0},
    };
}

void SandboxWorld::clamp_semantic_state(AgentSemanticState& semantic) {
    semantic.motivation = clamp01(semantic.motivation);
    semantic.affect = clamp01(semantic.affect);
    semantic.temperament = clamp01(semantic.temperament);
    semantic.trust = clamp01(semantic.trust);
    semantic.fear = clamp01(semantic.fear);
    semantic.loyalty = clamp01(semantic.loyalty);
}

void SandboxWorld::clamp_behavior_weights(AgentBehaviorWeights& behavior) {
    behavior.seek_interest_weight = std::max(0.0, std::min(3.0, behavior.seek_interest_weight));
    behavior.avoid_threat_weight = std::max(0.0, std::min(3.0, behavior.avoid_threat_weight));
    behavior.ally_pull_weight = std::max(0.0, std::min(3.0, behavior.ally_pull_weight));
    behavior.rival_repulsion_weight = std::max(0.0, std::min(3.0, behavior.rival_repulsion_weight));
    behavior.idle_wander_weight = std::max(0.0, std::min(1.5, behavior.idle_wander_weight));
}

void SandboxWorld::clamp_survival_state(AgentSurvivalState& survival) {
    survival.need_survival_weight = std::max(0.0, std::min(3.0, survival.need_survival_weight));
    survival.need_hydration_weight = std::max(0.0, std::min(3.0, survival.need_hydration_weight));
    survival.need_nutrition_weight = std::max(0.0, std::min(3.0, survival.need_nutrition_weight));
    survival.need_rest_weight = std::max(0.0, std::min(3.0, survival.need_rest_weight));

    survival.state_thirst_max = std::max(1.0, survival.state_thirst_max);
    survival.state_hunger_max = std::max(1.0, survival.state_hunger_max);
    survival.state_fatigue_max = std::max(1.0, survival.state_fatigue_max);

    survival.state_thirst_current = std::max(0.0, std::min(survival.state_thirst_max, survival.state_thirst_current));
    survival.state_hunger_current = std::max(0.0, std::min(survival.state_hunger_max, survival.state_hunger_current));
    survival.state_fatigue_current = std::max(0.0, std::min(survival.state_fatigue_max, survival.state_fatigue_current));

    survival.thirst_increase_per_tick = std::max(0.0, std::min(10.0, survival.thirst_increase_per_tick));
    survival.hunger_increase_per_tick = std::max(0.0, std::min(10.0, survival.hunger_increase_per_tick));
    survival.fatigue_increase_per_tick = std::max(0.0, std::min(10.0, survival.fatigue_increase_per_tick));

    survival.drink_thirst_reduction = std::max(0.0, std::min(100.0, survival.drink_thirst_reduction));
    survival.eat_hunger_reduction = std::max(0.0, std::min(100.0, survival.eat_hunger_reduction));
    survival.sleep_fatigue_reduction = std::max(0.0, std::min(100.0, survival.sleep_fatigue_reduction));
}

void SandboxWorld::clamp_object_resource_state(SandboxObject& object) {
    object.available_units = clamp_non_negative(object.available_units);
    object.max_units = clamp_non_negative(object.max_units);
    object.regen_per_tick = std::max(0.0, std::min(10.0, object.regen_per_tick));
    object.consumption_per_interaction = std::max(0.0, std::min(10.0, object.consumption_per_interaction));

    if (object.max_units < object.available_units) {
        object.max_units = object.available_units;
    }
    object.available_units = std::min(object.max_units, object.available_units);
}

SandboxAgent* SandboxWorld::find_agent(int agent_id) {
    for (auto& agent : agents_) {
        if (agent.agent_id == agent_id) {
            return &agent;
        }
    }
    return nullptr;
}

SandboxObject* SandboxWorld::find_object(int object_id) {
    for (auto& object : objects_) {
        if (object.object_id == object_id) {
            return &object;
        }
    }
    return nullptr;
}

bool SandboxWorld::rect_contains(const SandboxObstacle& rect, double x, double y, double radius) {
    return x + radius > rect.x && x - radius < (rect.x + rect.w) && y + radius > rect.y && y - radius < (rect.y + rect.h);
}

void SandboxWorld::clamp_to_bounds_and_obstacles(SandboxAgent& agent, double prev_x, double prev_y) const {
    agent.x = std::max(agent.radius, std::min(map_width_ - agent.radius, agent.x));
    agent.y = std::max(agent.radius, std::min(map_height_ - agent.radius, agent.y));

    for (const auto& obstacle : obstacles_) {
        if (!rect_contains(obstacle, agent.x, agent.y, agent.radius)) {
            continue;
        }

        const double try_x = std::max(agent.radius, std::min(map_width_ - agent.radius, prev_x));
        if (!rect_contains(obstacle, try_x, agent.y, agent.radius)) {
            agent.x = try_x;
            continue;
        }

        const double try_y = std::max(agent.radius, std::min(map_height_ - agent.radius, prev_y));
        if (!rect_contains(obstacle, agent.x, try_y, agent.radius)) {
            agent.y = try_y;
            continue;
        }

        agent.x = prev_x;
        agent.y = prev_y;
    }
}

void SandboxWorld::step_once() {
    step_once_with_input(std::nullopt);
}

void SandboxWorld::apply_player_interaction(SandboxAgent& updated, const std::vector<SandboxAgent>& current_agents) {
    const auto target = pick_interaction_target(updated, current_agents, objects_);
    if (!target.has_value()) {
        updated.last_interaction = "interact";
        updated.last_interaction_result = "no valid consumable target in front arc";
        return;
    }

    if (target->is_agent) {
        updated.last_interaction = "inspect " + target->label;
        updated.last_interaction_result = "no valid consumable target in front arc";
        updated.target_agent_id = target->id;
        updated.target_object_id = -1;
        return;
    }

    auto* object = find_object(target->id);
    if (object == nullptr) {
        updated.last_interaction = "interact";
        updated.last_interaction_result = "no valid consumable target in front arc";
        return;
    }

    updated.target_object_id = object->object_id;
    updated.target_agent_id = -1;

    std::ostringstream result;
    result << std::fixed << std::setprecision(0);
    if (object->interaction_action == "drink" && object->available_units >= object->consumption_per_interaction && object->consumption_per_interaction > 0.0) {
        const double before_thirst = updated.survival.state_thirst_current;
        const double before_units = object->available_units;
        updated.survival.state_thirst_current = std::max(0.0, updated.survival.state_thirst_current - updated.survival.drink_thirst_reduction);
        object->available_units = std::max(0.0, object->available_units - object->consumption_per_interaction);
        result << "drink " << object->label << ": thirst " << before_thirst << " -> " << updated.survival.state_thirst_current
               << ", units " << before_units << " -> " << object->available_units;
        updated.last_interaction = "drink " + object->label;
    } else if (object->interaction_action == "eat" && object->available_units >= object->consumption_per_interaction && object->consumption_per_interaction > 0.0) {
        const double before_hunger = updated.survival.state_hunger_current;
        const double before_units = object->available_units;
        updated.survival.state_hunger_current = std::max(0.0, updated.survival.state_hunger_current - updated.survival.eat_hunger_reduction);
        object->available_units = std::max(0.0, object->available_units - object->consumption_per_interaction);
        result << "eat " << object->label << ": hunger " << before_hunger << " -> " << updated.survival.state_hunger_current
               << ", units " << before_units << " -> " << object->available_units;
        updated.last_interaction = "eat " + object->label;
    } else if (object->interaction_action == "sleep") {
        const double before_fatigue = updated.survival.state_fatigue_current;
        updated.survival.state_fatigue_current = std::max(0.0, updated.survival.state_fatigue_current - updated.survival.sleep_fatigue_reduction);
        result << "sleep at " << object->label << ": fatigue " << before_fatigue << " -> " << updated.survival.state_fatigue_current;
        updated.last_interaction = "sleep at " + object->label;
    } else {
        updated.last_interaction = "interact";
        updated.last_interaction_result = "no valid consumable target in front arc";
        return;
    }

    clamp_survival_state(updated.survival);
    clamp_object_resource_state(*object);
    updated.last_interaction_result = result.str();
}

void SandboxWorld::advance_survival_tick() {
    // Deterministic update order:
    // 1) sort agents/objects by stable id,
    // 2) advance all agent scalar survival state,
    // 3) regenerate object resources.
    for (auto& agent : agents_) {
        agent.survival.state_thirst_current = std::min(agent.survival.state_thirst_max,
            agent.survival.state_thirst_current + agent.survival.thirst_increase_per_tick);
        agent.survival.state_hunger_current = std::min(agent.survival.state_hunger_max,
            agent.survival.state_hunger_current + agent.survival.hunger_increase_per_tick);
        agent.survival.state_fatigue_current = std::min(agent.survival.state_fatigue_max,
            agent.survival.state_fatigue_current + agent.survival.fatigue_increase_per_tick);
        clamp_survival_state(agent.survival);
    }

    for (auto& object : objects_) {
        object.available_units = std::min(object.max_units, object.available_units + object.regen_per_tick);
        clamp_object_resource_state(object);
    }
}

void SandboxWorld::update_survival_summaries() {
    for (auto& agent : agents_) {
        agent.survival.survival_summary = survival_summary_text(agent.survival);
    }
}

void SandboxWorld::step_once_with_input(const std::optional<SandboxPlayerInput>& player_input) {
    std::sort(objects_.begin(), objects_.end(), [](const SandboxObject& a, const SandboxObject& b) { return a.object_id < b.object_id; });
    std::sort(agents_.begin(), agents_.end(), [](const SandboxAgent& a, const SandboxAgent& b) { return a.agent_id < b.agent_id; });
    std::vector<SandboxAgent> next = agents_;

    for (std::size_t i = 0; i < agents_.size(); ++i) {
        const auto& agent = agents_[i];
        auto& updated = next[i];

        Vec2 net;
        double max_obj_attract = 0.0;
        double max_obj_repel = 0.0;
        int best_obj_attract = -1;
        int best_obj_repel = -1;
        double max_agent_attract = 0.0;
        double max_agent_repel = 0.0;
        int best_agent_attract = -1;
        int best_agent_repel = -1;
        double idle_mag = 0.0;

        const bool apply_player_input = updated.is_player_controlled;
        if (apply_player_input) {
            double move_x = 0.0;
            double move_y = 0.0;
            bool interact = false;
            if (player_input.has_value()) {
                move_x = std::max(-1.0, std::min(1.0, player_input->move_x));
                move_y = std::max(-1.0, std::min(1.0, player_input->move_y));
                interact = player_input->interact;
            }

            Vec2 move{move_x, move_y};
            const double move_len = length(move);
            if (move_len > kMinDistance) {
                const Vec2 dir = scale(move, 1.0 / move_len);
                net = scale(dir, std::max(0.0, updated.move_speed));
                updated.facing_radians = std::atan2(dir.y, dir.x);
                updated.last_action = "player_move";
                std::ostringstream out;
                out << "player input move_x=" << move_x << " move_y=" << move_y;
                updated.influence_summary = out.str();
            } else {
                updated.last_action = "player_idle";
                updated.influence_summary = "player-controlled agent idle (no movement input)";
            }

            updated.target_object_id = -1;
            updated.target_agent_id = -1;

            const double prev_x = updated.x;
            const double prev_y = updated.y;
            updated.x += net.x;
            updated.y += net.y;
            clamp_to_bounds_and_obstacles(updated, prev_x, prev_y);

            if (interact) {
                apply_player_interaction(updated, agents_);
            }
            continue;
        }

        for (const auto& object : objects_) {
            const auto [dir, dist] = direction_and_distance(agent.x, agent.y, object.x, object.y);
            const double inv = 1.0 / std::max(dist, object.interaction_radius * 0.5);

            const double seek_scale = updated.behavior.seek_interest_weight * (0.25 + updated.semantic.motivation);
            const double affect_scale = 0.5 + updated.semantic.affect;
            const double threat_scale = updated.behavior.avoid_threat_weight * (0.25 + updated.semantic.fear);
            const double loyalty_scale = updated.behavior.ally_pull_weight * (0.2 + updated.semantic.loyalty + 0.5 * updated.semantic.trust);

            const double attraction = inv * ((object.interest_tag * seek_scale + object.resource_tag * seek_scale + object.social_tag * loyalty_scale) * affect_scale);
            const double repulsion = inv * (object.threat_tag * threat_scale * (0.8 + updated.semantic.temperament));

            net = add(net, scale(dir, attraction - repulsion));

            if (attraction > max_obj_attract) {
                max_obj_attract = attraction;
                best_obj_attract = object.object_id;
            }
            if (repulsion > max_obj_repel) {
                max_obj_repel = repulsion;
                best_obj_repel = object.object_id;
            }
        }

        for (const auto& other : agents_) {
            if (other.agent_id == agent.agent_id) {
                continue;
            }
            const auto [dir, dist] = direction_and_distance(agent.x, agent.y, other.x, other.y);
            const double inv = 1.0 / std::max(dist, 30.0);
            if (is_ally(agent, other)) {
                const double pull = inv * updated.behavior.ally_pull_weight * (0.25 + updated.semantic.trust + updated.semantic.loyalty);
                net = add(net, scale(dir, pull));
                if (pull > max_agent_attract) {
                    max_agent_attract = pull;
                    best_agent_attract = other.agent_id;
                }
            } else {
                const double push = inv * updated.behavior.rival_repulsion_weight * (0.25 + updated.semantic.fear);
                net = add(net, scale(dir, -push));
                if (push > max_agent_repel) {
                    max_agent_repel = push;
                    best_agent_repel = other.agent_id;
                }
            }
        }

        const double idle_phase = static_cast<double>((tick_counter_ + 1) * (agent.agent_id + 3));
        Vec2 idle{
            std::cos(0.31 * idle_phase + 0.2 * agent.agent_id),
            std::sin(0.23 * idle_phase + 0.15 * agent.agent_id)
        };
        idle_mag = updated.behavior.idle_wander_weight * (0.2 + (1.0 - updated.semantic.temperament));
        net = add(net, scale(idle, idle_mag));

        const double move_mag = length(net);
        if (move_mag > kMinDistance) {
            net = scale(net, kStepSize / move_mag);
        }

        const double prev_x = updated.x;
        const double prev_y = updated.y;
        updated.x += net.x;
        updated.y += net.y;
        clamp_to_bounds_and_obstacles(updated, prev_x, prev_y);
        if (length(net) > kMinDistance) {
            updated.facing_radians = std::atan2(net.y, net.x);
        }

        updated.target_object_id = -1;
        updated.target_agent_id = -1;
        const double dominant = std::max({max_obj_attract, max_obj_repel, max_agent_attract, max_agent_repel, idle_mag * 0.25});

        if (dominant == max_obj_attract && best_obj_attract >= 0) {
            updated.last_action = "approach_object";
            updated.target_object_id = best_obj_attract;
            updated.influence_summary = "approach_object because seek_interest_weight*motivation and loyalty/trust social pull dominated";
        } else if (dominant == max_obj_repel && best_obj_repel >= 0) {
            updated.last_action = "avoid_object";
            updated.target_object_id = best_obj_repel;
            updated.influence_summary = "avoid_object because fear-scaled avoid_threat_weight dominated attraction";
        } else if (dominant == max_agent_attract && best_agent_attract >= 0) {
            updated.last_action = "approach_agent";
            updated.target_agent_id = best_agent_attract;
            updated.influence_summary = "approach_agent because trust+loyalty increased ally_pull_weight";
        } else if (dominant == max_agent_repel && best_agent_repel >= 0) {
            updated.last_action = "avoid_agent";
            updated.target_agent_id = best_agent_repel;
            updated.influence_summary = "avoid_agent because fear and rival_repulsion_weight dominated";
        } else {
            updated.last_action = "idle";
            updated.influence_summary = "idle because idle_wander_weight was strongest remaining influence";
        }
    }

    agents_ = std::move(next);
    advance_survival_tick();
    update_survival_summaries();
    ++tick_counter_;
}

void SandboxWorld::step_n(int steps) {
    const int bounded = std::max(0, std::min(steps, 500));
    for (int i = 0; i < bounded; ++i) {
        step_once();
    }
}

void SandboxWorld::step_n_with_input(int steps, const std::optional<SandboxPlayerInput>& player_input) {
    const int bounded = std::max(0, std::min(steps, 500));
    for (int i = 0; i < bounded; ++i) {
        step_once_with_input(player_input);
    }
}

nlohmann::json SandboxWorld::to_json() const {
    nlohmann::json agents_json = nlohmann::json::array({});
    for (const auto& agent : agents_) {
        agents_json.push_back(nlohmann::json{
            {"agent_id", agent.agent_id},
            {"label", agent.label},
            {"x", agent.x},
            {"y", agent.y},
            {"facing_radians", agent.facing_radians},
            {"radius", agent.radius},
            {"color_hex", agent.color_hex},
            {"selected", agent.selected},
            {"is_player_controlled", agent.is_player_controlled},
            {"move_speed", agent.move_speed},
            {"interaction_radius", agent.interaction_radius},
            {"last_action", agent.last_action},
            {"target_object_id", agent.target_object_id},
            {"target_agent_id", agent.target_agent_id},
            {"influence_summary", agent.influence_summary},
            {"last_interaction", agent.last_interaction},
            {"last_interaction_result", agent.last_interaction_result},
            {"semantic", semantic_to_json(agent.semantic)},
            {"behavior", behavior_to_json(agent.behavior)},
            {"survival", survival_to_json(agent.survival)},
        });
    }

    nlohmann::json objects_json = nlohmann::json::array({});
    for (const auto& object : objects_) {
        objects_json.push_back(nlohmann::json{
            {"object_id", object.object_id},
            {"label", object.label},
            {"kind", object.kind},
            {"x", object.x},
            {"y", object.y},
            {"radius", object.radius},
            {"interaction_radius", object.interaction_radius},
            {"color_hex", object.color_hex},
            {"interest_tag", object.interest_tag},
            {"threat_tag", object.threat_tag},
            {"social_tag", object.social_tag},
            {"resource_tag", object.resource_tag},
            {"object_kind", object.object_kind},
            {"resource_kind", object.resource_kind},
            {"available_units", object.available_units},
            {"max_units", object.max_units},
            {"regen_per_tick", object.regen_per_tick},
            {"interaction_action", object.interaction_action},
            {"consumption_per_interaction", object.consumption_per_interaction},
        });
    }

    nlohmann::json obstacles_json = nlohmann::json::array({});
    for (const auto& obstacle : obstacles_) {
        obstacles_json.push_back(nlohmann::json{
            {"obstacle_id", obstacle.obstacle_id},
            {"x", obstacle.x},
            {"y", obstacle.y},
            {"w", obstacle.w},
            {"h", obstacle.h},
        });
    }

    return nlohmann::json{
        {"ok", true},
        {"tick", tick_counter_},
        {"map", nlohmann::json{{"width", map_width_}, {"height", map_height_}}},
        {"agents", agents_json},
        {"objects", objects_json},
        {"obstacles", obstacles_json},
    };
}

std::string SandboxWorld::deterministic_snapshot() const {
    return to_json().dump();
}

}  // namespace proteus::sandbox
