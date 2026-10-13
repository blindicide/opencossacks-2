#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "assets/unit_desc.hpp"
#include "world/lock_map.hpp"

namespace oc2::world {

// Длительность шага симуляции при обычной скорости игры: 40 мс (DAT_059e93ba = 0x28;
// настройка скорости даёт 120 − ползунок мс). Оригинал делает один шаг на кадр вывода
// и подстраивает GameSpeed под реальную длительность кадра; здесь шаг фиксированный.
inline constexpr double kStepSeconds = 0.040;

// Направление 0..255 → вектор ×256, как таблицы DAT_0216a818 (cos) и DAT_0216a610 (sin):
// 0 — восток, 64 — юг (мировой Y вниз).
int cos256(int direction);
int sin256(int direction);
// Направление от (dx, dy) к началу координат: 0..255.
int directionOf(int dx, int dy);

struct Unit {
    const assets::UnitDesc* desc = nullptr;
    int player = 0;
    int x16 = 0, y16 = 0; // мировые координаты ×16, как в оригинале и в UNI3
    int direction = 0;    // 0..255
    const assets::UnitAnimation* animation = nullptr;
    int frame = 0;        // кадр текущей анимации
    bool moving = false;
    int targetX16 = 0, targetY16 = 0;          // текущая точка пути
    std::vector<std::pair<int, int>> path;     // следующие точки (мировые), в обратном порядке
    bool selected = false;

    int x() const { return x16 >> 4; }
    int y() const { return y16 >> 4; }
    bool building() const { return desc && desc->building; }
};

// Юниты мира и их шаг симуляции. Пока: проигрывание анимаций покоя и ходьбы,
// движение по пути A* с мгновенным поворотом (без столкновений юнитов).
class World {
public:
    // Карта непроходимости (без неё юниты ходят по прямой). Здания, добавленные
    // после, закрывают свои клетки.
    void setLockMap(LockMap lockMap) { lock_ = std::move(lockMap); }
    const LockMap& lockMap() const { return lock_; }

    // Добавляет юнит в мировой точке (x, y); возвращает его номер.
    std::size_t addUnit(const assets::UnitDesc& desc, int player, int x, int y, int direction);

    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }

    // Приказ идти в мировую точку (x, y). Здания не ходят.
    void orderMove(std::size_t unit, int x, int y);

    // Один шаг симуляции (kStepSeconds при обычной скорости).
    void step();

    std::uint64_t stepCount() const { return steps_; }

    // Ресурсы игроков (номера — как ID счётчиков на панели).
    std::array<std::array<int, 8>, 8> resources{};

private:
    static const assets::UnitAnimation* idleAnimation(const assets::UnitDesc& desc);
    static const assets::UnitAnimation* moveAnimation(const assets::UnitDesc& desc);
    void setAnimation(Unit& u, const assets::UnitAnimation* anim);

    std::vector<Unit> units_;
    LockMap lock_;
    std::uint64_t steps_ = 0;
};

} // namespace oc2::world
