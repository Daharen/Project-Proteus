#include "proteus/bootstrap/bootstrap_category.hpp"

namespace proteus::bootstrap {

BootstrapCategory ResolveBootstrapCategory(BootstrapRoute route, query::QueryDomain domain) {
    for (const auto& entry : kBootstrapCategoryMappingV1) {
        if (entry.route == route && entry.domain == domain) {
            return entry.category;
        }
    }
    return BootstrapCategory::BOOTSTRAP_CATEGORY_UNSPECIFIED_V1;
}

const char* BootstrapCategoryName(BootstrapCategory category) {
    switch (category) {
        case BootstrapCategory::BOOTSTRAP_CATEGORY_UNSPECIFIED_V1:
            return "BOOTSTRAP_CATEGORY_UNSPECIFIED_V1";
        case BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1:
            return "BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1";
        case BootstrapCategory::BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1:
            return "BOOTSTRAP_CATEGORY_SKILL_NAME_TITLES_V1";
        case BootstrapCategory::BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1:
            return "BOOTSTRAP_CATEGORY_TRAIT_PERK_TITLES_V1";
        case BootstrapCategory::BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1:
            return "BOOTSTRAP_CATEGORY_FACTION_ROLE_TITLES_V1";
        case BootstrapCategory::BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1:
            return "BOOTSTRAP_CATEGORY_ITEM_ARCHETYPE_TITLES_V1";
    }
    return "BOOTSTRAP_CATEGORY_UNSPECIFIED_V1";
}

}  // namespace proteus::bootstrap
