#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include "osupp/error.hpp"

namespace osu::detail::lzma {

// .osr replay data uses the legacy LZMA "alone" container, not .xz.
[[nodiscard]] std::expected<std::vector<std::byte>, ParseError> decompress(
    std::span<const std::byte> input, std::size_t max_output);

[[nodiscard]] std::expected<std::vector<std::byte>, WriteError> compress(
    std::span<const std::byte> input);

}  // namespace osu::detail::lzma
