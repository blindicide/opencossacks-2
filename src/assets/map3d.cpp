#include "assets/map3d.hpp"

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
    return m;
}

} // namespace oc2::assets
