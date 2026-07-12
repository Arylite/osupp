#include <filesystem>
#include <print>
#include <string>
#include <utility>

#include <osupp/osupp.hpp>

namespace {

void print_beatmap(const std::filesystem::path& path) {
    const auto result = osu::beatmap::parse_file(path);
    if (!result) {
        std::println(stderr, "{}: parse error at line {}: {}", path.string(),
                     result.error().line, result.error().message);
        return;
    }
    const auto& map = *result;
    std::println("{} (osu file format v{})", path.filename().string(), map.format_version);
    std::println("  title:   {}", map.metadata.title.value_or("<none>"));
    std::println("  artist:  {}", map.metadata.artist.value_or("<none>"));
    std::println("  version: {}", map.metadata.version.value_or("<none>"));
    std::println("  mode:    {}", std::to_underlying(map.general.mode.value_or(osu::Mode::osu)));
    std::println("  {} timing points, {} hit objects, {} events",
                 map.timing_points.size(), map.hit_objects.size(), map.events.size());
}

void print_replay(const std::filesystem::path& path) {
    const auto result = osu::replay::parse_file(path);
    if (!result) {
        std::println(stderr, "{}: parse error at offset {}: {}", path.string(),
                     result.error().offset, result.error().message);
        return;
    }
    const auto& replay = *result;
    std::println("{} (game version {})", path.filename().string(), replay.game_version);
    std::println("  player:    {}", replay.player_name);
    std::println("  score:     {} ({}x combo)", replay.score, replay.max_combo);
    std::println("  judgments: {}/{}/{}/{} miss", replay.count_300, replay.count_100,
                 replay.count_50, replay.count_miss);
    std::println("  mods:      0x{:08X}", std::to_underlying(replay.mods));
    std::println("  frames:    {}", replay.frames.size());
    if (replay.rng_seed) std::println("  rng seed:  {}", *replay.rng_seed);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::println(stderr, "usage: osupp_example <file.osu | file.osr> [more files...]");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path path{argv[i]};
        if (path.extension() == ".osr") {
            print_replay(path);
        } else {
            print_beatmap(path);
        }
    }
    return 0;
}
