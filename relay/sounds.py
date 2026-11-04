"""Muse-supplied sounds for generated native apps.

Muse can send sound effects (WAV) and music (OGG Vorbis) in the same source
submission, as base64 files in an optional `sounds` object; nothing is fetched
from the network. The builder normalises every WAV to the 48 kHz 16-bit stereo
the toolkit's `decode_wav` needs, checks that OGG music is 48 kHz Vorbis, and
copies the files into the folder app under assets/sounds/. It also generates
`include/muse_sounds.hpp` with one path constant per sound plus small helpers
to load/play effects and to stream looping music.
"""
from __future__ import annotations

import base64
import binascii
import re
import struct

MAX_SOUNDS = 12
MAX_INPUT_BYTES = 8 * 1024 * 1024           # each encoded file
MAX_TOTAL_OUTPUT = 24 * 1024 * 1024         # all converted files together
NAME = re.compile(r"[a-z][a-z0-9_]{0,31}\.(?:wav|ogg)")
RATE = 48000


class SoundError(ValueError):
    pass


def _wav_to_48k_stereo(data: bytes) -> bytes:
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SoundError("not a RIFF WAVE file")
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        kind, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if kind == b"fmt ":
            fmt = body
        elif kind == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    if not fmt or pcm is None or len(fmt) < 16:
        raise SoundError("WAV has no fmt/data chunk")
    tag, channels, rate, _, align, bits = struct.unpack("<HHIIHH", fmt[:16])
    if tag == 0xFFFE and len(fmt) >= 26:
        tag = struct.unpack("<H", fmt[24:26])[0]
    if tag != 1 or bits not in (8, 16, 24) or channels not in (1, 2) or not 8000 <= rate <= 96000:
        raise SoundError("use uncompressed PCM WAV (8/16/24-bit, mono or stereo, 8-96 kHz)")
    width = bits // 8
    frames = len(pcm) // (width * channels)
    if not frames:
        raise SoundError("WAV contains no audio")
    if frames > rate * 60:
        raise SoundError("sound effects must be at most 60 seconds; send long music as 48 kHz OGG")

    def sample(i: int) -> int:
        o = i * width
        if width == 1:
            return (pcm[o] - 128) << 8
        if width == 2:
            return struct.unpack_from("<h", pcm, o)[0]
        return int.from_bytes(pcm[o:o + 3], "little", signed=True) >> 8

    left = [sample(f * channels) for f in range(frames)]
    right = [sample(f * channels + 1) for f in range(frames)] if channels == 2 else left
    if rate != RATE:  # linear resampling is plenty for game sound effects
        count = max(1, frames * RATE // rate)
        step = (frames - 1) / max(1, count - 1)

        def resample(src):
            out = []
            for i in range(count):
                x = i * step
                j = int(x)
                k = min(j + 1, frames - 1)
                out.append(int(src[j] + (src[k] - src[j]) * (x - j)))
            return out
        left, right = resample(left), resample(right) if channels == 2 else None
        right = right if right is not None else left
    pcm16 = bytearray(len(left) * 4)
    for i, (l_, r_) in enumerate(zip(left, right)):
        struct.pack_into("<hh", pcm16, i * 4, max(-32768, min(32767, l_)), max(-32768, min(32767, r_)))
    header = (b"RIFF" + struct.pack("<I", 36 + len(pcm16)) + b"WAVEfmt " +
              struct.pack("<IHHIIHH", 16, 1, 2, RATE, RATE * 4, 4, 16) + b"data" + struct.pack("<I", len(pcm16)))
    return header + bytes(pcm16)


def _check_ogg(data: bytes) -> bytes:
    if data[:4] != b"OggS":
        raise SoundError("not an OGG file")
    ident = data.find(b"\x01vorbis")
    if ident < 0 or ident > 512 or len(data) < ident + 16:
        raise SoundError("OGG music must be Vorbis (not Opus)")
    channels = data[ident + 11]
    rate = struct.unpack("<I", data[ident + 12:ident + 16])[0]
    if channels not in (1, 2) or rate != RATE:
        raise SoundError(f"OGG music must be 48 kHz mono or stereo Vorbis (got {rate} Hz, {channels} channels)")
    return data


def validate_sounds(sounds) -> list[tuple[str, str, bytes]]:
    """Check, convert and return (identifier, file name, bytes) per sound."""
    if sounds is None:
        return []
    if not isinstance(sounds, dict) or len(sounds) > MAX_SOUNDS:
        raise SoundError(f"sounds must map up to {MAX_SOUNDS} names to base64 WAV/OGG data")
    out, total, idents = [], 0, set()
    for name in sorted(sounds):
        value = sounds[name]
        if not isinstance(name, str) or not NAME.fullmatch(name):
            raise SoundError("sound names look like capture.wav or theme.ogg: lowercase letters, digits, underscore")
        ident = name.rsplit(".", 1)[0]
        if ident in idents:
            raise SoundError(f"{name}: two sounds share the name {ident}")
        idents.add(ident)
        if not isinstance(value, str) or len(value) > MAX_INPUT_BYTES * 4 // 3 + 4:
            raise SoundError(f"{name}: send base64 data of at most {MAX_INPUT_BYTES} bytes")
        try:
            raw = base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise SoundError(f"{name}: invalid base64") from exc
        try:
            converted = _wav_to_48k_stereo(raw) if name.endswith(".wav") else _check_ogg(raw)
        except (SoundError, struct.error) as exc:
            raise SoundError(f"{name}: {exc}") from exc
        total += len(converted)
        if total > MAX_TOTAL_OUTPUT:
            raise SoundError("sounds exceed 24 MiB after conversion; shorten effects or compress music as OGG")
        out.append((ident, name, converted))
    return out


def header(sounds) -> str:
    lines = [
        "// Generated by the MuseStation relay from Muse's sounds. Do not edit.",
        "#pragma once",
        "#include <algorithm>",
        "#include <string>",
        "#include <vector>",
        '#include "audio/mixer.hpp"',
        '#include "audio/music.hpp"',
        '#include "audio/stream_ring.hpp"',
        '#include "audio/wav.hpp"',
        '#include "core/save_file.hpp"',
        "",
        "namespace muse_sounds {",
    ]
    for ident, name, _ in sounds:
        lines.append(f'inline constexpr const char *{ident} = "/app0/assets/sounds/{name}";')
    lines += [
        "",
        "// A sound effect (WAV). Keep it alive while it may play.",
        "struct Effect {",
        "  hui::audio::DecodedWav wav;",
        "  hui::audio::Clip clip{};",
        "  bool ok = false;",
        "};",
        "inline bool load(Effect &effect, const char *path) {",
        "  std::string data;",
        "  if (!hui::save::read_file(path, &data, 32u << 20)) return false;",
        "  effect.wav = hui::audio::decode_wav(data);",
        "  if (!effect.wav.ok()) return false;",
        "  effect.clip = {effect.wav.samples.data(), effect.wav.frames};",
        "  return effect.ok = true;",
        "}",
        "inline void play(hui::audio::Mixer &mixer, const Effect &effect, float gain = 1.0f, float pitch = 1.0f) {",
        "  if (effect.ok) {",
        "    hui::audio::PlayParams params;",
        "    params.gain = gain;",
        "    params.pitch = pitch;",
        "    mixer.play_clip(&effect.clip, params);",
        "  }",
        "}",
        "",
        "// Looping background music (48 kHz OGG Vorbis) streamed from memory.",
        "// Call start() BEFORE the audio output thread starts, then pump() every frame.",
        "class Music {",
        " public:",
        "  bool start(hui::audio::Mixer &mixer, const char *path, float gain = 0.6f) {",
        "    std::string data;",
        "    if (!hui::save::read_file(path, &data, 32u << 20) || !track_.open(std::move(data)).empty()) return false;",
        "    track_.set_looping(true);",
        "    mixer.attach_stream(0, &ring_);",
        "    mixer.set_stream_gain(0, gain, 0.0f);",
        "    buffer_.resize(4096 * 2);",
        "    ok_ = true;",
        "    pump();",
        "    return true;",
        "  }",
        "  void pump() {",
        "    while (ok_ && ring_.available() < 48000 * 7 / 10) {",
        "      const int room = int(std::min<std::size_t>(4096, ring_.space()));",
        "      if (room <= 0) break;",
        "      const int got = track_.decode(buffer_.data(), room);",
        "      if (got > 0) ring_.write(buffer_.data(), std::size_t(got));",
        "      if (got < room) break;",
        "    }",
        "  }",
        "",
        " private:",
        "  hui::audio::MusicTrack track_;",
        "  hui::audio::StreamRing ring_{1u << 16};",
        "  std::vector<float> buffer_;",
        "  bool ok_ = false;",
        "};",
        "}  // namespace muse_sounds",
        "",
    ]
    return "\n".join(lines)
