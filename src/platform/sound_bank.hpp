#pragma once

#include <filesystem>
#include <future>
#include <vector>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>

#include "assets/sound_list.hpp"
#include "platform/audio.hpp"

namespace oc2::platform {

// Звуки игры по именам групп Sound\Sounds.xml: файлы читаются при первом
// проигрывании; MinimalPause(sec) и PauseVariation(sec) ограничивают повторы группы.
class SoundBank {
public:
    SoundBank(Audio& audio, std::filesystem::path dataRoot);

    // Проигрывает группу (случайный вариант). Неизвестное имя — тишина (сообщение один раз).
    void play(std::string_view group, float volume = 1.0f, float pan = 0.0f);
    // Музыка из файла (ogg/wav), путь относительно данных.
    void playMusic(const std::string& file, bool loop = true);

    // Озвучка реплики из файла (путь относительно данных); возвращает длительность в шагах по 40 мс
    // (0 — файла нет). startNow = false — только узнать длительность.
    int playVoice(const std::string& file, bool startNow = true);

    // Фоновая музыка как у оригинала (FUN_01504cc0): случайная дорожка из Music/*.ogg,
    // не из пяти последних; следующая разбирается в фоне, когда текущая кончилась.
    void updateMusic();

    std::size_t groupCount() const { return list_.groups.size(); }

private:
    Audio::Buffer load(const std::string& file);

    Audio& audio_;
    std::filesystem::path root_;
    assets::SoundList list_;
    std::unordered_map<std::string, Audio::Buffer> cache_;      // файл → звук (nullptr — не читается)
    std::unordered_map<std::string, double> nextAllowed_;       // группа → когда можно снова
    std::unordered_map<std::string, bool> reported_;
    std::mt19937 random_{12345};
    std::vector<std::filesystem::path> tracks_;
    std::vector<std::filesystem::path> recent_;
    std::future<Audio::Buffer> pendingMusic_;
};

} // namespace oc2::platform
