#include "render/sprite_sheet.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace oc2::render {

SpriteSheet::SpriteSheet(SpriteRenderer& renderer, assets::G2dFile file)
    : renderer_(renderer), file_(std::move(file)) {
    pages_.reserve(file_.pages().size());
    for (std::size_t i = 0; i < file_.pages().size(); ++i) {
        const assets::G2dPage& page = file_.pages()[i];
        // Пустые страницы встречаются; вместо них — прозрачный пиксель, чтобы номера совпадали.
        if (page.width == 0 || page.height == 0) {
            assets::Image empty(1, 1);
            pages_.push_back(renderer_.createTexture(empty));
        } else {
            pages_.push_back(renderer_.createTexture(file_.decodePage(i)));
        }
    }
}

void SpriteSheet::draw(std::size_t frame, float x, float y, float scale, std::uint32_t tint, bool mirror) const {
    if (frame >= frameCount()) throw std::out_of_range("нет кадра " + std::to_string(frame));
    const assets::G2dFrame& fr = file_.frames()[frame];
    std::vector<SpriteVertex> vertices;
    for (std::uint32_t pi = fr.firstPart; pi < fr.firstPart + fr.partCount; ++pi) {
        const assets::G2dPart& part = file_.parts()[pi];
        const assets::G2dPage& page = file_.pages()[part.page];
        if (page.width == 0 || page.height == 0) continue;

        vertices.clear();
        for (std::uint32_t k = 0; k < part.vertexCount; ++k) {
            const assets::G2dVertex& v = file_.vertices()[part.firstVertex + k];
            SpriteVertex sv{};
            sv.x = x + float(mirror ? int(width()) - v.x : v.x) * scale;
            sv.y = y + float(v.y) * scale;
            sv.u = float(v.u) / float(page.width);
            sv.v = float(v.v) / float(page.height);
            // ARGB → RGBA с умножением на tint
            auto mul = [](std::uint32_t a, std::uint32_t b) { return std::uint8_t((a * b + 127) / 255); };
            sv.color = {mul((v.color >> 16) & 0xFF, (tint >> 16) & 0xFF), mul((v.color >> 8) & 0xFF, (tint >> 8) & 0xFF),
                        mul(v.color & 0xFF, tint & 0xFF), mul(v.color >> 24, tint >> 24)};
            vertices.push_back(sv);
        }
        std::span<const std::uint16_t> idx(file_.indices().data() + part.firstIndex,
                                           std::size_t(part.triangleCount) * 3);
        renderer_.drawTriangles(pages_[part.page], vertices, idx);
    }
}

} // namespace oc2::render
