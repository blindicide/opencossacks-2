#include "assets/campaign_list.hpp"

#include "assets/xml_tree.hpp"

namespace oc2::assets {

CampaignList CampaignList::parse(std::string_view xml) {
    const XmlNode doc = parseXml(xml);
    const XmlNode* top = &doc;
    if (!top->child("cvi_Campaign") && top->children.size() == 1) top = &top->children.front();
    CampaignList out;
    for (const XmlNode& c : top->children) {
        if (c.name != "cvi_Campaign") continue;
        Campaign camp;
        camp.nameKey = c.value("Name").value_or("");
        camp.desk = c.value("DeskName").value_or("");
        if (const XmlNode* missions = c.child("Missions"))
            for (const XmlNode& m : missions->children) {
                if (m.name != "cvi_Mission") continue;
                CampaignMission mission;
                mission.nameKey = m.value("Name").value_or("");
                mission.descriptionKey = m.value("GameDescription").value_or("");
                mission.mapX = int(m.intValue("MapX").value_or(0));
                mission.mapY = int(m.intValue("MapY").value_or(0));
                if (const XmlNode* scene = m.child("Scene"))
                    if (const XmlNode* game = scene->child("cvi_mfGame")) mission.map = game->value("MissFileName").value_or("");
                camp.missions.push_back(std::move(mission));
            }
        out.campaigns.push_back(std::move(camp));
    }
    return out;
}

} // namespace oc2::assets
