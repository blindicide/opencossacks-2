#pragma once

#include <cstdint>
#include <vector>

namespace oc2::assets {

// Изображение RGBA8 (R, G, B, A подряд), строки сверху вниз без выравнивания.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    Image() = default;
    Image(std::uint32_t w, std::uint32_t h) : width(w), height(h), rgba(std::size_t(w) * h * 4, 0) {}

    std::uint8_t* pixel(std::uint32_t x, std::uint32_t y) { return &rgba[(std::size_t(y) * width + x) * 4]; }
    const std::uint8_t* pixel(std::uint32_t x, std::uint32_t y) const {
        return &rgba[(std::size_t(y) * width + x) * 4];
    }
};

} // namespace oc2::assets
