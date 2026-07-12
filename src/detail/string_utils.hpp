#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>
#include <vector>

namespace osu::detail {

[[nodiscard]] constexpr std::string_view trim(std::string_view s) noexcept {
    constexpr std::string_view ws = " \t\r\n";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string_view::npos) return {};
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

[[nodiscard]] inline std::vector<std::string_view> split(std::string_view s, char delim) {
    std::vector<std::string_view> out;
    for (auto part : s | std::views::split(delim)) {
        out.emplace_back(std::string_view{part});
    }
    return out;
}

// Like split() but the last element keeps any remaining delimiters
// (needed for hit sample filenames, which may contain ':').
[[nodiscard]] inline std::vector<std::string_view> split_n(std::string_view s, char delim,
                                                           std::size_t max_parts) {
    std::vector<std::string_view> out;
    while (out.size() + 1 < max_parts) {
        const auto pos = s.find(delim);
        if (pos == std::string_view::npos) break;
        out.push_back(s.substr(0, pos));
        s.remove_prefix(pos + 1);
    }
    out.push_back(s);
    return out;
}

template <typename T>
concept ParsableNumber = std::integral<T> || std::floating_point<T>;

template <ParsableNumber T>
[[nodiscard]] std::optional<T> parse_num(std::string_view s) noexcept {
    s = trim(s);
    if (s.starts_with('+')) s.remove_prefix(1);  // from_chars rejects a leading '+'
    if (s.empty()) return std::nullopt;
    T value{};
    std::from_chars_result result{};
    if constexpr (std::floating_point<T>) {
        result = std::from_chars(s.data(), s.data() + s.size(), value, std::chars_format::general);
    } else {
        result = std::from_chars(s.data(), s.data() + s.size(), value);
    }
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) return std::nullopt;
    return value;
}

}  // namespace osu::detail
