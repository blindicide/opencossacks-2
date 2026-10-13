#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace oc2::assets {

// Миссия из Missions\Missions.txt (загрузчик — около FUN_00fd1570): пять строк
// «ID, ключ названия, DLL сценария, карта, описание», затем число N и N строк
// дополнительных данных (у сражений — картинка «файл.jpg ширина высота» и рамка).
struct Mission {
    std::string id;          // «#Skirmish18»
    std::string nameKey;     // «#Skirmish18_TXT»
    std::string dll;         // «-» — без сценария
    std::string map;         // путь с «\» как в данных
    std::string description; // путь к .txt
    std::vector<std::string> extra;
};

struct MissionList {
    std::vector<Mission> missions;

    // Missions.txt (Windows-1251). Разбор останавливается, когда пяти строк подряд
    // уже не набрать, как в оригинале. FormatError — если нет числа N.
    static MissionList parse(std::string_view text);
    // SingleMiss.txt / SingleBatl.txt: число, затем ID; неизвестные ID пропускаются.
    // Возвращает номера в missions.
    std::vector<std::size_t> selection(std::string_view text) const;

    const Mission* find(std::string_view id) const;
};

} // namespace oc2::assets
