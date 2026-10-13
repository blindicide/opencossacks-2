#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

#include "assets/map3d.hpp"
#include "render/effects.hpp"
#include "render/gp_images.hpp"
#include "render/sprite_renderer.hpp"
#include "render/terrain_renderer.hpp"
#include "world/world.hpp"

namespace oc2::render {

// Спрайты карты: неподвижные объекты TRE2 (деревья, камни, ямы, стены) и юниты мира.
// Объекты — как в выводе оригинала (FUN_00e468e0): левый верхний угол кадра =
// (x − dx, y/2 − dy − z), где (dx, dy) — привязка из списка .lst, z — высота
// поверхности в точке объекта. Юниты — (x + ax + fx, y/2 + ay + fy − z), где
// (ax, ay) — SETANMPARAM анимации, (fx, fy) — SHADOW её файла USERLC.
// Порядок вывода — по мировому Y (дальние раньше).
// Цвет игрока 0..7 (ARGB).
std::uint32_t playerColor(int player);

class MapObjects {
public:
    // map должна жить дольше MapObjects (по ней считается высота под юнитами).
    MapObjects(SpriteRenderer& renderer, GpImages& images, const std::filesystem::path& dataRoot,
               const assets::Map3d& map);

    // Ставит в очередь спрайтов видимые объекты, юниты world и эффекты (рисовать после рельефа).
    void draw(const MapCamera& camera, float screenWidth, float screenHeight, const world::World& world,
              const Effects* effects = nullptr);

    std::size_t staticCount() const { return items_.size(); }

    // Призрак здания при выборе места: покой (#STANDLO) в точке (x, y), цвет — множитель ARGB.
    void drawGhost(const assets::UnitDesc& desc, int x, int y, const MapCamera& camera, std::uint32_t color);

    // Точка земли под юнитом в пикселях проекции (X = x, Y = y/2 − z).
    float groundX(const world::Unit& u) const { return float(u.x()); }
    float groundY(const world::Unit& u) const { return float((u.y() >> 1) - map_.heightAt(u.x(), u.y())); }

private:
    struct Item {
        float x, y;    // левый верхний угол кадра в пикселях проекции
        float w, h;    // размер кадра (для отсечения)
        GpId gp;
        std::uint32_t frame;
        int sortY;     // мировой Y
        bool mirror = false;
    };

    void appendUnit(const world::Unit& u, std::vector<Item>& out);
    // Кадр анимации описания desc в мировой точке; layer — номер кадра анимации.
    void appendFrame(const assets::UnitDesc& desc, const assets::UnitAnimation& anim, std::size_t layer, int direction,
                     int x, int y, int lift, int sortY, std::vector<Item>& out);
    GpId gpFor(const assets::UnitSpriteFile& file);
    void drawItem(const Item& it, const MapCamera& camera, float right, float bottom, std::uint32_t color = 0xFFFFFFFF);

    SpriteRenderer& renderer_;
    GpImages& images_;
    const assets::Map3d& map_;
    std::vector<Item> items_;   // неподвижные, отсортированы
    std::vector<Item> dynamic_; // юниты текущего кадра
    std::unordered_map<const assets::UnitSpriteFile*, GpId> gpCache_;
    std::optional<TextureId> ring_; // textures\selection\round3.tga (SelType.xml: Round)
};

} // namespace oc2::render
