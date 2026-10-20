// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
namespace muse {
// Decode bounded PCM16 WAV and resample to the AudioOut mixer's 48 kHz stereo.
// Linear interpolation avoids repeating each 24 kHz neural speech sample twice.
bool decode_speech_wav(const std::string &bytes, std::vector<float> &stereo);
}
