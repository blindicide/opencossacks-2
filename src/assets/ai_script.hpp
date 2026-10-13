#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "assets/xml_tree.hpp"

namespace oc2::assets {

// Сценарий стратегического ИИ нации (AI/<Нация>.ai.xml): папки с условием, в них
// правила «условие → действия». Условие — цепочка: каждый элемент вложенным
// <Condition> продолжается следующим, все должны выполняться (И).
enum class AiOp { Less, LessEqual, Equal, NotEqual, MoreEqual, More };

struct AiCondition {
    enum class Kind {
        Resources,       // CurResources: ResType, OpType, Value
        ReadyUnits,      // AmountOfReadyUnits: UnitType, OpType, Value (готовые, живые)
        GamingTime,      // GamingTime: OpType, Time (секунды игры)
        UnitsProduced,   // UnitsProduced: UnitType, OpType, Value (сколько произвёл за игру)
        OnceInPeriod,    // OnceInPeriod: Period (минуты — своё прочтение)
        FreeLivePlaces,  // FreeLivePlaces: OpType, Value (предел населения − занято)
        TotalLivePlaces, // TotalLivePlaces: OpType, Value (предел населения)
        Difficulty,      // Difficulty: OpType, DifficultyValue
        EnemyNearBase,   // EnemyNearBase: Radius, MinAmount, MaxAmount (и виды — пока все)
        UnitsLost,       // UnitsLost: UnitType, OpType, Value
        Unsupported,     // AmountOfSettlements, UpgradeIsRun — механики ещё нет: ложно
    };
    Kind kind = Kind::Unsupported;
    AiOp op = AiOp::More;
    int value = 0;
    int resource = -1;   // 0 WOOD … 5 COAL
    std::string unitType;
    int radius = 0, minAmount = 0, maxAmount = 0;
};

struct AiAction {
    enum class Kind { Build, Produce, AddResources, BuyResource, PeonsDistribution, Unsupported };
    Kind kind = Kind::Unsupported;
    std::string unitType;
    int maxAmount = 0, resourcePercent = 100, probability = 100, packSize = 1;
    // Build/Requirements.
    bool nearTrees = false, nearStones = false, nearRoad = false, multiple = true;
    int minDistFromCenter = 0, minPeasants = 1, maxPeasants = 1;
    // AddResources / BuyResource.
    int resource = -1, sellResource = -1, amount = 0;
    // PeonsDistribution: сколько крестьян на дереве, камне, еде.
    int onWood = 0, onStone = 0, onFood = 0;
};

struct AiTrigger {
    std::vector<AiCondition> condition;
    std::vector<AiAction> actions;
};

struct AiFolder {
    std::vector<AiCondition> condition;
    std::vector<AiTrigger> rules;
};

struct AiScript {
    std::vector<AiFolder> folders;
    std::vector<std::string> unitIds; // <Units>/<Buildings> — по суффиксу видно нацию

    static AiScript parse(const XmlNode& root);
    // Код нации из идентификаторов («(RU)»); пусто — не найден.
    std::string nationCode() const;
};

// Уровни сложности по возрастанию, как в DifficultyValue.
int aiDifficultyLevel(std::string_view name);
bool aiCompare(int a, AiOp op, int b);

} // namespace oc2::assets
