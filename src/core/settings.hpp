#pragma once

#include <filesystem>

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

    static std::filesystem::path defaultPath();
    // Читает ключ=значение; нет файла или строка не разобрана — остаётся прежнее значение.
    void load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

} // namespace oc2::core
