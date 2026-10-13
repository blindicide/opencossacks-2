#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "assets/ai_script.hpp"
#include "world/settlements.hpp"
#include "world/unit_catalog.hpp"
#include "world/world.hpp"

namespace oc2::world {

// Сценарий ИИ для нации с кодом code («RU»): по AI/router.xml — первый сценарий маршрута,
// чьи юниты с этим кодом; если в маршрутах нет — любой AI/*.ai.xml с этим кодом.
std::optional<assets::AiScript> loadAiScript(const std::filesystem::path& dataRoot, std::string_view code);
// Код нации игрока по суффиксу «(XX)» типов его юнитов; пусто — юнитов нет.
std::string playerNation(const World& world, const UnitCatalog& catalog, int player);

// Стратегический ИИ игрока по сценарию AI/<Нация>.ai.xml: раз в kThinkSteps шагов
// проверяет условия папок и правил и выполняет действия (стройка, производство,
// ресурсы, распределение крестьян), свободных крестьян шлёт в шахты и на добычу.
// Своё прочтение сценариев, без кода оригинала (его ИИ — классы AI_Folder/AI_Trigger,
// разобраны только условия времени):
// - время игры и OnceInPeriod — в секундах по 24 шага (счётчик DAT_0262f014 растёт
//   на GameSpeed = 256 за шаг, секунда — 0x1800 = 24·256; FUN_015f4e80);
// - место стройки — по спирали от центра базы (первое своё здание), не ближе
//   MinDistFromCenter, с запасом клеток вокруг, NearTrees/NearStones — дерево или камень
//   в 400; строят до MaxPeasants ближайших свободных крестьян;
// - за раз начинается не больше одной стройки;
// - BuyResource — продажа Amount на общем рынке World::market (без проверки, есть ли рынок у игрока);
//   AmountOfSettlements — по Settlements::owned (без уровней улучшений), UpgradeIsRun ложно.
class StrategicAi {
public:
    static constexpr int kThinkSteps = 25;
    static constexpr int kBaseRadius = 2500;   // где искать источники, шахты и место стройки
    static constexpr int kMineCrew = 5;         // сколько крестьян ИИ держит в шахте
    static constexpr int kMineShare = 3;        // в шахтах — не больше 1/kMineShare крестьян
    static constexpr int kGatherersPerSource = 3;

    StrategicAi(World& world, UnitCatalog& catalog, int player, assets::AiScript script, int difficulty = 1,
                std::uint32_t seed = 1);

    // Вызывать после каждого шага мира.
    void update();
    int player() const { return player_; }
    // Последнее распределение крестьян: дерево, камень, еда.
    std::array<int, 3> distribution() const { return {onWood_, onStone_, onFood_}; }

    // Поселения для AmountOfSettlements; без них условие ложно.
    void setSettlements(const Settlements* s) { settlements_ = s; }

    // Выполнено ли условие сейчас; сработавший OnceInPeriod отсчитывает новый период.
    bool holds(const assets::AiCondition& c);

private:
    void think();
    bool chainHolds(const std::vector<assets::AiCondition>& chain);
    void act(const assets::AiAction& a);
    bool build(const assets::AiAction& a);
    bool produce(const assets::AiAction& a);
    void buyResource(const assets::AiAction& a);
    void employIdle();
    bool sowNear(int x, int y);
    std::optional<std::pair<int, int>> findSite(const assets::UnitDesc& desc, const assets::AiAction& a);
    std::pair<int, int> baseCenter() const;
    int count(const assets::UnitDesc* desc, bool readyOnly) const;
    bool roll(int percent);
    bool sourceWithin(int x, int y, int radius, int resource) const;
    void indexSources();
    bool hasBase(int resource) const;

    World& world_;
    UnitCatalog& catalog_;
    int player_;
    assets::AiScript script_;
    int difficulty_;
    const Settlements* settlements_ = nullptr;
    std::mt19937 rng_;
    int onWood_ = 1, onStone_ = 1, onFood_ = 1;
    bool buildStarted_ = false; // в этом раздумье уже начата стройка
    // OnceInPeriod: шаг, с которого отсчитывается период (по адресу условия).
    std::unordered_map<const assets::AiCondition*, std::uint64_t> periodStart_;
    // Источники по клеткам 256×256 для быстрых проверок окрестности.
    static constexpr int kGridShift = 8;
    std::unordered_map<std::int64_t, std::vector<std::size_t>> grid_;
};

} // namespace oc2::world
