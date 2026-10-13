#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace oc2::assets {

// Оружие (снаряды и эффекты) из weapon.nds:
//  [MEMBERS]     «ИМЯ #АНИМАЦИЯ урон радиус прозрачность гравитация распространение скорость циклы …»
//                — анимация из WEAPON.ADS;
//  [CHILDWEAPON] «ИМЯ a b c n ДИТЯ1 … ДИТЯn» — после себя порождает одно из детей;
//  [SOUND]       «ИМЯ ГРУППА» — звук из Sounds.xml.
struct Weapon {
    std::string name;
    std::string animation; // «#SHOTFOG»
    int damage = 0, radius = 0;
    std::string transparency; // NONE, DARK, WHITE, …
    std::vector<std::string> children;
    std::string sound;
};

struct WeaponList {
    std::unordered_map<std::string, Weapon> weapons; // имя в верхнем регистре

    static WeaponList parse(std::string_view text);
    const Weapon* find(std::string_view name) const;
};

} // namespace oc2::assets
