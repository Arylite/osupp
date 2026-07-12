#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "detail/binary_io.hpp"
#include "detail/lzma.hpp"
#include "detail/string_utils.hpp"
#include "osupp/replay.hpp"

namespace osu::replay {

namespace {

// Replay data is text; a hostile file could still claim a huge size.
constexpr std::size_t max_decompressed_size = std::size_t{256} << 20;

// .osr layout changes: online score id widened to 8 bytes in 2014.
constexpr std::int32_t version_score_id_long = 20140721;

[[nodiscard]] std::unexpected<ParseError> fail(ParseErrorCode code, std::string message) {
    return std::unexpected(ParseError{code, std::move(message)});
}

[[nodiscard]] std::expected<std::optional<std::vector<LifeBarPoint>>, ParseError> parse_life_bar(
    const std::optional<std::string>& text) {
    if (!text) return std::optional<std::vector<LifeBarPoint>>{};
    std::vector<LifeBarPoint> points;
    for (const auto entry : detail::split(*text, ',')) {
        if (detail::trim(entry).empty()) continue;  // stable leaves a trailing comma
        const auto pair = detail::split(entry, '|');
        if (pair.size() != 2) {
            return fail(ParseErrorCode::invalid_value,
                        std::format("malformed life bar entry: '{}'", std::string_view{entry}));
        }
        const auto time = detail::parse_num<int>(pair[0]);
        const auto life = detail::parse_num<double>(pair[1]);
        if (!time || !life) {
            return fail(ParseErrorCode::bad_number,
                        std::format("malformed life bar entry: '{}'", std::string_view{entry}));
        }
        points.push_back(LifeBarPoint{*time, *life});
    }
    return std::optional{std::move(points)};
}

struct DecodedFrames {
    std::vector<ReplayFrame> frames;
    std::optional<std::int32_t> rng_seed;
};

[[nodiscard]] std::expected<DecodedFrames, ParseError> parse_frames(std::string_view text) {
    DecodedFrames decoded;
    for (const auto entry : detail::split(text, ',')) {
        if (entry.empty()) continue;
        const auto fields = detail::split(entry, '|');
        if (fields.size() != 4) {
            return fail(ParseErrorCode::invalid_value,
                        std::format("malformed replay frame: '{}'", std::string_view{entry}));
        }
        const auto delta = detail::parse_num<std::int64_t>(fields[0]);
        if (!delta) {
            return fail(ParseErrorCode::bad_number,
                        std::format("malformed replay frame: '{}'", std::string_view{entry}));
        }
        // The RNG seed rides in a trailing pseudo-frame, not a real input.
        if (*delta == -12345) {
            const auto seed = detail::parse_num<std::int32_t>(fields[3]);
            if (!seed) {
                return fail(ParseErrorCode::bad_number, "malformed RNG seed frame");
            }
            decoded.rng_seed = *seed;
            continue;
        }
        const auto x = detail::parse_num<float>(fields[1]);
        const auto y = detail::parse_num<float>(fields[2]);
        const auto keys = detail::parse_num<std::uint32_t>(fields[3]);
        if (!x || !y || !keys) {
            return fail(ParseErrorCode::bad_number,
                        std::format("malformed replay frame: '{}'", std::string_view{entry}));
        }
        decoded.frames.push_back(
            ReplayFrame{*delta, *x, *y, static_cast<ReplayKeys>(*keys)});
    }
    return decoded;
}

}  // namespace

std::expected<Replay, ParseError> parse(std::span<const std::byte> data) {
    detail::Reader reader{data};
    Replay replay;

    const auto mode = reader.read<std::uint8_t>();
    if (!mode) return std::unexpected(mode.error());
    if (*mode > 3) {
        return std::unexpected(ParseError{ParseErrorCode::invalid_value,
                                          std::format("game mode out of range: {}", *mode), 0, 0});
    }
    replay.mode = static_cast<Mode>(*mode);

    const auto version = reader.read<std::int32_t>();
    if (!version) return std::unexpected(version.error());
    replay.game_version = *version;

    for (auto* field : {&replay.beatmap_md5, &replay.player_name, &replay.replay_md5}) {
        auto text = reader.read_osu_string();
        if (!text) return std::unexpected(text.error());
        *field = std::move(*text).value_or(std::string{});
    }

    for (auto* count : {&replay.count_300, &replay.count_100, &replay.count_50,
                        &replay.count_geki, &replay.count_katu, &replay.count_miss}) {
        const auto value = reader.read<std::uint16_t>();
        if (!value) return std::unexpected(value.error());
        *count = *value;
    }

    const auto score = reader.read<std::int32_t>();
    if (!score) return std::unexpected(score.error());
    replay.score = *score;

    const auto max_combo = reader.read<std::uint16_t>();
    if (!max_combo) return std::unexpected(max_combo.error());
    replay.max_combo = *max_combo;

    const auto perfect = reader.read<std::uint8_t>();
    if (!perfect) return std::unexpected(perfect.error());
    replay.perfect = *perfect != 0;

    const auto mods = reader.read<std::uint32_t>();
    if (!mods) return std::unexpected(mods.error());
    replay.mods = static_cast<Mods>(*mods);

    const auto life_bar_text = reader.read_osu_string();
    if (!life_bar_text) return std::unexpected(life_bar_text.error());
    auto life_bar = parse_life_bar(*life_bar_text);
    if (!life_bar) return std::unexpected(life_bar.error());
    replay.life_bar = std::move(*life_bar);

    const auto ticks = reader.read<std::int64_t>();
    if (!ticks) return std::unexpected(ticks.error());
    replay.timestamp = timestamp_from_ticks(*ticks);

    const auto compressed_size = reader.read<std::int32_t>();
    if (!compressed_size) return std::unexpected(compressed_size.error());
    if (*compressed_size < 0) {
        return fail(ParseErrorCode::invalid_value,
                    std::format("negative replay data size: {}", *compressed_size));
    }
    const auto compressed = reader.read_bytes(static_cast<std::size_t>(*compressed_size));
    if (!compressed) return std::unexpected(compressed.error());

    if (*compressed_size > 0) {
        const auto decompressed = detail::lzma::decompress(*compressed, max_decompressed_size);
        if (!decompressed) return std::unexpected(decompressed.error());
        const std::string_view frame_text{reinterpret_cast<const char*>(decompressed->data()),
                                          decompressed->size()};
        auto decoded = parse_frames(frame_text);
        if (!decoded) return std::unexpected(decoded.error());
        replay.frames = std::move(decoded->frames);
        replay.rng_seed = decoded->rng_seed;
    }

    if (replay.game_version >= version_score_id_long) {
        if (reader.remaining() >= 8) {
            const auto id = reader.read<std::int64_t>();
            if (!id) return std::unexpected(id.error());
            replay.online_score_id = *id;
        }
    } else if (reader.remaining() >= 4) {
        const auto id = reader.read<std::int32_t>();
        if (!id) return std::unexpected(id.error());
        replay.online_score_id = *id;
    }

    if (has_flag(replay.mods, Mods::target_practice) && reader.remaining() >= 8) {
        const auto accuracy = reader.read<double>();
        if (!accuracy) return std::unexpected(accuracy.error());
        replay.target_practice_accuracy = *accuracy;
    }
    // Any remaining trailing bytes are ignored for forward compatibility.
    return replay;
}

std::expected<Replay, ParseError> parse_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(ParseError{ParseErrorCode::io_failure,
                                          std::format("cannot open '{}'", path.string())});
    }
    const std::string bytes{std::istreambuf_iterator<char>(file),
                            std::istreambuf_iterator<char>()};
    if (file.bad()) {
        return std::unexpected(ParseError{ParseErrorCode::io_failure,
                                          std::format("read failure on '{}'", path.string())});
    }
    return parse(std::as_bytes(std::span{bytes.data(), bytes.size()}));
}

}  // namespace osu::replay
