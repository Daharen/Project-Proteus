#include "core/semantic/candidate_semantic_validator.h"

#include "proteus/math/deterministic_math.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace proteus::semantic {
namespace {

constexpr std::size_t kRationaleMaxLength = 120;
constexpr std::size_t kSubstringMaxDiff = 4;
constexpr std::size_t kLevenshteinMaxLength = 12;
constexpr std::size_t kLevenshteinRejectThreshold = 2;
constexpr double kJaccardRejectThreshold = 0.8;
constexpr int kJaccardPrecision = 6;

constexpr std::array<std::pair<const char*, const char*>, 10> kLightStemTable = {{
    {"sneaking", "sneak"},
    {"sneaker", "sneak"},
    {"sneakster", "sneak"},
    {"assassins", "assassin"},
    {"illusionists", "illusion"},
    {"rogues", "rogue"},
    {"spies", "spy"},
    {"warriors", "warrior"},
    {"mages", "mage"},
    {"thieves", "thief"},
}};


constexpr std::array<const char*, 6> kCharacterClassBannedLabels = {{
    "owner",
    "pet owner",
    "caretaker",
    "handler",
    "guardian",
    "pet guardian",
}};

bool IsTitleCaseLabel(const std::string& label) {
    bool new_word = true;
    for (char c : label) {
        if (c == ' ') {
            new_word = true;
            continue;
        }
        if (new_word) {
            if (!(c >= 'A' && c <= 'Z')) {
                return false;
            }
            new_word = false;
        } else if (!(c >= 'a' && c <= 'z')) {
            return false;
        }
    }
    return !label.empty();
}

std::size_t WordCount(const std::string& input) {
    std::size_t count = 0;
    bool in_word = false;
    for (char c : input) {
        if (c == ' ') {
            in_word = false;
            continue;
        }
        if (!in_word) {
            ++count;
            in_word = true;
        }
    }
    return count;
}

bool ContainsOnlyLettersAndSpaces(const std::string& input) {
    for (char c : input) {
        if (c == ' ') {
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            return false;
        }
    }
    return !input.empty();
}

bool IsBannedForCategory(const std::string& normalized_label, bootstrap::BootstrapCategory category) {
    if (category != bootstrap::BootstrapCategory::BOOTSTRAP_CATEGORY_CHARACTER_CLASS_TITLES_V1) {
        return false;
    }
    for (const auto* banned : kCharacterClassBannedLabels) {
        if (normalized_label == banned) {
            return true;
        }
    }
    return false;
}
bool IsAsciiWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string TrimAndCollapseWhitespace(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool in_space = false;
    for (char c : input) {
        if (IsAsciiWhitespace(c)) {
            in_space = true;
            continue;
        }
        if (in_space && !out.empty()) {
            out.push_back(' ');
        }
        out.push_back(c);
        in_space = false;
    }
    return out;
}

bool IsAllowedLabelChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ';
}

std::string StripSuffixIfPresent(const std::string& input, const std::string& suffix, std::size_t min_len) {
    if (input.size() <= min_len || input.size() < suffix.size()) {
        return input;
    }
    if (input.compare(input.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return input.substr(0, input.size() - suffix.size());
    }
    return input;
}

std::vector<std::string> Tokenize(const std::string& input) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : input) {
        if (c == ' ') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

std::size_t LevenshteinDistance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> prev(b.size() + 1);
    std::vector<std::size_t> curr(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        prev[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        curr[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({
                prev[j] + 1,
                curr[j - 1] + 1,
                prev[j - 1] + cost,
            });
        }
        prev.swap(curr);
    }
    return prev[b.size()];
}

bool LooksDefinitional(const std::string& normalized_label, const std::string& normalized_rationale) {
    if (normalized_rationale.find(" is a ") != std::string::npos ||
        normalized_rationale.find(" is an ") != std::string::npos ||
        normalized_rationale.find(" is the ") != std::string::npos) {
        return true;
    }

    if (normalized_rationale.rfind(normalized_label + " is ", 0) == 0) {
        return true;
    }
    return false;
}

void MaybeTrace(const std::string& msg) {
#if !defined(NDEBUG) && defined(PROTEUS_TRACE_SEMANTIC_VALIDATION)
    std::cerr << "[semantic_validation] " << msg << "\n";
#else
    (void)msg;
#endif
}

}  // namespace

std::string NormalizeCandidateLabel(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (unsigned char uc : input) {
        char c = static_cast<char>(uc);
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c + ('a' - 'A')));
        } else if (uc <= 0x7F) {
            out.push_back(c);
        } else {
            out.push_back(' ');
        }
    }

    out = TrimAndCollapseWhitespace(out);

    std::string whitelisted;
    whitelisted.reserve(out.size());
    for (char c : out) {
        if (IsAllowedLabelChar(c)) {
            whitelisted.push_back(c);
        } else {
            whitelisted.push_back(' ');
        }
    }
    out = TrimAndCollapseWhitespace(whitelisted);

    out = StripSuffixIfPresent(out, "s", 4);
    out = StripSuffixIfPresent(out, "er", 6);
    out = StripSuffixIfPresent(out, "or", 6);
    out = StripSuffixIfPresent(out, "ist", 6);

    if (out.rfind("a ", 0) == 0) {
        out = out.substr(2);
    } else if (out.rfind("an ", 0) == 0) {
        out = out.substr(3);
    } else if (out.rfind("the ", 0) == 0) {
        out = out.substr(4);
    }

    for (const auto& [from, to] : kLightStemTable) {
        if (out == from) {
            out = to;
            break;
        }
    }

    out = TrimAndCollapseWhitespace(out);
    return out;
}

std::string SerializeRejectCode(CandidateSemanticRejectCode code) {
    switch (code) {
        case CandidateSemanticRejectCode::SEMANTIC_DUPLICATE_V1:
            return "SEMANTIC_DUPLICATE_V1";
        case CandidateSemanticRejectCode::SEMANTIC_STEM_COLLISION_V1:
            return "SEMANTIC_STEM_COLLISION_V1";
        case CandidateSemanticRejectCode::SEMANTIC_ECHO_V1:
            return "SEMANTIC_ECHO_V1";
        case CandidateSemanticRejectCode::SEMANTIC_OVERLAP_V1:
            return "SEMANTIC_OVERLAP_V1";
    }
    return "SEMANTIC_DUPLICATE_V1";
}

CandidateSemanticValidationResult ValidateCandidateSetDeterministic(
    const std::vector<CandidateSemanticItem>& candidates,
    const std::string& query_intent,
    bootstrap::BootstrapCategory category
) {
    CandidateSemanticValidationResult result;
    result.normalized_labels.reserve(candidates.size());

    const std::string normalized_intent = NormalizeCandidateLabel(query_intent);

    for (const auto& candidate : candidates) {
        result.normalized_labels.push_back(NormalizeCandidateLabel(candidate.label));
    }

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::string normalized_rationale = NormalizeCandidateLabel(candidates[i].short_rationale);
        if (candidates[i].short_rationale.size() > kRationaleMaxLength ||
            normalized_rationale == result.normalized_labels[i] ||
            LooksDefinitional(result.normalized_labels[i], normalized_rationale) ||
            (!normalized_intent.empty() && normalized_rationale == normalized_intent)) {
            result.ok = false;
            result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_OVERLAP_V1, i, i});
        }

        if (candidates[i].label.size() > 32 ||
            !ContainsOnlyLettersAndSpaces(candidates[i].label) ||
            WordCount(candidates[i].label) < 1 ||
            WordCount(candidates[i].label) > 3 ||
            !IsTitleCaseLabel(candidates[i].label)) {
            result.ok = false;
            result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_OVERLAP_V1, i, i});
        }

        if (IsBannedForCategory(result.normalized_labels[i], category)) {
            result.ok = false;
            result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_OVERLAP_V1, i, i});
        }

        if (!normalized_intent.empty() && result.normalized_labels[i] == normalized_intent) {
            result.ok = false;
            result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_ECHO_V1, i, i});
        }
    }

    for (std::size_t i = 0; i < result.normalized_labels.size(); ++i) {
        for (std::size_t j = i + 1; j < result.normalized_labels.size(); ++j) {
            const std::string& a = result.normalized_labels[i];
            const std::string& b = result.normalized_labels[j];

            if (a == b) {
                result.ok = false;
                result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_DUPLICATE_V1, i, j});
                continue;
            }

            const std::size_t max_len = std::max(a.size(), b.size());
            const std::size_t min_len = std::min(a.size(), b.size());

            if (min_len > 0) {
                const bool contained = (a.find(b) != std::string::npos) || (b.find(a) != std::string::npos);
                if (contained && (max_len - min_len) < kSubstringMaxDiff) {
                    result.ok = false;
                    result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_STEM_COLLISION_V1, i, j});
                }
            }

            if (max_len <= kLevenshteinMaxLength && LevenshteinDistance(a, b) <= kLevenshteinRejectThreshold) {
                result.ok = false;
                result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_STEM_COLLISION_V1, i, j});
            }

            const auto a_tokens = Tokenize(a);
            const auto b_tokens = Tokenize(b);
            std::set<std::string> union_tokens(a_tokens.begin(), a_tokens.end());
            union_tokens.insert(b_tokens.begin(), b_tokens.end());
            std::set<std::string> inter_tokens;
            for (const auto& token : a_tokens) {
                if (std::find(b_tokens.begin(), b_tokens.end(), token) != b_tokens.end()) {
                    inter_tokens.insert(token);
                }
            }
            if (!union_tokens.empty()) {
                const double jaccard = static_cast<double>(inter_tokens.size()) / static_cast<double>(union_tokens.size());
                const double rounded_jaccard = math::RoundFixed(jaccard, kJaccardPrecision);
                if (rounded_jaccard >= kJaccardRejectThreshold) {
                    result.ok = false;
                    result.rejections.push_back(CandidateSemanticRejection{CandidateSemanticRejectCode::SEMANTIC_OVERLAP_V1, i, j});
                }
            }
        }
    }

    for (std::size_t i = 0; i < result.normalized_labels.size(); ++i) {
        MaybeTrace("label[" + std::to_string(i) + "]=" + result.normalized_labels[i]);
    }
    for (const auto& rej : result.rejections) {
        MaybeTrace("reject=" + SerializeRejectCode(rej.code));
    }

    return result;
}

}  // namespace proteus::semantic
