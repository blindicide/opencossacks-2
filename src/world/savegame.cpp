#include "world/savegame.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "core/settings.hpp"

namespace oc2::world {

namespace fs = std::filesystem;

fs::path SaveGame::directory() { return core::Settings::defaultPath().parent_path() / "saves"; }

fs::path SaveGame::pathFor(const std::string& name) {
    std::string file;
    for (const char c : name)
        file += (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
                 static_cast<unsigned char>(c) < 32)
                    ? '_'
                    : c;
    if (file.empty() || file == "." || file == "..") file = "save";
    file += ".sav";
    return directory() / fs::path(std::u8string(file.begin(), file.end()));
}

bool SaveGame::write() const {
    std::error_code ec;
    fs::create_directories(directory(), ec);
    std::ofstream out(pathFor(name), std::ios::trunc);
    if (!out) return false;
    out << "version=" << kVersion << "\nname=" << name << "\ntime=" << time << "\nmap=" << map << "\nsetup=" << setup
        << "\ndifficulty=" << difficulty << "\nplayer=" << player << "\ncampaign=" << campaign << "\nmission=" << mission
        << "\nsteps=" << steps << '\n';
    for (const auto& s : slots) out << "slot=" << s[0] << ' ' << s[1] << ' ' << s[2] << ' ' << s[3] << '\n';
    for (const World::Command& c : journal)
        out << "cmd " << c.step << ' ' << c.kind << ' ' << c.a[0] << ' ' << c.a[1] << ' ' << c.a[2] << ' ' << c.a[3] << ' '
            << c.s << '\n';
    return bool(out);
}

std::optional<SaveGame> SaveGame::read(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;
    SaveGame g;
    std::string line;
    std::size_t slot = 0;
    bool versionOk = false;
    while (std::getline(in, line)) {
        if (line.rfind("cmd ", 0) == 0) {
            std::istringstream ss(line.substr(4));
            World::Command c;
            ss >> c.step >> c.kind >> c.a[0] >> c.a[1] >> c.a[2] >> c.a[3];
            if (!ss) return std::nullopt;
            std::getline(ss >> std::ws, c.s);
            g.journal.push_back(std::move(c));
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        try {
            if (key == "version") versionOk = std::stoi(val) == kVersion;
            else if (key == "name") g.name = val;
            else if (key == "time") g.time = std::stoll(val);
            else if (key == "map") g.map = val;
            else if (key == "setup") g.setup = val == "1";
            else if (key == "difficulty") g.difficulty = std::stoi(val);
            else if (key == "player") g.player = std::stoi(val);
            else if (key == "campaign") g.campaign = std::stoi(val);
            else if (key == "mission") g.mission = std::stoi(val);
            else if (key == "steps") g.steps = std::stoull(val);
            else if (key == "slot" && slot < g.slots.size()) {
                std::istringstream ss(val);
                ss >> g.slots[slot][0] >> g.slots[slot][1] >> g.slots[slot][2] >> g.slots[slot][3];
                ++slot;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (!versionOk || g.map.empty() || g.name.empty()) return std::nullopt;
    return g;
}

std::vector<SaveGame> SaveGame::list() {
    std::vector<SaveGame> all;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory(), ec))
        if (entry.path().extension() == ".sav")
            if (auto g = read(entry.path())) all.push_back(std::move(*g));
    std::sort(all.begin(), all.end(), [](const SaveGame& a, const SaveGame& b) { return a.time > b.time; });
    return all;
}

bool SaveGame::remove(const std::string& name) {
    std::error_code ec;
    return fs::remove(pathFor(name), ec);
}

} // namespace oc2::world
