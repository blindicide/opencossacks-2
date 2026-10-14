#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "assets/audio_decode.hpp"

struct SDL_AudioStream;

namespace oc2::platform {

// Микшер поверх SDL3: стерео 44,1 кГц, 16 бит. Звуки — заранее переведённые в
// этот формат буферы; одновременно звучит до kVoices эффектов и одна музыка.
// Если звуковое устройство не открылось (сервер без звука), всё молча игнорируется.
class Audio {
public:
    static constexpr int kRate = 44100;
    static constexpr int kVoices = 32;
    using Buffer = std::shared_ptr<const std::vector<std::int16_t>>; // стерео-отсчёты

    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool available() const { return stream_ != nullptr; }

    // Переводит звук в формат микшера.
    static Buffer prepare(const assets::Pcm& pcm);

    // volume 0..1; pan −1 (лево) … 1 (право).
    void play(const Buffer& sound, float volume = 1.0f, float pan = 0.0f);
    void playMusic(const Buffer& music, float volume, bool loop);
    void stopMusic();
    bool musicPlaying() const;

    float masterVolume = 1.0f, musicVolume = 0.6f, sfxVolume = 0.85f;

private:
    struct Voice {
        Buffer data;
        std::size_t pos = 0;
        float left = 1.0f, right = 1.0f;
        bool loop = false;
    };
    static void callback(void* self, SDL_AudioStream* stream, int additional, int total);
    void mix(std::int16_t* out, std::size_t frames);

    SDL_AudioStream* stream_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    Voice music_;
    std::vector<std::int16_t> scratch_;
};

} // namespace oc2::platform
