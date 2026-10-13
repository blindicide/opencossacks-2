#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "assets/settlement.hpp"
#include "world/unit_catalog.hpp"
#include "world/world.hpp"

namespace oc2::world {

// Нейтральные поселения (DipServer.dll, ProcessTribe): постройка-ядро и дома вокруг,
// приносят владельцу ресурс по описанию Dip/<Ресурс>Neutral.dip.xml. Своё прочтение:
// - поселение — ядро (BldMel/BldRudCoal/BldRudGold/BldRudIron с «(FR)», игрок 7)
//   и дома SelFraDom* того же игрока в kRadius от него (SXML_2 карты для этого не годится);
// - захват — как в ProcessTribe: в kRadius своих (с союзниками) меньше kMinDefenders,
//   охраны (CopID) владельца нет и все чужие юниты — одной не союзной владельцу команды;
//   поселение переходит к игроку этой команды, у которого там больше всего юнитов;
// - доход: каждые PeonTimeOnProduce шагов (·256 тиков при GameSpeed 256) накопитель
//   растёт на число крестьян; набрав ObozResAmount — столько же владельцу, пока его
//   запас ниже MaxResAmount. Крестьяне не выводятся юнитами: их число — PeonMaxAmount,
//   обоз (ObozID) не ходит, ресурс зачисляется сразу;
// - охрана: у не нейтрального владельца недостающая (до CopMaxAmount) родится у ядра
//   за CopBuidTime/2 шагов; у нейтрального не родится (игрок 7 не воюет);
// - погибли все постройки поселения — оно ничьё (7) и больше не действует.
class Settlements {
public:
    static constexpr int kRadius = 1100;
    static constexpr int kMinDefenders = 10;

    struct Settlement {
        assets::SettlementDesc desc;
        int resource = -1;
        std::size_t core = 0;
        std::vector<std::size_t> buildings; // ядро и дома
        int owner = 7;
        int accum = 0;                      // накопитель дохода
        int produceIn = 0;                  // шагов до следующего накопления
        int copIn = 0;                      // шагов до следующего охранника
        std::vector<std::size_t> cops;
        bool active = true;
    };

    // Ищет поселения среди юнитов мира; описания — из Dip/ в dataRoot.
    Settlements(World& world, UnitCatalog& catalog, const std::filesystem::path& dataRoot);
    // Для проверок: поселения с готовыми описаниями (core — номер ядра в мире).
    Settlements(World& world, UnitCatalog& catalog, std::vector<Settlement> list);

    // Вызывать после каждого шага мира.
    void step();
    // Сколько поселений у игрока; resource ≥ 0 — только с этим ресурсом.
    int owned(int player, int resource = -1) const;
    const std::vector<Settlement>& list() const { return list_; }

private:
    static std::vector<Settlement> discover(const World& world, const UnitCatalog& catalog,
                                            const std::filesystem::path& dataRoot);
    void capture(Settlement& s);
    void produce(Settlement& s);
    void guard(Settlement& s);
    void setOwner(Settlement& s, int player);

    World& world_;
    UnitCatalog& catalog_;
    std::vector<Settlement> list_;
};

} // namespace oc2::world
