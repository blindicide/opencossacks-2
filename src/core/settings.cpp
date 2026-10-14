#include "core/settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

namespace oc2::core {

std::filesystem::path Settings::defaultPath() {
    std::filesystem::path base;
#ifdef _WIN32
    if (const char* d = std::getenv("APPDATA")) base = d;
#else
    if (const char* d = std::getenv("XDG_CONFIG_HOME"); d && *d) base = d;
    else if (const char* h = std::getenv("HOME")) base = std::filesystem::path(h) / ".config";
#endif
    if (base.empty()) return "settings.ini";
    return base / "opencossacks2" / "settings.ini";
}

void Settings::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (key.rfind("profile_", 0) == 0) { // profile_N=нация|сложность|имя
            const std::string rest = line.substr(eq + 1);
            const auto a = rest.find('|'), b = a == std::string::npos ? a : rest.find('|', a + 1);
            if (b != std::string::npos) try {
                    Profile p;
                    p.race = std::clamp(std::stoi(rest.substr(0, a)), 0, 20);
                    p.difficulty = std::clamp(std::stoi(rest.substr(a + 1, b - a - 1)), 0, 2);
                    p.name = rest.substr(b + 1);
                    if (!p.name.empty() && profiles.size() < 32) profiles.push_back(std::move(p));
                } catch (const std::exception&) {
                }
            continue;
        }
        int value = 0;
        try {
            value = std::stoi(line.substr(eq + 1));
        } catch (const std::exception&) {
            continue;
        }
        if (key.rfind("campaign_", 0) == 0) {
            try {
                const auto n = std::size_t(std::stoul(key.substr(9)));
                if (n < 64) {
                    if (campaignDone.size() <= n) campaignDone.resize(n + 1, 0);
                    campaignDone[n] = std::max(0, value);
                }
            } catch (const std::exception&) {
            }
            continue;
        }
        const int pct = std::clamp(value, 0, 100);
        if (key == "profile") profile = value;
        else if (key == "sound") soundVolume = pct;
        else if (key == "music") musicVolume = pct;
        else if (key == "scroll") scrollSpeed = pct;
        else if (key == "music_on") musicOn = value != 0;
        else if (key == "hints") showHint = value != 0;
        else if (key == "video") showVideo = value != 0;
        else if (key == "arcade") arcade = value != 0;
        else if (key == "fog") fogOfWar = value != 0;
        else if (key == "fullscreen") fullscreen = value != 0;
    }
}

void Settings::save(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    out << "sound=" << soundVolume << "\nmusic=" << musicVolume << "\nscroll=" << scrollSpeed
        << "\nmusic_on=" << musicOn << "\nhints=" << showHint << "\nvideo=" << showVideo
        << "\narcade=" << arcade << "\nfog=" << fogOfWar << "\nfullscreen=" << fullscreen << "\n";
    out << "profile=" << profile << "\n";
    for (std::size_t i = 0; i < profiles.size(); ++i)
        out << "profile_" << i << "=" << profiles[i].race << "|" << profiles[i].difficulty << "|" << profiles[i].name << "\n";
    for (std::size_t i = 0; i < campaignDone.size(); ++i) out << "campaign_" << i << "=" << campaignDone[i] << "\n";
}

} // namespace oc2::core
