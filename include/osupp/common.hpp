#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace osu {

enum class Mode : std::uint8_t {
    osu = 0,
    taiko = 1,
    fruits = 2,  // "catch" is a C++ keyword; osu! calls this ruleset "fruits" internally
    mania = 3,
};

// 0 means "inherit the default" everywhere this appears.
enum class SampleSet : std::uint8_t {
    none = 0,
    normal = 1,
    soft = 2,
    drum = 3,
};

// Bitmask; 0 means the normal hitsound alone.
enum class HitSound : std::uint8_t {
    none = 0,
    normal = 1,
    whistle = 2,
    finish = 4,
    clap = 8,
};

// Bitmask from the timing point "effects" field.
enum class TimingEffects : std::uint8_t {
    none = 0,
    kiai = 1,
    omit_first_barline = 8,
};

enum class Mods : std::uint32_t {
    none = 0,
    no_fail = 1u << 0,
    easy = 1u << 1,
    touch_device = 1u << 2,
    hidden = 1u << 3,
    hard_rock = 1u << 4,
    sudden_death = 1u << 5,
    double_time = 1u << 6,
    relax = 1u << 7,
    half_time = 1u << 8,
    nightcore = 1u << 9,
    flashlight = 1u << 10,
    autoplay = 1u << 11,
    spun_out = 1u << 12,
    autopilot = 1u << 13,
    perfect = 1u << 14,
    key4 = 1u << 15,
    key5 = 1u << 16,
    key6 = 1u << 17,
    key7 = 1u << 18,
    key8 = 1u << 19,
    fade_in = 1u << 20,
    random = 1u << 21,
    cinema = 1u << 22,
    target_practice = 1u << 23,
    key9 = 1u << 24,
    key_coop = 1u << 25,
    key1 = 1u << 26,
    key3 = 1u << 27,
    key2 = 1u << 28,
    score_v2 = 1u << 29,
    mirror = 1u << 30,
};

// Bitmask of pressed inputs in a replay frame. osu!mania packs its own
// column bits into the same field, so unnamed bits are meaningful too.
enum class ReplayKeys : std::uint32_t {
    none = 0,
    mouse1 = 1,
    mouse2 = 2,
    key1 = 4,
    key2 = 8,
    smoke = 16,
};

template <typename E>
inline constexpr bool enable_bitmask_operators = false;
template <> inline constexpr bool enable_bitmask_operators<HitSound> = true;
template <> inline constexpr bool enable_bitmask_operators<TimingEffects> = true;
template <> inline constexpr bool enable_bitmask_operators<Mods> = true;
template <> inline constexpr bool enable_bitmask_operators<ReplayKeys> = true;

template <typename E>
concept BitmaskEnum = std::is_enum_v<E> && enable_bitmask_operators<E>;

template <BitmaskEnum E>
[[nodiscard]] constexpr E operator|(E a, E b) noexcept {
    return static_cast<E>(std::to_underlying(a) | std::to_underlying(b));
}

template <BitmaskEnum E>
[[nodiscard]] constexpr E operator&(E a, E b) noexcept {
    return static_cast<E>(std::to_underlying(a) & std::to_underlying(b));
}

template <BitmaskEnum E>
[[nodiscard]] constexpr E operator^(E a, E b) noexcept {
    return static_cast<E>(std::to_underlying(a) ^ std::to_underlying(b));
}

template <BitmaskEnum E>
[[nodiscard]] constexpr E operator~(E a) noexcept {
    return static_cast<E>(~std::to_underlying(a));
}

template <BitmaskEnum E>
constexpr E& operator|=(E& a, E b) noexcept { return a = a | b; }
template <BitmaskEnum E>
constexpr E& operator&=(E& a, E b) noexcept { return a = a & b; }
template <BitmaskEnum E>
constexpr E& operator^=(E& a, E b) noexcept { return a = a ^ b; }

template <BitmaskEnum E>
[[nodiscard]] constexpr bool has_flag(E value, E flag) noexcept {
    return (std::to_underlying(value) & std::to_underlying(flag)) == std::to_underlying(flag)
        && std::to_underlying(flag) != 0;
}

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    auto operator<=>(const Rgb&) const = default;
};

}  // namespace osu
