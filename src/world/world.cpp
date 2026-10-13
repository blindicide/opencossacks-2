#include "world/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace oc2::world {

namespace {

// Таблица оригинала: 256·cos с отбрасыванием дробной части (236 у 22,5°, 97 у 67,5°).
const std::array<int, 256>& cosTable() {
    static const std::array<int, 256> table = [] {
        std::array<int, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[std::size_t(i)] = int(256.0 * std::cos(2.0 * std::numbers::pi * i / 256.0));
        return t;
    }();
    return table;
}

} // namespace

int cos256(int direction) { return cosTable()[std::size_t(direction & 255)]; }
int sin256(int direction) { return cosTable()[std::size_t((direction - 64) & 255)]; }

int directionOf(int dx, int dy) {
    if (dx == 0 && dy == 0) return 0;
    const double a = std::atan2(double(dy), double(dx)); // −π..π, Y вниз — по часовой
    return int(std::lround(a * 128.0 / std::numbers::pi)) & 255;
}

const assets::UnitAnimation* World::idleAnimation(const assets::UnitDesc& desc) {
    for (const char* name : {"#STANDLO", "@REST", "#REST", "#STAND", "#PSTAND"})
        if (const assets::UnitAnimation* a = desc.animation(name); a && !a->frames.empty()) return a;
    for (const assets::UnitAnimation& a : desc.animations)
        if (!a.frames.empty()) return &a;
    return nullptr;
}

const assets::UnitAnimation* World::moveAnimation(const assets::UnitDesc& desc) {
    for (const char* name : {"@MOTION_L", "#MOTION_L"})
        if (const assets::UnitAnimation* a = desc.animation(name); a && !a->frames.empty()) return a;
    return nullptr;
}

void World::setAnimation(Unit& u, const assets::UnitAnimation* anim) {
    if (u.animation == anim) return;
    u.animation = anim;
    u.frame = 0;
}

std::size_t World::addUnit(const assets::UnitDesc& desc, int player, int x, int y, int direction) {
    Unit u;
    u.desc = &desc;
    u.player = player;
    u.x16 = x * 16;
    u.y16 = y * 16;
    u.direction = direction & 255;
    u.animation = idleAnimation(desc);
    // Чтобы стоящие рядом не шевелились в такт — разный начальный кадр покоя.
    if (u.animation && !desc.building) u.frame = int((units_.size() * 7) % u.animation->frames.size());
    if (desc.building && lock_.width() > 0) lock_.addBuilding(desc, x, y);
    units_.push_back(u);
    return units_.size() - 1;
}

void World::orderMove(std::size_t index, int x, int y) {
    Unit& u = units_.at(index);
    if (u.building() || !moveAnimation(*u.desc) || u.desc->step <= 0) return;
    u.path.clear();
    if (lock_.width() > 0)
        u.path = findPath(lock_, u.x(), u.y(), x, y);
    else
        u.path = {{x, y}};
    if (u.path.empty()) {
        u.moving = false;
        return;
    }
    std::reverse(u.path.begin(), u.path.end()); // берём с конца
    u.targetX16 = u.path.back().first * 16;
    u.targetY16 = u.path.back().second * 16;
    u.path.pop_back();
    u.moving = true;
}

void World::step() {
    ++steps_;
    for (Unit& u : units_) {
        if (u.building()) continue; // кадры зданий — слои, не время
        if (u.moving) {
            const int dx = u.targetX16 - u.x16, dy = u.targetY16 - u.y16;
            // Смещение за шаг: (cos·шаг >> 8, sin·шаг >> 8) в единицах 1/16 (FUN_018af490, FUN_00f370e0).
            const double dist = std::hypot(double(dx), double(dy));
            if (dist <= double(u.desc->step)) {
                u.x16 = u.targetX16;
                u.y16 = u.targetY16;
                if (u.path.empty()) {
                    u.moving = false;
                } else {
                    u.targetX16 = u.path.back().first * 16;
                    u.targetY16 = u.path.back().second * 16;
                    u.path.pop_back();
                }
            } else {
                u.direction = directionOf(dx, dy);
                u.x16 += (cos256(u.direction) * u.desc->step) >> 8;
                u.y16 += (sin256(u.direction) * u.desc->step) >> 8;
            }
        }
        setAnimation(u, u.moving ? moveAnimation(*u.desc) : idleAnimation(*u.desc));
        // Один кадр за шаг при GameSpeed = 256 (OneObject::SetNextFrame).
        if (u.animation && !u.animation->frames.empty())
            u.frame = (u.frame + 1) % int(u.animation->frames.size());
    }
}

} // namespace oc2::world
