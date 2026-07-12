#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "osupp/beatmap.hpp"

namespace osu::beatmap {

namespace {

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

// Integral doubles are written without a decimal point, matching stable.
[[nodiscard]] std::string fmt_num(double value) {
    if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1e15) {
        return std::format("{}", static_cast<long long>(value));
    }
    return std::format("{}", value);
}

[[nodiscard]] std::string_view sample_set_name(SampleSet set) {
    switch (set) {
        case SampleSet::normal: return "Normal";
        case SampleSet::soft: return "Soft";
        case SampleSet::drum: return "Drum";
        case SampleSet::none: break;
    }
    return "None";
}

[[nodiscard]] std::string sample_str(const HitSample& sample) {
    return std::format("{}:{}:{}:{}:{}", std::to_underlying(sample.normal_set),
                       std::to_underlying(sample.addition_set), sample.index, sample.volume,
                       sample.filename);
}

struct Out {
    std::string text;

    void line(std::string_view s) {
        text += s;
        text += "\r\n";
    }

    template <typename... Args>
    void linef(std::format_string<Args...> fmt, Args&&... args) {
        std::format_to(std::back_inserter(text), fmt, std::forward<Args>(args)...);
        text += "\r\n";
    }

    void blank() { text += "\r\n"; }
};

void write_general(Out& out, const General& g) {
    out.line("[General]");
    const auto kv = [&](std::string_view key, std::string_view value) {
        out.linef("{}: {}", key, value);
    };
    if (g.audio_filename) kv("AudioFilename", *g.audio_filename);
    if (g.audio_lead_in) kv("AudioLeadIn", std::format("{}", *g.audio_lead_in));
    if (g.preview_time) kv("PreviewTime", std::format("{}", *g.preview_time));
    if (g.countdown) kv("Countdown", std::format("{}", *g.countdown));
    if (g.sample_set) kv("SampleSet", sample_set_name(*g.sample_set));
    if (g.stack_leniency) kv("StackLeniency", fmt_num(*g.stack_leniency));
    if (g.mode) kv("Mode", std::format("{}", std::to_underlying(*g.mode)));
    if (g.letterbox_in_breaks) kv("LetterboxInBreaks", *g.letterbox_in_breaks ? "1" : "0");
    if (g.use_skin_sprites) kv("UseSkinSprites", *g.use_skin_sprites ? "1" : "0");
    if (g.overlay_position) kv("OverlayPosition", *g.overlay_position);
    if (g.skin_preference) kv("SkinPreference", *g.skin_preference);
    if (g.epilepsy_warning) kv("EpilepsyWarning", *g.epilepsy_warning ? "1" : "0");
    if (g.countdown_offset) kv("CountdownOffset", std::format("{}", *g.countdown_offset));
    if (g.special_style) kv("SpecialStyle", *g.special_style ? "1" : "0");
    if (g.widescreen_storyboard) kv("WidescreenStoryboard", *g.widescreen_storyboard ? "1" : "0");
    if (g.samples_match_playback_rate) {
        kv("SamplesMatchPlaybackRate", *g.samples_match_playback_rate ? "1" : "0");
    }
    for (const auto& [key, value] : g.extra) kv(key, value);
}

void write_editor(Out& out, const Editor& e) {
    out.line("[Editor]");
    if (e.bookmarks) {
        std::string joined;
        for (const int bookmark : *e.bookmarks) {
            if (!joined.empty()) joined += ',';
            joined += std::format("{}", bookmark);
        }
        out.linef("Bookmarks: {}", joined);
    }
    if (e.distance_spacing) out.linef("DistanceSpacing: {}", fmt_num(*e.distance_spacing));
    if (e.beat_divisor) out.linef("BeatDivisor: {}", *e.beat_divisor);
    if (e.grid_size) out.linef("GridSize: {}", *e.grid_size);
    if (e.timeline_zoom) out.linef("TimelineZoom: {}", fmt_num(*e.timeline_zoom));
    for (const auto& [key, value] : e.extra) out.linef("{}: {}", key, value);
}

void write_metadata(Out& out, const Metadata& m) {
    out.line("[Metadata]");
    const auto kv = [&](std::string_view key, const std::optional<std::string>& value) {
        if (value) out.linef("{}:{}", key, *value);
    };
    kv("Title", m.title);
    kv("TitleUnicode", m.title_unicode);
    kv("Artist", m.artist);
    kv("ArtistUnicode", m.artist_unicode);
    kv("Creator", m.creator);
    kv("Version", m.version);
    kv("Source", m.source);
    if (m.tags) {
        std::string joined;
        for (const auto& tag : *m.tags) {
            if (!joined.empty()) joined += ' ';
            joined += tag;
        }
        out.linef("Tags:{}", joined);
    }
    if (m.beatmap_id) out.linef("BeatmapID:{}", *m.beatmap_id);
    if (m.beatmap_set_id) out.linef("BeatmapSetID:{}", *m.beatmap_set_id);
    for (const auto& [key, value] : m.extra) out.linef("{}:{}", key, value);
}

void write_difficulty(Out& out, const Difficulty& d) {
    out.line("[Difficulty]");
    const auto kv = [&](std::string_view key, const std::optional<double>& value) {
        if (value) out.linef("{}:{}", key, fmt_num(*value));
    };
    kv("HPDrainRate", d.hp_drain_rate);
    kv("CircleSize", d.circle_size);
    kv("OverallDifficulty", d.overall_difficulty);
    kv("ApproachRate", d.approach_rate);
    kv("SliderMultiplier", d.slider_multiplier);
    kv("SliderTickRate", d.slider_tick_rate);
    for (const auto& [key, value] : d.extra) out.linef("{}:{}", key, value);
}

void write_events(Out& out, const std::vector<Event>& events) {
    out.line("[Events]");
    for (const auto& event : events) {
        std::visit(Overloaded{
                       [&](const BackgroundEvent& e) {
                           out.linef("0,{},\"{}\",{},{}", e.start_time, e.filename, e.x_offset,
                                     e.y_offset);
                       },
                       [&](const VideoEvent& e) {
                           if (e.x_offset != 0 || e.y_offset != 0) {
                               out.linef("Video,{},\"{}\",{},{}", e.start_time, e.filename,
                                         e.x_offset, e.y_offset);
                           } else {
                               out.linef("Video,{},\"{}\"", e.start_time, e.filename);
                           }
                       },
                       [&](const BreakEvent& e) {
                           out.linef("2,{},{}", e.start_time, e.end_time);
                       },
                       [&](const RawEvent& e) { out.line(e.line); },
                   },
                   event);
    }
}

void write_timing_points(Out& out, const std::vector<TimingPoint>& points) {
    out.line("[TimingPoints]");
    for (const auto& tp : points) {
        out.linef("{},{},{},{},{},{},{},{}", fmt_num(tp.time), fmt_num(tp.beat_length), tp.meter,
                  std::to_underlying(tp.sample_set), tp.sample_index, tp.volume,
                  tp.uninherited ? 1 : 0, std::to_underlying(tp.effects));
    }
}

void write_colours(Out& out, const Colours& c) {
    out.line("[Colours]");
    const auto rgb = [](Rgb v) { return std::format("{},{},{}", v.r, v.g, v.b); };
    for (std::size_t i = 0; i < c.combo.size(); ++i) {
        out.linef("Combo{} : {}", i + 1, rgb(c.combo[i]));
    }
    if (c.slider_track_override) {
        out.linef("SliderTrackOverride : {}", rgb(*c.slider_track_override));
    }
    if (c.slider_border) out.linef("SliderBorder : {}", rgb(*c.slider_border));
    for (const auto& [key, value] : c.extra) out.linef("{} : {}", key, value);
}

void write_hit_objects(Out& out, const std::vector<HitObject>& objects) {
    out.line("[HitObjects]");
    for (const auto& o : objects) {
        int type = o.new_combo ? 0b0100 : 0;
        type |= (o.combo_skip & 0b0111) << 4;
        std::visit(Overloaded{
                       [&](const Circle&) { type |= 0b0001; },
                       [&](const Slider&) { type |= 0b0010; },
                       [&](const Spinner&) { type |= 0b1000; },
                       [&](const Hold&) { type |= 0b1000'0000; },
                   },
                   o.kind);
        auto base = std::format("{},{},{},{},{}", o.x, o.y, o.time, type,
                                std::to_underlying(o.hit_sound));

        std::visit(
            Overloaded{
                [&](const Circle&) { out.linef("{},{}", base, sample_str(o.sample)); },
                [&](const Slider& s) {
                    std::string curve{static_cast<char>(s.curve_type)};
                    for (const auto& p : s.curve_points) {
                        curve += std::format("|{}:{}", p.x, p.y);
                    }
                    base += std::format(",{},{},{}", curve, s.slides, fmt_num(s.length));
                    const bool default_tail =
                        s.edge_sounds.empty() && s.edge_sets.empty() && o.sample == HitSample{};
                    if (default_tail) {
                        out.line(base);
                        return;
                    }
                    // Edge lists must be present once anything after them is written;
                    // fill missing ones with per-edge defaults (slides + 1 entries).
                    std::string sounds;
                    if (s.edge_sounds.empty()) {
                        for (int i = 0; i <= s.slides; ++i) sounds += i == 0 ? "0" : "|0";
                    } else {
                        for (std::size_t i = 0; i < s.edge_sounds.size(); ++i) {
                            if (i > 0) sounds += '|';
                            sounds += std::format("{}", std::to_underlying(s.edge_sounds[i]));
                        }
                    }
                    std::string sets;
                    if (s.edge_sets.empty()) {
                        for (int i = 0; i <= s.slides; ++i) sets += i == 0 ? "0:0" : "|0:0";
                    } else {
                        for (std::size_t i = 0; i < s.edge_sets.size(); ++i) {
                            if (i > 0) sets += '|';
                            sets += std::format("{}:{}", std::to_underlying(s.edge_sets[i].normal),
                                                std::to_underlying(s.edge_sets[i].addition));
                        }
                    }
                    out.linef("{},{},{},{}", base, sounds, sets, sample_str(o.sample));
                },
                [&](const Spinner& s) {
                    out.linef("{},{},{}", base, s.end_time, sample_str(o.sample));
                },
                [&](const Hold& h) {
                    out.linef("{},{}:{}", base, h.end_time, sample_str(o.sample));
                },
            },
            o.kind);
    }
}

}  // namespace

std::expected<std::string, WriteError> write(const Beatmap& map) {
    Out out;
    out.linef("osu file format v{}", map.format_version);
    out.blank();

    write_general(out, map.general);
    out.blank();
    if (map.editor) {
        write_editor(out, *map.editor);
        out.blank();
    }
    write_metadata(out, map.metadata);
    out.blank();
    write_difficulty(out, map.difficulty);
    out.blank();
    write_events(out, map.events);
    out.blank();
    write_timing_points(out, map.timing_points);
    out.blank();
    if (map.colours) {
        write_colours(out, *map.colours);
        out.blank();
    }
    write_hit_objects(out, map.hit_objects);

    for (const auto& section : map.unknown_sections) {
        out.blank();
        out.linef("[{}]", section.name);
        for (const auto& line : section.lines) out.line(line);
    }
    return std::move(out.text);
}

std::expected<void, WriteError> write_file(const Beatmap& map,
                                           const std::filesystem::path& path) {
    auto text = write(map);
    if (!text) return std::unexpected(text.error());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(WriteError{WriteErrorCode::io_failure,
                                          std::format("cannot open '{}'", path.string())});
    }
    file.write(text->data(), static_cast<std::streamsize>(text->size()));
    file.close();
    if (!file) {
        return std::unexpected(WriteError{WriteErrorCode::io_failure,
                                          std::format("write failure on '{}'", path.string())});
    }
    return {};
}

}  // namespace osu::beatmap
