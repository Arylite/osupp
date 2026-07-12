#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <vector>

#include "osupp/common.hpp"
#include "osupp/error.hpp"

namespace osu::replay {

// .osr timestamps are .NET ticks: 100 ns units since 0001-01-01 UTC.
// Stored as a std::chrono time point so no precision is lost round-tripping.
using WindowsTicks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
using Timestamp = std::chrono::sys_time<WindowsTicks>;

inline constexpr std::int64_t ticks_at_unix_epoch = 621'355'968'000'000'000;

[[nodiscard]] constexpr Timestamp timestamp_from_ticks(std::int64_t ticks) noexcept {
    return Timestamp{WindowsTicks{ticks - ticks_at_unix_epoch}};
}

[[nodiscard]] constexpr std::int64_t ticks_from_timestamp(Timestamp t) noexcept {
    return t.time_since_epoch().count() + ticks_at_unix_epoch;
}

struct ReplayFrame {
    std::int64_t time_delta_ms = 0;
    float x = 0.0f;
    float y = 0.0f;
    ReplayKeys keys = ReplayKeys::none;

    auto operator<=>(const ReplayFrame&) const = default;
};

struct LifeBarPoint {
    int time_ms = 0;
    double life = 0.0;  // 0..1

    auto operator<=>(const LifeBarPoint&) const = default;
};

struct Replay {
    Mode mode = Mode::osu;
    std::int32_t game_version = 0;
    std::string beatmap_md5;
    std::string player_name;
    std::string replay_md5;
    std::uint16_t count_300 = 0;
    std::uint16_t count_100 = 0;
    std::uint16_t count_50 = 0;
    std::uint16_t count_geki = 0;
    std::uint16_t count_katu = 0;
    std::uint16_t count_miss = 0;
    std::int32_t score = 0;
    std::uint16_t max_combo = 0;
    bool perfect = false;
    Mods mods = Mods::none;
    std::optional<std::vector<LifeBarPoint>> life_bar;  // nullopt when the field is absent
    Timestamp timestamp{};
    std::vector<ReplayFrame> frames;
    // Carried by a trailing pseudo-frame "-12345|0|0|seed" (game version >= 20130319).
    std::optional<std::int32_t> rng_seed;
    std::int64_t online_score_id = 0;
    std::optional<double> target_practice_accuracy;  // present when Mods::target_practice is set

    bool operator==(const Replay&) const = default;
};

[[nodiscard]] std::expected<Replay, ParseError> parse(std::span<const std::byte> data);
[[nodiscard]] std::expected<Replay, ParseError> parse_file(const std::filesystem::path& path);

[[nodiscard]] std::expected<std::vector<std::byte>, WriteError> write(const Replay& replay);
[[nodiscard]] std::expected<void, WriteError> write_file(const Replay& replay,
                                                         const std::filesystem::path& path);

}  // namespace osu::replay
