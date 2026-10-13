#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "render/sprite_renderer.hpp"

namespace oc2::render {

class SpriteSheet;

using GpId = std::uint32_t;

// Изображения «GP» по именам из данных игры («interf3\\Fonts\\FontG16» — без
// расширения). Как в оригинале, ищется .g17, затем .g16, затем .g2d, затем
// копия в кэше Cash/ (имя с «_» вместо «\\»).
// Файл читается при первом обращении, кадры .g16/.g17 попадают в видеопамять
// при первом рисовании.
class GpImages {
public:
    GpImages(SpriteRenderer& renderer, std::filesystem::path dataRoot);
    ~GpImages();
    GpImages(const GpImages&) = delete;
    GpImages& operator=(const GpImages&) = delete;

    // Номер изображения по имени (регистр и вид косой черты не важны).
    GpId id(std::string_view name);

    // false, если файла нет или он не читается (сообщение пишется один раз).
    bool valid(GpId id);
    std::size_t frameCount(GpId id);
    // Размер кадра в пикселях (для .g2d — размер рамки спрайта).
    std::uint32_t frameWidth(GpId id, std::size_t frame);
    std::uint32_t frameHeight(GpId id, std::size_t frame);

    // Рисует кадр левым верхним углом в (x, y). color — ARGB-множитель
    // (0xFFFFFFFF — без изменений); mirror — отражение по горизонтали в пределах рамки.
    void draw(GpId id, std::size_t frame, float x, float y, std::uint32_t color = 0xFFFFFFFF,
              float scale = 1.0f, bool mirror = false);

private:
    struct Entry;
    Entry& load(GpId id);

    SpriteRenderer& renderer_;
    std::filesystem::path root_;
    std::unordered_map<std::string, GpId> ids_;
    std::vector<std::unique_ptr<Entry>> entries_;
};

// ARGB-множитель → цвет вершины спрайта (там 0x80 — без изменений).
std::array<std::uint8_t, 4> tintToVertexColor(std::uint32_t argb);

} // namespace oc2::render
