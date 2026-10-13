#include "platform/sound_bank.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>

#include <SDL3/SDL.h>

#include "assets/game_paths.hpp"

namespace oc2::platform {

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string gamePath(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

} // namespace

SoundBank::SoundBank(Audio& audio, std::filesystem::path dataRoot) : audio_(audio), root_(std::move(dataRoot)) {
    if (auto path = assets::findCaseInsensitive(root_, "Sound/Sounds.xml")) {
        try {
            const auto bytes = readFile(*path);
            list_ = assets::SoundList::parse({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Sounds.xml: %s\n", e.what());
        }
    }
}

Audio::Buffer SoundBank::load(const std::string& file) {
    auto it = cache_.find(file);
    if (it != cache_.end()) return it->second;
    Audio::Buffer buf;
    if (auto path = assets::findCaseInsensitive(root_, gamePath(file))) {
        try {
            const auto bytes = readFile(*path);
            const std::string ext = path->extension().string();
            const bool ogg = ext == ".ogg" || ext == ".OGG" || ext == ".Ogg";
            buf = Audio::prepare(ogg ? assets::decodeOgg(bytes) : assets::decodeWav(bytes));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "звук %s: %s\n", file.c_str(), e.what());
        }
    } else {
        std::fprintf(stderr, "нет звука %s\n", file.c_str());
    }
    cache_.emplace(file, buf);
    return buf;
}

void SoundBank::play(std::string_view name, float volume, float pan) {
    if (!audio_.available() || name.empty() || name == "NONE") return;
    const assets::SoundGroup* g = list_.find(name);
    if (!g || g->variants.empty()) {
        if (!reported_[std::string(name)]) {
            reported_[std::string(name)] = true;
            std::fprintf(stderr, "нет группы звуков %s\n", std::string(name).c_str());
        }
        return;
    }
    const double now = double(SDL_GetTicksNS()) / 1e9;
    double& next = nextAllowed_[g->name];
    if (now < next) return;
    std::uniform_real_distribution<float> var(0.0f, g->pauseVariation);
    next = now + g->minPause + var(random_);
    std::uniform_int_distribution<std::size_t> pick(0, g->variants.size() - 1);
    const assets::SoundVariant& v = g->variants[pick(random_)];
    if (auto buf = load(v.file)) audio_.play(buf, volume * g->volume * v.volume, pan);
}

void SoundBank::updateMusic() {
    if (!audio_.available()) return;
    if (pendingMusic_.valid()) {
        if (pendingMusic_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        if (Audio::Buffer b = pendingMusic_.get()) audio_.playMusic(b, 1.0f, false);
        return;
    }
    if (audio_.musicPlaying()) return;
    if (tracks_.empty()) {
        std::error_code ec;
        if (auto dir = assets::findCaseInsensitive(root_, "Music"))
            for (const auto& e : std::filesystem::directory_iterator(*dir, ec)) {
                std::string ext = e.path().extension().string();
                for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (ext == ".ogg") tracks_.push_back(e.path());
            }
        std::sort(tracks_.begin(), tracks_.end());
        if (tracks_.empty()) return;
    }
    std::vector<std::filesystem::path> fresh;
    for (const auto& t : tracks_)
        if (std::find(recent_.begin(), recent_.end(), t) == recent_.end()) fresh.push_back(t);
    if (fresh.empty()) fresh = tracks_;
    std::uniform_int_distribution<std::size_t> pick(0, fresh.size() - 1);
    const std::filesystem::path track = fresh[pick(random_)];
    recent_.push_back(track);
    if (recent_.size() > 5 || recent_.size() >= tracks_.size()) recent_.erase(recent_.begin());
    pendingMusic_ = std::async(std::launch::async, [track]() -> Audio::Buffer {
        try {
            return Audio::prepare(assets::decodeOgg(readFile(track)));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "музыка %s: %s\n", track.string().c_str(), e.what());
            return nullptr;
        }
    });
}

void SoundBank::playMusic(const std::string& file, bool loop) {
    if (!audio_.available()) return;
    if (auto buf = load(file)) audio_.playMusic(buf, 1.0f, loop);
}

} // namespace oc2::platform
