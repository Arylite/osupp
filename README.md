# osupp

C++ library for reading and writing osu! file formats:

- **`.osu`** beatmaps (text) - typed model for every section, storyboard data kept raw
- **`.osr`** replays (binary) - full field coverage including LZMA-compressed input frames and the RNG seed

No exceptions for expected failures: every fallible operation returns `std::expected`.

```cpp
#include <osupp/osupp.hpp>

auto map = osu::beatmap::parse_file("song.osu");
if (map) {
    map->metadata.creator = "me";
    (void)osu::beatmap::write_file(*map, "song-edited.osu");
}

auto replay = osu::replay::parse_file("play.osr");
if (replay) {
    std::println("{} frames, seed {}", replay->frames.size(), replay->rng_seed.value_or(0));
}
```
