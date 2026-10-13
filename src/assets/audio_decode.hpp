#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace oc2::assets {

// Звук: 16-битные отсчёты, каналы чередуются.
struct Pcm {
    int channels = 0;
    int rate = 0;
    std::vector<std::int16_t> samples;

    std::size_t frames() const { return channels > 0 ? samples.size() / std::size_t(channels) : 0; }
};

// WAV с PCM 8 или 16 бит (так записаны все звуки игры). FormatError на чужом формате.
Pcm decodeWav(std::span<const std::uint8_t> file);
// Ogg Vorbis (музыка) через stb_vorbis.
Pcm decodeOgg(std::span<const std::uint8_t> file);

// Переводит звук в стерео с частотой rate (линейная интерполяция) — формат микшера.
Pcm toStereo(const Pcm& in, int rate);

} // namespace oc2::assets
