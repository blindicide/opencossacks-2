#include "assets/g16.hpp"

#include <string>
#include <utility>

#include "assets/byte_reader.hpp"
#include "assets/compression.hpp"

namespace oc2::assets {

namespace {

constexpr std::uint32_t kTagGU16 = 0x36315547; // "GU16"
constexpr std::uint32_t kTagGN16 = 0x36314E47; // "GN16"
constexpr std::uint32_t kTagGPAL = 0x4C415047; // "GPAL"
constexpr std::uint32_t kTagGINF = 0x464E4947; // "GINF"

constexpr std::size_t kPaletteOffset = 0x422; // таблица цветов внутри раздела GPAL
constexpr std::size_t kInfoValueOffset = 0x18;

// Знаковое 12-битное поле.
std::int32_t signed12(std::uint32_t v) {
    v &= 0xFFF;
    return (v & 0x800) ? std::int32_t(v) - 0x1000 : std::int32_t(v);
}

} // namespace

G16File G16File::parse(std::vector<std::uint8_t> data) {
    // .g17 — тот же .g16 в сжатом блоке движка (на практике bzip2).
    if (data.size() >= 9 && data[0] >= 1 && data[0] <= 4) {
        ByteReader probe(data);
        auto tag = probe.read<std::uint32_t>(0);
        if (tag != kTagGU16 && tag != kTagGN16) data = unpackBlock(data);
    }

    G16File f;
    f.data_ = std::move(data);
    ByteReader in(f.data_);
    bool haveMain = false;

    // Файл — последовательность разделов «u32 метка, u32 размер, данные».
    for (std::size_t pos = 0; pos + 8 <= in.size();) {
        const auto tag = in.read<std::uint32_t>(pos);
        const auto size = in.read<std::uint32_t>(pos + 4);
        if (size < 8 || size > in.size() - pos) throw FormatError("g16: раздел выходит за пределы файла");

        if ((tag == kTagGU16 || tag == kTagGN16) && !haveMain) {
            if (pos != 0) throw FormatError("g16: основной раздел должен быть первым");
            haveMain = true;
            ByteReader sec(in.slice(pos, size));
            std::vector<std::pair<std::uint32_t, std::uint8_t>> table; // смещение, метод

            if (tag == kTagGU16) {
                f.kind_ = Kind::Uniform;
                const std::uint32_t perChunk = sec.read<std::uint8_t>(8);
                const std::uint32_t frames = sec.read<std::uint16_t>(9);
                const auto width = sec.read<std::uint16_t>(0x0B);
                const auto height = sec.read<std::uint16_t>(0x0D);
                f.maxUnpacked_ = sec.read<std::uint32_t>(0x0F);
                const std::uint32_t chunks = sec.read<std::uint16_t>(0x13);
                if (frames != 0 && perChunk == 0) throw FormatError("g16: ноль кадров в блоке");
                if (frames != 0 && (frames + perChunk - 1) / perChunk != chunks)
                    throw FormatError("g16: число блоков не соответствует числу кадров");
                sec.requireTable(0x15, chunks, 4);
                for (std::uint32_t i = 0; i < chunks; ++i) {
                    auto e = sec.read<std::uint32_t>(0x15 + i * 4);
                    table.emplace_back(e >> 4, std::uint8_t(e & 0xF));
                }
                for (std::uint32_t i = 0; i < frames; ++i)
                    f.frames_.push_back({width, height, i / perChunk, i % perChunk});
                for (std::uint32_t i = 0; i < chunks; ++i) {
                    std::uint32_t first = i * perChunk;
                    f.chunks_.push_back({0, 0, 0, first, std::min(perChunk, frames - first)});
                }
            } else {
                f.kind_ = Kind::NonUniform;
                const std::uint32_t frames = sec.read<std::uint16_t>(8);
                f.maxUnpacked_ = sec.read<std::uint32_t>(0x0A);
                const std::uint32_t chunks = sec.read<std::uint16_t>(0x0E);
                sec.requireTable(0x10, chunks, 6);
                const std::size_t frameTable = 0x10 + std::size_t(chunks) * 6;
                sec.requireTable(frameTable, frames, 8);
                std::uint32_t first = 0;
                for (std::uint32_t i = 0; i < chunks; ++i) {
                    auto e = sec.read<std::uint32_t>(0x10 + i * 6);
                    auto count = sec.read<std::uint16_t>(0x10 + i * 6 + 4);
                    table.emplace_back(e >> 4, std::uint8_t(e & 0xF));
                    f.chunks_.push_back({0, 0, 0, first, count});
                    first += count;
                }
                for (std::uint32_t i = 0; i < frames; ++i) {
                    const std::size_t at = frameTable + std::size_t(i) * 8;
                    const auto width = sec.read<std::uint16_t>(at + 2);
                    const auto height = sec.read<std::uint16_t>(at + 4);
                    const std::uint32_t chunk = sec.read<std::uint16_t>(at + 6);
                    if (chunk >= chunks) throw FormatError("g16: кадр ссылается на несуществующий блок");
                    const Chunk& c = f.chunks_[chunk];
                    if (i < c.firstFrame || i >= c.firstFrame + c.frameCount)
                        throw FormatError("g16: кадр " + std::to_string(i) + " вне своего блока");
                    f.frames_.push_back({width, height, chunk, i - c.firstFrame});
                }
            }

            // Размер блока — до начала следующего или до конца раздела.
            for (std::size_t i = 0; i < table.size(); ++i) {
                const std::uint32_t begin = table[i].first;
                const std::uint32_t end = i + 1 < table.size() ? table[i + 1].first : size;
                if (begin < 8 || end < begin + 4 || end > size)
                    throw FormatError("g16: блок " + std::to_string(i) + " за пределами раздела");
                f.chunks_[i].offset = std::uint32_t(pos) + begin;
                f.chunks_[i].size = end - begin;
                f.chunks_[i].method = table[i].second;
            }
        } else if (tag == kTagGPAL) {
            auto raw = in.slice(pos + kPaletteOffset, 256 * 4);
            std::array<std::uint16_t, 256> palette{};
            for (std::size_t i = 0; i < 256; ++i)
                palette[i] = static_cast<std::uint16_t>(((raw[i * 4] & 0xF0) << 4) | (raw[i * 4 + 1] & 0xF0) |
                                                        (raw[i * 4 + 2] >> 4));
            f.palette_ = palette;
        } else if (tag == kTagGINF) {
            f.info_ = in.read<std::uint16_t>(pos + kInfoValueOffset);
        }
        pos += size;
    }
    if (!haveMain) throw FormatError("g16: нет раздела GU16 или GN16");
    return f;
}

std::uint16_t G16File::frameWidth(std::size_t frame) const { return frames_.at(frame).width; }
std::uint16_t G16File::frameHeight(std::size_t frame) const { return frames_.at(frame).height; }

std::vector<G16Tile> G16File::decodeTiles(std::size_t frame) const {
    if (frame >= frames_.size()) throw FormatError("g16: нет кадра " + std::to_string(frame));
    const Frame& fr = frames_[frame];
    const Chunk& c = chunks_[fr.chunk];
    ByteReader in(data_);
    // Первое u32 блока — размер буфера плиток после декодирования; в разборе не нужен.
    const auto payload = in.slice(c.offset + 4, c.size - 4);

    std::vector<std::uint8_t> unpacked;
    std::span<const std::uint8_t> body;
    switch (c.method & 3) {
    case 0:
        body = payload;
        break;
    case 2:
        unpacked = lzo1xDecompress(payload, maxUnpacked_);
        body = unpacked;
        break;
    default:
        // Метод 1 оригинал тоже не поддерживает для этих цветов: такие блоки не загружаются.
        throw FormatError("g16: метод сжатия блока " + std::to_string(c.method) + " не поддерживается");
    }
    const bool direct = (c.method & 0xC) == 0xC;
    if (!direct && (c.method & 0xC) != 0)
        throw FormatError("g16: способ хранения цветов " + std::to_string(c.method) + " не поддерживается");
    if (!direct && !palette_) throw FormatError("g16: блок с палитрой, но нет раздела GPAL");

    // Начало блока: u32 смещение цветов (от байта 4), u32 смещение альфы (от байта 8),
    // затем по кадрам: u16 число плиток и их описатели.
    ByteReader b(body);
    std::size_t colorPos = 4 + std::size_t(b.read<std::uint32_t>(0));
    std::size_t alphaPos = 8 + std::size_t(b.read<std::uint32_t>(4));
    std::size_t tablePos = 8;

    std::vector<G16Tile> result;
    for (std::uint32_t f = 0; f <= fr.indexInChunk; ++f) {
        const auto tiles = b.read<std::uint16_t>(tablePos);
        tablePos += 2;
        const bool wanted = f == fr.indexInChunk;
        for (std::uint32_t t = 0; t < tiles; ++t) {
            const auto desc = b.read<std::uint32_t>(tablePos);
            tablePos += 4;
            const unsigned shift = desc >> 28;
            if (shift > 10) throw FormatError("g16: плитка слишком велика");
            G16Tile tile{signed12(desc >> 12), signed12(desc), 1u << shift, {}};
            const std::size_t rowBytes = (tile.size + 1) / 2;
            const auto alpha = b.slice(alphaPos, rowBytes * tile.size);
            alphaPos += alpha.size();
            if (wanted) tile.pixels.assign(std::size_t(tile.size) * tile.size, 0);

            // Байт альфы — два пикселя (старшая тетрада — левый); цвет хранится только
            // у пикселей с ненулевой альфой.
            for (std::uint32_t y = 0; y < tile.size; ++y) {
                for (std::uint32_t x = 0; x < tile.size; ++x) {
                    const std::uint8_t a = alpha[y * rowBytes + x / 2];
                    const std::uint32_t nibble = (x % 2 == 0) ? (a >> 4) : (a & 0xF);
                    if (nibble == 0) continue;
                    std::uint16_t color;
                    if (direct) {
                        color = b.read<std::uint16_t>(colorPos);
                        colorPos += 2;
                    } else {
                        color = (*palette_)[b.read<std::uint8_t>(colorPos)];
                        colorPos += 1;
                    }
                    if (wanted) tile.pixels[y * tile.size + x] = static_cast<std::uint16_t>((nibble << 12) | color);
                }
            }
            if (wanted) result.push_back(std::move(tile));
        }
    }
    return result;
}

Image G16File::decodeFrame(std::size_t frame) const {
    const std::vector<G16Tile> tiles = decodeTiles(frame);
    Image img(frameWidth(frame), frameHeight(frame));
    for (const G16Tile& t : tiles) {
        for (std::uint32_t y = 0; y < t.size; ++y) {
            const std::int64_t py = std::int64_t(t.y) + y;
            if (py < 0 || py >= img.height) continue;
            for (std::uint32_t x = 0; x < t.size; ++x) {
                const std::int64_t px = std::int64_t(t.x) + x;
                if (px < 0 || px >= img.width) continue;
                const std::uint16_t v = t.pixels[y * t.size + x];
                if ((v >> 12) == 0) continue; // прозрачные пиксели не затирают соседние плитки
                std::uint8_t* dst = img.pixel(std::uint32_t(px), std::uint32_t(py));
                dst[0] = static_cast<std::uint8_t>(((v >> 8) & 0xF) * 17);
                dst[1] = static_cast<std::uint8_t>(((v >> 4) & 0xF) * 17);
                dst[2] = static_cast<std::uint8_t>((v & 0xF) * 17);
                dst[3] = static_cast<std::uint8_t>((v >> 12) * 17);
            }
        }
    }
    return img;
}

} // namespace oc2::assets
