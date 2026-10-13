#include "assets/map3d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "assets/byte_reader.hpp"
#include "assets/compression.hpp"

namespace oc2::assets {

namespace {

bool isTagChar(std::uint8_t c) { return c >= 0x20 && c < 0x7F; }

} // namespace

const MapSection* Map3d::section(std::string_view tag) const {
    for (const MapSection& s : sections_)
        if (s.tag == tag) return &s;
    return nullptr;
}

std::array<int, 8> Map3d::startResources(int player) const {
    std::array<int, 8> r{};
    const MapSection* s = section("RESU");
    if (!s || s->size < 256 || player < 0 || player > 7) return r;
    const ByteReader reader(sectionData(*s));
    for (std::size_t i = 0; i < 8; ++i) r[i] = reader.read<std::int32_t>((std::size_t(player) * 8 + i) * 4);
    return r;
}

Image Map3d::preview() const {
    constexpr std::uint32_t kW = 256, kH = 240;
    const MapSection* v = section("VIE2");
    if (!v || v->size != kW * kH * 2) return {};
    const ByteReader r(sectionData(*v));
    Image img(kW, kH);
    for (std::uint32_t i = 0; i < kW * kH; ++i) {
        const auto c = r.read<std::uint16_t>(std::size_t(i) * 2);
        std::uint8_t* p = &img.rgba[std::size_t(i) * 4];
        p[0] = std::uint8_t(((c >> 8) & 15) * 17);
        p[1] = std::uint8_t(((c >> 4) & 15) * 17);
        p[2] = std::uint8_t((c & 15) * 17);
        p[3] = 255;
    }
    return img;
}

int Map3d::heightAt(int x, int y) const {
    // Дословный перенос FUN_00ed4600: у чётных столбцов вершина ниже на полклетки,
    // поэтому клетка режется на треугольники по-разному для чётных и нечётных столбцов.
    const int limit = rows_ * 15 / 16 * kCell; // 0xF0 << сдвиг размера карты, в клетках × 32
    if (limit < 2 * kCell) return heights_.empty() ? 0 : heights_[0]; // крошечная карта
    x = std::clamp(x, 0, limit);
    y = std::clamp(y, kCell, limit);
    const int w = vertsInLine_;
    const auto h = [&](int i) { return int(heights_[std::size_t(i)]); };
    const int col = x >> 5;
    const int fx = x & 31;
    if ((col & 1) == 0) {
        const int half = fx >> 1;
        const int row = (y + half) >> 5;
        if (((y + half) & 31) < fx) {
            const int i = row * w + col;
            const int a = h(i), b = h(i + 1), c = h(i + 1 + w);
            return (((x - col * 32) * (((b + c) >> 1) - a)) >> 5) + a + (((y - row * 32) * (c - b)) >> 5);
        }
        const int i = row * w + col;
        const int a = h(i), b = h(i + w), c = h(i + w + 1);
        return (c - (((x - (col * 32 + 32)) * (((a + b) >> 1) - c)) >> 5)) + (((y - (row * 32 + 16)) * (b - a)) >> 5);
    }
    const int half = 15 - (fx >> 1);
    const int row = (y + half) >> 5;
    if (32 - fx < ((y + half) & 31)) {
        const int i = col + 1 + row * w;
        const int a = h(i + w - 1), b = h(i), c = h(i + w);
        return (((x - col * 32) * (((b + c) >> 1) - a)) >> 5) + a + (((y - (row * 32 + 16)) * (c - b)) >> 5);
    }
    const int i = row * w + col;
    const int b = h(i + 1), a = h(i), c = h(i + w);
    return (b - (((x - (col * 32 + 32)) * (((a + c) >> 1) - b)) >> 5)) + (((y - row * 32) * (c - a)) >> 5);
}

Map3d Map3d::parse(std::span<const std::uint8_t> file) {
    Map3d m;
    // Обычно карта сжата; автосохранение редактора (Models\MapAutosave.m3d) лежит как есть.
    if (file.size() >= 4 && std::memcmp(file.data(), "3DMP", 4) == 0)
        m.data_.assign(file.begin(), file.end());
    else
        m.data_ = unpackBlock(file);
    const ByteReader r(m.data_);
    if (m.data_.size() < 12 || std::memcmp(m.data_.data(), "3DMP", 4) != 0)
        throw FormatError("это не карта .m3d (нет метки 3DMP)");

    // Разделы идут до конца данных; в конце некоторых карт — мусор без метки,
    // оригинал на нём тоже останавливается (неизвестная метка — выход из цикла).
    std::size_t p = 12;
    while (p + 8 <= m.data_.size()) {
        const std::uint8_t* t = m.data_.data() + p;
        if (!isTagChar(t[0]) || !isTagChar(t[1]) || !isTagChar(t[2]) || !isTagChar(t[3])) break;
        const auto size = r.read<std::uint32_t>(p + 4);
        if (size < 4 || size - 4 > m.data_.size() - (p + 8)) break;
        m.sections_.push_back({std::string(reinterpret_cast<const char*>(t), 4), p + 8, size - 4});
        p += 4 + size;
    }

    // Размер сетки — по разделу высот: (N + 1) · N значений int16 (FUN_00f98170).
    const MapSection* surf = m.section("SURF");
    if (!surf) throw FormatError("в карте нет рельефа (раздел SURF)");
    const std::size_t count = surf->size / 2;
    const auto n = static_cast<int>(std::lround((std::sqrt(4.0 * double(count) + 1.0) - 1.0) / 2.0));
    if (n <= 1 || std::size_t(n + 1) * std::size_t(n) != count)
        throw FormatError("размер раздела SURF не соответствует квадратной сетке");
    m.vertsInLine_ = n + 1;
    m.rows_ = n;

    m.heights_.resize(count);
    std::memcpy(m.heights_.data(), m.data_.data() + surf->offset, count * 2);

    if (const MapSection* tile = m.section("TILE")) {
        if (tile->size < count) throw FormatError("раздел TILE короче сетки");
        m.facture_.assign(m.data_.begin() + std::ptrdiff_t(tile->offset),
                          m.data_.begin() + std::ptrdiff_t(tile->offset + count));
    } else {
        m.facture_.assign(count, 0);
    }
    // TIL2: два массива по байту на вершину (FUN_00f98790).
    if (const MapSection* t2 = m.section("TIL2"); t2 && t2->size >= count * 2) {
        const auto base = m.data_.begin() + std::ptrdiff_t(t2->offset);
        m.facture2_.assign(base, base + std::ptrdiff_t(count));
        m.blend2_.assign(base + std::ptrdiff_t(count), base + std::ptrdiff_t(count * 2));
    }
    // TRE2: u32 количество, записи «u16 вид, i32 x, i32 y, u16 номер, u16 признаки,
    // u8 есть ли данные, [64 байта данных]».
    if (const MapSection* tre = m.section("TRE2"); tre && tre->size >= 4) {
        const ByteReader t(m.sectionData(*tre));
        const auto n = t.read<std::uint32_t>(0);
        std::size_t p = 4;
        for (std::uint32_t i = 0; i < n; ++i) {
            if (p + 15 > tre->size) throw FormatError("раздел TRE2 короче заявленного числа объектов");
            MapObject o;
            o.kind[0] = char(t.read<std::uint8_t>(p));
            o.kind[1] = char(t.read<std::uint8_t>(p + 1));
            o.x = t.read<std::int32_t>(p + 2);
            o.y = t.read<std::int32_t>(p + 6);
            o.type = t.read<std::uint16_t>(p + 10);
            o.flags = t.read<std::uint16_t>(p + 12);
            o.hasExtra = t.read<std::uint8_t>(p + 14) != 0;
            p += 15 + (o.hasExtra ? 64 : 0);
            m.objects_.push_back(o);
        }
    }
    // UNI3: u32 количество, записи «u8 игрок, u16 номер, i32 x·16, i32 y·16, u16 жизнь,
    // u16, i16, i16, u8 направление, u8 признаки, char[33] тип».
    if (const MapSection* uni = m.section("UNI3"); uni && uni->size >= 4) {
        const ByteReader t(m.sectionData(*uni));
        const auto n = t.read<std::uint32_t>(0);
        if (std::uint64_t(n) * 54 > uni->size - 4) throw FormatError("раздел UNI3 короче заявленного числа юнитов");
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::size_t p = 4 + std::size_t(i) * 54;
            MapUnit u;
            u.player = t.read<std::uint8_t>(p);
            u.id = t.read<std::uint16_t>(p + 1);
            u.x = t.read<std::int32_t>(p + 3) >> 4;
            u.y = t.read<std::int32_t>(p + 7) >> 4;
            u.life = t.read<std::uint16_t>(p + 11);
            u.field2 = t.read<std::uint16_t>(p + 13);
            u.field3 = t.read<std::int16_t>(p + 15);
            u.field4 = t.read<std::int16_t>(p + 17);
            u.direction = t.read<std::uint8_t>(p + 19);
            u.flags = t.read<std::uint8_t>(p + 20);
            const auto name = t.slice(p + 21, 33);
            u.type.assign(reinterpret_cast<const char*>(name.data()),
                          std::find(name.begin(), name.end(), std::uint8_t(0)) - name.begin());
            m.units_.push_back(std::move(u));
        }
    }
    return m;
}

} // namespace oc2::assets
