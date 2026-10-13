#include "assets/g2d.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>

#include "assets/byte_reader.hpp"
#include "assets/compression.hpp"

namespace oc2::assets {

namespace {

constexpr std::uint32_t kMagic = 0x44325547; // "GU2D"
constexpr std::size_t kFrameSize = 8;
constexpr std::size_t kPartSize = 18;
constexpr std::size_t kPageSize = 32;
constexpr std::uint32_t kNeutralColor = 0xFF808080;

// Палитра кодировок 1 и 2: байт «число цветов − 1», затем RGB по 3 байта.
// Возвращает ARGB4444 без альфы и смещение данных за палитрой.
std::pair<std::array<std::uint16_t, 256>, std::size_t> readPalette(const ByteReader& in) {
    std::array<std::uint16_t, 256> palette{};
    std::size_t count = std::size_t(in.read<std::uint8_t>(0)) + 1;
    auto rgb = in.slice(1, count * 3);
    for (std::size_t i = 0; i < count; ++i) {
        palette[i] = static_cast<std::uint16_t>(((rgb[i * 3] & 0xF0) << 4) | (rgb[i * 3 + 1] & 0xF0) |
                                                (rgb[i * 3 + 2] >> 4));
    }
    return {palette, 1 + count * 3};
}

std::vector<std::uint16_t> decodePixels(std::span<const std::uint8_t> src, std::size_t count,
                                        std::uint8_t encoding) {
    ByteReader in(src);
    std::vector<std::uint16_t> out(count, 0);

    switch (encoding) {
    case 0: {
        auto raw = in.slice(0, count * 2);
        std::memcpy(out.data(), raw.data(), raw.size());
        return out;
    }
    case 1: {
        // По байту индекса на пиксель, затем по тетраде альфы (старшая — чётный пиксель).
        auto [palette, pos] = readPalette(in);
        auto index = in.slice(pos, count);
        auto alpha = in.slice(pos + count, count / 2);
        // Как в оригинале, обрабатываются только пары; у нечётного последнего пикселя альфы нет.
        for (std::size_t j = 0; j < count / 2; ++j) {
            std::uint8_t a = alpha[j];
            out[j * 2] = static_cast<std::uint16_t>(((a & 0xF0) << 8) | palette[index[j * 2]]);
            out[j * 2 + 1] = static_cast<std::uint16_t>(((a & 0x0F) << 12) | palette[index[j * 2 + 1]]);
        }
        return out;
    }
    case 2: {
        // Байт: биты 0–2 — альфа, 3–7 — индекс. Альфа 0 с ненулевым индексом —
        // не пиксель, а число следующих пикселей с младшим битом альфы.
        auto [palette, pos] = readPalette(in);
        auto stream = src.subspan(std::min(pos, src.size()));
        std::size_t k = 0;
        std::uint32_t lowBitRun = 0;
        for (std::size_t i = 0; i < count;) {
            if (k >= stream.size()) throw FormatError("g2d: поток пикселей закончился раньше времени");
            std::uint8_t b = stream[k++];
            std::uint32_t a = b & 7u;
            std::uint32_t idx = b >> 3;
            if (a == 0 && idx != 0) {
                lowBitRun = idx;
                continue;
            }
            std::uint32_t alpha = a << 1;
            if (lowBitRun != 0) {
                alpha |= 1;
                --lowBitRun;
            }
            out[i++] = static_cast<std::uint16_t>((alpha << 12) | palette[idx]);
        }
        return out;
    }
    default:
        throw FormatError("g2d: неизвестная кодировка страницы " + std::to_string(encoding));
    }
}

// Ориентированная площадь (удвоенная) — функция ребра для растеризации.
double edge(double ax, double ay, double bx, double by, double px, double py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Правило «верхнее-левое ребро»: пиксель на общем ребре двух треугольников
// закрашивается ровно один раз (для треугольника с положительной площадью).
bool topLeft(double ax, double ay, double bx, double by) {
    return (ay == by && bx < ax) || (by < ay);
}

void blendOver(std::uint8_t* dst, const std::array<double, 4>& src) {
    double sa = src[3] / 255.0;
    if (sa <= 0.0) return;
    double da = dst[3] / 255.0;
    double oa = sa + da * (1.0 - sa);
    for (int c = 0; c < 3; ++c) {
        double v = (src[c] * sa + dst[c] * da * (1.0 - sa)) / oa;
        dst[c] = static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
    }
    dst[3] = static_cast<std::uint8_t>(std::clamp(std::lround(oa * 255.0), 0L, 255L));
}

} // namespace

void argb4444ToRgba8(std::uint16_t argb, std::uint8_t* rgba) {
    rgba[0] = static_cast<std::uint8_t>(((argb >> 8) & 0xF) * 17);
    rgba[1] = static_cast<std::uint8_t>(((argb >> 4) & 0xF) * 17);
    rgba[2] = static_cast<std::uint8_t>((argb & 0xF) * 17);
    rgba[3] = static_cast<std::uint8_t>(((argb >> 12) & 0xF) * 17);
}

G2dFile G2dFile::parse(std::vector<std::uint8_t> data) {
    G2dFile f;
    f.data_ = std::move(data);
    ByteReader in(f.data_);

    if (in.read<std::uint32_t>(0) != kMagic) throw FormatError("g2d: нет сигнатуры GU2D");
    auto frameCount = in.read<std::uint32_t>(0x08);
    auto vertexOffset = in.read<std::uint32_t>(0x0C);
    auto vertexCount = in.read<std::uint32_t>(0x10);
    auto indexOffset = in.read<std::uint32_t>(0x14);
    auto indexCount = in.read<std::uint32_t>(0x18);
    auto partOffset = in.read<std::uint32_t>(0x1C);
    auto partCount = in.read<std::uint32_t>(0x20);
    auto pageOffset = in.read<std::uint32_t>(0x24);
    f.pixelsOffset_ = in.read<std::uint32_t>(0x28);
    auto pageCount = in.read<std::uint32_t>(0x2C);
    f.version_ = in.read<std::uint32_t>(0x30);
    f.width_ = in.read<std::uint16_t>(0x34);
    f.height_ = in.read<std::uint16_t>(0x36);

    // В версии 5 нет байта флагов, и заголовок на байт короче.
    std::uint8_t flags = 0;
    std::size_t headerSize = 0x39;
    if (f.version_ >= 6) {
        flags = in.read<std::uint8_t>(0x39);
        headerSize = 0x3A;
    }
    const std::size_t vertexSize = (flags & 1) ? 14 : 10;

    in.requireTable(headerSize, frameCount, kFrameSize);
    in.requireTable(vertexOffset, vertexCount, vertexSize);
    in.requireTable(indexOffset, indexCount, 2);
    in.requireTable(partOffset, partCount, kPartSize);
    in.requireTable(pageOffset, pageCount, kPageSize);

    f.frames_.reserve(frameCount);
    for (std::uint32_t i = 0; i < frameCount; ++i) {
        std::size_t at = headerSize + i * kFrameSize;
        G2dFrame fr{in.read<std::uint32_t>(at), in.read<std::uint32_t>(at + 4)};
        if (fr.firstPart > partCount || fr.partCount > partCount - fr.firstPart)
            throw FormatError("g2d: кадр " + std::to_string(i) + " ссылается на несуществующие части");
        f.frames_.push_back(fr);
    }

    f.pages_.reserve(pageCount);
    for (std::uint32_t i = 0; i < pageCount; ++i) {
        std::size_t at = pageOffset + i * kPageSize;
        auto format = in.read<std::uint16_t>(at + 4);
        G2dPage pg{in.read<std::uint16_t>(at), in.read<std::uint16_t>(at + 2),
                   static_cast<std::uint8_t>(format & 0xFF), static_cast<std::uint8_t>(format >> 8),
                   in.read<std::uint32_t>(at + 8), in.read<std::uint32_t>(at + 12)};
        if (std::uint64_t(f.pixelsOffset_) + pg.offset > in.size())
            throw FormatError("g2d: данные страницы " + std::to_string(i) + " за пределами файла");
        f.pages_.push_back(pg);
    }

    f.indices_.resize(indexCount);
    if (indexCount != 0) {
        auto raw = in.slice(indexOffset, std::size_t(indexCount) * 2);
        std::memcpy(f.indices_.data(), raw.data(), raw.size());
    }

    f.vertices_.reserve(vertexCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i) {
        std::size_t at = vertexOffset + i * vertexSize;
        G2dVertex v{in.read<std::int16_t>(at), in.read<std::int16_t>(at + 2), in.read<std::int16_t>(at + 4),
                    in.read<std::int16_t>(at + 6), in.read<std::int16_t>(at + 8),
                    (flags & 1) ? in.read<std::uint32_t>(at + 10) : kNeutralColor};
        f.vertices_.push_back(v);
    }

    f.parts_.reserve(partCount);
    for (std::uint32_t i = 0; i < partCount; ++i) {
        std::size_t at = partOffset + i * kPartSize;
        G2dPart p{in.read<std::uint16_t>(at), in.read<std::uint32_t>(at + 2), in.read<std::uint32_t>(at + 6),
                  in.read<std::uint32_t>(at + 10), in.read<std::uint32_t>(at + 14)};
        const std::string where = "g2d: часть " + std::to_string(i);
        if (p.page >= pageCount) throw FormatError(where + " ссылается на несуществующую страницу");
        if (p.firstVertex > vertexCount || p.vertexCount > vertexCount - p.firstVertex)
            throw FormatError(where + " ссылается на несуществующие вершины");
        std::uint64_t idxEnd = std::uint64_t(p.firstIndex) + std::uint64_t(p.triangleCount) * 3;
        if (idxEnd > indexCount) throw FormatError(where + " ссылается на несуществующие индексы");
        for (std::uint64_t k = p.firstIndex; k < idxEnd; ++k)
            if (f.indices_[k] >= p.vertexCount) throw FormatError(where + ": индекс вершины вне части");
        f.parts_.push_back(p);
    }
    return f;
}

std::vector<std::uint16_t> G2dFile::decodePageArgb4444(std::size_t page) const {
    if (page >= pages_.size()) throw FormatError("g2d: нет страницы " + std::to_string(page));
    const G2dPage& pg = pages_[page];
    std::span<const std::uint8_t> all(data_);
    // Размер из таблицы страниц не всегда точен, поэтому данные берутся до конца файла;
    // декодеры сами проверяют границы.
    auto src = all.subspan(pixelsOffset_ + pg.offset);
    const std::size_t count = std::size_t(pg.width) * pg.height;
    if (pg.compression != 0) {
        std::vector<std::uint8_t> raw = unpackBlock(src);
        return decodePixels(raw, count, pg.encoding);
    }
    return decodePixels(src, count, pg.encoding);
}

Image G2dFile::decodePage(std::size_t page) const {
    auto argb = decodePageArgb4444(page);
    Image img(pages_[page].width, pages_[page].height);
    for (std::size_t i = 0; i < argb.size(); ++i) argb4444ToRgba8(argb[i], &img.rgba[i * 4]);
    return img;
}

Image G2dFile::renderFrame(std::size_t frame) const {
    if (frame >= frames_.size()) throw FormatError("g2d: нет кадра " + std::to_string(frame));
    Image img(width_, height_);
    std::map<std::size_t, Image> cache; // страницы, уже декодированные для этого кадра

    const G2dFrame& fr = frames_[frame];
    for (std::uint32_t pi = fr.firstPart; pi < fr.firstPart + fr.partCount; ++pi) {
        const G2dPart& part = parts_[pi];
        auto it = cache.find(part.page);
        if (it == cache.end()) it = cache.emplace(part.page, decodePage(part.page)).first;
        const Image& tex = it->second;
        if (tex.width == 0 || tex.height == 0) continue;

        for (std::uint32_t t = 0; t < part.triangleCount; ++t) {
            std::array<const G2dVertex*, 3> v{};
            for (int k = 0; k < 3; ++k)
                v[k] = &vertices_[part.firstVertex + indices_[part.firstIndex + t * 3 + k]];
            double area = edge(v[0]->x, v[0]->y, v[1]->x, v[1]->y, v[2]->x, v[2]->y);
            if (area == 0.0) continue;
            if (area < 0.0) {
                std::swap(v[1], v[2]);
                area = -area;
            }

            int x0 = std::max(0, int(std::floor(std::min({v[0]->x, v[1]->x, v[2]->x}))));
            int y0 = std::max(0, int(std::floor(std::min({v[0]->y, v[1]->y, v[2]->y}))));
            int x1 = std::min(int(width_) - 1, int(std::max({v[0]->x, v[1]->x, v[2]->x})));
            int y1 = std::min(int(height_) - 1, int(std::max({v[0]->y, v[1]->y, v[2]->y})));

            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5, py = y + 0.5;
                    std::array<double, 3> w{
                        edge(v[1]->x, v[1]->y, v[2]->x, v[2]->y, px, py),
                        edge(v[2]->x, v[2]->y, v[0]->x, v[0]->y, px, py),
                        edge(v[0]->x, v[0]->y, v[1]->x, v[1]->y, px, py),
                    };
                    bool inside = true;
                    for (int k = 0; k < 3 && inside; ++k) {
                        const G2dVertex* a = v[(k + 1) % 3];
                        const G2dVertex* b = v[(k + 2) % 3];
                        inside = w[k] > 0.0 || (w[k] == 0.0 && topLeft(a->x, a->y, b->x, b->y));
                    }
                    if (!inside) continue;

                    double u = 0.0, vv = 0.0;
                    std::array<double, 4> color{0.0, 0.0, 0.0, 0.0};
                    for (int k = 0; k < 3; ++k) {
                        double b = w[k] / area;
                        u += b * v[k]->u;
                        vv += b * v[k]->v;
                        color[0] += b * ((v[k]->color >> 16) & 0xFF);
                        color[1] += b * ((v[k]->color >> 8) & 0xFF);
                        color[2] += b * (v[k]->color & 0xFF);
                        color[3] += b * (v[k]->color >> 24);
                    }
                    // Вершины задают углы пикселей, поэтому центр пикселя даёт u + 0.5.
                    auto tx = std::uint32_t(std::clamp(int(std::floor(u)), 0, int(tex.width) - 1));
                    auto ty = std::uint32_t(std::clamp(int(std::floor(vv)), 0, int(tex.height) - 1));
                    const std::uint8_t* texel = tex.pixel(tx, ty);
                    std::array<double, 4> src{};
                    for (int c = 0; c < 3; ++c) src[c] = std::min(255.0, texel[c] * color[c] / 128.0);
                    src[3] = texel[3] * color[3] / 255.0;
                    blendOver(img.pixel(std::uint32_t(x), std::uint32_t(y)), src);
                }
            }
        }
    }
    return img;
}

} // namespace oc2::assets
