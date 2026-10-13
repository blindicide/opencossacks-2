#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "assets/image.hpp"

namespace oc2::assets {

// Пакет спрайтов .g2d (сигнатура GU2D). Формат: docs/formats/g2d.md.
// Кадр состоит из частей; часть — треугольная сетка, натянутая на одну страницу.

struct G2dVertex {
    std::int16_t x, y, z; // положение в координатах спрайта
    std::int16_t u, v;    // пиксель на странице
    std::uint32_t color;  // ARGB; 0xFF808080 — нейтральный
};

struct G2dPart {
    std::uint16_t page;
    std::uint32_t firstVertex, vertexCount;
    std::uint32_t firstIndex, triangleCount;
};

struct G2dFrame {
    std::uint32_t firstPart, partCount;
};

struct G2dPage {
    std::uint16_t width, height;
    std::uint8_t compression; // 0 — без сжатия, иначе блок со стандартным заголовком
    std::uint8_t encoding;    // 0 — ARGB4444, 1 — палитра и альфа, 2 — палитра и поток
    std::uint32_t offset;     // от начала области пикселей
    std::uint32_t size;
};

class G2dFile {
public:
    // Разбирает файл и проверяет все ссылки между таблицами; бросает FormatError.
    static G2dFile parse(std::vector<std::uint8_t> data);

    std::uint32_t version() const { return version_; }
    std::uint16_t width() const { return width_; }
    std::uint16_t height() const { return height_; }

    const std::vector<G2dFrame>& frames() const { return frames_; }
    const std::vector<G2dPart>& parts() const { return parts_; }
    const std::vector<G2dVertex>& vertices() const { return vertices_; }
    const std::vector<std::uint16_t>& indices() const { return indices_; }
    const std::vector<G2dPage>& pages() const { return pages_; }

    // Пиксели страницы в исходном виде ARGB4444.
    std::vector<std::uint16_t> decodePageArgb4444(std::size_t page) const;
    // Страница в RGBA8.
    Image decodePage(std::size_t page) const;

    // Собирает кадр в изображение размером со спрайт. Цвет вершины умножается
    // на текстуру с удвоением: 0x80 соответствует ровно 1, то есть без изменений.
    // Перекраска в цвет нации не выполняется.
    Image renderFrame(std::size_t frame) const;

private:
    std::vector<std::uint8_t> data_;
    std::uint32_t version_ = 0;
    std::uint16_t width_ = 0, height_ = 0;
    std::uint32_t pixelsOffset_ = 0;
    std::vector<G2dFrame> frames_;
    std::vector<G2dPart> parts_;
    std::vector<G2dVertex> vertices_;
    std::vector<std::uint16_t> indices_;
    std::vector<G2dPage> pages_;
};

// ARGB4444 → RGBA8 (каждые 4 бита растягиваются до 8 умножением на 17).
void argb4444ToRgba8(std::uint16_t argb, std::uint8_t* rgba);

} // namespace oc2::assets
