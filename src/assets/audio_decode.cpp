#include "assets/audio_decode.hpp"

#include <cstdlib>
#include <cstring>

#include "assets/byte_reader.hpp"

// stb_vorbis — одна единица трансляции с реализацией, только чтение из памяти.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_vorbis.c"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace oc2::assets {

Pcm decodeWav(std::span<const std::uint8_t> file) {
    const ByteReader r(file);
    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 || std::memcmp(file.data() + 8, "WAVE", 4) != 0)
        throw FormatError("это не WAV (нет RIFF/WAVE)");
    Pcm pcm;
    int bits = 0, format = 0;
    std::span<const std::uint8_t> data;
    std::size_t p = 12;
    while (p + 8 <= file.size()) {
        const auto size = r.read<std::uint32_t>(p + 4);
        const std::size_t body = p + 8;
        const std::size_t len = std::min<std::size_t>(size, file.size() - body); // у части файлов длина завышена
        if (std::memcmp(file.data() + p, "fmt ", 4) == 0 && len >= 16) {
            format = r.read<std::uint16_t>(body);
            pcm.channels = r.read<std::uint16_t>(body + 2);
            pcm.rate = int(r.read<std::uint32_t>(body + 4));
            bits = r.read<std::uint16_t>(body + 14);
        } else if (std::memcmp(file.data() + p, "data", 4) == 0) {
            data = file.subspan(body, len);
        }
        p = body + size + (size & 1);
    }
    if (format != 1 || (bits != 8 && bits != 16) || pcm.channels < 1 || pcm.channels > 2 || pcm.rate <= 0)
        throw FormatError("WAV: поддерживается только PCM 8/16 бит, 1–2 канала");
    if (bits == 16) {
        pcm.samples.resize(data.size() / 2);
        for (std::size_t i = 0; i < pcm.samples.size(); ++i)
            pcm.samples[i] = std::int16_t(std::uint16_t(data[2 * i] | (data[2 * i + 1] << 8)));
    } else {
        pcm.samples.resize(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) pcm.samples[i] = std::int16_t((int(data[i]) - 128) * 256);
    }
    pcm.samples.resize(pcm.frames() * std::size_t(pcm.channels));
    return pcm;
}

Pcm decodeOgg(std::span<const std::uint8_t> file) {
    int channels = 0, rate = 0;
    short* out = nullptr;
    const int frames = stb_vorbis_decode_memory(file.data(), int(file.size()), &channels, &rate, &out);
    if (frames < 0 || !out) throw FormatError("Ogg Vorbis не читается");
    Pcm pcm;
    pcm.channels = channels;
    pcm.rate = rate;
    pcm.samples.assign(out, out + std::size_t(frames) * std::size_t(channels));
    std::free(out);
    return pcm;
}

Pcm toStereo(const Pcm& in, int rate) {
    Pcm out;
    out.channels = 2;
    out.rate = rate;
    const std::size_t n = in.frames();
    if (n == 0 || in.channels < 1 || in.rate <= 0) return out;
    const std::size_t m = std::size_t((double(n) * rate) / in.rate);
    out.samples.resize(m * 2);
    const double step = double(in.rate) / rate;
    for (std::size_t i = 0; i < m; ++i) {
        const double src = double(i) * step;
        const std::size_t a = std::min(std::size_t(src), n - 1), b = std::min(a + 1, n - 1);
        const double t = src - double(a);
        for (int c = 0; c < 2; ++c) {
            const int ch = std::min(c, in.channels - 1);
            const double va = in.samples[a * std::size_t(in.channels) + std::size_t(ch)];
            const double vb = in.samples[b * std::size_t(in.channels) + std::size_t(ch)];
            out.samples[i * 2 + std::size_t(c)] = std::int16_t(va + (vb - va) * t);
        }
    }
    return out;
}

} // namespace oc2::assets
