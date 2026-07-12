#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <osupp/osupp.hpp>

namespace {

const std::filesystem::path data_dir{OSUPP_TEST_DATA_DIR};

}  // namespace

TEST_CASE("parse example replay") {
    const auto result = osu::replay::parse_file(data_dir / "example.osr");
    REQUIRE(result.has_value());
    const auto& replay = *result;

    CHECK(replay.mode == osu::Mode::osu);
    CHECK(replay.game_version == 20240101);
    CHECK(replay.beatmap_md5 == "9c1b3f6a0e2d4c8b7a5f0e1d2c3b4a59");
    CHECK(replay.player_name == "osupp");
    CHECK(replay.replay_md5 == "0f1e2d3c4b5a69788796a5b4c3d2e1f0");
    CHECK(replay.count_300 == 10);
    CHECK(replay.count_100 == 2);
    CHECK(replay.count_50 == 1);
    CHECK(replay.count_geki == 3);
    CHECK(replay.count_katu == 1);
    CHECK(replay.count_miss == 0);
    CHECK(replay.score == 123456);
    CHECK(replay.max_combo == 42);
    CHECK_FALSE(replay.perfect);
    CHECK(replay.mods == (osu::Mods::hidden | osu::Mods::double_time));

    REQUIRE(replay.life_bar.has_value());
    REQUIRE(replay.life_bar->size() == 3);
    CHECK((*replay.life_bar)[1] == osu::replay::LifeBarPoint{500, 0.85});

    using namespace std::chrono;
    CHECK(time_point_cast<seconds>(replay.timestamp) ==
          sys_days{June / 1 / 2024} + 12h);

    REQUIRE(replay.frames.size() == 3);
    CHECK(replay.frames[0] ==
          osu::replay::ReplayFrame{0, 256.0f, 192.0f, osu::ReplayKeys::none});
    CHECK(replay.frames[1] ==
          osu::replay::ReplayFrame{16, 260.5f, 200.0f, osu::ReplayKeys::mouse1});
    CHECK(replay.frames[2].y == 210.25f);

    CHECK(replay.rng_seed == 424242);
    CHECK(replay.online_score_id == 987654321);
    CHECK_FALSE(replay.target_practice_accuracy.has_value());
}

TEST_CASE("replay round-trips through write and parse") {
    const auto original = osu::replay::parse_file(data_dir / "example.osr");
    REQUIRE(original.has_value());

    const auto first_write = osu::replay::write(*original);
    REQUIRE(first_write.has_value());

    const auto reparsed = osu::replay::parse(*first_write);
    REQUIRE(reparsed.has_value());
    CHECK(*reparsed == *original);

    const auto second_write = osu::replay::write(*reparsed);
    REQUIRE(second_write.has_value());
    CHECK(*first_write == *second_write);
}

TEST_CASE("hand-built replay survives a round-trip") {
    osu::replay::Replay replay;
    replay.mode = osu::Mode::mania;
    replay.game_version = 20240101;
    replay.beatmap_md5 = "00112233445566778899aabbccddeeff";
    replay.player_name = std::string(300, 'a');  // forces a two-byte ULEB128 length
    replay.replay_md5 = "ffeeddccbbaa99887766554433221100";
    replay.count_300 = 999;
    replay.count_miss = 1;
    replay.score = 1'000'000;
    replay.max_combo = 512;
    replay.perfect = false;
    replay.mods = osu::Mods::target_practice;
    replay.life_bar = std::vector{osu::replay::LifeBarPoint{0, 1.0}};
    replay.timestamp = osu::replay::timestamp_from_ticks(638'527'824'000'000'000);
    replay.frames = {
        {0, 36.5f, 100.0f, osu::ReplayKeys::none},
        {12, 40.0f, 120.0f, osu::ReplayKeys::key1 | osu::ReplayKeys::key2},
    };
    replay.rng_seed = 1337;
    replay.online_score_id = 5'000'000'000;  // needs the 8-byte field
    replay.target_practice_accuracy = 98.5;

    const auto bytes = osu::replay::write(replay);
    REQUIRE(bytes.has_value());
    const auto reparsed = osu::replay::parse(*bytes);
    REQUIRE(reparsed.has_value());
    CHECK(*reparsed == replay);
}

TEST_CASE("replay file round-trips through the filesystem") {
    const auto original = osu::replay::parse_file(data_dir / "example.osr");
    REQUIRE(original.has_value());

    const auto temp = std::filesystem::temp_directory_path() / "osupp_roundtrip.osr";
    REQUIRE(osu::replay::write_file(*original, temp).has_value());
    const auto reloaded = osu::replay::parse_file(temp);
    std::filesystem::remove(temp);
    REQUIRE(reloaded.has_value());
    CHECK(*reloaded == *original);
}

TEST_CASE("timestamp conversion is lossless at the tick level") {
    constexpr std::int64_t ticks = 638'527'824'000'000'001;  // deliberately odd
    CHECK(osu::replay::ticks_from_timestamp(osu::replay::timestamp_from_ticks(ticks)) == ticks);
}

TEST_CASE("malformed replays produce clean errors") {
    SECTION("empty input") {
        const auto result = osu::replay::parse(std::span<const std::byte>{});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::truncated);
    }
    SECTION("game mode out of range") {
        const std::byte bytes[] = {std::byte{0x09}};
        const auto result = osu::replay::parse(bytes);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::invalid_value);
    }
    SECTION("bad string flag") {
        const std::byte bytes[] = {std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                   std::byte{0x00}, std::byte{0x00}, std::byte{0x05}};
        const auto result = osu::replay::parse(bytes);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::invalid_value);
    }
    SECTION("truncated file") {
        const auto good = osu::replay::parse_file(data_dir / "example.osr");
        REQUIRE(good.has_value());
        const auto bytes = osu::replay::write(*good);
        REQUIRE(bytes.has_value());
        const auto result = osu::replay::parse(std::span{*bytes}.first(20));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == osu::ParseErrorCode::truncated);
    }
    SECTION("corrupted compressed data") {
        const auto good = osu::replay::parse_file(data_dir / "example.osr");
        REQUIRE(good.has_value());
        auto bytes = osu::replay::write(*good);
        REQUIRE(bytes.has_value());
        // Somewhere inside the LZMA block: after the fixed fields, before the score id.
        const auto target = bytes->size() - 12;
        (*bytes)[target] ^= std::byte{0xFF};
        (*bytes)[target - 1] ^= std::byte{0xFF};
        const auto result = osu::replay::parse(*bytes);
        CHECK_FALSE(result.has_value());
    }
}
