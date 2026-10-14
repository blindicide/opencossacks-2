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
        assets::BigQuests quests; // Quests.dat; пусто — без событий и заданий
        assets::BigDirections directions; // DirectionDataBase: с какой стороны идёт нападение
    };

    struct SectorState {
        int owner = 0;
        int garrison = 0; // отрядов обороны
        int level = 0;    // уровень обороны 0..3
        int blocked = 0;  // ходов без дохода (диверсия на складах)
        int dry = 0;      // ходов без рекрутов (отравленные колодцы)
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
        int from = -1; // сектор, откуда идёт нападение (-1 — неизвестно)
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
    int recruitIncome(int nation) const;

    // Диверсия игрока (SECT_SABOTAGE/SECT_SAB_PERCENT по уровню обороны цели): 0 — отравить колодцы,
    // 1 — разрушить склады, 2 — взорвать казармы, 3 — поднять восстание.
    static constexpr int kDiversions = 4;
    int diversionCost(int type, int sector) const;
    int diversionPercent(int type, int sector) const;
    bool canDiversion(int type, int sector) const;
    // Платит золото и разыгрывает шанс; итог — в messages (CWM_DiversionDone/Fail). false — нельзя.
    bool diversion(int type, int sector);

    // Международный рынок: цена ресурса в условных единицах (догадка — таблица обмена не разобрана),
    // обмен берёт комиссию. exchangeGet — сколько получит игрок за give единиц from.
    static int marketValue(int res);
    static int exchangeGet(int from, int to, int give);
    // Обмен give единиц from на to у игрока; false — не хватает или одинаковые ресурсы.
    bool exchange(int from, int to, int give);
    // Ресурсы нации, которых ей не хватает (торговцы рассказывают об этом на рынке).
    std::vector<int> shortages(int nation) const;

    // Дипломатия (CDT_/CDM_ из BigMapData.txt). В начале кампании все нации воюют друг с другом;
    // мир действует, пока кто-то не объявит войну, пакт о ненападении и право прохода — 10 ходов,
    // альянс — оба сразу. Цены в тысячах золота и шансы согласия ИИ — догадка (таблицы не разобраны).
    enum class Treaty { Peace, NonAggression, Passage, Alliance };
    struct Relation {
        bool war = true;
        int nonAggressionUntil = 0; // ход, до которого (включительно) действует пакт
        int passageUntil = 0;
    };
    const Relation& relation(int a, int b) const { return relations_[std::size_t(a * assets::kBigNations + b)]; }
    bool atWar(int a, int b) const { return a != b && relation(a, b).war; }
    bool hasPassage(int a, int b) const { return a == b || (!atWar(a, b) && relation(a, b).passageUntil >= turn_); }
    // Цена договора для игрока в тысячах золота.
    int treatyPrice(Treaty t, int nation) const;
    bool canDeclareWar(int nation) const { return nation != player_ && nations_[std::size_t(nation)].alive && !atWar(player_, nation); }
    bool canPropose(Treaty t, int nation) const;
    void declareWar(int nation); // игрок объявляет войну
    // Игрок предлагает договор: ИИ принимает или отклоняет (messages), золото уходит при согласии.
    bool propose(Treaty t, int nation);

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

    // Событие или ответ с текстом из BigMapData.txt: ключ CQM_…, аргументы по порядку %s/%d.
    // Аргумент «SectorNN» экран заменяет названием города, «@Тип» — названием отряда.
    struct Message {
        std::string key;
        std::vector<std::string> args;
    };
    std::vector<Message> messages;

    // Предложение игроку, на которое нужен ответ (мятеж, наёмники, продажа сектора, задание).
    struct Offer {
        enum class Kind { Rebellion, AddSquad, SellSector, Quest, Treaty } kind = Kind::Quest;
        Message text;
        int sector = -1;
        int gold = 0;           // цена, награда или выкуп
        int giver = -1;         // нация-заказчик
        int target = -1;        // нация-цель (AttackNation)
        int deadline = 0;       // ход, к которому надо выполнить
        std::string squad;      // тип отряда (AddSquad)
        std::string quest;      // имя задания (AttackNation, CaptureSector, DefendSector)
    };
    std::vector<Offer> offers;
    // Ответ на первое предложение. false — принять нельзя (нет золота или места); предложение остаётся.
    bool answer(bool accept);
    bool canAccept(const Offer& o) const;

    // Принятые задания: игрок получает золото, выполнив условие до срока.
    struct ActiveQuest {
        std::string name;
        int giver = 0, target = -1, sector = -1, gold = 0, deadline = 0;
    };
    const std::vector<ActiveQuest>& quests() const { return quests_; }

    std::string serialize() const;
    static std::optional<BigGame> deserialize(std::shared_ptr<const Data> data, const std::string& text);

private:
    std::uint32_t rnd();
    void startTurn();
    void grow(int nation);
    void promote(int nation);
    std::optional<Attack> aiTurn(int nation);
    void giveExp(int nation, int exp);
    void rollEvents();
    void completeQuests(const Attack& a, const Outcome& o, int defender);
    void loseSector(int sector);
    void setRelation(int a, int b, const Relation& r) {
        relations_[std::size_t(a * assets::kBigNations + b)] = r;
        relations_[std::size_t(b * assets::kBigNations + a)] = r;
    }
    void diplomacyTurn();
    int randomOwned(int nation);
    bool isPort(int sector) const;
    std::string sectorKey(int id) const { return data_->map.sectors[std::size_t(id)].name; }

    std::shared_ptr<const Data> data_;
    int player_ = 0;
    int turn_ = 1;
    int nextNation_ = 0; // кто ходит следующим в конце хода
    std::uint32_t seed_ = 1;
    std::vector<SectorState> sectors_;
    std::vector<ActiveQuest> quests_;
    std::vector<int> lastFired_; // по определениям Quests.dat: ход последнего срабатывания
    std::array<NationState, assets::kBigNations> nations_;
    std::array<Relation, assets::kBigNations * assets::kBigNations> relations_{};
};

} // namespace oc2::world
