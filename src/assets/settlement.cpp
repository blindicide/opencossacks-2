#include "assets/settlement.hpp"

#include <cstdlib>

namespace oc2::assets {

namespace {

int intOf(const XmlNode& n, std::string_view name) { return int(n.intValue(name).value_or(0)); }

SettlementDesc::PerResource perResource(const XmlNode* n) {
    SettlementDesc::PerResource out{};
    if (!n) return out;
    for (const XmlNode& c : n->children)
        if (const int r = UnitDesc::resourceId(c.name); r >= 0) out[std::size_t(r)] = int(std::strtol(c.text.c_str(), nullptr, 10));
    return out;
}

} // namespace

int SettlementDesc::resource() const {
    for (std::size_t i = 0; i < obozResAmount.size(); ++i)
        if (obozResAmount[i] > 0) return int(i);
    return -1;
}

SettlementDesc SettlementDesc::parse(const XmlNode& root) {
    // Корень — тег «<>»: узел или единственный ребёнок, как у сценариев ИИ.
    const XmlNode* top = &root;
    if (!top->child("StartOwner") && top->children.size() == 1) top = &top->children.front();
    SettlementDesc d;
    d.startOwner = int(top->intValue("StartOwner").value_or(7));
    d.maxResAmount = perResource(top->child("MaxResAmount"));
    d.copId = top->value("CopID").value_or("");
    d.copBuildTime = intOf(*top, "CopBuidTime");
    d.copMaxAmount = intOf(*top, "CopMaxAmount");
    d.peonId = top->value("PeonID").value_or("");
    d.peonBuildTime = intOf(*top, "PeonBuidTime");
    d.peonMaxAmount = perResource(top->child("PeonMaxAmount"));
    d.peonTimeOnProduce = perResource(top->child("PeonTimeOnProduce"));
    d.obozId = top->value("ObozID").value_or("");
    d.obozResAmount = perResource(top->child("ObozResAmount"));
    if (const XmlNode* ups = top->child("Upgrades"))
        for (const XmlNode& u : ups->children) {
            if (u.name != "SetlUpgrade") continue;
            Upgrade up;
            up.duration = intOf(u, "Duration");
            up.effectType = u.value("EffType").value_or("");
            up.effect = perResource(u.child("Effect"));
            up.price = perResource(u.child("Price"));
            d.upgrades.push_back(std::move(up));
        }
    return d;
}

} // namespace oc2::assets
