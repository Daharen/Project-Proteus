#include "proteus/responders/response_envelope.h"

#include <algorithm>
#include <string_view>

#include "proteus/query/query_identity.hpp"

namespace proteus::responders {
namespace {

constexpr char kRecordSeparator = '\x1E';
constexpr char kFieldSeparator = '\x1F';

std::string normalize_newlines(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                ++i;
            }
            out.push_back('\n');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string escape_delimiters(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const char c : input) {
        if (c == '\\') {
            out += "\\\\";
            continue;
        }
        if (c == kRecordSeparator) {
            out += "\\x1E";
            continue;
        }
        if (c == kFieldSeparator) {
            out += "\\x1F";
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string canonical_string(std::string_view input) {
    return escape_delimiters(normalize_newlines(input));
}

void append_field(std::string& target, std::string_view value) {
    target.append(value.begin(), value.end());
    target.push_back(kFieldSeparator);
}

}  // namespace

std::string CanonicalizeEnvelopeForHash(const ResponseEnvelope& envelope) {
    std::string out;
    append_field(out, std::to_string(envelope.query_id));
    append_field(out, canonical_string(envelope.stable_player_id));
    append_field(out, std::to_string(static_cast<std::uint32_t>(envelope.responder_kind)));

    std::vector<const ContentBlock*> ordered_blocks;
    ordered_blocks.reserve(envelope.content_blocks.size());
    for (const auto& block : envelope.content_blocks) {
        ordered_blocks.push_back(&block);
    }
    std::stable_sort(ordered_blocks.begin(), ordered_blocks.end(), [](const ContentBlock* lhs, const ContentBlock* rhs) {
        return lhs->ordinal < rhs->ordinal;
    });

    append_field(out, std::to_string(ordered_blocks.size()));
    for (const auto* block : ordered_blocks) {
        append_field(out, std::to_string(static_cast<std::uint32_t>(block->kind)));
        append_field(out, std::to_string(block->ordinal));
        append_field(out, canonical_string(block->text));
        append_field(out, std::to_string(block->options.size()));
        for (const auto& option : block->options) {
            append_field(out, canonical_string(option));
        }
        out.push_back(kRecordSeparator);
    }

    std::vector<std::string> intents = envelope.mechanical_intents;
    std::sort(intents.begin(), intents.end());
    append_field(out, std::to_string(intents.size()));
    for (const auto& intent : intents) {
        append_field(out, canonical_string(intent));
    }

    append_field(out, canonical_string(envelope.provenance_tag));
    append_field(out, std::to_string(envelope.schema_version_tag));
    return out;
}

std::uint64_t ComputeEnvelopeHash64(const ResponseEnvelope& envelope) {
    const std::string canonical = CanonicalizeEnvelopeForHash(envelope);
    return query::QueryHash64(canonical);
}

}  // namespace proteus::responders
