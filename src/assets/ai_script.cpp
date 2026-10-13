#include "assets/ai_script.hpp"

#include <algorithm>
#include <cctype>

#include "assets/unit_desc.hpp"

namespace oc2::assets {

namespace {

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

AiOp parseOp(const XmlNode& n) {
    const std::string op = n.value("OpType").value_or("more");
    if (equalsNoCase(op, "less")) return AiOp::Less;
    if (equalsNoCase(op, "less equal")) return AiOp::LessEqual;
    if (equalsNoCase(op, "equal")) return AiOp::Equal;
    if (equalsNoCase(op, "not equal")) return AiOp::NotEqual;
    if (equalsNoCase(op, "more equal")) return AiOp::MoreEqual;
    return AiOp::More;
}

int intOf(const XmlNode& n, std::string_view name, int fallback = 0) {
    return int(n.intValue(name).value_or(fallback));
}

int resourceOf(const XmlNode& n, std::string_view name) {
    const auto v = n.value(name);
    return v ? UnitDesc::resourceId(*v) : -1;
}

AiCondition parseCondition(const XmlNode& e) {
    using K = AiCondition::Kind;
    AiCondition c;
    c.op = parseOp(e);
    c.value = intOf(e, "Value");
    c.unitType = e.value("UnitType").value_or("");
    if (e.name == "CurResources") {
        c.kind = K::Resources;
        c.resource = resourceOf(e, "ResType");
    } else if (e.name == "AmountOfReadyUnits") {
        c.kind = K::ReadyUnits;
    } else if (e.name == "GamingTime") {
        c.kind = K::GamingTime;
        c.value = intOf(e, "Time");
    } else if (e.name == "UnitsProduced") {
        c.kind = K::UnitsProduced;
    } else if (e.name == "OnceInPeriod") {
        c.kind = K::OnceInPeriod;
        c.value = intOf(e, "Period");
    } else if (e.name == "FreeLivePlaces") {
        c.kind = K::FreeLivePlaces;
    } else if (e.name == "TotalLivePlaces") {
        c.kind = K::TotalLivePlaces;
    } else if (e.name == "Difficulty") {
        c.kind = K::Difficulty;
        c.value = aiDifficultyLevel(e.value("DifficultyValue").value_or("Normal"));
    } else if (e.name == "EnemyNearBase") {
        c.kind = K::EnemyNearBase;
        c.radius = intOf(e, "Radius");
        c.minAmount = intOf(e, "MinAmount");
        c.maxAmount = intOf(e, "MaxAmount", 1000000);
    } else if (e.name == "UnitsLost") {
        c.kind = K::UnitsLost;
    }
    return c;
}

// Цепочка условий в обёртке: wrapper/<Condition>/<Элемент>/<Condition>/<Элемент>…
std::vector<AiCondition> parseChain(const XmlNode* wrapper) {
    std::vector<AiCondition> out;
    const XmlNode* link = wrapper ? wrapper->child("Condition") : nullptr;
    while (link) {
        const XmlNode* element = nullptr;
        for (const XmlNode& c : link->children)
            if (c.name != "Description") {
                element = &c;
                break;
            }
        if (!element) break;
        out.push_back(parseCondition(*element));
        link = element->child("Condition");
    }
    return out;
}

AiAction parseAction(const XmlNode& e) {
    using K = AiAction::Kind;
    AiAction a;
    a.unitType = e.value("UnitType").value_or("");
    a.maxAmount = intOf(e, "MaxAmount");
    a.resourcePercent = intOf(e, "ResourcePercent", 100);
    a.probability = intOf(e, "Probability", 100);
    if (e.name == "Build") {
        a.kind = K::Build;
        if (const XmlNode* r = e.child("Requirements")) {
            a.nearTrees = r->boolValue("NearTrees").value_or(false);
            a.nearStones = r->boolValue("NearStones").value_or(false);
            a.nearRoad = r->boolValue("NearRoad").value_or(false);
            a.multiple = r->boolValue("MultipleConstruction").value_or(true);
            a.minDistFromCenter = intOf(*r, "MinDistFromCenter");
            a.minPeasants = std::max(1, intOf(*r, "MinPeasants", 1));
            a.maxPeasants = std::max(a.minPeasants, intOf(*r, "MaxPeasants", a.minPeasants));
        }
    } else if (e.name == "Produce") {
        a.kind = K::Produce;
        a.packSize = std::max(1, intOf(e, "PackSize", 1));
    } else if (e.name == "AddResources") {
        a.kind = K::AddResources;
        a.resource = resourceOf(e, "ResType");
        a.amount = intOf(e, "Amount");
    } else if (e.name == "BuyResource") {
        a.kind = K::BuyResource;
        a.sellResource = resourceOf(e, "ResTypeToSell");
        a.resource = resourceOf(e, "ResTypeToBuy");
        a.amount = intOf(e, "Amount");
    } else if (e.name == "PeonsDistribution") {
        a.kind = K::PeonsDistribution;
        a.onWood = intOf(e, "OnWood");
        a.onStone = intOf(e, "OnStone");
        a.onFood = intOf(e, "OnFood");
    }
    return a;
}

void collectIds(const XmlNode* list, std::vector<std::string>& out) {
    if (!list) return;
    for (const XmlNode& d : list->children)
        if (auto id = d.value("UnitID")) out.push_back(*id);
}

} // namespace

AiScript AiScript::parse(const XmlNode& root) {
    // Корень документа — тег «<>»; парсер может отдать его самим узлом или единственным ребёнком.
    const XmlNode* top = &root;
    if (!top->child("Folders") && !top->child("Units") && top->children.size() == 1) top = &top->children.front();
    AiScript s;
    collectIds(top->child("Units"), s.unitIds);
    collectIds(top->child("Buildings"), s.unitIds);
    if (const XmlNode* folders = top->child("Folders"))
        for (const XmlNode& f : folders->children) {
            if (f.name != "AI_Folder") continue;
            AiFolder folder;
            folder.condition = parseChain(f.child("FolderCondition"));
            if (const XmlNode* rules = f.child("Rules"))
                for (const XmlNode& t : rules->children) {
                    if (t.name != "AI_Trigger") continue;
                    AiTrigger trigger;
                    trigger.condition = parseChain(t.child("Condition"));
                    if (const XmlNode* actions = t.child("Action"))
                        for (const XmlNode& a : actions->children)
                            if (a.name != "Description") trigger.actions.push_back(parseAction(a));
                    folder.rules.push_back(std::move(trigger));
                }
            s.folders.push_back(std::move(folder));
        }
    return s;
}

std::string AiScript::nationCode() const {
    for (const std::string& id : unitIds) {
        const auto open = id.rfind('('), close = id.rfind(')');
        if (open != std::string::npos && close != std::string::npos && close > open + 1)
            return id.substr(open + 1, close - open - 1);
    }
    return {};
}

int aiDifficultyLevel(std::string_view name) {
    if (equalsNoCase(name, "Easy")) return 0;
    if (equalsNoCase(name, "Hard")) return 2;
    if (equalsNoCase(name, "VeryHard")) return 3;
    return 1;
}

bool aiCompare(int a, AiOp op, int b) {
    switch (op) {
    case AiOp::Less: return a < b;
    case AiOp::LessEqual: return a <= b;
    case AiOp::Equal: return a == b;
    case AiOp::NotEqual: return a != b;
    case AiOp::MoreEqual: return a >= b;
    case AiOp::More: return a > b;
    }
    return false;
}

} // namespace oc2::assets
