#include "png_writer.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace oc2::tools {

namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void putBe32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(std::uint8_t(v >> 24));
    out.push_back(std::uint8_t(v >> 16));
    out.push_back(std::uint8_t(v >> 8));
    out.push_back(std::uint8_t(v));
}

void putChunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& body) {
    putBe32(out, static_cast<std::uint32_t>(body.size()));
    std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    putBe32(out, crc32(&out[start], out.size() - start));
}

} // namespace

void writePng(const std::filesystem::path& path, const assets::Image& image) {
    // Строки с байтом фильтра 0.
    std::vector<std::uint8_t> raw;
    const std::size_t stride = std::size_t(image.width) * 4;
    raw.reserve((stride + 1) * image.height);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const std::uint8_t* row = image.rgba.data() + y * stride;
        raw.insert(raw.end(), row, row + stride);
    }

    // zlib-поток из несжатых блоков deflate по 65535 байт.
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::size_t pos = 0;
    do {
        std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(std::uint8_t(n));
        z.push_back(std::uint8_t(n >> 8));
        z.push_back(std::uint8_t(~n));
        z.push_back(std::uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(pos), raw.begin() + std::ptrdiff_t(pos + n));
        pos += n;
    } while (pos < raw.size());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    putBe32(z, (b << 16) | a);

    std::vector<std::uint8_t> ihdr;
    putBe32(ihdr, image.width);
    putBe32(ihdr, image.height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8 бит, RGBA, без чересстрочности

    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    putChunk(out, "IHDR", ihdr);
    putChunk(out, "IDAT", z);
    putChunk(out, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("не удалось создать " + path.string());
    file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!file) throw std::runtime_error("ошибка записи " + path.string());
}

} // namespace oc2::tools
