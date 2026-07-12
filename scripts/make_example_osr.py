#!/usr/bin/env python3
"""Regenerates tests/data/example.osr.

Field values must stay in sync with the expectations in tests/replay_tests.cpp.
"""
import lzma
import struct
from datetime import datetime, timezone
from pathlib import Path


def uleb128(n: int) -> bytes:
    out = bytearray()
    while True:
        byte = n & 0x7F
        n >>= 7
        if n:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def osu_string(s: str | None) -> bytes:
    if s is None:
        return b"\x00"
    raw = s.encode("utf-8")
    return b"\x0b" + uleb128(len(raw)) + raw


TICKS_AT_UNIX_EPOCH = 621_355_968_000_000_000
timestamp = datetime(2024, 6, 1, 12, 0, 0, tzinfo=timezone.utc)
ticks = TICKS_AT_UNIX_EPOCH + int(timestamp.timestamp()) * 10_000_000

frames = "0|256|192|0,16|260.5|200|1,17|270|210.25|1,-12345|0|0|424242"
compressed = lzma.compress(frames.encode(), format=lzma.FORMAT_ALONE, preset=6)

out = bytearray()
out += struct.pack("<B", 0)                            # mode: osu!
out += struct.pack("<i", 20240101)                     # game version
out += osu_string("9c1b3f6a0e2d4c8b7a5f0e1d2c3b4a59")  # beatmap MD5
out += osu_string("osupp")                             # player name
out += osu_string("0f1e2d3c4b5a69788796a5b4c3d2e1f0")  # replay MD5
out += struct.pack("<6H", 10, 2, 1, 3, 1, 0)           # 300/100/50/geki/katu/miss
out += struct.pack("<i", 123456)                       # score
out += struct.pack("<H", 42)                           # max combo
out += struct.pack("<B", 0)                            # perfect
out += struct.pack("<I", 72)                           # mods: HD | DT
out += osu_string("0|1,500|0.85,1000|0.5,")            # life bar, trailing comma like stable
out += struct.pack("<q", ticks)
out += struct.pack("<i", len(compressed))
out += compressed
out += struct.pack("<q", 987654321)                    # online score id

path = Path(__file__).resolve().parent.parent / "tests" / "data" / "example.osr"
path.write_bytes(bytes(out))
print(f"wrote {path} ({len(out)} bytes)")
