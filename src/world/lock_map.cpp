#include "world/lock_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <queue>
#include <tuple>

namespace oc2::world {

LockMap LockMap::fromTerrain(const assets::Map3d& map) {
    const int size = map.rows() * assets::Map3d::kCell / kCell; // 512 · 32 / 16 = 1024
    LockMap lm(size, size);
    const int playable = map.rows() * 15 / 16 * assets::Map3d::kCell; // как в heightAt
    for (int cy = 0; cy < size; ++cy)
        for (int cx = 0; cx < size; ++cx) {
            const int x = cx * kCell + kCell / 2, y = cy * kCell + kCell / 2;
            if (x >= playable || y >= playable || y < assets::Map3d::kCell || map.heightAt(x, y) <= kWaterDepth)
                lm.setLocked(cx, cy);
        }
    return lm;
}

void LockMap::addBuilding(const assets::UnitDesc& desc, int x, int y) { setPoints(desc, x, y, desc.lockPoints, true); }

void LockMap::setPoints(const assets::UnitDesc& desc, int x, int y, const std::vector<std::pair<int, int>>& points,
                        bool value) {
    const auto [cx, cy] = corner(desc, x, y);
    for (const auto& [lx, ly] : points) setLocked(cx + lx, cy + ly, value);
}

bool LockMap::pointsOpen(const assets::UnitDesc& desc, int x, int y,
                         const std::vector<std::pair<int, int>>& points) const {
    const auto [cx, cy] = corner(desc, x, y);
    for (const auto& [lx, ly] : points)
        if (locked(cx + lx, cy + ly)) return false;
    return true;
}

std::optional<std::pair<int, int>> LockMap::nearestOpen(int cx, int cy, int maxRadius) const {
    if (!locked(cx, cy)) return std::make_pair(cx, cy);
    for (int r = 1; r <= maxRadius; ++r) {
        std::optional<std::pair<int, int>> best;
        int bestD = std::numeric_limits<int>::max();
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r || locked(cx + dx, cy + dy)) continue;
                const int d = dx * dx + dy * dy;
                if (d < bestD) {
                    bestD = d;
                    best = std::make_pair(cx + dx, cy + dy);
                }
            }
        if (best) return best;
    }
    return std::nullopt;
}

bool LockMap::lineOpen(int x0, int y0, int x1, int y1) const {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (locked(x0, y0)) return false;
        if (x0 == x1 && y0 == y1) return true;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            // Диагональный шаг не должен проходить между двумя закрытыми клетками.
            if (e2 <= dx && locked(x0 + sx, y0) && locked(x0, y0 + sy)) return false;
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

std::vector<std::pair<int, int>> findPath(const LockMap& map, int fromX, int fromY, int toX, int toY,
                                          std::size_t maxNodes) {
    constexpr int C = LockMap::kCell;
    const int w = map.width(), h = map.height();
    if (w == 0 || h == 0) return {{toX, toY}};
    auto clampCell = [&](int v, int n) { return std::clamp(v / C, 0, n - 1); };
    int sx = clampCell(fromX, w), sy = clampCell(fromY, h);
    if (map.locked(sx, sy)) {
        // Юнит стоит в закрытой клетке (поставлен картой) — выходим к ближайшей открытой.
        if (auto o = map.nearestOpen(sx, sy, 8)) std::tie(sx, sy) = *o;
    }
    int gx = clampCell(toX, w), gy = clampCell(toY, h);
    bool exactGoal = true;
    if (map.locked(gx, gy)) {
        auto o = map.nearestOpen(gx, gy, 32);
        if (!o) return {};
        std::tie(gx, gy) = *o;
        exactGoal = false;
    }
    if (sx == gx && sy == gy) return exactGoal ? std::vector<std::pair<int, int>>{{toX, toY}} : std::vector<std::pair<int, int>>{};
    // Прямая видимость — путь из одной точки.
    if (map.lineOpen(sx, sy, gx, gy)) {
        if (exactGoal) return {{toX, toY}};
        return {{gx * C + C / 2, gy * C + C / 2}};
    }

    const std::size_t n = std::size_t(w) * std::size_t(h);
    std::vector<float> g(n, std::numeric_limits<float>::infinity());
    std::vector<std::int32_t> parent(n, -1);
    std::vector<std::uint8_t> closed(n, 0);
    auto idx = [&](int x, int y) { return std::size_t(y) * std::size_t(w) + std::size_t(x); };
    auto heuristic = [&](int x, int y) {
        const float dx = float(std::abs(x - gx)), dy = float(std::abs(y - gy));
        return (dx + dy) + (1.41421356f - 2.0f) * std::min(dx, dy);
    };
    using Node = std::pair<float, std::int32_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const std::size_t start = idx(sx, sy), goal = idx(gx, gy);
    g[start] = 0.0f;
    open.push({heuristic(sx, sy), std::int32_t(start)});
    std::size_t best = start;
    float bestH = heuristic(sx, sy);
    std::size_t expanded = 0;
    while (!open.empty() && expanded < maxNodes) {
        const auto [f, ci] = open.top();
        open.pop();
        const auto cur = std::size_t(ci);
        if (closed[cur]) continue;
        closed[cur] = 1;
        ++expanded;
        const int cx = int(cur % std::size_t(w)), cy = int(cur / std::size_t(w));
        if (const float hh = heuristic(cx, cy); hh < bestH) {
            bestH = hh;
            best = cur;
        }
        if (cur == goal) break;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = cx + dx, ny = cy + dy;
                if (map.locked(nx, ny)) continue;
                if (dx != 0 && dy != 0 && (map.locked(cx + dx, cy) || map.locked(cx, cy + dy))) continue;
                const std::size_t ni = idx(nx, ny);
                if (closed[ni]) continue;
                const float ng = g[cur] + (dx != 0 && dy != 0 ? 1.41421356f : 1.0f);
                if (ng < g[ni]) {
                    g[ni] = ng;
                    parent[ni] = std::int32_t(cur);
                    open.push({ng + heuristic(nx, ny), std::int32_t(ni)});
                }
            }
    }
    const bool reached = closed[goal] != 0;
    std::vector<std::pair<int, int>> cells;
    for (std::int64_t c = std::int64_t(reached ? goal : best); c != -1; c = parent[std::size_t(c)])
        cells.emplace_back(int(std::size_t(c) % std::size_t(w)), int(std::size_t(c) / std::size_t(w)));
    std::reverse(cells.begin(), cells.end()); // от старта к цели
    if (cells.size() <= 1) return {};

    // Сглаживание: из каждой точки — вперёд, пока следующая клетка пути ещё видна.
    std::vector<std::pair<int, int>> out;
    std::size_t i = 0;
    while (i + 1 < cells.size()) {
        std::size_t j = i + 1;
        while (j + 1 < cells.size() &&
               map.lineOpen(cells[i].first, cells[i].second, cells[j + 1].first, cells[j + 1].second))
            ++j;
        out.emplace_back(cells[j].first * C + C / 2, cells[j].second * C + C / 2);
        i = j;
    }
    if (reached && exactGoal) out.back() = {toX, toY};
    return out;
}

} // namespace oc2::world
