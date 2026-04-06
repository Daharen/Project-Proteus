#include "proteus/sandbox/sandbox_world.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <cmath>

namespace {

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

void freeze_background_drift(proteus::sandbox::SandboxWorld& world) {
    for (int id = 1; id <= 5; ++id) {
        auto* agent = world.find_agent(id);
        ASSERT_EQ(agent == nullptr, false);
        agent->survival.thirst_increase_per_tick = 0.0;
        agent->survival.hunger_increase_per_tick = 0.0;
        agent->survival.fatigue_increase_per_tick = 0.0;
        proteus::sandbox::SandboxWorld::clamp_survival_state(agent->survival);
    }
    for (int object_id = 101; object_id <= 108; ++object_id) {
        auto* object = world.find_object(object_id);
        if (object == nullptr) {
            continue;
        }
        object->regen_per_tick = 0.0;
        proteus::sandbox::SandboxWorld::clamp_object_resource_state(*object);
    }
}

void setup_player_front_interaction(proteus::sandbox::SandboxWorld& world, int object_id) {
    auto* player = world.find_agent(1);
    auto* target = world.find_object(object_id);
    ASSERT_EQ(player == nullptr, false);
    ASSERT_EQ(target == nullptr, false);
    player->x = target->x - 20.0;
    player->y = target->y;
    player->facing_radians = 0.0;
    player->interaction_radius = 80.0;
    player->move_speed = 0.0;
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

TEST(SandboxWorldTest, SurvivalDriftPerStepIsDeterministic) {
    proteus::sandbox::SandboxWorld world;
    const auto* before = find_agent(world, 1);
    ASSERT_EQ(before == nullptr, false);

    const double start_thirst = before->survival.state_thirst_current;
    const double start_hunger = before->survival.state_hunger_current;
    const double start_fatigue = before->survival.state_fatigue_current;

    world.step_n(4);

    const auto* after = find_agent(world, 1);
    ASSERT_EQ(after == nullptr, false);
    EXPECT_EQ(std::abs(after->survival.state_thirst_current - (start_thirst + 4.0 * before->survival.thirst_increase_per_tick)) < 1e-9, true);
    EXPECT_EQ(std::abs(after->survival.state_hunger_current - (start_hunger + 4.0 * before->survival.hunger_increase_per_tick)) < 1e-9, true);
    EXPECT_EQ(std::abs(after->survival.state_fatigue_current - (start_fatigue + 4.0 * before->survival.fatigue_increase_per_tick)) < 1e-9, true);
}

TEST(SandboxWorldTest, ResourceRegenerationPerStepIsDeterministic) {
    proteus::sandbox::SandboxWorld world;
    auto* water = world.find_object(101);
    ASSERT_EQ(water == nullptr, false);
    water->available_units = 5.0;
    water->max_units = 10.0;
    water->regen_per_tick = 0.5;
    proteus::sandbox::SandboxWorld::clamp_object_resource_state(*water);

    world.step_n(6);

    const auto* water_after = find_object(world, 101);
    ASSERT_EQ(water_after == nullptr, false);
    EXPECT_EQ(water_after->available_units, 8.0);
}

TEST(SandboxWorldTest, DrinkingReducesThirstAndWaterUnitsDeterministically) {
    proteus::sandbox::SandboxWorld world;
    freeze_background_drift(world);
    setup_player_front_interaction(world, 101);

    auto* player = world.find_agent(1);
    auto* water = world.find_object(101);
    ASSERT_EQ(player == nullptr, false);
    ASSERT_EQ(water == nullptr, false);
    player->survival.state_thirst_current = 62.0;
    player->survival.drink_thirst_reduction = 25.0;
    water->available_units = 10.0;
    water->consumption_per_interaction = 1.0;

    world.step_once_with_input(proteus::sandbox::SandboxPlayerInput{0.0, 0.0, true});

    const auto* after_player = find_agent(world, 1);
    const auto* after_water = find_object(world, 101);
    ASSERT_EQ(after_player == nullptr, false);
    ASSERT_EQ(after_water == nullptr, false);
    EXPECT_EQ(after_player->survival.state_thirst_current, 37.0);
    EXPECT_EQ(after_water->available_units, 9.0);
    EXPECT_EQ(after_player->last_interaction_result, "drink water_source_a: thirst 62 -> 37, units 10 -> 9");
}

TEST(SandboxWorldTest, EatingReducesHungerAndFoodUnitsDeterministically) {
    proteus::sandbox::SandboxWorld world;
    freeze_background_drift(world);
    setup_player_front_interaction(world, 103);

    auto* player = world.find_agent(1);
    auto* food = world.find_object(103);
    ASSERT_EQ(player == nullptr, false);
    ASSERT_EQ(food == nullptr, false);
    player->survival.state_hunger_current = 55.0;
    player->survival.eat_hunger_reduction = 20.0;
    food->available_units = 8.0;
    food->consumption_per_interaction = 1.0;

    world.step_once_with_input(proteus::sandbox::SandboxPlayerInput{0.0, 0.0, true});

    const auto* after_player = find_agent(world, 1);
    const auto* after_food = find_object(world, 103);
    ASSERT_EQ(after_player == nullptr, false);
    ASSERT_EQ(after_food == nullptr, false);
    EXPECT_EQ(after_player->survival.state_hunger_current, 35.0);
    EXPECT_EQ(after_food->available_units, 7.0);
    EXPECT_EQ(after_player->last_interaction_result, "eat food_source_a: hunger 55 -> 35, units 8 -> 7");
}

TEST(SandboxWorldTest, RestingAtShelterReducesFatigueDeterministically) {
    proteus::sandbox::SandboxWorld world;
    freeze_background_drift(world);
    setup_player_front_interaction(world, 106);

    auto* player = world.find_agent(1);
    ASSERT_EQ(player == nullptr, false);
    player->survival.state_fatigue_current = 70.0;
    player->survival.sleep_fatigue_reduction = 30.0;

    world.step_once_with_input(proteus::sandbox::SandboxPlayerInput{0.0, 0.0, true});

    const auto* after_player = find_agent(world, 1);
    ASSERT_EQ(after_player == nullptr, false);
    EXPECT_EQ(after_player->survival.state_fatigue_current, 40.0);
    EXPECT_EQ(after_player->last_interaction_result, "sleep at shelter_a: fatigue 70 -> 40");
}

TEST(SandboxWorldTest, BatchedAndSingleStepPathsProduceIdenticalState) {
    proteus::sandbox::SandboxWorld batched;
    proteus::sandbox::SandboxWorld single;

    const auto input = std::optional<proteus::sandbox::SandboxPlayerInput>(proteus::sandbox::SandboxPlayerInput{0.0, -1.0, false});
    batched.step_n_with_input(12, input);
    for (int i = 0; i < 12; ++i) {
        single.step_once_with_input(input);
    }

    EXPECT_EQ(batched.deterministic_snapshot(), single.deterministic_snapshot());
}

TEST(SandboxWorldTest, InvalidInteractionLeavesStateUnchangedExceptResultText) {
    proteus::sandbox::SandboxWorld world;
    freeze_background_drift(world);

    auto* player = world.find_agent(1);
    ASSERT_EQ(player == nullptr, false);
    player->x = 200.0;
    player->y = 200.0;
    player->facing_radians = 0.0;
    player->interaction_radius = 20.0;

    const auto thirst_before = player->survival.state_thirst_current;
    const auto hunger_before = player->survival.state_hunger_current;
    const auto fatigue_before = player->survival.state_fatigue_current;
    const auto* water = find_object(world, 101);
    const auto* food = find_object(world, 103);
    ASSERT_EQ(water == nullptr, false);
    ASSERT_EQ(food == nullptr, false);
    const auto water_before = water->available_units;
    const auto food_before = food->available_units;
    world.step_once_with_input(proteus::sandbox::SandboxPlayerInput{0.0, 0.0, true});

    const auto* after_player = find_agent(world, 1);
    ASSERT_EQ(after_player == nullptr, false);
    EXPECT_EQ(after_player->last_interaction_result, "no valid consumable target in front arc");

    EXPECT_EQ(after_player->survival.state_thirst_current, thirst_before);
    EXPECT_EQ(after_player->survival.state_hunger_current, hunger_before);
    EXPECT_EQ(after_player->survival.state_fatigue_current, fatigue_before);
    EXPECT_EQ(find_object(world, 101)->available_units, water_before);
    EXPECT_EQ(find_object(world, 103)->available_units, food_before);
}
