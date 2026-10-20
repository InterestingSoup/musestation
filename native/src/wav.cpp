// SPDX-License-Identifier: GPL-3.0-or-later
#include "wav.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace muse {
namespace {
unsigned u16(const unsigned char *p) { return p[0] | unsigned(p[1]) << 8; }
unsigned u32(const unsigned char *p) { return u16(p) | u16(p + 2) << 16; }
}
bool decode_speech_wav(const std::string &bytes, std::vector<float> &out) {
  out.clear();
  if (bytes.size() < 44 || bytes.size() > 32 * 1024 * 1024 ||
      std::memcmp(bytes.data(), "RIFF", 4) ||
      std::memcmp(bytes.data() + 8, "WAVE", 4)) return false;
  const auto *p = reinterpret_cast<const unsigned char *>(bytes.data());
  if (uint64_t(u32(p + 4)) + 8 != bytes.size()) return false;
  unsigned rate = 0, channels = 0;
  size_t start = 0, length = 0, at = 12;
  bool format_seen = false, data_seen = false;
  while (at + 8 <= bytes.size()) {
    size_t n = u32(p + at + 4);
    if (n > bytes.size() - at - 8) return false;
    if (!std::memcmp(p + at, "fmt ", 4)) {
      if (format_seen || n < 16 || u16(p + at + 8) != 1 ||
          u16(p + at + 22) != 16) return false;
      format_seen = true;
      channels = u16(p + at + 10);
      rate = u32(p + at + 12);
      if ((channels != 1 && channels != 2) || rate < 8000 || rate > 96000 ||
          u16(p + at + 20) != channels * 2 ||
          u32(p + at + 16) != rate * channels * 2) return false;
    } else if (!std::memcmp(p + at, "data", 4)) {
      if (data_seen) return false;
      data_seen = true;
      start = at + 8;
      length = n;
    }
    const size_t step = 8 + n + (n & 1);
    if (step > bytes.size() - at) return false;
    at += step;
  }
  if (at != bytes.size() || !format_seen || !data_seen || !length ||
      length % (channels * 2)) return false;
  const size_t frames = length / (channels * 2);
  if (frames > size_t(rate) * 180) return false;
  const size_t count = frames * 48000 / rate;
  if (!count || count > 48000 * 180) return false;
  out.resize(count * 2);
  for (size_t i = 0; i < count; ++i) {
    const uint64_t position = uint64_t(i) * rate;
    const size_t first = size_t(position / 48000);
    const size_t next = std::min(first + 1, frames - 1);
    const float fraction = float(position % 48000) / 48000.f;
    for (unsigned ch = 0; ch < 2; ++ch) {
      const unsigned source_ch = channels == 1 ? 0 : ch;
      const float a = float(int16_t(u16(p + start + (first * channels + source_ch) * 2)));
      const float b = float(int16_t(u16(p + start + (next * channels + source_ch) * 2)));
      out[2 * i + ch] = (a + (b - a) * fraction) / 32768.f;
    }
  }
  return true;
}
}
