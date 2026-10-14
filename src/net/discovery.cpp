#include "net/discovery.hpp"

#include <algorithm>
#include <sstream>

#include "core/version.hpp"

namespace oc2::net {

namespace {
std::string safe(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}
} // namespace

std::string Discovery::encode(const std::string& version, std::uint16_t port, int humans, const std::string& name,
                              const std::string& map) {
    return "OC2D\t" + version + '\t' + std::to_string(port) + '\t' + std::to_string(humans) + '\t' + safe(name) + '\t' + safe(map);
}

bool Discovery::decode(const std::string& text, const std::string& version, Game& out) {
    std::vector<std::string> f;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, '\t');) f.push_back(part);
    if (f.size() < 6 || f[0] != "OC2D" || f[1] != version) return false;
    try {
        const int port = std::stoi(f[2]);
        if (port <= 0 || port > 65535) return false;
        out.port = std::uint16_t(port);
        out.humans = std::stoi(f[3]);
    } catch (const std::exception&) {
        return false;
    }
    out.name = f[4];
    out.map = f[5];
    return true;
}

void Discovery::listen(bool on) {
    listening_ = on;
    if (on && !beacon_) beacon_ = Beacon::open(kPort);
    if (!on && line_.empty()) beacon_.reset();
}

void Discovery::announce(const std::string& name, std::uint16_t tcpPort, const std::string& map, int humans) {
    if (!beacon_) beacon_ = Beacon::open(kPort);
    line_ = encode(kVersion, tcpPort, humans, name, map);
}

void Discovery::stop() {
    listening_ = false;
    line_.clear();
    games_.clear();
    beacon_.reset();
}

void Discovery::pump() {
    if (!beacon_) return;
    const auto now = std::chrono::steady_clock::now();
    if (!line_.empty() && now - lastSend_ >= std::chrono::seconds(1)) {
        beacon_->announce(line_);
        lastSend_ = now;
    }
    std::string msg, from;
    while (beacon_->poll(msg, from)) {
        if (!listening_) continue;
        Game g;
        if (!decode(msg, kVersion, g)) continue;
        g.host = from;
        g.seen = now;
        auto it = std::find_if(games_.begin(), games_.end(), [&](const Game& o) { return o.host == g.host && o.port == g.port; });
        if (it != games_.end()) *it = g;
        else games_.push_back(g);
    }
    games_.erase(std::remove_if(games_.begin(), games_.end(), [&](const Game& g) { return now - g.seen > std::chrono::seconds(4); }),
                 games_.end());
}

} // namespace oc2::net
