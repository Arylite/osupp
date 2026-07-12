#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "detail/string_utils.hpp"
#include "osupp/beatmap.hpp"

namespace osu::beatmap {

namespace {

using detail::parse_num;
using detail::split;
using detail::split_n;
using detail::trim;

[[nodiscard]] std::unexpected<ParseError> fail(ParseErrorCode code, std::size_t line,
                                               std::string message) {
    return std::unexpected(ParseError{code, std::move(message), line, 0});
}

template <detail::ParsableNumber T>
[[nodiscard]] std::expected<T, ParseError> num(std::string_view text, std::size_t line,
                                               std::string_view what) {
    if (const auto value = parse_num<T>(text)) return *value;
    return fail(ParseErrorCode::bad_number, line,
                std::format("invalid number for {}: '{}'", what, text));
}

[[nodiscard]] std::expected<bool, ParseError> flag01(std::string_view text, std::size_t line,
                                                     std::string_view what) {
    const auto value = num<int>(text, line, what);
    if (!value) return std::unexpected(value.error());
    return *value != 0;
}

[[nodiscard]] std::expected<SampleSet, ParseError> sample_set_from_int(int value,
                                                                       std::size_t line) {
    if (value < 0 || value > 3) {
        return fail(ParseErrorCode::invalid_value, line,
                    std::format("sample set out of range: {}", value));
    }
    return static_cast<SampleSet>(value);
}

[[nodiscard]] std::optional<SampleSet> sample_set_from_name(std::string_view name) {
    if (name == "None") return SampleSet::none;
    if (name == "Normal") return SampleSet::normal;
    if (name == "Soft") return SampleSet::soft;
    if (name == "Drum") return SampleSet::drum;
    return std::nullopt;
}

[[nodiscard]] std::expected<void, ParseError> apply_general(General& section, std::string_view key,
                                                            std::string_view value,
                                                            std::size_t line) {
    const auto set_int = [&](std::optional<int>& field) -> std::expected<void, ParseError> {
        const auto v = num<int>(value, line, key);
        if (!v) return std::unexpected(v.error());
        field = *v;
        return {};
    };
    const auto set_double = [&](std::optional<double>& field) -> std::expected<void, ParseError> {
        const auto v = num<double>(value, line, key);
        if (!v) return std::unexpected(v.error());
        field = *v;
        return {};
    };
    const auto set_bool = [&](std::optional<bool>& field) -> std::expected<void, ParseError> {
        const auto v = flag01(value, line, key);
        if (!v) return std::unexpected(v.error());
        field = *v;
        return {};
    };

    if (key == "AudioFilename") {
        section.audio_filename = std::string(value);
    } else if (key == "AudioLeadIn") {
        return set_int(section.audio_lead_in);
    } else if (key == "PreviewTime") {
        return set_int(section.preview_time);
    } else if (key == "Countdown") {
        return set_int(section.countdown);
    } else if (key == "SampleSet") {
        if (const auto set = sample_set_from_name(value)) {
            section.sample_set = *set;
        } else {
            // Legacy values such as "All" survive round-trips through extra.
            section.extra.emplace_back(std::string(key), std::string(value));
        }
    } else if (key == "StackLeniency") {
        return set_double(section.stack_leniency);
    } else if (key == "Mode") {
        const auto v = num<int>(value, line, key);
        if (!v) return std::unexpected(v.error());
        if (*v < 0 || *v > 3) {
            return fail(ParseErrorCode::invalid_value, line,
                        std::format("game mode out of range: {}", *v));
        }
        section.mode = static_cast<Mode>(*v);
    } else if (key == "LetterboxInBreaks") {
        return set_bool(section.letterbox_in_breaks);
    } else if (key == "UseSkinSprites") {
        return set_bool(section.use_skin_sprites);
    } else if (key == "OverlayPosition") {
        section.overlay_position = std::string(value);
    } else if (key == "SkinPreference") {
        section.skin_preference = std::string(value);
    } else if (key == "EpilepsyWarning") {
        return set_bool(section.epilepsy_warning);
    } else if (key == "CountdownOffset") {
        return set_int(section.countdown_offset);
    } else if (key == "SpecialStyle") {
        return set_bool(section.special_style);
    } else if (key == "WidescreenStoryboard") {
        return set_bool(section.widescreen_storyboard);
    } else if (key == "SamplesMatchPlaybackRate") {
        return set_bool(section.samples_match_playback_rate);
    } else {
        section.extra.emplace_back(std::string(key), std::string(value));
    }
    return {};
}

[[nodiscard]] std::expected<void, ParseError> apply_editor(Editor& section, std::string_view key,
                                                           std::string_view value,
                                                           std::size_t line) {
    if (key == "Bookmarks") {
        std::vector<int> bookmarks;
        for (const auto token : split(value, ',')) {
            if (trim(token).empty()) continue;
            const auto v = num<int>(token, line, key);
            if (!v) return std::unexpected(v.error());
            bookmarks.push_back(*v);
        }
        section.bookmarks = std::move(bookmarks);
        return {};
    }
    const auto set_double = [&](std::optional<double>& field) -> std::expected<void, ParseError> {
        const auto v = num<double>(value, line, key);
        if (!v) return std::unexpected(v.error());
        field = *v;
        return {};
    };
    const auto set_int = [&](std::optional<int>& field) -> std::expected<void, ParseError> {
        const auto v = num<int>(value, line, key);
        if (!v) return std::unexpected(v.error());
        field = *v;
        return {};
    };
    if (key == "DistanceSpacing") return set_double(section.distance_spacing);
    if (key == "BeatDivisor") return set_int(section.beat_divisor);
    if (key == "GridSize") return set_int(section.grid_size);
    if (key == "TimelineZoom") return set_double(section.timeline_zoom);
    section.extra.emplace_back(std::string(key), std::string(value));
    return {};
}

[[nodiscard]] std::expected<void, ParseError> apply_metadata(Metadata& section,
                                                             std::string_view key,
                                                             std::string_view value,
                                                             std::size_t line) {
    const auto set_str = [&](std::optional<std::string>& field) { field = std::string(value); };
    if (key == "Title") {
        set_str(section.title);
    } else if (key == "TitleUnicode") {
        set_str(section.title_unicode);
    } else if (key == "Artist") {
        set_str(section.artist);
    } else if (key == "ArtistUnicode") {
        set_str(section.artist_unicode);
    } else if (key == "Creator") {
        set_str(section.creator);
    } else if (key == "Version") {
        set_str(section.version);
    } else if (key == "Source") {
        set_str(section.source);
    } else if (key == "Tags") {
        std::vector<std::string> tags;
        for (const auto token : split(value, ' ')) {
            if (!token.empty()) tags.emplace_back(token);
        }
        section.tags = std::move(tags);
    } else if (key == "BeatmapID") {
        const auto v = num<std::int64_t>(value, line, key);
        if (!v) return std::unexpected(v.error());
        section.beatmap_id = *v;
    } else if (key == "BeatmapSetID") {
        const auto v = num<std::int64_t>(value, line, key);
        if (!v) return std::unexpected(v.error());
        section.beatmap_set_id = *v;
    } else {
        section.extra.emplace_back(std::string(key), std::string(value));
    }
    return {};
}

[[nodiscard]] std::expected<void, ParseError> apply_difficulty(Difficulty& section,
                                                               std::string_view key,
                                                               std::string_view value,
                                                               std::size_t line) {
    const auto set = [&](std::optional<double>& field) -> std::expected<void, ParseError> {
        const auto v = num<double>(value, line, key);
        if (!v) return std::unexpected(v.error());
        field = *v;
        return {};
    };
    if (key == "HPDrainRate") return set(section.hp_drain_rate);
    if (key == "CircleSize") return set(section.circle_size);
    if (key == "OverallDifficulty") return set(section.overall_difficulty);
    if (key == "ApproachRate") return set(section.approach_rate);
    if (key == "SliderMultiplier") return set(section.slider_multiplier);
    if (key == "SliderTickRate") return set(section.slider_tick_rate);
    section.extra.emplace_back(std::string(key), std::string(value));
    return {};
}

[[nodiscard]] std::optional<Rgb> parse_rgb(std::string_view value) {
    const auto parts = split(value, ',');
    if (parts.size() != 3) return std::nullopt;
    std::uint8_t components[3];
    for (int i = 0; i < 3; ++i) {
        const auto v = parse_num<int>(parts[i]);
        if (!v || *v < 0 || *v > 255) return std::nullopt;
        components[i] = static_cast<std::uint8_t>(*v);
    }
    return Rgb{components[0], components[1], components[2]};
}

void apply_colours(Colours& section, std::vector<std::pair<int, Rgb>>& combos,
                   std::string_view key, std::string_view value) {
    if (key.starts_with("Combo")) {
        const auto index = parse_num<int>(key.substr(5));
        const auto rgb = parse_rgb(value);
        if (index && rgb) {
            combos.emplace_back(*index, *rgb);
            return;
        }
    } else if (key == "SliderTrackOverride") {
        if (const auto rgb = parse_rgb(value)) {
            section.slider_track_override = *rgb;
            return;
        }
    } else if (key == "SliderBorder") {
        if (const auto rgb = parse_rgb(value)) {
            section.slider_border = *rgb;
            return;
        }
    }
    // Anything non-conforming (4-component colours, unknown keys) is preserved raw.
    section.extra.emplace_back(std::string(key), std::string(value));
}

struct QuotedField {
    std::string text;
    std::size_t next;
};

// Filenames are usually double-quoted and may contain commas, so splitting
// on ',' can cut them apart; rejoin until the closing quote.
[[nodiscard]] std::expected<QuotedField, ParseError> quoted_field(
    const std::vector<std::string_view>& parts, std::size_t index, std::size_t line) {
    if (index >= parts.size()) {
        return fail(ParseErrorCode::bad_field, line, "missing filename field in event");
    }
    const auto first = trim(parts[index]);
    if (!first.starts_with('"')) return QuotedField{std::string(first), index + 1};
    std::string joined(first);
    std::size_t i = index;
    while (joined.size() < 2 || !joined.ends_with('"')) {
        if (++i >= parts.size()) {
            return fail(ParseErrorCode::bad_field, line, "unterminated quoted filename");
        }
        joined += ',';
        joined += parts[i];
    }
    return QuotedField{joined.substr(1, joined.size() - 2), i + 1};
}

[[nodiscard]] std::expected<Event, ParseError> parse_event(std::string_view raw,
                                                           std::size_t line) {
    const auto trimmed = trim(raw);
    if (trimmed.starts_with("//")) return Event{RawEvent{std::string(raw)}};

    const auto parts = split(trimmed, ',');
    const auto head = trim(parts[0]);

    const auto parse_media = [&](auto event) -> std::expected<Event, ParseError> {
        if (parts.size() < 3) {
            return fail(ParseErrorCode::bad_field, line, "media event needs at least 3 fields");
        }
        const auto start = num<int>(parts[1], line, "event start time");
        if (!start) return std::unexpected(start.error());
        auto file = quoted_field(parts, 2, line);
        if (!file) return std::unexpected(file.error());
        event.start_time = *start;
        event.filename = std::move(file->text);
        if (file->next < parts.size()) {
            const auto x = num<int>(parts[file->next], line, "event x offset");
            if (!x) return std::unexpected(x.error());
            event.x_offset = *x;
        }
        if (file->next + 1 < parts.size()) {
            const auto y = num<int>(parts[file->next + 1], line, "event y offset");
            if (!y) return std::unexpected(y.error());
            event.y_offset = *y;
        }
        return Event{std::move(event)};
    };

    if (head == "0") return parse_media(BackgroundEvent{});
    if (head == "1" || head == "Video") return parse_media(VideoEvent{});
    if (head == "2" || head == "Break") {
        if (parts.size() < 3) {
            return fail(ParseErrorCode::bad_field, line, "break event needs 3 fields");
        }
        const auto start = num<int>(parts[1], line, "break start time");
        if (!start) return std::unexpected(start.error());
        const auto end = num<int>(parts[2], line, "break end time");
        if (!end) return std::unexpected(end.error());
        return Event{BreakEvent{*start, *end}};
    }
    // Storyboard objects, commands (indented lines) and unknown events stay verbatim.
    return Event{RawEvent{std::string(raw)}};
}

[[nodiscard]] std::expected<TimingPoint, ParseError> parse_timing_point(std::string_view text,
                                                                        std::size_t line) {
    const auto parts = split(text, ',');
    if (parts.size() < 2) {
        return fail(ParseErrorCode::bad_field, line,
                    "timing point needs at least time and beat length");
    }
    TimingPoint tp;
    const auto time = num<double>(parts[0], line, "timing point time");
    if (!time) return std::unexpected(time.error());
    tp.time = *time;
    const auto beat_length = num<double>(parts[1], line, "beat length");
    if (!beat_length) return std::unexpected(beat_length.error());
    tp.beat_length = *beat_length;
    if (parts.size() > 2) {
        const auto meter = num<int>(parts[2], line, "meter");
        if (!meter) return std::unexpected(meter.error());
        tp.meter = *meter;
    }
    if (parts.size() > 3) {
        const auto raw_set = num<int>(parts[3], line, "timing point sample set");
        if (!raw_set) return std::unexpected(raw_set.error());
        const auto set = sample_set_from_int(*raw_set, line);
        if (!set) return std::unexpected(set.error());
        tp.sample_set = *set;
    }
    if (parts.size() > 4) {
        const auto index = num<int>(parts[4], line, "sample index");
        if (!index) return std::unexpected(index.error());
        tp.sample_index = *index;
    }
    if (parts.size() > 5) {
        const auto volume = num<int>(parts[5], line, "volume");
        if (!volume) return std::unexpected(volume.error());
        tp.volume = *volume;
    }
    if (parts.size() > 6) {
        const auto uninherited = flag01(parts[6], line, "uninherited flag");
        if (!uninherited) return std::unexpected(uninherited.error());
        tp.uninherited = *uninherited;
    }
    if (parts.size() > 7) {
        const auto effects = num<int>(parts[7], line, "effects");
        if (!effects) return std::unexpected(effects.error());
        tp.effects = static_cast<TimingEffects>(*effects & 0xFF);
    }
    return tp;
}

[[nodiscard]] std::expected<HitSample, ParseError> parse_hit_sample(std::string_view text,
                                                                    std::size_t line) {
    HitSample sample;
    if (trim(text).empty()) return sample;
    // normalSet:additionSet:index:volume:filename - the filename may contain ':'.
    const auto parts = split_n(text, ':', 5);
    if (parts.size() < 2) {
        return fail(ParseErrorCode::bad_field, line,
                    std::format("malformed hit sample: '{}'", text));
    }
    const auto normal = num<int>(parts[0], line, "hit sample normal set");
    if (!normal) return std::unexpected(normal.error());
    const auto normal_set = sample_set_from_int(*normal, line);
    if (!normal_set) return std::unexpected(normal_set.error());
    sample.normal_set = *normal_set;
    const auto addition = num<int>(parts[1], line, "hit sample addition set");
    if (!addition) return std::unexpected(addition.error());
    const auto addition_set = sample_set_from_int(*addition, line);
    if (!addition_set) return std::unexpected(addition_set.error());
    sample.addition_set = *addition_set;
    if (parts.size() > 2) {
        const auto index = num<int>(parts[2], line, "hit sample index");
        if (!index) return std::unexpected(index.error());
        sample.index = *index;
    }
    if (parts.size() > 3) {
        const auto volume = num<int>(parts[3], line, "hit sample volume");
        if (!volume) return std::unexpected(volume.error());
        sample.volume = *volume;
    }
    if (parts.size() > 4) sample.filename = std::string(parts[4]);
    return sample;
}

[[nodiscard]] std::expected<Slider, ParseError> parse_slider_params(
    const std::vector<std::string_view>& parts, std::size_t line) {
    if (parts.size() < 8) {
        return fail(ParseErrorCode::bad_field, line, "slider needs curve, slides and length");
    }
    Slider slider;
    const auto curve_parts = split(parts[5], '|');
    const auto curve_token = trim(curve_parts[0]);
    if (curve_token.size() != 1 || std::string_view("BCLP").find(curve_token[0]) ==
                                       std::string_view::npos) {
        return fail(ParseErrorCode::invalid_value, line,
                    std::format("unknown slider curve type: '{}'", curve_token));
    }
    slider.curve_type = static_cast<CurveType>(curve_token[0]);
    for (std::size_t i = 1; i < curve_parts.size(); ++i) {
        const auto point = split(curve_parts[i], ':');
        if (point.size() != 2) {
            return fail(ParseErrorCode::bad_field, line,
                        std::format("malformed slider point: '{}'", curve_parts[i]));
        }
        const auto x = num<int>(point[0], line, "slider point x");
        if (!x) return std::unexpected(x.error());
        const auto y = num<int>(point[1], line, "slider point y");
        if (!y) return std::unexpected(y.error());
        slider.curve_points.push_back(SliderPoint{*x, *y});
    }
    const auto slides = num<int>(parts[6], line, "slider slides");
    if (!slides) return std::unexpected(slides.error());
    slider.slides = *slides;
    const auto length = num<double>(parts[7], line, "slider length");
    if (!length) return std::unexpected(length.error());
    slider.length = *length;
    if (parts.size() > 8 && !trim(parts[8]).empty()) {
        for (const auto token : split(parts[8], '|')) {
            const auto sound = num<int>(token, line, "slider edge sound");
            if (!sound) return std::unexpected(sound.error());
            slider.edge_sounds.push_back(static_cast<HitSound>(*sound & 0xFF));
        }
    }
    if (parts.size() > 9 && !trim(parts[9]).empty()) {
        for (const auto token : split(parts[9], '|')) {
            const auto pair = split(token, ':');
            if (pair.size() != 2) {
                return fail(ParseErrorCode::bad_field, line,
                            std::format("malformed slider edge set: '{}'", token));
            }
            EdgeSet edge;
            for (int i = 0; i < 2; ++i) {
                const auto raw = num<int>(pair[i], line, "slider edge set");
                if (!raw) return std::unexpected(raw.error());
                const auto set = sample_set_from_int(*raw, line);
                if (!set) return std::unexpected(set.error());
                (i == 0 ? edge.normal : edge.addition) = *set;
            }
            slider.edge_sets.push_back(edge);
        }
    }
    return slider;
}

[[nodiscard]] std::expected<HitObject, ParseError> parse_hit_object(std::string_view text,
                                                                    std::size_t line) {
    const auto parts = split(text, ',');
    if (parts.size() < 5) {
        return fail(ParseErrorCode::bad_field, line, "hit object needs at least 5 fields");
    }
    HitObject object;
    const auto x = num<int>(parts[0], line, "hit object x");
    if (!x) return std::unexpected(x.error());
    object.x = *x;
    const auto y = num<int>(parts[1], line, "hit object y");
    if (!y) return std::unexpected(y.error());
    object.y = *y;
    const auto time = num<int>(parts[2], line, "hit object time");
    if (!time) return std::unexpected(time.error());
    object.time = *time;
    const auto type = num<int>(parts[3], line, "hit object type");
    if (!type) return std::unexpected(type.error());
    const auto sound = num<int>(parts[4], line, "hit object hitsound");
    if (!sound) return std::unexpected(sound.error());
    object.hit_sound = static_cast<HitSound>(*sound & 0xFF);

    object.new_combo = (*type & 0b0100) != 0;
    object.combo_skip = static_cast<std::uint8_t>((*type >> 4) & 0b0111);
    const bool is_circle = (*type & 0b0001) != 0;
    const bool is_slider = (*type & 0b0010) != 0;
    const bool is_spinner = (*type & 0b1000) != 0;
    const bool is_hold = (*type & 0b1000'0000) != 0;
    if (int(is_circle) + int(is_slider) + int(is_spinner) + int(is_hold) != 1) {
        return fail(ParseErrorCode::invalid_value, line,
                    std::format("hit object type {} does not select exactly one kind", *type));
    }

    const auto sample_at = [&](std::size_t index) -> std::expected<void, ParseError> {
        if (parts.size() > index) {
            const auto sample = parse_hit_sample(parts[index], line);
            if (!sample) return std::unexpected(sample.error());
            object.sample = *sample;
        }
        return {};
    };

    if (is_circle) {
        object.kind = Circle{};
        if (const auto s = sample_at(5); !s) return std::unexpected(s.error());
    } else if (is_slider) {
        auto slider = parse_slider_params(parts, line);
        if (!slider) return std::unexpected(slider.error());
        object.kind = std::move(*slider);
        if (const auto s = sample_at(10); !s) return std::unexpected(s.error());
    } else if (is_spinner) {
        if (parts.size() < 6) {
            return fail(ParseErrorCode::bad_field, line, "spinner needs an end time");
        }
        const auto end = num<int>(parts[5], line, "spinner end time");
        if (!end) return std::unexpected(end.error());
        object.kind = Spinner{*end};
        if (const auto s = sample_at(6); !s) return std::unexpected(s.error());
    } else {
        if (parts.size() < 6) {
            return fail(ParseErrorCode::bad_field, line, "hold needs an end time");
        }
        // Hold format glues the sample onto the end time: endTime:hitSample
        const auto hold_parts = split_n(parts[5], ':', 2);
        const auto end = num<int>(hold_parts[0], line, "hold end time");
        if (!end) return std::unexpected(end.error());
        object.kind = Hold{*end};
        if (hold_parts.size() > 1) {
            const auto sample = parse_hit_sample(hold_parts[1], line);
            if (!sample) return std::unexpected(sample.error());
            object.sample = *sample;
        }
    }
    return object;
}

enum class Section {
    none,
    general,
    editor,
    metadata,
    difficulty,
    events,
    timing_points,
    colours,
    hit_objects,
    unknown,
};

[[nodiscard]] Section section_from_name(std::string_view name) {
    if (name == "General") return Section::general;
    if (name == "Editor") return Section::editor;
    if (name == "Metadata") return Section::metadata;
    if (name == "Difficulty") return Section::difficulty;
    if (name == "Events") return Section::events;
    if (name == "TimingPoints") return Section::timing_points;
    if (name == "Colours") return Section::colours;
    if (name == "HitObjects") return Section::hit_objects;
    return Section::unknown;
}

}  // namespace

std::expected<Beatmap, ParseError> parse(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);

    Beatmap map;
    bool header_done = false;
    Section section = Section::none;
    std::vector<std::pair<int, Rgb>> combo_colours;

    std::size_t line_no = 0;
    for (const auto part : text | std::views::split('\n')) {
        ++line_no;
        std::string_view raw{part};
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        const auto line = trim(raw);
        if (line.empty()) continue;

        if (!header_done) {
            constexpr std::string_view prefix = "osu file format v";
            if (!line.starts_with(prefix)) {
                return fail(ParseErrorCode::bad_header, line_no,
                            "expected 'osu file format vN' header");
            }
            const auto version = num<int>(line.substr(prefix.size()), line_no, "format version");
            if (!version) return std::unexpected(version.error());
            map.format_version = *version;
            header_done = true;
            continue;
        }

        if (line.starts_with('[') && line.ends_with(']')) {
            const auto name = trim(line.substr(1, line.size() - 2));
            section = section_from_name(name);
            if (section == Section::editor && !map.editor) map.editor.emplace();
            if (section == Section::colours && !map.colours) map.colours.emplace();
            if (section == Section::unknown) {
                map.unknown_sections.push_back(RawSection{std::string(name), {}});
            }
            continue;
        }

        const auto key_value = [&]() -> std::expected<std::pair<std::string_view,
                                                                std::string_view>, ParseError> {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos) {
                return fail(ParseErrorCode::bad_field, line_no, "expected 'Key: Value'");
            }
            return std::pair{trim(line.substr(0, colon)), trim(line.substr(colon + 1))};
        };

        switch (section) {
            case Section::none:
                return fail(ParseErrorCode::bad_field, line_no, "content outside of any section");
            case Section::general: {
                const auto kv = key_value();
                if (!kv) return std::unexpected(kv.error());
                const auto applied = apply_general(map.general, kv->first, kv->second, line_no);
                if (!applied) return std::unexpected(applied.error());
                break;
            }
            case Section::editor: {
                const auto kv = key_value();
                if (!kv) return std::unexpected(kv.error());
                const auto applied = apply_editor(*map.editor, kv->first, kv->second, line_no);
                if (!applied) return std::unexpected(applied.error());
                break;
            }
            case Section::metadata: {
                const auto kv = key_value();
                if (!kv) return std::unexpected(kv.error());
                const auto applied = apply_metadata(map.metadata, kv->first, kv->second, line_no);
                if (!applied) return std::unexpected(applied.error());
                break;
            }
            case Section::difficulty: {
                const auto kv = key_value();
                if (!kv) return std::unexpected(kv.error());
                const auto applied =
                    apply_difficulty(map.difficulty, kv->first, kv->second, line_no);
                if (!applied) return std::unexpected(applied.error());
                break;
            }
            case Section::colours: {
                const auto kv = key_value();
                if (!kv) return std::unexpected(kv.error());
                apply_colours(*map.colours, combo_colours, kv->first, kv->second);
                break;
            }
            case Section::events: {
                // Pass the untrimmed line: storyboard command indentation matters.
                auto event = parse_event(raw, line_no);
                if (!event) return std::unexpected(event.error());
                map.events.push_back(std::move(*event));
                break;
            }
            case Section::timing_points: {
                const auto tp = parse_timing_point(line, line_no);
                if (!tp) return std::unexpected(tp.error());
                map.timing_points.push_back(*tp);
                break;
            }
            case Section::hit_objects: {
                auto object = parse_hit_object(line, line_no);
                if (!object) return std::unexpected(object.error());
                map.hit_objects.push_back(std::move(*object));
                break;
            }
            case Section::unknown:
                map.unknown_sections.back().lines.emplace_back(raw);
                break;
        }
    }

    if (!header_done) {
        return fail(ParseErrorCode::bad_header, 0, "empty input, expected osu file header");
    }

    if (!combo_colours.empty()) {
        std::ranges::stable_sort(combo_colours, {}, &std::pair<int, Rgb>::first);
        for (const auto& [index, rgb] : combo_colours) map.colours->combo.push_back(rgb);
    }
    return map;
}

std::expected<Beatmap, ParseError> parse(std::span<const std::byte> data) {
    return parse(std::string_view{reinterpret_cast<const char*>(data.data()), data.size()});
}

std::expected<Beatmap, ParseError> parse_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(ParseError{ParseErrorCode::io_failure,
                                          std::format("cannot open '{}'", path.string())});
    }
    const std::string text{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};
    if (file.bad()) {
        return std::unexpected(ParseError{ParseErrorCode::io_failure,
                                          std::format("read failure on '{}'", path.string())});
    }
    return parse(text);
}

}  // namespace osu::beatmap
