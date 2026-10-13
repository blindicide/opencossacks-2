#pragma once

#include <cstddef>
#include <vector>

#include "assets/g2d.hpp"
#include "render/sprite_renderer.hpp"

namespace oc2::render {

// Пакет спрайтов .g2d, загруженный в видеопамять: страницы — текстуры,
// кадры рисуются своими треугольными частями.
class SpriteSheet {
public:
    SpriteSheet(SpriteRenderer& renderer, assets::G2dFile file);

    std::size_t frameCount() const { return file_.frames().size(); }
    // Размер спрайта (рамка, в которой заданы координаты кадров).
    std::uint16_t width() const { return file_.width(); }
    std::uint16_t height() const { return file_.height(); }

    // Рисует кадр так, что левый верхний угол рамки спрайта — в (x, y) пикселях экрана.
    // tint — ARGB-множитель цвета (0xFFFFFFFF — без изменений); mirror — отражение
    // по горизонтали в пределах рамки спрайта.
    void draw(std::size_t frame, float x, float y, float scale = 1.0f, std::uint32_t tint = 0xFFFFFFFF,
              bool mirror = false) const;

private:
    SpriteRenderer& renderer_;
    assets::G2dFile file_;
    std::vector<TextureId> pages_;
};

} // namespace oc2::render
