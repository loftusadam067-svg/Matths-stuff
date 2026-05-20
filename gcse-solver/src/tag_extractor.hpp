#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace tag_extractor {

[[nodiscard]] inline std::optional<std::string> extract_calc_block(std::string_view llm_output) {
    constexpr std::string_view open_tag  = "[CALC]";
    constexpr std::string_view close_tag = "[/CALC]";

    const auto open_pos = llm_output.find(open_tag);
    if (open_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto content_start = open_pos + open_tag.size();
    const auto close_pos     = llm_output.find(close_tag, content_start);
    if (close_pos == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view content = llm_output.substr(content_start, close_pos - content_start);

    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto first = std::find_if(content.begin(), content.end(), not_space);
    auto last  = std::find_if(content.rbegin(), content.rend(), not_space).base();

    if (first >= last) {
        return std::string{};
    }
    return std::string(first, last);
}

}  // namespace tag_extractor
