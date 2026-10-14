#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace oc2::assets {

// Кампании из Dialogs\campaign.xml (cvi_Campaign): имя-ключ, окно карты в меню
// («CampFrance») и миссии с картой .m3d и положением на карте кампании.
struct CampaignMission {
    std::string nameKey;        // «#Miss1»
    std::string descriptionKey; // «#MissDesc01»
    std::string map;            // путь с «\» как в данных
    int mapX = 0, mapY = 0;
};

struct Campaign {
    std::string nameKey; // «#CampFrance»
    std::string desk;    // DeskName — окно карты кампании в M_Single
    std::vector<CampaignMission> missions;
};

struct CampaignList {
    std::vector<Campaign> campaigns;
    // Разбор campaign.xml (Windows-1251).
    static CampaignList parse(std::string_view xml);
};

// Миссии «Обучения» и «Миссий» из Missions\ew2.xml (cvi_Missions).
struct Ew2Mission {
    std::string map;            // FileM3D, путь с «\»
    std::string nameKey;        // «#EW2_Tut01»
    std::string descriptionKey; // «#EW2_Tut01_descr»
    bool tutorial = false;
};

struct Ew2List {
    std::vector<Ew2Mission> missions;
    static Ew2List parse(std::string_view xml);
};

} // namespace oc2::assets
