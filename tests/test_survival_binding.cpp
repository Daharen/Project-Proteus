#include "proteus/sandbox/sandbox_world.hpp"
#include "proteus/sandbox/survival_binding.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string write_temp_file(const std::string& name, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path.string();
}

const char* kValidBindingJson = R"JSON({
  "state_to_seek_action": {
    "thirst": "seek_water",
    "hunger": "seek_food",
    "fatigue": "seek_shelter"
  },
  "seek_to_terminal_action": {
    "seek_water": "drink",
    "seek_food": "eat",
    "seek_shelter": "sleep"
  },
  "action_to_resource_kind": {
    "drink": "water",
    "eat": "food",
    "sleep": "shelter"
  },
  "need_to_state_variable": {
    "hydration": "thirst",
    "nutrition": "hunger",
    "sleep": "fatigue"
  },
  "hazard_to_response_action": {
    "dehydration": "drink",
    "starvation": "eat",
    "exposure": "seek_shelter"
  }
})JSON";

}  // namespace

TEST(SurvivalBindingTest, ValidBindingJsonParsesSuccessfully) {
    const auto path = write_temp_file("proteus_survival_binding_valid.json", kValidBindingJson);
    const auto loaded = proteus::sandbox::load_survival_binding_from_file(path);

    ASSERT_EQ(loaded.ok, true);
    EXPECT_EQ(loaded.binding.state_to_seek_action.at("thirst"), "seek_water");
    EXPECT_EQ(loaded.binding.seek_to_terminal_action.at("seek_water"), "drink");
    EXPECT_EQ(loaded.binding.action_to_resource_kind.at("drink"), "water");
}

TEST(SurvivalBindingTest, MissingRequiredSectionFailsValidation) {
    const auto path = write_temp_file("proteus_survival_binding_missing.json", R"JSON({
      "state_to_seek_action": {"thirst": "seek_water"},
      "seek_to_terminal_action": {"seek_water": "drink"},
      "action_to_resource_kind": {"drink": "water"},
      "need_to_state_variable": {"hydration": "thirst"}
    })JSON");

    const auto loaded = proteus::sandbox::load_survival_binding_from_file(path);
    ASSERT_EQ(loaded.ok, false);
    EXPECT_EQ(loaded.errors.empty(), false);
}

TEST(SurvivalBindingTest, PreviewHighThirstResolvesToWaterChain) {
    const auto loaded = proteus::sandbox::parse_survival_binding_json(nlohmann::json::parse(kValidBindingJson), "inline");
    ASSERT_EQ(loaded.ok, true);

    proteus::sandbox::AgentSurvivalState state;
    state.state_thirst_current = 95.0;
    state.state_thirst_max = 100.0;
    state.state_hunger_current = 30.0;
    state.state_hunger_max = 100.0;
    state.state_fatigue_current = 40.0;
    state.state_fatigue_max = 100.0;

    const auto preview = proteus::sandbox::compute_survival_preview(state, loaded.binding);
    ASSERT_EQ(preview.has_value(), true);
    EXPECT_EQ(preview->highest_state_pressure, "thirst");
    EXPECT_EQ(preview->recommended_seek_action, "seek_water");
    EXPECT_EQ(preview->recommended_terminal_action, "drink");
    EXPECT_EQ(preview->recommended_resource_kind, "water");
}

TEST(SurvivalBindingTest, PreviewHighHungerResolvesToFoodChain) {
    const auto loaded = proteus::sandbox::parse_survival_binding_json(nlohmann::json::parse(kValidBindingJson), "inline");
    ASSERT_EQ(loaded.ok, true);

    proteus::sandbox::AgentSurvivalState state;
    state.state_thirst_current = 20.0;
    state.state_thirst_max = 100.0;
    state.state_hunger_current = 99.0;
    state.state_hunger_max = 100.0;
    state.state_fatigue_current = 40.0;
    state.state_fatigue_max = 100.0;

    const auto preview = proteus::sandbox::compute_survival_preview(state, loaded.binding);
    ASSERT_EQ(preview.has_value(), true);
    EXPECT_EQ(preview->highest_state_pressure, "hunger");
    EXPECT_EQ(preview->recommended_seek_action, "seek_food");
    EXPECT_EQ(preview->recommended_terminal_action, "eat");
    EXPECT_EQ(preview->recommended_resource_kind, "food");
}

TEST(SurvivalBindingTest, PreviewHighFatigueResolvesToShelterChain) {
    const auto loaded = proteus::sandbox::parse_survival_binding_json(nlohmann::json::parse(kValidBindingJson), "inline");
    ASSERT_EQ(loaded.ok, true);

    proteus::sandbox::AgentSurvivalState state;
    state.state_thirst_current = 20.0;
    state.state_thirst_max = 100.0;
    state.state_hunger_current = 20.0;
    state.state_hunger_max = 100.0;
    state.state_fatigue_current = 100.0;
    state.state_fatigue_max = 100.0;

    const auto preview = proteus::sandbox::compute_survival_preview(state, loaded.binding);
    ASSERT_EQ(preview.has_value(), true);
    EXPECT_EQ(preview->highest_state_pressure, "fatigue");
    EXPECT_EQ(preview->recommended_seek_action, "seek_shelter");
    EXPECT_EQ(preview->recommended_terminal_action, "sleep");
    EXPECT_EQ(preview->recommended_resource_kind, "shelter");
}

TEST(SurvivalBindingTest, TieBreakUsesFixedOrderThirstThenHungerThenFatigue) {
    const auto loaded = proteus::sandbox::parse_survival_binding_json(nlohmann::json::parse(kValidBindingJson), "inline");
    ASSERT_EQ(loaded.ok, true);

    proteus::sandbox::AgentSurvivalState state;
    state.state_thirst_current = 50.0;
    state.state_thirst_max = 100.0;
    state.state_hunger_current = 50.0;
    state.state_hunger_max = 100.0;
    state.state_fatigue_current = 50.0;
    state.state_fatigue_max = 100.0;

    const auto preview = proteus::sandbox::compute_survival_preview(state, loaded.binding);
    ASSERT_EQ(preview.has_value(), true);
    EXPECT_EQ(preview->highest_state_pressure, "thirst");
}

TEST(SurvivalBindingTest, ReloadFailurePreservesLastValidBinding) {
    const auto valid_path = write_temp_file("proteus_survival_binding_reload_valid.json", kValidBindingJson);
    const auto invalid_path = write_temp_file("proteus_survival_binding_reload_invalid.json", "{\n  \"state_to_seek_action\": {\"thirst\": \"\"}\n}\n");

    proteus::sandbox::SandboxWorld world;
    std::string err;

    ASSERT_EQ(world.reload_survival_binding(valid_path, &err), true);
    EXPECT_EQ(err.empty(), true);

    auto status_after_valid = world.survival_binding_status_json();
    ASSERT_EQ(status_after_valid.at("loaded").get<bool>(), true);
    EXPECT_EQ(status_after_valid.at("mappings").at("state_to_seek_action").at("thirst").get<std::string>(), "seek_water");

    ASSERT_EQ(world.reload_survival_binding(invalid_path, &err), false);
    EXPECT_EQ(err.empty(), false);

    auto status_after_invalid = world.survival_binding_status_json();
    ASSERT_EQ(status_after_invalid.at("loaded").get<bool>(), true);
    EXPECT_EQ(status_after_invalid.at("mappings").at("state_to_seek_action").at("thirst").get<std::string>(), "seek_water");
}
