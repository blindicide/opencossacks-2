#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace oc2::core {

// Настройки игрока (окно «Настройки»): громкость, скорость прокрутки и флаги.
// Хранятся в ~/.config/opencossacks2/settings.ini (XDG_CONFIG_HOME учитывается),
// в Windows — рядом, в %APPDATA%\opencossacks2.
struct Settings {
    int soundVolume = 85;  // 0..100, звуки
    int musicVolume = 60;  // 0..100, музыка
    int scrollSpeed = 50;  // 0..100, скорость прокрутки карты
    bool musicOn = true;
    bool showHint = true;
    bool showVideo = true;
    bool arcade = false;   // аркадный режим
    bool fogOfWar = true;  // туман войны (в settings.xml оригинала DisableFogOfWar=false)
    // Профили игрока (имя в UTF-8) и номер выбранного (−1 — профилей нет).
    struct Profile {
        std::string name;
        int race = 0;       // номер нации в списке меню
        int difficulty = 1; // 0..2
    };
    std::vector<Profile> profiles;
    int profile = -1;
    const Profile* currentProfile() const {
        return profile >= 0 && std::size_t(profile) < profiles.size() ? &profiles[std::size_t(profile)] : nullptr;
    }
    // Пройдено миссий в каждой кампании (по порядку campaign.xml).
    std::vector<int> campaignDone;

    static std::filesystem::path defaultPath();
    // Читает ключ=значение; нет файла или строка не разобрана — остаётся прежнее значение.
    void load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

} // namespace oc2::core
