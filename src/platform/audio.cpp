#include "platform/audio.hpp"

#include <algorithm>
#include <cstdio>

#include <SDL3/SDL.h>

namespace oc2::platform {

Audio::Audio() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "звук недоступен: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::callback, this);
    if (!stream_) {
        std::fprintf(stderr, "звук недоступен: %s\n", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream_);
}

Audio::~Audio() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

Audio::Buffer Audio::prepare(const assets::Pcm& pcm) {
    assets::Pcm stereo = assets::toStereo(pcm, kRate);
    return std::make_shared<const std::vector<std::int16_t>>(std::move(stereo.samples));
}

void Audio::play(const Buffer& sound, float volume, float pan) {
    if (!stream_ || !sound || sound->empty()) return;
    pan = std::clamp(pan, -1.0f, 1.0f);
    Voice v;
    v.data = sound;
    v.left = volume * std::min(1.0f, 1.0f - pan);
    v.right = volume * std::min(1.0f, 1.0f + pan);
    std::lock_guard lock(mutex_);
    if (int(voices_.size()) >= kVoices) voices_.erase(voices_.begin()); // самый старый уступает
    voices_.push_back(std::move(v));
}

void Audio::playMusic(const Buffer& music, float volume, bool loop) {
    if (!stream_) return;
    std::lock_guard lock(mutex_);
    music_ = Voice{music, 0, volume, volume, loop};
}

void Audio::stopMusic() {
    std::lock_guard lock(mutex_);
    music_ = Voice{};
}

bool Audio::musicPlaying() const {
    std::lock_guard lock(mutex_);
    return music_.data != nullptr;
}

void Audio::callback(void* self, SDL_AudioStream* stream, int additional, int) {
    auto* a = static_cast<Audio*>(self);
    const std::size_t frames = std::size_t(std::max(additional, 0)) / 4;
    if (frames == 0) return;
    a->scratch_.assign(frames * 2, 0);
    a->mix(a->scratch_.data(), frames);
    SDL_PutAudioStreamData(stream, a->scratch_.data(), int(frames * 4));
}

void Audio::mix(std::int16_t* out, std::size_t frames) {
    std::vector<float> acc(frames * 2, 0.0f);
    std::lock_guard lock(mutex_);
    auto add = [&](Voice& v, float gain) {
        const auto& s = *v.data;
        const std::size_t total = s.size() / 2;
        for (std::size_t i = 0; i < frames; ++i) {
            if (v.pos >= total) {
                if (!v.loop || total == 0) return false;
                v.pos = 0;
            }
            acc[i * 2] += float(s[v.pos * 2]) * v.left * gain;
            acc[i * 2 + 1] += float(s[v.pos * 2 + 1]) * v.right * gain;
            ++v.pos;
        }
        return true;
    };
    for (auto it = voices_.begin(); it != voices_.end();)
        it = add(*it, masterVolume) ? it + 1 : voices_.erase(it);
    if (music_.data && !add(music_, masterVolume * musicVolume)) music_ = Voice{};
    for (std::size_t i = 0; i < frames * 2; ++i)
        out[i] = std::int16_t(std::clamp(acc[i], -32768.0f, 32767.0f));
}

} // namespace oc2::platform
