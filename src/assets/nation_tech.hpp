#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oc2::assets {

// Дерево развития из файла нации .nds (в оригинале — разделы [ACCESSCONTROL], [UPGRADE], [UPGRADEPLACE]).

// [ACCESSCONTROL]: «ТИП n ТРЕБУЕМЫЙ…» (n имён, возможно на следующих строках) — юнит или здание
// доступно, когда у игрока есть все требуемые готовые здания и исследованные улучшения.
std::vector<std::pair<std::string, std::vector<std::string>>> parseNationAccess(std::string_view text);

// [UPGRADE]: «ИМЯ значок #POSITION столбец строка #LEVEL n имя #COST k РЕС кол… #TIME t», затем
// строки действия («LIFE GRP 1 BldBlg(AU) +15000», «ATTRANGE ATTINDEX 0 GRP 1 BldFor(AU) +10%», …).
struct UpgradeDef {
    std::string name;
    std::string icon;                  // «U4»
    int column = 0, row = 0, level = 0;
    std::array<int, 6> cost{};         // по ресурсам UnitDesc::resourceId
    int time = 0;                      // шагов исследования (единица — своё допущение)
    std::vector<std::vector<std::string>> effects; // слова каждой строки действия
};
std::vector<UpgradeDef> parseNationUpgrades(std::string_view text);

// [UPGRADEPLACE]: «ЗДАНИЕ n», затем n имён улучшений, которые оно исследует.
std::vector<std::pair<std::string, std::vector<std::string>>> parseNationUpgradePlace(std::string_view text);

} // namespace oc2::assets
