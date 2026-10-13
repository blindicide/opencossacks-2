#pragma once

#include <filesystem>
#include <vector>

#include "assets/map3d.hpp"
#include "render/gp_images.hpp"
#include "render/terrain_renderer.hpp"
#include "world/unit_catalog.hpp"

namespace oc2::render {

// Спрайты карты: объекты TRE2 (деревья, камни, ямы, стены) и юниты UNI3
// (здания, животные, войска) в покое — первый кадр анимации стояния.
// Объекты — как в выводе оригинала (FUN_00e468e0): левый верхний угол кадра =
// (x − dx, y/2 − dy − z), где (dx, dy) — привязка из списка .lst, z — высота
// поверхности в точке объекта. Юниты — (x + ax + fx, y/2 + ay + fy − z), где
// (ax, ay) — SETANMPARAM анимации, (fx, fy) — SHADOW её файла USERLC.
// Порядок вывода — по мировому Y (дальние раньше).
class MapObjects {
public:
    // units — каталог типов юнитов; без него юниты не выводятся.
    MapObjects(GpImages& images, const std::filesystem::path& dataRoot, const assets::Map3d& map,
               world::UnitCatalog* units);

    // Добавляет видимые объекты в очередь спрайтов (рисовать после рельефа).
    void draw(const MapCamera& camera, float screenWidth, float screenHeight);

    std::size_t count() const { return items_.size(); }

private:
    struct Item {
        float x, y;    // левый верхний угол кадра в пикселях проекции
        float w, h;    // размер кадра (для отсечения)
        GpId gp;
        std::uint32_t frame;
        int sortY;     // мировой Y объекта
        bool mirror = false;
    };

    void addUnits(const assets::Map3d& map, world::UnitCatalog& units);

    GpImages& images_;
    std::vector<Item> items_;
};

} // namespace oc2::render
