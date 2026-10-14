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
        int value = 0;
        try {
            value = std::stoi(line.substr(eq + 1));
        } catch (const std::exception&) {
            continue;
        }
        const int pct = std::clamp(value, 0, 100);
        if (key == "sound") soundVolume = pct;
        else if (key == "music") musicVolume = pct;
        else if (key == "scroll") scrollSpeed = pct;
        else if (key == "music_on") musicOn = value != 0;
        else if (key == "hints") showHint = value != 0;
        else if (key == "video") showVideo = value != 0;
        else if (key == "arcade") arcade = value != 0;
    }
}

void Settings::save(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    out << "sound=" << soundVolume << "\nmusic=" << musicVolume << "\nscroll=" << scrollSpeed
        << "\nmusic_on=" << musicOn << "\nhints=" << showHint << "\nvideo=" << showVideo
        << "\narcade=" << arcade << "\n";
}

} // namespace oc2::core
