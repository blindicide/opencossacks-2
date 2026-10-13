#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace oc2::assets {

// Звуки из Sound\Sounds.xml: группа (имя из .md и окон — «INTERF_NAVEL», «PIVEN»)
// и её варианты; при проигрывании берётся случайный вариант.
struct SoundVariant {
    std::string file; // «SOUND\INTERFACE\INTERF_SELECT1.WAV»
    float volume = 1.0f;
    bool looped = false;
};

struct SoundGroup {
    std::string name;
    float volume = 1.0f;
    float minPause = 0.0f;       // секунды между повторами группы
    float pauseVariation = 0.0f;
    std::vector<SoundVariant> variants;
};

struct SoundList {
    std::unordered_map<std::string, SoundGroup> groups; // имя в верхнем регистре → группа

    static SoundList parse(std::string_view xml);
    const SoundGroup* find(std::string_view name) const;
};

} // namespace oc2::assets
