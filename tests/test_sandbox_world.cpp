#include "proteus/sandbox/sandbox_world.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace {

double distance(double x1, double y1, double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

const proteus::sandbox::SandboxAgent* find_agent(const proteus::sandbox::SandboxWorld& world, int id) {
    for (const auto& agent : world.agents()) {
        if (agent.agent_id == id) {
            return &agent;
        }
    }
    return nullptr;
}

const proteus::sandbox::SandboxObject* find_object(const proteus::sandbox::SandboxWorld& world, int id) {
    for (const auto& object : world.objects()) {
        if (object.object_id == id) {
            return &object;
        }
    }
    return nullptr;
}

}  // namespace

TEST(SandboxWorldTest, ResetIsDeterministic) {
    proteus::sandbox::SandboxWorld world;
    const auto snap1 = world.deterministic_snapshot();

    world.step_n(3);
    world.reset();

    const auto snap2 = world.deterministic_snapshot();
    EXPECT_EQ(snap1, snap2);
}

TEST(SandboxWorldTest, StepFromSameInitialStateIsDeterministic) {
    proteus::sandbox::SandboxWorld a;
    proteus::sandbox::SandboxWorld b;

    a.step_once();
    b.step_once();

    EXPECT_EQ(a.deterministic_snapshot(), b.deterministic_snapshot());
}

TEST(SandboxWorldTest, IncreasingFearIncreasesThreatSeparation) {
    proteus::sandbox::SandboxWorld baseline;
    proteus::sandbox::SandboxWorld fearful;

    auto* fearful_agent = fearful.find_agent(1);
    ASSERT_EQ(fearful_agent == nullptr, false);
    fearful_agent->semantic.fear = 1.0;
    fearful_agent->behavior.avoid_threat_weight = 2.5;
    proteus::sandbox::SandboxWorld::clamp_semantic_state(fearful_agent->semantic);
    proteus::sandbox::SandboxWorld::clamp_behavior_weights(fearful_agent->behavior);

    baseline.step_n(8);
    fearful.step_n(8);

    const auto* baseline_agent = find_agent(baseline, 1);
    const auto* fearful_agent_after = find_agent(fearful, 1);
    const auto* threat = find_object(baseline, 102);
    ASSERT_EQ(baseline_agent == nullptr, false);
    ASSERT_EQ(fearful_agent_after == nullptr, false);
    ASSERT_EQ(threat == nullptr, false);

    const double base_dist = distance(baseline_agent->x, baseline_agent->y, threat->x, threat->y);
    const double fear_dist = distance(fearful_agent_after->x, fearful_agent_after->y, threat->x, threat->y);
    EXPECT_GT(fear_dist, base_dist);
}

TEST(SandboxWorldTest, IncreasingTrustAndLoyaltyPullsTowardAllyBeacon) {
    proteus::sandbox::SandboxWorld baseline;
    proteus::sandbox::SandboxWorld loyal;

    auto* loyal_agent = loyal.find_agent(1);
    ASSERT_EQ(loyal_agent == nullptr, false);
    loyal_agent->semantic.trust = 1.0;
    loyal_agent->semantic.loyalty = 1.0;
    loyal_agent->behavior.ally_pull_weight = 2.5;
    proteus::sandbox::SandboxWorld::clamp_semantic_state(loyal_agent->semantic);
    proteus::sandbox::SandboxWorld::clamp_behavior_weights(loyal_agent->behavior);

    baseline.step_n(10);
    loyal.step_n(10);

    const auto* baseline_agent = find_agent(baseline, 1);
    const auto* loyal_agent_after = find_agent(loyal, 1);
    const auto* ally_beacon = find_object(baseline, 104);
    ASSERT_EQ(baseline_agent == nullptr, false);
    ASSERT_EQ(loyal_agent_after == nullptr, false);
    ASSERT_EQ(ally_beacon == nullptr, false);

    const double base_dist = distance(baseline_agent->x, baseline_agent->y, ally_beacon->x, ally_beacon->y);
    const double loyal_dist = distance(loyal_agent_after->x, loyal_agent_after->y, ally_beacon->x, ally_beacon->y);
    EXPECT_GT(base_dist, loyal_dist);
}
