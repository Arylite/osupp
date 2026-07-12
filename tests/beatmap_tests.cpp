#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

#include <osupp/osupp.hpp>

namespace {

const std::filesystem::path data_dir{OSUPP_TEST_DATA_DIR};

osu::beatmap::Beatmap parse_or_fail(std::string_view text) {
    auto result = osu::beatmap::parse(text);
    REQUIRE(result.has_value());
    return std::move(*result);
}

}  // namespace

TEST_CASE("parse example beatmap") {
    const auto result = osu::beatmap::parse_file(data_dir / "example.osu");
    REQUIRE(result.has_value());
    const auto& map = *result;

    CHECK(map.format_version == 14);

    CHECK(map.general.audio_filename == "audio.mp3");
    CHECK(map.general.preview_time == 1200);
    CHECK(map.general.stack_leniency == 0.7);
    CHECK(map.general.mode == osu::Mode::osu);
    CHECK(map.general.sample_set == osu::SampleSet::normal);
    CHECK(map.general.widescreen_storyboard == true);

    REQUIRE(map.editor.has_value());
    CHECK(map.editor->bookmarks == std::vector{1000, 2000});
    CHECK(map.editor->distance_spacing == 1.2);

    CHECK(map.metadata.title == "Example Song");
    CHECK(map.metadata.artist == "Example Artist");
    CHECK(map.metadata.source == "");
    CHECK(map.metadata.tags == std::vector<std::string>{"example", "test", "beatmap"});
    CHECK(map.metadata.beatmap_id == 123456);

    CHECK(map.difficulty.circle_size == 4.0);
    CHECK(map.difficulty.approach_rate == 9.0);
    CHECK(map.difficulty.slider_multiplier == 1.6);

    REQUIRE(map.events.size() == 5);
    const auto& background = std::get<osu::beatmap::BackgroundEvent>(map.events[1]);
    CHECK(background.filename == "bg.jpg");
    const auto& break_event = std::get<osu::beatmap::BreakEvent>(map.events[3]);
    CHECK(break_event.start_time == 24000);
    CHECK(break_event.end_time == 28000);

    REQUIRE(map.timing_points.size() == 2);
    CHECK(map.timing_points[0].time == 1000.0);
    CHECK(map.timing_points[0].beat_length == 342.857142857143);
    CHECK(map.timing_points[0].uninherited);
    CHECK(map.timing_points[0].sample_set == osu::SampleSet::normal);
    CHECK(map.timing_points[1].beat_length == -50.0);
    CHECK_FALSE(map.timing_points[1].uninherited);
    CHECK(osu::has_flag(map.timing_points[1].effects, osu::TimingEffects::kiai));

    REQUIRE(map.colours.has_value());
    REQUIRE(map.colours->combo.size() == 2);
    CHECK(map.colours->combo[0] == osu::Rgb{255, 128, 64});

    REQUIRE(map.hit_objects.size() == 3);
    CHECK(std::holds_alternative<osu::beatmap::Circle>(map.hit_objects[0].kind));
    CHECK(map.hit_objects[0].new_combo);

    const auto& slider = std::get<osu::beatmap::Slider>(map.hit_objects[1].kind);
    CHECK(slider.curve_type == osu::beatmap::CurveType::perfect);
    CHECK(slider.curve_points ==
          std::vector{osu::beatmap::SliderPoint{150, 150}, osu::beatmap::SliderPoint{200, 100}});
    CHECK(slider.slides == 1);
    CHECK(slider.length == 140.0);
    CHECK(slider.edge_sounds == std::vector{osu::HitSound::whistle, osu::HitSound::none});
    CHECK(slider.edge_sets ==
          std::vector{osu::beatmap::EdgeSet{osu::SampleSet::normal, osu::SampleSet::none},
                      osu::beatmap::EdgeSet{osu::SampleSet::none, osu::SampleSet::soft}});

    const auto& spinner = std::get<osu::beatmap::Spinner>(map.hit_objects[2].kind);
    CHECK(spinner.end_time == 4500);
    CHECK(map.hit_objects[2].hit_sound == osu::HitSound::finish);
}

TEST_CASE("beatmap round-trips through write and parse") {
    const auto original = osu::beatmap::parse_file(data_dir / "example.osu");
    REQUIRE(original.has_value());

    const auto first_write = osu::beatmap::write(*original);
    REQUIRE(first_write.has_value());

    const auto reparsed = osu::beatmap::parse(*first_write);
    REQUIRE(reparsed.has_value());
    CHECK(*reparsed == *original);

    const auto second_write = osu::beatmap::write(*reparsed);
    REQUIRE(second_write.has_value());
    CHECK(*first_write == *second_write);
}

TEST_CASE("beatmap file round-trips through the filesystem") {
    const auto original = osu::beatmap::parse_file(data_dir / "example.osu");
    REQUIRE(original.has_value());

    const auto temp = std::filesystem::temp_directory_path() / "osupp_roundtrip.osu";
    REQUIRE(osu::beatmap::write_file(*original, temp).has_value());
    const auto reloaded = osu::beatmap::parse_file(temp);
    std::filesystem::remove(temp);
    REQUIRE(reloaded.has_value());
    CHECK(*reloaded == *original);
}

TEST_CASE("mania hold objects carry end time and sample") {
    const auto map = parse_or_fail(
        "osu file format v14\n"
        "\n"
        "[General]\n"
        "Mode: 3\n"
        "\n"
        "[HitObjects]\n"
        "64,192,1000,128,0,2000:1:2:3:70:hit.wav\n");
    REQUIRE(map.hit_objects.size() == 1);
    const auto& hold = std::get<osu::beatmap::Hold>(map.hit_objects[0].kind);
    CHECK(hold.end_time == 2000);
    const auto& sample = map.hit_objects[0].sample;
    CHECK(sample.normal_set == osu::SampleSet::normal);
    CHECK(sample.addition_set == osu::SampleSet::soft);
    CHECK(sample.index == 3);
    CHECK(sample.volume == 70);
    CHECK(sample.filename == "hit.wav");
}

TEST_CASE("unknown keys and sections are preserved") {
    const auto map = parse_or_fail(
        "osu file format v14\n"
        "\n"
        "[General]\n"
        "AudioFilename: song.mp3\n"
        "MyCustomKey: 12\n"
        "\n"
        "[FutureSection]\n"
        "anything,goes,here\n");
    REQUIRE(map.general.extra.size() == 1);
    CHECK(map.general.extra[0] == std::pair<std::string, std::string>{"MyCustomKey", "12"});
    REQUIRE(map.unknown_sections.size() == 1);
    CHECK(map.unknown_sections[0].name == "FutureSection");

    const auto text = osu::beatmap::write(map);
    REQUIRE(text.has_value());
    CHECK(text->contains("MyCustomKey: 12"));
    CHECK(text->contains("[FutureSection]"));
    CHECK(text->contains("anything,goes,here"));
}

TEST_CASE("malformed beatmaps produce clean errors") {
    SECTION("empty input") {
        const auto result = osu::beatmap::parse("");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::bad_header);
    }
    SECTION("missing header") {
        const auto result = osu::beatmap::parse("hello world\n");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::bad_header);
    }
    SECTION("non-numeric format version") {
        const auto result = osu::beatmap::parse("osu file format vX\n");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::bad_number);
    }
    SECTION("truncated hit object") {
        const auto result = osu::beatmap::parse(
            "osu file format v14\n[HitObjects]\n1,2,3\n");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::bad_field);
        CHECK(result.error().line == 3);
    }
    SECTION("non-numeric value in General") {
        const auto result = osu::beatmap::parse(
            "osu file format v14\n[General]\nPreviewTime: abc\n");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::bad_number);
        CHECK(result.error().line == 3);
    }
    SECTION("unknown slider curve type") {
        const auto result = osu::beatmap::parse(
            "osu file format v14\n[HitObjects]\n100,100,1500,2,0,X|150:150,1,140\n");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::invalid_value);
    }
    SECTION("hit object type selects no kind") {
        const auto result = osu::beatmap::parse(
            "osu file format v14\n[HitObjects]\n100,100,1500,4,0\n");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::invalid_value);
    }
}
