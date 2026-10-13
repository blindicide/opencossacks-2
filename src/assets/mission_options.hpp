#pragma once

#include <array>
#include <string>
#include <string_view>

namespace oc2::assets {

// Настройки миссии из раздела MOPT карты (XML <Players><PlayerN>…).
struct MissionPlayer {
    std::string team;            // «Team1»; пусто — без команды
    std::string nation;          // «FRANCE»
    bool disableInSingle = false;
    bool autoTransfer = false;   // AutoTransferUnitsToNearestTeam
};

struct MissionOptions {
    int maxPlayers = 0;
    bool disableTeams = false;
    std::array<MissionPlayer, 7> players{};

    // Текст XML (Windows-1251); пустой текст — настройки по умолчанию.
    static MissionOptions parse(std::string_view xml);

    // Команды для мира: номер команды каждого игрока 0..7 (одинаковые — союзники).
    // Игроки без команды или при DisableTeams — каждый сам за себя; 7 — свой номер.
    std::array<int, 8> teams() const;
    // Кому в одиночной игре отходят юниты игрока p: отключённый (DisableInSingle)
    // с AutoTransferUnitsToNearestTeam — первому включённому союзнику; иначе p.
    int singleOwner(int p) const;
};

} // namespace oc2::assets
