#pragma once

#include "proteus/query/query_identity.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace proteus::bootstrap {

constexpr std::int64_t kBootstrapCategoryMappingVersion = 1;

enum class BootstrapCategory : std::int64_t {
    BOOTSTRAP_CATEGORY_UNSPECIFIED_V1 = 0,
    BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1 = 1,
    BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1 = 2,
    BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1 = 3,
    BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1 = 4,
    BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1 = 5,
};

enum class BootstrapRoute : std::int64_t {
    FunnelBootstrapV1 = 0,
    QueryBootstrapV1 = 1,
};

struct BootstrapCategoryMappingEntry {
    BootstrapRoute route;
    query::QueryDomain domain;
    BootstrapCategory category;
};

constexpr std::array<BootstrapCategoryMappingEntry, 8> kBootstrapCategoryMappingV1 = {{
    {BootstrapRoute::FunnelBootstrapV1, query::QueryDomain::Class, BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1},
    {BootstrapRoute::FunnelBootstrapV1, query::QueryDomain::Generic, BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1},
    {BootstrapRoute::FunnelBootstrapV1, query::QueryDomain::Skill, BootstrapCategory::BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1},
    {BootstrapRoute::FunnelBootstrapV1, query::QueryDomain::NpcIntent, BootstrapCategory::BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1},
    {BootstrapRoute::FunnelBootstrapV1, query::QueryDomain::DialogueLine, BootstrapCategory::BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1},
    {BootstrapRoute::FunnelBootstrapV1, query::QueryDomain::DialogueOption, BootstrapCategory::BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1},
    {BootstrapRoute::QueryBootstrapV1, query::QueryDomain::Generic, BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1},
    {BootstrapRoute::QueryBootstrapV1, query::QueryDomain::Skill, BootstrapCategory::BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1},
}};

BootstrapCategory ResolveBootstrapCategory(BootstrapRoute route, query::QueryDomain domain);
const char* BootstrapCategoryName(BootstrapCategory category);

}  // namespace proteus::bootstrap
