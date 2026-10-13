#pragma once

#include <cstdint>
#include <vector>

#include "assets/map3d.hpp"

namespace oc2::world {

// Вершина треугольника рельефа. Треугольники не делят вершины: у каждого
// своя фактура и свой случайный сдвиг текстуры, как в оригинале.
struct TerrainVertex {
    float x, y, z;    // мировые координаты, z — высота
    float u, v;       // координаты в фактуре (1.0 = 64 мировые единицы)
    std::uint8_t facture1, facture2, pad0, pad1;
    std::uint8_t light;  // освещение: 128 — без изменений
    std::uint8_t blend2; // вес второго слоя 0..255
    std::uint8_t pad2, pad3;
};
static_assert(sizeof(TerrainVertex) == 28);

// Освещённость вершины по FUN_01555110: нормаль по шести соседям, скалярное
// произведение на вектор света, ограничение 120..250, затем к 128 для ровной земли.
std::uint8_t vertexLight(const assets::Map3d& map, int x, int y);

// Треугольники рельефа построчно сверху вниз — порядок «от дальнего к ближнему»,
// поэтому буфер глубины не нужен. Ячейка (x, y) — два треугольника
// (i, i+1, i+1+V) и (i, i+1+V, i+V), как в FUN_015590b0. Фактура треугольника —
// наибольший номер среди его вершин (FUN_01556110), вес второго слоя —
// min(255, TIL2b · 6 / 5).
std::vector<TerrainVertex> buildTerrainMesh(const assets::Map3d& map);

} // namespace oc2::world
