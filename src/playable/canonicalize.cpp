#include "proteus/playable/canonicalize.hpp"

#include <algorithm>
#include <cctype>

namespace proteus::playable {

namespace {

bool is_ascii_upper(char ch) {
    return ch >= 'A' && ch <= 'Z';
}

bool is_trailing_punctuation(char ch) {
    return ch == '.' || ch == '!' || ch == '?' || ch == ';' || ch == ':';
}

}  // namespace

std::string canonicalize_prompt(const std::string& raw_prompt) {
    std::string normalized;
    normalized.reserve(raw_prompt.size());

    for (std::size_t i = 0; i < raw_prompt.size(); ++i) {
        char ch = raw_prompt[i];

        if (ch == '\t' || ch == '\n' || ch == '\r') {
            ch = ' ';
        }

        // UTF-8 curly quote normalization: “ ” -> ", ‘ ’ -> '
        if (static_cast<unsigned char>(ch) == 0xE2 && i + 2 < raw_prompt.size()) {
            const auto b1 = static_cast<unsigned char>(raw_prompt[i + 1]);
            const auto b2 = static_cast<unsigned char>(raw_prompt[i + 2]);
            if (b1 == 0x80 && (b2 == 0x9C || b2 == 0x9D)) {
                normalized.push_back('"');
                i += 2;
                continue;
            }
            if (b1 == 0x80 && (b2 == 0x98 || b2 == 0x99)) {
                normalized.push_back('\'');
                i += 2;
                continue;
            }
        }

        normalized.push_back(ch);
    }

    const auto first_non_space = normalized.find_first_not_of(' ');
    if (first_non_space == std::string::npos) {
        return std::string{};
    }
    const auto last_non_space = normalized.find_last_not_of(' ');
    std::string trimmed = normalized.substr(first_non_space, last_non_space - first_non_space + 1);

    std::string collapsed;
    collapsed.reserve(trimmed.size());
    bool previous_space = false;
    for (char ch : trimmed) {
        if (ch == ' ') {
            if (!previous_space) {
                collapsed.push_back(' ');
                previous_space = true;
            }
            continue;
        }

        previous_space = false;
        collapsed.push_back(is_ascii_upper(ch) ? static_cast<char>(ch - 'A' + 'a') : ch);
    }

    while (!collapsed.empty() && is_trailing_punctuation(collapsed.back())) {
        collapsed.pop_back();
    }

    return collapsed;
}

}  // namespace proteus::playable
