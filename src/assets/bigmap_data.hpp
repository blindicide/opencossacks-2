#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oc2::assets {

// Нации «Завоевания Европы» в порядке Sectors.dat, HeroArmy.dat и Quests.dat.
constexpr int kBigNations = 9;
// Коды Nations.lst в том же порядке.
const char* bigNationCode(int nation);
// Название нации (UTF-8).
const char* bigNationName(int nation);

// Сектор карты Европы из Missions\StatData\Sectors.dat.
struct BigSector {
    int id = 0;
    std::pair<int, int> center;  // левый верхний угол картинки сектора на карте 1410×1068
    std::pair<int, int> city;    // значок города
    std::pair<int, int> fort;    // значок крепости
    int defOwner = 0;            // хозяин при начале, «защитная» нация
    int defence = 0;             // уровень обороны 0..3 (SectGarrisonSquads)
    int population = 0;
    std::string name;            // «Sector00» — папка карты боя
    std::vector<int> neighbors;
    std::vector<std::array<int, 3>> arrows; // по одной на соседа: x, y, признак
    int villages = 0;
    int resource = 0;            // 1..4 — вид добавки (еда, золото, железо, уголь)
    int owner = 0;               // начальный хозяин
};

struct BigMap {
    int width = 0, height = 0;
    std::vector<BigSector> sectors;
    // Sectors.dat (Windows-1251). FormatError — если нет ни одного сектора.
    static BigMap parse(std::string_view text);
};

// Missions\StatData\BigMapConst.dat: «#ИМЯ значения…» и «ИМЯ значения…», «//» — комментарий.
struct BigMapConst {
    std::map<std::string, std::vector<std::string>> values;
    static BigMapConst parse(std::string_view text);
    int integer(const std::string& key, int def = 0, std::size_t index = 0) const;
    // NATCOLORn — шестнадцатеричное ARGB.
    std::uint32_t color(int nation, std::uint32_t def = 0xFF808080u) const;
};

// Missions\StatData\HeroArmy.dat: состав армии героя по званию (0 — лейтенант … 5 — маршал) и нации.
struct HeroArmyRow {
    int squads = 0;                 // всего отрядов
    std::vector<int> layout;        // числа до перечня особых отрядов
    std::vector<std::pair<std::string, int>> specials; // тип и число отрядов
};

struct HeroArmy {
    static constexpr int kRanks = 6;
    std::array<std::array<HeroArmyRow, kBigNations>, kRanks> rows;
    static HeroArmy parse(std::string_view text);
};

} // namespace oc2::assets
