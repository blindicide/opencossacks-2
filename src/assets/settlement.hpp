#pragma once

#include <array>
#include <string>
#include <vector>

#include "assets/unit_desc.hpp"
#include "assets/xml_tree.hpp"

namespace oc2::assets {

// Описание поселения (Dip/<Ресурс><Владелец>.dip.xml, читает DipServer.dll).
// Массивы по ресурсам — в номерах UnitDesc::resourceId (0 WOOD … 5 COAL).
struct SettlementDesc {
    using PerResource = std::array<int, UnitDesc::kResources>;
    struct Upgrade {
        int duration = 0;          // Duration (единицы не разобраны)
        std::string effectType;    // EffType: «PEASANT+» и др.
        PerResource effect{};
        PerResource price{};
    };

    int startOwner = 7;
    PerResource maxResAmount{};      // предел запаса владельца, выше которого доход не идёт
    std::string copId;               // охрана поселения
    int copBuildTime = 0, copMaxAmount = 0;
    std::string peonId;              // крестьяне поселения
    int peonBuildTime = 0;
    PerResource peonMaxAmount{};
    PerResource peonTimeOnProduce{}; // период накопления (шагов ·256 — по ProcessTribe)
    std::string obozId;              // обоз с доходом
    PerResource obozResAmount{};     // сколько везёт обоз
    std::vector<Upgrade> upgrades;

    // Главный ресурс: первый с ненулевым ObozResAmount; −1 — нет.
    int resource() const;

    static SettlementDesc parse(const XmlNode& root);
};

} // namespace oc2::assets
