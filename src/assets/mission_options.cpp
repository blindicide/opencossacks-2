#include "assets/mission_options.hpp"

#include <map>

#include "assets/xml_tree.hpp"

namespace oc2::assets {

MissionOptions MissionOptions::parse(std::string_view xml) {
    MissionOptions o;
    if (xml.empty()) return o;
    const XmlNode doc = parseXml(xml);
    o.maxPlayers = int(doc.intValue("MaxPlayers").value_or(0));
    const XmlNode* players = doc.child("Players");
    if (!players) return o;
    o.disableTeams = players->boolValue("DisableTeams").value_or(false);
    for (std::size_t p = 0; p < o.players.size(); ++p) {
        const XmlNode* n = players->child("Player" + std::to_string(p));
        if (!n) continue;
        MissionPlayer& mp = o.players[p];
        mp.team = n->value("Team").value_or("");
        mp.nation = n->value("Nation").value_or("");
        mp.disableInSingle = n->boolValue("DisableInSingle").value_or(false);
        mp.autoTransfer = n->boolValue("AutoTransferUnitsToNearestTeam").value_or(false);
    }
    return o;
}

std::array<int, 8> MissionOptions::teams() const {
    std::array<int, 8> t{0, 1, 2, 3, 4, 5, 6, 7};
    if (disableTeams) return t;
    std::map<std::string, int> ids;
    for (std::size_t p = 0; p < players.size(); ++p) {
        const std::string& name = players[p].team;
        if (name.empty()) continue;
        auto it = ids.emplace(name, int(p)).first; // номер команды — первый её игрок
        t[p] = it->second;
    }
    return t;
}

int MissionOptions::singleOwner(int p) const {
    if (p < 0 || p >= int(players.size())) return p;
    const MissionPlayer& mp = players[std::size_t(p)];
    if (!mp.disableInSingle || !mp.autoTransfer || mp.team.empty() || disableTeams) return p;
    for (std::size_t q = 0; q < players.size(); ++q)
        if (int(q) != p && !players[q].disableInSingle && players[q].team == mp.team) return int(q);
    return p;
}

} // namespace oc2::assets
