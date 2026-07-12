#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "osupp/common.hpp"
#include "osupp/error.hpp"

namespace osu::beatmap {

// Unknown "Key: Value" lines are kept here verbatim (trimmed) so that
// deprecated or future fields survive a parse/write round-trip.
using ExtraFields = std::vector<std::pair<std::string, std::string>>;

struct General {
    std::optional<std::string> audio_filename;
    std::optional<int> audio_lead_in;
    std::optional<int> preview_time;
    std::optional<int> countdown;
    std::optional<SampleSet> sample_set;
    std::optional<double> stack_leniency;
    std::optional<Mode> mode;
    std::optional<bool> letterbox_in_breaks;
    std::optional<bool> use_skin_sprites;
    std::optional<std::string> overlay_position;
    std::optional<std::string> skin_preference;
    std::optional<bool> epilepsy_warning;
    std::optional<int> countdown_offset;
    std::optional<bool> special_style;
    std::optional<bool> widescreen_storyboard;
    std::optional<bool> samples_match_playback_rate;
    ExtraFields extra;

    bool operator==(const General&) const = default;
};

struct Editor {
    std::optional<std::vector<int>> bookmarks;
    std::optional<double> distance_spacing;
    std::optional<int> beat_divisor;
    std::optional<int> grid_size;
    std::optional<double> timeline_zoom;
    ExtraFields extra;

    bool operator==(const Editor&) const = default;
};

struct Metadata {
    std::optional<std::string> title;
    std::optional<std::string> title_unicode;
    std::optional<std::string> artist;
    std::optional<std::string> artist_unicode;
    std::optional<std::string> creator;
    std::optional<std::string> version;
    std::optional<std::string> source;
    std::optional<std::vector<std::string>> tags;
    std::optional<std::int64_t> beatmap_id;
    std::optional<std::int64_t> beatmap_set_id;
    ExtraFields extra;

    bool operator==(const Metadata&) const = default;
};

struct Difficulty {
    std::optional<double> hp_drain_rate;
    std::optional<double> circle_size;
    std::optional<double> overall_difficulty;
    std::optional<double> approach_rate;  // absent in old formats
    std::optional<double> slider_multiplier;
    std::optional<double> slider_tick_rate;
    ExtraFields extra;

    bool operator==(const Difficulty&) const = default;
};

struct BackgroundEvent {
    int start_time = 0;
    std::string filename;
    int x_offset = 0;
    int y_offset = 0;

    bool operator==(const BackgroundEvent&) const = default;
};

struct VideoEvent {
    int start_time = 0;
    std::string filename;
    int x_offset = 0;
    int y_offset = 0;

    bool operator==(const VideoEvent&) const = default;
};

struct BreakEvent {
    int start_time = 0;
    int end_time = 0;

    auto operator<=>(const BreakEvent&) const = default;
};

// Storyboard lines, comments and unrecognized events, preserved verbatim.
struct RawEvent {
    std::string line;

    auto operator<=>(const RawEvent&) const = default;
};

using Event = std::variant<BackgroundEvent, VideoEvent, BreakEvent, RawEvent>;

struct TimingPoint {
    double time = 0.0;         // ms; decimal values appear in old files
    double beat_length = 0.0;  // ms per beat if uninherited, else negative velocity multiplier
    int meter = 4;
    SampleSet sample_set = SampleSet::none;
    int sample_index = 0;
    int volume = 100;
    bool uninherited = true;
    TimingEffects effects = TimingEffects::none;

    auto operator<=>(const TimingPoint&) const = default;
};

struct Colours {
    std::vector<Rgb> combo;  // Combo1..ComboN, written back renumbered from 1
    std::optional<Rgb> slider_track_override;
    std::optional<Rgb> slider_border;
    ExtraFields extra;

    bool operator==(const Colours&) const = default;
};

enum class CurveType : char {
    bezier = 'B',
    catmull = 'C',
    linear = 'L',
    perfect = 'P',
};

struct SliderPoint {
    int x = 0;
    int y = 0;

    auto operator<=>(const SliderPoint&) const = default;
};

struct EdgeSet {
    SampleSet normal = SampleSet::none;
    SampleSet addition = SampleSet::none;

    auto operator<=>(const EdgeSet&) const = default;
};

struct HitSample {
    SampleSet normal_set = SampleSet::none;
    SampleSet addition_set = SampleSet::none;
    int index = 0;
    int volume = 0;
    std::string filename;

    auto operator<=>(const HitSample&) const = default;
};

struct Circle {
    bool operator==(const Circle&) const = default;
};

struct Slider {
    CurveType curve_type = CurveType::bezier;
    std::vector<SliderPoint> curve_points;  // control points after the head position
    int slides = 1;
    double length = 0.0;
    std::vector<HitSound> edge_sounds;      // empty means "all default"
    std::vector<EdgeSet> edge_sets;

    bool operator==(const Slider&) const = default;
};

struct Spinner {
    int end_time = 0;

    auto operator<=>(const Spinner&) const = default;
};

struct Hold {
    int end_time = 0;

    auto operator<=>(const Hold&) const = default;
};

struct HitObject {
    int x = 0;
    int y = 0;
    int time = 0;
    bool new_combo = false;
    std::uint8_t combo_skip = 0;  // bits 4-6 of the type field
    HitSound hit_sound = HitSound::none;
    HitSample sample;
    std::variant<Circle, Slider, Spinner, Hold> kind;

    bool operator==(const HitObject&) const = default;
};

// Sections this library does not model ([Foo] with unknown Foo), kept verbatim.
struct RawSection {
    std::string name;
    std::vector<std::string> lines;

    bool operator==(const RawSection&) const = default;
};

struct Beatmap {
    int format_version = 14;
    General general;
    std::optional<Editor> editor;
    Metadata metadata;
    Difficulty difficulty;
    std::vector<Event> events;
    std::vector<TimingPoint> timing_points;
    std::optional<Colours> colours;
    std::vector<HitObject> hit_objects;
    std::vector<RawSection> unknown_sections;

    bool operator==(const Beatmap&) const = default;
};

[[nodiscard]] std::expected<Beatmap, ParseError> parse(std::string_view text);
[[nodiscard]] std::expected<Beatmap, ParseError> parse(std::span<const std::byte> data);
[[nodiscard]] std::expected<Beatmap, ParseError> parse_file(const std::filesystem::path& path);

[[nodiscard]] std::expected<std::string, WriteError> write(const Beatmap& map);
[[nodiscard]] std::expected<void, WriteError> write_file(const Beatmap& map,
                                                         const std::filesystem::path& path);

}  // namespace osu::beatmap
