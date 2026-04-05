#pragma once

#include "proteus/sandbox/sandbox_input.hpp"
#include "proteus/sandbox/sandbox_types.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace proteus::sandbox {

class SandboxWorld {
public:
    SandboxWorld();

    void reset();
    void step_once();
    void step_once_with_input(const std::optional<SandboxPlayerInput>& player_input);
    void step_n(int steps);
    void step_n_with_input(int steps, const std::optional<SandboxPlayerInput>& player_input);

    int tick() const { return tick_counter_; }
    double width() const { return map_width_; }
    double height() const { return map_height_; }

    const std::vector<SandboxAgent>& agents() const { return agents_; }
    const std::vector<SandboxObject>& objects() const { return objects_; }
    const std::vector<SandboxObstacle>& obstacles() const { return obstacles_; }

    SandboxAgent* find_agent(int agent_id);
    SandboxObject* find_object(int object_id);

    static void clamp_semantic_state(AgentSemanticState& semantic);
    static void clamp_behavior_weights(AgentBehaviorWeights& behavior);

    nlohmann::json to_json() const;
    std::string deterministic_snapshot() const;

private:
    void seed_world();
    void clamp_to_bounds_and_obstacles(SandboxAgent& agent, double prev_x, double prev_y) const;
    static bool rect_contains(const SandboxObstacle& rect, double x, double y, double radius);

    double map_width_ = 640.0;
    double map_height_ = 420.0;
    int tick_counter_ = 0;
    std::vector<SandboxAgent> agents_;
    std::vector<SandboxObject> objects_;
    std::vector<SandboxObstacle> obstacles_;
};

}  // namespace proteus::sandbox
