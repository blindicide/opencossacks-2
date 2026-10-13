#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "assets/map3d.hpp"
#include "assets/unit_desc.hpp"

namespace oc2::world {

// Карта непроходимости: клетки 16×16 мировых единиц, как битовая карта оригинала
// (FUN_00f89f30: 1024×1024 клеток у карты 512×512).
// Отступление: оригинал строит её кодом, который в декомпиляции потерял указатели;
// здесь клетка закрыта, если она под водой глубже 16 (непрозрачность воды ≥ ½,
// FUN_0168e5f0), за краем игровой области (heightAt прижимает точки к 15/16 карты)
// или под зданием (LOCKPOINTS).
class LockMap {
public:
    static constexpr int kCell = 16;
    static constexpr int kWaterDepth = -16;

    LockMap() = default;
    LockMap(int width, int height) : width_(width), height_(height), cells_(std::size_t(width) * std::size_t(height), 0) {}

    static LockMap fromTerrain(const assets::Map3d& map);

    int width() const { return width_; }
    int height() const { return height_; }
    bool locked(int cx, int cy) const {
        return cx < 0 || cy < 0 || cx >= width_ || cy >= height_ || cells_[std::size_t(cy) * std::size_t(width_) + std::size_t(cx)] != 0;
    }
    void setLocked(int cx, int cy, bool value = true) {
        if (cx >= 0 && cy >= 0 && cx < width_ && cy < height_)
            cells_[std::size_t(cy) * std::size_t(width_) + std::size_t(cx)] = value ? 1 : 0;
    }

    // Клетки здания в мировой точке (x, y): угол — (x + ax, y + 2·ay) / 16
    // (OneObject::GetCornerXY), к нему — LOCKPOINTS.
    void addBuilding(const assets::UnitDesc& desc, int x, int y);

    // Ближайшая открытая клетка к (cx, cy) в радиусе maxRadius клеток (по квадратам).
    std::optional<std::pair<int, int>> nearestOpen(int cx, int cy, int maxRadius) const;

    // Прямая между клетками проходима (по клеткам отрезка Брезенхэма).
    bool lineOpen(int x0, int y0, int x1, int y1) const;

private:
    int width_ = 0, height_ = 0;
    std::vector<std::uint8_t> cells_;
};

// Путь A* по 8 соседям без срезания углов; точки — центры клеток в мировых
// координатах, без начальной; сглажен «по прямой видимости». Если цель
// недостижима, путь ведёт к ближайшей к ней достигнутой клетке; maxNodes
// ограничивает работу. Пустой путь — уже на месте или идти некуда.
std::vector<std::pair<int, int>> findPath(const LockMap& map, int fromX, int fromY, int toX, int toY,
                                          std::size_t maxNodes = 300000);

} // namespace oc2::world
