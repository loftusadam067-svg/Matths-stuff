#pragma once

#include <cstdio>
#include <string>
#include <string_view>

namespace output_formatter {

namespace detail {
[[nodiscard]] inline std::string to_single_line(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        out.push_back((c == '\n' || c == '\r' || c == '\t') ? ' ' : c);
    }
    return out;
}
}  // namespace detail

inline void print_result(std::string_view problem, std::string_view code, std::string_view answer) {
    std::printf("[PROB]: %s\n", detail::to_single_line(problem).c_str());
    std::printf("[CODE]: %s\n", detail::to_single_line(code).c_str());
    std::printf("[ANS]:  %s\n", detail::to_single_line(answer).c_str());
    std::fflush(stdout);
}

inline void print_error(std::string_view problem, std::string_view code, std::string_view error_reason) {
    std::printf("[PROB]:  %s\n", detail::to_single_line(problem).c_str());
    std::printf("[CODE]:  %s\n", detail::to_single_line(code).c_str());
    std::printf("[ERROR]: %s\n", detail::to_single_line(error_reason).c_str());
    std::fflush(stdout);
}

}  // namespace output_formatter
