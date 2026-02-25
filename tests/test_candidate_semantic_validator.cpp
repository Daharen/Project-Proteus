#include "core/semantic/candidate_semantic_validator.h"
#include "proteus/bootstrap/dimension_contract_registry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::vector<proteus::semantic::CandidateSemanticItem> MakeItems(const std::vector<std::string>& labels) {
    std::vector<proteus::semantic::CandidateSemanticItem> out;
    for (const auto& label : labels) {
        out.push_back(proteus::semantic::CandidateSemanticItem{label, "distinct rationale"});
    }
    return out;
}

void AssertAllObjectNodesClosed(const nlohmann::json& node) {
    if (!node.is_object()) {
        return;
    }
    if (node.contains("type") && node.at("type").is_string() && node.at("type").get<std::string>() == "object") {
        EXPECT_EQ(node.contains("additionalProperties"), true);
        EXPECT_EQ(node.at("additionalProperties").get<bool>(), false);
    }
    if (node.contains("properties") && node.at("properties").is_object()) {
        for (const auto& item : node.at("properties").items()) {
            AssertAllObjectNodesClosed(item.value());
        }
    }
    if (node.contains("items")) {
        AssertAllObjectNodesClosed(node.at("items"));
    }
    for (const char* defs_key : {"definitions", "$defs"}) {
        if (node.contains(defs_key) && node.at(defs_key).is_object()) {
            for (const auto& item : node.at(defs_key).items()) {
                AssertAllObjectNodesClosed(item.value());
            }
        }
    }
}

}  // namespace

TEST(CandidateSemanticValidatorTest, SneakFamilyDeterministicallyRejected) {
    const auto result = proteus::semantic::ValidateCandidateSetDeterministic(
        MakeItems({"Sneak", "Sneaker", "Sneaking", "Sneaks", "Sneakster"}),
        "rogue stealth",
        proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1
    );
    EXPECT_EQ(result.ok, false);
    EXPECT_GT(result.rejections.size(), 0);
}

TEST(CandidateSemanticValidatorTest, DistinctSetPasses) {
    const auto result = proteus::semantic::ValidateCandidateSetDeterministic(
        MakeItems({"Rogue", "Assassin", "Shadow", "Spy", "Illusionist"}),
        "stealth class",
        proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1
    );
    EXPECT_EQ(result.ok, true);
    EXPECT_EQ(result.rejections.empty(), true);
}

TEST(CandidateSemanticValidatorTest, RerunDeterminism) {
    const auto input = MakeItems({"Sneak", "Sneaker", "Sneaking", "Sneaks", "Sneakster"});
    const auto first = proteus::semantic::ValidateCandidateSetDeterministic(input, "sneak", proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1);
    const auto second = proteus::semantic::ValidateCandidateSetDeterministic(input, "sneak", proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1);
    ASSERT_EQ(first.rejections.size(), second.rejections.size());
    EXPECT_EQ(first.normalized_labels, second.normalized_labels);
    for (std::size_t i = 0; i < first.rejections.size(); ++i) {
        EXPECT_EQ(first.rejections[i].code, second.rejections[i].code);
        EXPECT_EQ(first.rejections[i].first_index, second.rejections[i].first_index);
        EXPECT_EQ(first.rejections[i].second_index, second.rejections[i].second_index);
    }
}

TEST(CandidateSemanticValidatorTest, EchoRejected) {
    const auto result = proteus::semantic::ValidateCandidateSetDeterministic(
        MakeItems({"Assassin", "Rogue", "Scout"}),
        "Assassin",
        proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1
    );
    EXPECT_EQ(result.ok, false);
    bool saw_echo = false;
    for (const auto& rejection : result.rejections) {
        saw_echo = saw_echo || rejection.code == proteus::semantic::CandidateSemanticRejectCode::SEMANTIC_ECHO_V1;
    }
    EXPECT_EQ(saw_echo, true);
}

TEST(CandidateSemanticValidatorTest, RationaleSanityRejected) {
    std::vector<proteus::semantic::CandidateSemanticItem> items = {
        {"Rogue", "Rogue is a class"},
        {"Assassin", "Assassin"},
        {"Shadow", "stealth operator"},
    };
    const auto result = proteus::semantic::ValidateCandidateSetDeterministic(items, "stealth", proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1);
    EXPECT_EQ(result.ok, false);
}

TEST(CandidateSemanticValidatorTest, CandidateSchemaRequiresRationaleAndClosesObjects) {
    const auto schema = proteus::bootstrap::BuildBootstrapSchema_ClassCandidateSet();
    const auto required = schema.at("properties").at("proposals").at("items").at("properties").at("proposal_json").at("required");
    ASSERT_EQ(required.is_array(), true);
    bool has_short_rationale = false;
    for (const auto& value : required) {
        if (value.is_string() && value.get<std::string>() == "short_rationale") {
            has_short_rationale = true;
        }
    }
    EXPECT_EQ(has_short_rationale, true);
    AssertAllObjectNodesClosed(schema);
}

TEST(CandidateSemanticValidatorTest, NormalizationVersionLocked) {
    EXPECT_EQ(proteus::semantic::kCandidateNormalizationVersion, 1);
}
