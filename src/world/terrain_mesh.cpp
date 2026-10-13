#include "world/terrain_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace oc2::world {

namespace {

// Вектор света (x, y, z): ровная земля получает z. Значения оригинала задаются
// при загрузке настроек (FUN по адресу записи DAT_021effb0) и пока не найдены;
// направление — сверху слева, как у теней на спрайтах.
constexpr int kLightX = 90, kLightY = -90, kLightZ = 200;

constexpr float kFactureSpan = 64.0f; // мировых единиц на одну фактуру

int heightAt(const assets::Map3d& map, int x, int y) {
    if (x < 0 || y < 0 || x >= map.vertsInLine() || y >= map.rows()) return 0;
    return map.height(x, y);
}

// Детерминированный «случайный» номер для сдвига текстуры (в оригинале —
// таблица случайных чисел DAT_0522bbf8 по (номер вершины · k) & 0x1FFF).
std::uint32_t hash(std::uint32_t i, std::uint32_t k) {
    std::uint32_t h = i * 0x9E3779B1u ^ k * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

} // namespace

std::uint8_t vertexLight(const assets::Map3d& map, int x, int y) {
    // Соседи на сетке со сдвинутыми нечётными столбцами.
    int down, rightLow, right, up, left, leftLow;
    if ((x & 1) == 0) {
        down = heightAt(map, x, y + 1);
        rightLow = heightAt(map, x + 1, y + 1);
        right = heightAt(map, x + 1, y);
        up = heightAt(map, x, y - 1);
        left = heightAt(map, x - 1, y);
        leftLow = heightAt(map, x - 1, y + 1);
    } else {
        down = heightAt(map, x, y + 1);
        rightLow = heightAt(map, x + 1, y);
        right = heightAt(map, x + 1, y - 1);
        up = heightAt(map, x, y - 1);
        left = heightAt(map, x - 1, y - 1);
        leftLow = heightAt(map, x - 1, y);
    }
    const int ny = up - down;
    const int nx = ((rightLow - left) + right - leftLow) >> 1;
    const double len = std::sqrt(double(nx * nx + ny * ny + 0x1000));
    int v = int(std::lround(double(nx * kLightX + ny * kLightY + kLightZ * 64) / len));
    v = std::clamp(v, 120, 250);
    return std::uint8_t(std::min(255, (v << 7) / kLightZ));
}

std::vector<TerrainVertex> buildTerrainMesh(const assets::Map3d& map) {
    const int w = map.vertsInLine(), h = map.rows();
    std::vector<std::uint8_t> light(map.vertexCount());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) light[map.index(x, y)] = vertexLight(map, x, y);

    std::vector<TerrainVertex> out;
    out.reserve(std::size_t(w - 1) * std::size_t(h - 1) * 6);

    auto emit = [&](std::array<std::array<int, 2>, 3> tri, std::uint32_t salt) {
        std::uint8_t f1 = 0, f2 = 0;
        for (const auto& p : tri) {
            f1 = std::max(f1, map.facture(p[0], p[1]));
            f2 = std::max(f2, map.facture2(p[0], p[1]));
        }
        // Текстура привязана к первой вершине треугольника со случайным сдвигом
        // 0..31 и 0..27 мировых единиц (FUN_015590b0). В атласе оригинала 8×8 фактур,
        // ячейка (32 единицы) занимает 32/512 атласа — половину фактуры, то есть
        // фактура покрывает 64 мировые единицы.
        const std::uint32_t id = std::uint32_t(map.index(tri[0][0], tri[0][1]));
        const float offU = float(hash(id, salt) & 31u), offV = float(hash(id, salt + 2) % 28u);
        const float ox = map.worldX(tri[0][0]), oy = map.worldY(tri[0][0], tri[0][1]);
        for (const auto& p : tri) {
            TerrainVertex v{};
            v.x = map.worldX(p[0]);
            v.y = map.worldY(p[0], p[1]);
            v.z = float(map.height(p[0], p[1]));
            v.u = (v.x - ox + offU) / kFactureSpan;
            v.v = (v.y - oy + offV) / kFactureSpan;
            v.facture1 = f1;
            v.facture2 = f2;
            v.light = light[map.index(p[0], p[1])];
            v.blend2 = std::uint8_t(std::min(255, map.blend2(p[0], p[1]) * 6 / 5));
            out.push_back(v);
        }
    };

    for (int y = 0; y < h - 1; ++y)
        for (int x = 0; x < w - 1; ++x) {
            emit({{{x, y}, {x + 1, y}, {x + 1, y + 1}}}, 0x2F);
            emit({{{x, y}, {x + 1, y + 1}, {x, y + 1}}}, 0x6F);
        }
    return out;
}

} // namespace oc2::world
