#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "assets/image.hpp"

namespace oc2::assets {

// Пакет спрайтов .g16 (сигнатуры GU16 и GN16) и его сжатый вариант .g17.
// Формат: docs/formats/g16.md. Кадр собирается из квадратных плиток.

struct G16Tile {
    std::int32_t x, y;  // положение в кадре (может быть отрицательным)
    std::uint32_t size; // сторона квадрата, степень двойки
    std::vector<std::uint16_t> pixels; // size × size, ARGB4444
};

class G16File {
public:
    enum class Kind { Uniform, NonUniform }; // GU16 — все кадры одного размера, GN16 — разные

    // Разбирает .g16 или .g17 (сжатый .g16) и проверяет таблицы; бросает FormatError.
    static G16File parse(std::vector<std::uint8_t> data);

    Kind kind() const { return kind_; }
    std::size_t frameCount() const { return frames_.size(); }
    std::uint16_t frameWidth(std::size_t frame) const;
    std::uint16_t frameHeight(std::size_t frame) const;
    // Значение из раздела GINF (назначение пока не установлено).
    std::optional<std::uint16_t> infoValue() const { return info_; }

    // Плитки кадра. Распаковывается весь блок, в котором лежит кадр.
    std::vector<G16Tile> decodeTiles(std::size_t frame) const;
    // Кадр, собранный из плиток, размером frameWidth × frameHeight.
    Image decodeFrame(std::size_t frame) const;

private:
    struct Frame {
        std::uint16_t width, height;
        std::uint32_t chunk;
        std::uint32_t indexInChunk;
    };
    struct Chunk {
        std::uint32_t offset, size; // от начала данных, без u32 размера распаковки
        std::uint8_t method;        // биты 0–1 — сжатие, 2–3 — цвета
        std::uint32_t firstFrame, frameCount;
    };

    std::vector<std::uint8_t> data_;
    Kind kind_ = Kind::Uniform;
    std::vector<Frame> frames_;
    std::vector<Chunk> chunks_;
    std::optional<std::array<std::uint16_t, 256>> palette_; // ARGB4444 без альфы
    std::optional<std::uint16_t> info_;
    std::uint32_t maxUnpacked_ = 0; // верхняя граница распакованного блока (из заголовка)
};

} // namespace oc2::assets
