#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "osupp/error.hpp"

namespace osu::detail {

template <typename T>
concept BinaryScalar = (std::integral<T> || std::floating_point<T>) && sizeof(T) <= 8;

// All .osr scalars are little-endian.
class Reader {
public:
    explicit Reader(std::span<const std::byte> data) noexcept : data_{data} {}

    [[nodiscard]] std::size_t offset() const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }

    template <BinaryScalar T>
    [[nodiscard]] std::expected<T, ParseError> read() {
        if (remaining() < sizeof(T)) return fail(std::format("need {} bytes", sizeof(T)));
        std::array<std::byte, sizeof(T)> raw{};
        std::ranges::copy(data_.subspan(pos_, sizeof(T)), raw.begin());
        pos_ += sizeof(T);
        if constexpr (std::endian::native == std::endian::big) std::ranges::reverse(raw);
        return std::bit_cast<T>(raw);
    }

    [[nodiscard]] std::expected<std::uint64_t, ParseError> read_uleb128() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (remaining() == 0) return fail("unterminated ULEB128");
            const auto byte = std::to_integer<std::uint8_t>(data_[pos_++]);
            value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return value;
        }
        return std::unexpected(ParseError{ParseErrorCode::invalid_value,
                                          "ULEB128 value exceeds 64 bits", 0, pos_});
    }

    // osu! string: 0x00 = absent, 0x0B = ULEB128 length + UTF-8 bytes.
    [[nodiscard]] std::expected<std::optional<std::string>, ParseError> read_osu_string() {
        const auto flag = read<std::uint8_t>();
        if (!flag) return std::unexpected(flag.error());
        if (*flag == 0x00) return std::optional<std::string>{};
        if (*flag != 0x0B) {
            return std::unexpected(ParseError{ParseErrorCode::invalid_value,
                                              std::format("bad string flag 0x{:02X}", *flag), 0,
                                              pos_ - 1});
        }
        const auto length = read_uleb128();
        if (!length) return std::unexpected(length.error());
        if (*length > remaining()) return fail("string length past end");
        const auto bytes = data_.subspan(pos_, static_cast<std::size_t>(*length));
        pos_ += static_cast<std::size_t>(*length);
        return std::optional<std::string>{std::in_place,
                                          reinterpret_cast<const char*>(bytes.data()),
                                          bytes.size()};
    }

    [[nodiscard]] std::expected<std::span<const std::byte>, ParseError> read_bytes(
        std::size_t count) {
        if (remaining() < count) return fail(std::format("need {} bytes", count));
        const auto result = data_.subspan(pos_, count);
        pos_ += count;
        return result;
    }

private:
    [[nodiscard]] std::unexpected<ParseError> fail(std::string_view what) const {
        return std::unexpected(ParseError{ParseErrorCode::truncated,
                                          std::format("truncated input: {}", what), 0, pos_});
    }

    std::span<const std::byte> data_;
    std::size_t pos_ = 0;
};

class Writer {
public:
    template <BinaryScalar T>
    void write(T value) {
        auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
        if constexpr (std::endian::native == std::endian::big) std::ranges::reverse(raw);
        out_.insert(out_.end(), raw.begin(), raw.end());
    }

    void write_uleb128(std::uint64_t value) {
        do {
            auto byte = static_cast<std::uint8_t>(value & 0x7F);
            value >>= 7;
            if (value != 0) byte |= 0x80;
            out_.push_back(std::byte{byte});
        } while (value != 0);
    }

    void write_osu_string(const std::optional<std::string_view>& s) {
        if (!s) {
            write<std::uint8_t>(0x00);
            return;
        }
        write<std::uint8_t>(0x0B);
        write_uleb128(s->size());
        const auto bytes = std::as_bytes(std::span{s->data(), s->size()});
        out_.insert(out_.end(), bytes.begin(), bytes.end());
    }

    void write_bytes(std::span<const std::byte> bytes) {
        out_.insert(out_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::vector<std::byte> take() && noexcept { return std::move(out_); }

private:
    std::vector<std::byte> out_;
};

}  // namespace osu::detail
