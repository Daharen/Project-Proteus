#include "core/funnel/bootstrap_prompt_composer.h"
#include "proteus/bootstrap/bootstrap_category.hpp"
#include "proteus/bootstrap/dimension_contract_registry.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

nlohmann::json make_candidate_artifact(const std::string& name) {
    return nlohmann::json{
        {"normalized_query_text", "pet class"},
        {"bootstrap_category", static_cast<double>(static_cast<std::int64_t>(proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1))},
        {"intent_tags", nlohmann::json::array({"pets"})},
        {"synopsis", "s"},
        {"safety_flags", nlohmann::json::array({})},
        {"proposals", nlohmann::json::array({
            nlohmann::json{{"proposal_id", "p1"}, {"proposal_kind", 1}, {"proposal_title", name}, {"proposal_body", "b1"}, {"proposal_json", nlohmann::json{{"mode", "candidate_set"}, {"name", name}, {"short_rationale", "distinct role"}}}},
            nlohmann::json{{"proposal_id", "p2"}, {"proposal_kind", 1}, {"proposal_title", "Summoner"}, {"proposal_body", "b2"}, {"proposal_json", nlohmann::json{{"mode", "candidate_set"}, {"name", "Summoner"}, {"short_rationale", "calls allies"}}}},
            nlohmann::json{{"proposal_id", "p3"}, {"proposal_kind", 1}, {"proposal_title", "Warden"}, {"proposal_body", "b3"}, {"proposal_json", nlohmann::json{{"mode", "candidate_set"}, {"name", "Warden"}, {"short_rationale", "protective archetype"}}}},
            nlohmann::json{{"proposal_id", "p4"}, {"proposal_kind", 1}, {"proposal_title", "Binder"}, {"proposal_body", "b4"}, {"proposal_json", nlohmann::json{{"mode", "candidate_set"}, {"name", "Binder"}, {"short_rationale", "binds entities"}}}},
            nlohmann::json{{"proposal_id", "p5"}, {"proposal_kind", 1}, {"proposal_title", "Ranger"}, {"proposal_body", "b5"}, {"proposal_json", nlohmann::json{{"mode", "candidate_set"}, {"name", "Ranger"}, {"short_rationale", "field specialist"}}}}
        })}
    };
}

}  // namespace

TEST(BootstrapPromptComposerTest, CategoryMappingDeterministic) {
    EXPECT_EQ(proteus::bootstrap::kBootstrapCategoryMappingVersion, 1);
    EXPECT_EQ(
        proteus::bootstrap::ResolveBootstrapCategory(proteus::bootstrap::BootstrapRoute::FunnelBootstrapV1, proteus::query::QueryDomain::Class),
        proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1
    );
    EXPECT_EQ(
        proteus::bootstrap::ResolveBootstrapCategory(proteus::bootstrap::BootstrapRoute::FunnelBootstrapV1, proteus::query::QueryDomain::Skill),
        proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1
    );
}

TEST(BootstrapPromptComposerTest, CategoryInstructionBlocksExactV1) {
    EXPECT_EQ(
        std::string(proteus::funnel::CategoryInstructionBlockV1(proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1)),
        std::string("CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1\nGenerate 5 character class titles appropriate to RPGs across fantasy, sci-fi, and hybrid settings.\nTitles must feel like class names from real games.\nDo not output mundane civilian roles.\nDo not output Owner, Pet Owner, Caretaker, Handler, Guardian.\nIf context implies companion control, bias toward archetypes: Beastmaster, Summoner, Tamer, Binder, Packlord, Warden.\nAvoid five variants of the same root word.\nEach candidate must represent a different fantasy of play.")
    );
    EXPECT_EQ(
        std::string(proteus::funnel::CategoryInstructionBlockV1(proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1)),
        std::string("CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1\nGenerate 5 skill names that would appear in an RPG skill tree.\nNames must be 1-3 words and action-evocative.\nPrefer verbs or evocative noun phrases over generic labels.\nAvoid Skill, Ability, Power in the name.\nAvoid trivial paraphrases.")
    );
    EXPECT_EQ(
        std::string(proteus::funnel::CategoryInstructionBlockV1(proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1)),
        std::string("CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1\nGenerate 5 trait or perk titles suitable for passive bonuses.\nNames must feel like perk cards or traits.\nAvoid literal descriptions like More Damage.\nPrefer flavorful archetype language.")
    );
    EXPECT_EQ(
        std::string(proteus::funnel::CategoryInstructionBlockV1(proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1)),
        std::string("CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1\nGenerate 5 faction role titles.\nTitles must be believable within a faction hierarchy.\nAvoid modern corporate titles.\nAvoid purely generic roles like Member.")
    );
    EXPECT_EQ(
        std::string(proteus::funnel::CategoryInstructionBlockV1(proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1)),
        std::string("CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1\nGenerate 5 item archetype names.\nNames must be category-level, not unique legendary names.\nAvoid Thing, Object, Item.")
    );
}

TEST(BootstrapPromptComposerTest, ComposedPromptContainsExpectedFragmentsPerCategory) {
    const auto prompt = proteus::funnel::ComposeBootstrapPrompt(proteus::funnel::BootstrapPromptTypedContext{
        .bootstrap_category = proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1,
        .schema_version = 1,
        .candidate_count = 5,
        .context_tokens = std::vector<std::string>{"has pets they control"}
    });
    EXPECT_EQ(prompt.find("Generate 5 character class titles") != std::string::npos, true);
    EXPECT_EQ(prompt.find("Owner") != std::string::npos, true);
    EXPECT_EQ(prompt.find("PetOwner") != std::string::npos, true);
    EXPECT_EQ(prompt.find("CONSTRAINTS:") != std::string::npos, true);
    EXPECT_EQ(prompt.find("CONTEXT_TOKENS:") != std::string::npos, true);
}

TEST(BootstrapPromptComposerTest, SchemaRegexAndLimitsArePresent) {
    const auto schema = proteus::bootstrap::BuildBootstrapSchema_ClassCandidateSet();
    const auto name_node = schema.at("properties").at("proposals").at("items").at("properties").at("proposal_json").at("properties").at("name");
    EXPECT_EQ(name_node.at("pattern").get<std::string>(), "^[A-Za-z]+(?: [A-Za-z]+){0,2}$");
    EXPECT_EQ(name_node.at("maxLength").get<double>(), 32.0);
}

TEST(BootstrapPromptComposerTest, PetOwnerFailsSchemaValidationDeterministically) {
    std::vector<std::string> issues;
    const auto artifact = make_candidate_artifact("PetOwner");
    EXPECT_EQ(proteus::bootstrap::ValidateBootstrapArtifact_ClassCandidateSet(artifact, issues), false);
}

TEST(BootstrapPromptComposerTest, OwnerFailsBannedListValidationDeterministically) {
    std::vector<std::string> issues;
    const auto artifact = make_candidate_artifact("Owner");
    EXPECT_EQ(proteus::bootstrap::ValidateBootstrapArtifact_ClassCandidateSet(artifact, issues), false);
}

TEST(BootstrapPromptComposerTest, BeastmasterAndSummonerPassSemanticAndSchema) {
    std::vector<std::string> issues;
    auto artifact = make_candidate_artifact("Beastmaster");
    EXPECT_EQ(proteus::bootstrap::ValidateBootstrapArtifact_ClassCandidateSet(artifact, issues), true);
}

TEST(BootstrapPromptComposerTest, BeastMasterPassesButHyphenFails) {
    {
        std::vector<std::string> issues;
        const auto artifact = make_candidate_artifact("Beast Master");
        EXPECT_EQ(proteus::bootstrap::ValidateBootstrapArtifact_ClassCandidateSet(artifact, issues), true);
    }
    {
        std::vector<std::string> issues;
        const auto artifact = make_candidate_artifact("Beast-master");
        EXPECT_EQ(proteus::bootstrap::ValidateBootstrapArtifact_ClassCandidateSet(artifact, issues), false);
    }
}

TEST(BootstrapPromptComposerTest, RepairMessageContainsCategorySpecificConstraints) {
    const auto msg = proteus::funnel::BuildSemanticRepairInstruction(
        proteus::bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1,
        std::vector<std::string>{"SEMANTIC_DUPLICATE_V1"}
    );
    EXPECT_EQ(msg.find("CATEGORY=BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1") != std::string::npos, true);
    EXPECT_EQ(msg.find("SEMANTIC_DUPLICATE_V1") != std::string::npos, true);
    EXPECT_EQ(msg.find("Beastmaster,Summoner,Tamer,Binder,Packlord") != std::string::npos, true);
}

TEST(BootstrapPromptComposerTest, ControlPlanePurityGuardNoPromptTextInSelectionCode) {
    const auto category_src = slurp("src/bootstrap/bootstrap_category.cpp");
    const auto schema_src = slurp("src/bootstrap/dimension_contract_registry.cpp");
    const auto validator_src = slurp("src/core/semantic/candidate_semantic_validator.cpp");
    EXPECT_EQ(category_src.find("prompt_text") == std::string::npos, true);
    EXPECT_EQ(schema_src.find("prompt_text") == std::string::npos, true);
    EXPECT_EQ(validator_src.find("prompt_text") == std::string::npos, true);
}
