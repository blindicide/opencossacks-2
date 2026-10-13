#pragma once

#include <filesystem>
#include <vector>

#include "assets/map3d.hpp"
#include "render/gp_images.hpp"
#include "render/terrain_renderer.hpp"

namespace oc2::render {

// Спрайт-объекты карты (раздел TRE2): деревья, камни, ямы, стены.
// Положение — как в выводе объектов оригинала (FUN_00e468e0):
// левый верхний угол кадра = (x − dx, y/2 − dy − z), где (dx, dy) — привязка
// из списка .lst, z — высота поверхности в точке объекта.
// Порядок вывода — по мировому Y (дальние раньше).
class MapObjects {
public:
    MapObjects(GpImages& images, const std::filesystem::path& dataRoot, const assets::Map3d& map);

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
    };

    GpImages& images_;
    std::vector<Item> items_;
};

} // namespace oc2::render
