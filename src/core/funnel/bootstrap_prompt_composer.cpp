#include "core/funnel/bootstrap_prompt_composer.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <vector>

namespace proteus::funnel {
namespace {

constexpr const char* kGlobalInstructionBlockV1 =
    "GLOBAL_INSTRUCTIONS_V1\n"
    "Output mode is candidate_set.\n"
    "Produce exactly 5 candidates.\n"
    "Each candidate name must be 1-3 words.\n"
    "Each candidate name must be Title Case.\n"
    "Each candidate name must contain only letters and spaces.\n"
    "No camelCase.\n"
    "No concatenation like PetOwner.\n"
    "No generic real-world job titles.\n"
    "No Owner, Caretaker, Handler, Guardian, Pet Owner.\n"
    "Avoid singular/plural variants of the same root.\n"
    "Candidates must be meaningfully distinct archetypes, not near-synonyms.\n"
    "Prefer established RPG archetype language over literal description.\n"
    "If context suggests pets/companions, prefer Beastmaster, Summoner, Tamer, Binder, Warden, Ranger style naming.\n"
    "short_rationale must be <= 120 chars and must not be definitional.\n"
    "short_rationale must not restate the name.";

constexpr const char* kCategoryCharacterClassV1 =
    "CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1\n"
    "Generate 5 character class titles appropriate to RPGs across fantasy, sci-fi, and hybrid settings.\n"
    "Titles must feel like class names from real games.\n"
    "Do not output mundane civilian roles.\n"
    "Do not output Owner, Pet Owner, Caretaker, Handler, Guardian.\n"
    "If context implies companion control, bias toward archetypes: Beastmaster, Summoner, Tamer, Binder, Packlord, Warden.\n"
    "Avoid five variants of the same root word.\n"
    "Each candidate must represent a different fantasy of play.";

constexpr const char* kCategorySkillV1 =
    "CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1\n"
    "Generate 5 skill names that would appear in an RPG skill tree.\n"
    "Names must be 1-3 words and action-evocative.\n"
    "Prefer verbs or evocative noun phrases over generic labels.\n"
    "Avoid Skill, Ability, Power in the name.\n"
    "Avoid trivial paraphrases.";

constexpr const char* kCategoryTraitV1 =
    "CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1\n"
    "Generate 5 trait or perk titles suitable for passive bonuses.\n"
    "Names must feel like perk cards or traits.\n"
    "Avoid literal descriptions like More Damage.\n"
    "Prefer flavorful archetype language.";

constexpr const char* kCategoryFactionV1 =
    "CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1\n"
    "Generate 5 faction role titles.\n"
    "Titles must be believable within a faction hierarchy.\n"
    "Avoid modern corporate titles.\n"
    "Avoid purely generic roles like Member.";

constexpr const char* kCategoryItemV1 =
    "CATEGORY_BLOCK_V1: BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1\n"
    "Generate 5 item archetype names.\n"
    "Names must be category-level, not unique legendary names.\n"
    "Avoid Thing, Object, Item.";

std::vector<std::string> SortedUniqueTokens(const std::vector<std::string>& in) {
    std::vector<std::string> out = in;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

const char* CategoryExamples(bootstrap::BootstrapCategory category) {
    switch (category) {
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1:
            return "Beastmaster,Summoner,Tamer,Binder,Packlord";
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1:
            return "Shadow Step,Arc Lash,Iron Resolve,Chain Pull,Soul Weave";
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1:
            return "Cold Focus,Iron Nerves,Quick Recovery,Silent Footing,Last Stand";
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1:
            return "Initiate,Warden,Marshal,High Templar,Archivist";
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1:
            return "Heavy Blade,Focus Staff,Ward Charm,Field Kit,Siege Relic";
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_UNSPECIFIED_V1:
            return "Ranger,Warden,Summoner,Sentinel,Invoker";
    }
    return "Ranger,Warden,Summoner,Sentinel,Invoker";
}

}  // namespace

const char* GlobalBootstrapInstructionBlockV1() {
    return kGlobalInstructionBlockV1;
}

const char* CategoryInstructionBlockV1(bootstrap::BootstrapCategory category) {
    switch (category) {
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1:
            return kCategoryCharacterClassV1;
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1:
            return kCategorySkillV1;
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1:
            return kCategoryTraitV1;
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1:
            return kCategoryFactionV1;
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1:
            return kCategoryItemV1;
        case bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_UNSPECIFIED_V1:
            return kCategoryCharacterClassV1;
    }
    return kCategoryCharacterClassV1;
}

std::string ComposeBootstrapPrompt(const BootstrapPromptTypedContext& context) {
    std::ostringstream out;
    out << "BOOTSTRAP_PROMPT_COMPOSER_VERSION=" << kBootstrapPromptComposerVersion << "\n";
    out << "SCHEMA_VERSION=" << context.schema_version << "\n";
    out << "CATEGORY=" << bootstrap::BootstrapCategoryName(context.bootstrap_category) << "\n";
    out << "CANDIDATE_COUNT=" << context.candidate_count << "\n\n";
    out << GlobalBootstrapInstructionBlockV1() << "\n\n";
    out << CategoryInstructionBlockV1(context.bootstrap_category) << "\n\n";
    out << "CONSTRAINTS:\n";
    out << "- output_json: strict candidate_set envelope\n";
    out << "- candidates_exact_count: " << context.candidate_count << "\n";
    out << "- candidate_name_pattern: letters_and_spaces_only\n";
    out << "- candidate_name_word_count: 1_to_3\n";
    out << "- candidate_name_max_len: 32\n";
    out << "- short_rationale_max_len: 120\n";
    out << "- additionalProperties: false\n\n";
    out << "CONTEXT_TOKENS:\n";

    const auto tokens = SortedUniqueTokens(context.context_tokens);
    if (tokens.empty()) {
        out << "- none\n";
    } else {
        for (const auto& token : tokens) {
            out << "- " << token << "\n";
        }
    }
    return out.str();
}

std::string BuildSemanticRepairInstruction(
    bootstrap::BootstrapCategory category,
    const std::vector<std::string>& reject_codes
) {
    std::ostringstream out;
    out << "SEMANTIC_REPAIR_V1\n";
    out << "CATEGORY=" << bootstrap::BootstrapCategoryName(category) << "\n";
    out << "REJECT_CODES=";
    for (std::size_t i = 0; i < reject_codes.size(); ++i) {
        out << reject_codes[i];
        if (i + 1 < reject_codes.size()) {
            out << ",";
        }
    }
    out << "\n";
    out << "REMINDER=letters_and_spaces_only|title_case|1_to_3_words|short_rationale_max_120|no_definitions|no_near_duplicates\n";
    out << "ALLOWED_EXAMPLES=" << CategoryExamples(category) << "\n";
    return out.str();
}

}  // namespace proteus::funnel
