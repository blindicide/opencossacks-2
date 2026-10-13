#pragma once

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "assets/unit_desc.hpp"
#include "assets/weapon_list.hpp"

namespace oc2::render {

// Эффекты оружия: дым и вспышки выстрелов (анимации WEAPON.ADS по оружию weapon.nds).
// Упрощение: цепочка детей [CHILDWEAPON] разворачивается сразу (случайный ребёнок
// на каждом шаге), все видимые звенья проигрываются разом в точке выстрела; снаряды
// не летят (урон наносит бой мира).
class Effects {
public:
    explicit Effects(const std::filesystem::path& dataRoot);

    // Порождает эффект оружия в мировой точке (дуло: на 24 единицы вперёд по направлению).
    // Возвращает группу звука выстрела, если она есть.
    std::optional<std::string> spawn(const std::string& weapon, int x, int y, int direction);
    // Шаг симуляции: кадры вперёд, законченные эффекты убираются.
    void step();

    struct Instance {
        const assets::UnitAnimation* animation = nullptr;
        int frame = 0;
        int x = 0, y = 0; // мировые
        int lift = 0;     // подъём над землёй в пикселях экрана
    };
    const std::vector<Instance>& instances() const { return instances_; }
    const assets::UnitDesc& ads() const { return ads_; }

private:
    assets::UnitDesc ads_;
    assets::WeaponList weapons_;
    std::vector<Instance> instances_;
    std::mt19937 random_{777};
};

} // namespace oc2::render
