#include "proteus/sandbox/sandbox_world.hpp"

#include <algorithm>
#include <cmath>

namespace proteus::sandbox {

namespace {

constexpr double kStepSize = 3.0;
constexpr double kMinDistance = 1e-5;
constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
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

bool is_ally(const SandboxAgent& lhs, const SandboxAgent& rhs) {
    return (lhs.agent_id % 2) == (rhs.agent_id % 2);
}

}  // namespace

SandboxWorld::SandboxWorld() {
    reset();
}

void SandboxWorld::reset() {
    tick_counter_ = 0;
    seed_world();
}

void SandboxWorld::seed_world() {
    agents_.clear();
    objects_.clear();
    obstacles_.clear();

    agents_ = {
        SandboxAgent{1, "agent_red", 110.0, 90.0, 0.0, 12.0, "#ef4444", false, "idle", "seeded", -1, -1,
            AgentSemanticState{0.85, 0.6, 0.4, 0.7, 0.25, 0.8},
            AgentBehaviorWeights{1.2, 0.9, 1.0, 0.5, 0.15}},
        SandboxAgent{2, "agent_blue", 250.0, 85.0, 0.2, 12.0, "#3b82f6", false, "idle", "seeded", -1, -1,
            AgentSemanticState{0.65, 0.5, 0.6, 0.55, 0.45, 0.55},
            AgentBehaviorWeights{1.0, 1.0, 0.9, 0.7, 0.2}},
        SandboxAgent{3, "agent_green", 420.0, 120.0, 0.6, 12.0, "#22c55e", false, "idle", "seeded", -1, -1,
            AgentSemanticState{0.6, 0.45, 0.55, 0.4, 0.6, 0.35},
            AgentBehaviorWeights{0.8, 1.2, 0.7, 1.1, 0.22}},
        SandboxAgent{4, "agent_yellow", 150.0, 300.0, 1.0, 12.0, "#facc15", false, "idle", "seeded", -1, -1,
            AgentSemanticState{0.5, 0.65, 0.35, 0.8, 0.2, 0.9},
            AgentBehaviorWeights{1.1, 0.8, 1.2, 0.5, 0.12}},
        SandboxAgent{5, "agent_purple", 360.0, 280.0, 2.0, 12.0, "#a855f7", false, "idle", "seeded", -1, -1,
            AgentSemanticState{0.55, 0.5, 0.7, 0.45, 0.55, 0.45},
            AgentBehaviorWeights{0.9, 1.1, 0.8, 1.0, 0.25}},
    };

    objects_ = {
        SandboxObject{101, "resource_node_a", "resource", 90.0, 210.0, 10.0, 48.0, "#34d399", 0.85, 0.0, 0.2, 0.9},
        SandboxObject{102, "danger_node_a", "danger", 305.0, 195.0, 11.0, 55.0, "#dc2626", 0.0, 0.9, 0.0, 0.0},
        SandboxObject{103, "neutral_waypoint_a", "waypoint", 520.0, 90.0, 9.0, 38.0, "#94a3b8", 0.35, 0.0, 0.1, 0.1},
        SandboxObject{104, "ally_beacon_a", "ally_beacon", 520.0, 300.0, 12.0, 52.0, "#2563eb", 0.25, 0.0, 0.95, 0.0},
        SandboxObject{105, "rival_beacon_a", "rival_beacon", 240.0, 335.0, 12.0, 52.0, "#7c3aed", 0.1, 0.5, 0.65, 0.0},
        SandboxObject{106, "rest_point_a", "rest", 72.0, 360.0, 10.0, 40.0, "#fde68a", 0.3, 0.0, 0.2, 0.6},
        SandboxObject{107, "resource_node_b", "resource", 600.0, 220.0, 10.0, 48.0, "#10b981", 0.8, 0.0, 0.1, 0.95},
        SandboxObject{108, "danger_node_b", "danger", 430.0, 340.0, 11.0, 55.0, "#b91c1c", 0.0, 0.85, 0.0, 0.0},
    };

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
        const double idle_mag = updated.behavior.idle_wander_weight * (0.2 + (1.0 - updated.semantic.temperament));
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
    ++tick_counter_;
}

void SandboxWorld::step_n(int steps) {
    const int bounded = std::max(0, std::min(steps, 500));
    for (int i = 0; i < bounded; ++i) {
        step_once();
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
            {"last_action", agent.last_action},
            {"target_object_id", agent.target_object_id},
            {"target_agent_id", agent.target_agent_id},
            {"influence_summary", agent.influence_summary},
            {"semantic", semantic_to_json(agent.semantic)},
            {"behavior", behavior_to_json(agent.behavior)},
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
