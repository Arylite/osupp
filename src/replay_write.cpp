#include <cstdint>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "detail/binary_io.hpp"
#include "detail/lzma.hpp"
#include "osupp/replay.hpp"

namespace osu::replay {

namespace {

constexpr std::int32_t version_score_id_long = 20140721;

[[nodiscard]] std::string encode_frames(const Replay& replay) {
    std::string text;
    for (const auto& frame : replay.frames) {
        if (!text.empty()) text += ',';
        std::format_to(std::back_inserter(text), "{}|{}|{}|{}", frame.time_delta_ms, frame.x,
                       frame.y, std::to_underlying(frame.keys));
    }
    if (replay.rng_seed) {
        if (!text.empty()) text += ',';
        std::format_to(std::back_inserter(text), "-12345|0|0|{}", *replay.rng_seed);
    }
    return text;
}

[[nodiscard]] std::string encode_life_bar(const std::vector<LifeBarPoint>& points) {
    std::string text;
    for (const auto& point : points) {
        if (!text.empty()) text += ',';
        std::format_to(std::back_inserter(text), "{}|{}", point.time_ms, point.life);
    }
    return text;
}

}  // namespace

std::expected<std::vector<std::byte>, WriteError> write(const Replay& replay) {
    const auto frame_text = encode_frames(replay);
    const auto compressed = detail::lzma::compress(
        std::as_bytes(std::span{frame_text.data(), frame_text.size()}));
    if (!compressed) return std::unexpected(compressed.error());
    if (compressed->size() > std::size_t{std::numeric_limits<std::int32_t>::max()}) {
        return std::unexpected(WriteError{WriteErrorCode::invalid_model,
                                          "compressed replay data exceeds 2 GiB"});
    }

    detail::Writer writer;
    writer.write<std::uint8_t>(std::to_underlying(replay.mode));
    writer.write<std::int32_t>(replay.game_version);
    writer.write_osu_string(std::string_view{replay.beatmap_md5});
    writer.write_osu_string(std::string_view{replay.player_name});
    writer.write_osu_string(std::string_view{replay.replay_md5});
    writer.write<std::uint16_t>(replay.count_300);
    writer.write<std::uint16_t>(replay.count_100);
    writer.write<std::uint16_t>(replay.count_50);
    writer.write<std::uint16_t>(replay.count_geki);
    writer.write<std::uint16_t>(replay.count_katu);
    writer.write<std::uint16_t>(replay.count_miss);
    writer.write<std::int32_t>(replay.score);
    writer.write<std::uint16_t>(replay.max_combo);
    writer.write<std::uint8_t>(replay.perfect ? 1 : 0);
    writer.write<std::uint32_t>(std::to_underlying(replay.mods));
    if (replay.life_bar) {
        writer.write_osu_string(std::string_view{encode_life_bar(*replay.life_bar)});
    } else {
        writer.write_osu_string(std::nullopt);
    }
    writer.write<std::int64_t>(ticks_from_timestamp(replay.timestamp));
    writer.write<std::int32_t>(static_cast<std::int32_t>(compressed->size()));
    writer.write_bytes(*compressed);

    if (replay.game_version >= version_score_id_long) {
        writer.write<std::int64_t>(replay.online_score_id);
    } else {
        if (replay.online_score_id > std::numeric_limits<std::int32_t>::max() ||
            replay.online_score_id < std::numeric_limits<std::int32_t>::min()) {
            return std::unexpected(WriteError{
                WriteErrorCode::invalid_model,
                std::format("score id {} does not fit the 4-byte field used before game "
                            "version {}",
                            replay.online_score_id, version_score_id_long)});
        }
        writer.write<std::int32_t>(static_cast<std::int32_t>(replay.online_score_id));
    }

    if (has_flag(replay.mods, Mods::target_practice)) {
        writer.write<double>(replay.target_practice_accuracy.value_or(0.0));
    }
    return std::move(writer).take();
}

std::expected<void, WriteError> write_file(const Replay& replay,
                                           const std::filesystem::path& path) {
    const auto bytes = write(replay);
    if (!bytes) return std::unexpected(bytes.error());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(WriteError{WriteErrorCode::io_failure,
                                          std::format("cannot open '{}'", path.string())});
    }
    file.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
    file.close();
    if (!file) {
        return std::unexpected(WriteError{WriteErrorCode::io_failure,
                                          std::format("write failure on '{}'", path.string())});
    }
    return {};
}

}  // namespace osu::replay
