#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/bigmap_data.hpp"

namespace oc2::world {

// «Завоевание Европы»: глобальная карта из 34 секторов, ходы, доходы, армии героев и бои за сектора.
// Данные — Sectors.dat, BigMapConst.dat, HeroArmy.dat; сами правила хода в оригинале внутри engine.exe
// (CSectStatData), здесь они собраны заново по этим константам.
class BigGame {
public:
    static constexpr int kRes = 6; // дерево, золото, камень, еда, железо, уголь — как в мире боя

    struct Data {
        assets::BigMap map;
        assets::BigMapConst k;
        assets::HeroArmy hero;
    };

    struct SectorState {
        int owner = 0;
        int garrison = 0; // отрядов обороны
        int level = 0;    // уровень обороны 0..3
    };

    struct NationState {
        std::array<int, kRes> res{};
        int recruits = 0;
        int exp = 0;
        int rank = 0;                    // 0 лейтенант … 5 маршал
        std::vector<std::string> army;   // отряды армии героя (типы юнитов)
        bool alive = true;
    };

    // Нападение: нация с отрядами на сектор.
    struct Attack {
        int nation = 0;
        int target = 0;
        std::vector<std::string> squads;
    };

    // Итог боя за сектор: уцелевшие отряды нападавшего и уничтоженные отряды обороны.
    struct Outcome {
        bool attackerWon = false;
        std::vector<bool> survived; // по порядку Attack::squads
        int defenderLost = 0;
    };

    explicit BigGame(std::shared_ptr<const Data> data, int playerNation, std::uint32_t seed = 1);

    const Data& data() const { return *data_; }
    int player() const { return player_; }
    int turn() const { return turn_; }
    const SectorState& sector(int id) const { return sectors_[std::size_t(id)]; }
    const NationState& nation(int n) const { return nations_[std::size_t(n)]; }
    int sectorCount() const { return int(sectors_.size()); }
    int ownedBy(int nation) const;

    // Отряды армии героя: сколько вмещает звание и какие стоят в строю.
    int armyCapacity(int nation) const;
    // Опыт, с которого начинается следующее звание (0 — высшее звание).
    int nextRankExp(int nation) const;
    // Сила отряда при расчёте боя без игры.
    static int squadPower(const std::string& type);
    // Цена отряда в ресурсах и людях; recruits — людей.
    struct Cost {
        std::array<int, kRes> res{};
        int recruits = 0;
    };
    static Cost squadCost(const std::string& type);
    // Основная пехота нации — по ней идёт оборона и пополнение ИИ.
    static std::string basicSquad(int nation);

    // Доход и пополнение по секторам нации за ход.
    std::array<int, kRes> income(int nation) const;
    int recruitIncome(int nation) const { return ownedBy(nation) * data_->k.integer("SECT_REG_RECRTS", 30); }

    bool adjacent(int a, int b) const;
    // Можно ли игроку напасть на сектор: чужой, рядом со своим, в армии есть отряды.
    bool canAttack(int target) const;
    // Сектор, из которого идёт нападение (свой соседний с целью).
    std::optional<int> attackBase(int nation, int target) const;
    // Сила обороны сектора (для расчёта и подсказки).
    int defencePower(int target) const;
    int attackPower(const std::vector<std::string>& squads) const;
    // Отряды обороны сектора по типам.
    std::vector<std::string> defenders(int target) const;

    // Нападение игрока: отряды берутся из армии (сколько вмещает звание); бой разыгрывается снаружи или по силе.
    Attack beginPlayerAttack(int target) const;
    // Розыгрыш по силам: возвращает итог без боя на карте.
    Outcome autoResolve(const Attack& a);
    // Применение итога: сектор переходит к нападавшему или обороне несёт потери.
    void apply(const Attack& a, const Outcome& o);

    // Найм отряда игроком в армию героя; false — нет места или ресурсов.
    bool recruit(int nation, const std::string& type);
    // Усиление обороны сектора на уровень за золото.
    bool upgradeDefence(int target);
    // Типы, которые нация может нанять сейчас (по званию).
    std::vector<std::string> recruitable(int nation) const;

    // Конец хода игрока: идёт ход остальных наций. Возвращает нападение на сектор игрока, которое
    // он решает сам; после apply() вызовите continueTurn(), чтобы дойти до конца хода.
    std::optional<Attack> endTurn();
    std::optional<Attack> continueTurn();
    // Итоговое состояние кампании: 1 победа, -1 поражение, 0 идёт.
    int result() const;

    // Сообщения хода (UTF-8) для окна событий.
    std::vector<std::string> log;

    std::string serialize() const;
    static std::optional<BigGame> deserialize(std::shared_ptr<const Data> data, const std::string& text);

private:
    std::uint32_t rnd();
    void startTurn();
    void grow(int nation);
    void promote(int nation);
    std::optional<Attack> aiTurn(int nation);
    void giveExp(int nation, int exp);

    std::shared_ptr<const Data> data_;
    int player_ = 0;
    int turn_ = 1;
    int nextNation_ = 0; // кто ходит следующим в конце хода
    std::uint32_t seed_ = 1;
    std::vector<SectorState> sectors_;
    std::array<NationState, assets::kBigNations> nations_;
};

} // namespace oc2::world
