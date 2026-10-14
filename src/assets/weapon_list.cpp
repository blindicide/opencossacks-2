#include "assets/weapon_list.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace oc2::assets {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

WeaponList WeaponList::parse(std::string_view text) {
    WeaponList list;
    std::istringstream in{std::string(text)};
    std::string line, section;
    while (std::getline(in, line)) {
        if (const auto c = line.find("//"); c != std::string::npos) line.resize(c);
        std::istringstream ls(line);
        std::vector<std::string> w;
        for (std::string t; ls >> t;) w.push_back(t);
        if (w.empty() || w[0][0] == '/') continue;
        if (w[0][0] == '[') {
            section = upper(w[0]);
            continue;
        }
        if (section == "[MEMBERS]" && w.size() >= 2) {
            Weapon& wp = list.weapons[upper(w[0])];
            wp.name = w[0];
            wp.animation = w[1];
            if (w.size() >= 4) {
                wp.damage = std::atoi(w[2].c_str());
                wp.radius = std::atoi(w[3].c_str());
            }
            if (w.size() >= 5) wp.transparency = w[4];
            if (w.size() >= 6) wp.gravity = w[5];
            if (w.size() >= 8) {
                wp.propagation = w[6];
                wp.speed = std::atoi(w[7].c_str());
            }
        } else if (section == "[CHILDWEAPON]" && w.size() >= 5) {
            auto it = list.weapons.find(upper(w[0]));
            if (it == list.weapons.end()) continue;
            const int n = std::atoi(w[4].c_str());
            for (int i = 0; i < n && 5 + std::size_t(i) < w.size(); ++i) it->second.children.push_back(w[5 + std::size_t(i)]);
        } else if (section == "[SOUND]" && w.size() >= 2) {
            auto it = list.weapons.find(upper(w[0]));
            if (it != list.weapons.end()) it->second.sound = w[1];
        }
    }
    return list;
}

const Weapon* WeaponList::find(std::string_view name) const {
    auto it = weapons.find(upper(name));
    return it == weapons.end() ? nullptr : &it->second;
}

const Weapon* WeaponList::flying(std::string_view name) const {
    const Weapon* w = find(name);
    for (int depth = 0; w && depth < 4; ++depth) {
        if ((w->propagation == "FLY" || w->propagation == "ANGLE") && w->speed > 0) return w;
        const Weapon* next = nullptr;
        for (const std::string& c : w->children)
            if (const Weapon* ch = find(c); ch && (ch->propagation == "FLY" || ch->propagation == "ANGLE")) {
                next = ch;
                break;
            }
        if (!next && !w->children.empty()) next = find(w->children.front());
        w = next;
    }
    return nullptr;
}

} // namespace oc2::assets
