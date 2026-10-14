#include "render/effects.hpp"

#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>

#include "assets/game_paths.hpp"
#include "world/world.hpp"

namespace oc2::render {

namespace {

std::string readText(const std::filesystem::path& root, const char* name) {
    auto path = assets::findCaseInsensitive(root, name);
    if (!path) {
        std::fprintf(stderr, "нет %s — эффекты оружия не выводятся\n", name);
        return {};
    }
    std::ifstream in(*path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

Effects::Effects(const std::filesystem::path& dataRoot) {
    try {
        ads_ = assets::UnitDesc::parse(readText(dataRoot, "WEAPON.ADS"));
        weapons_ = assets::WeaponList::parse(readText(dataRoot, "weapon.nds"));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "эффекты оружия: %s\n", e.what());
    }
}

std::optional<std::string> Effects::spawn(const std::string& weapon, int x, int y, int direction) {
    std::optional<std::string> sound;
    const int mx = x + ((world::cos256(direction) * 24) >> 8);
    const int my = y + ((world::sin256(direction) * 24) >> 8);
    const assets::Weapon* w = weapons_.find(weapon);
    for (int depth = 0; w && depth < 6; ++depth) {
        if (!sound && !w->sound.empty()) sound = w->sound;
        if (w->animation != "#POINT") // точка — невидимый носитель цепочки
            // В weapon.nds анимация пишется через «#», в WEAPON.ADS часть — через «@».
            if (const assets::UnitAnimation* a = ads_.animation(w->animation)
                                                     ? ads_.animation(w->animation)
                                                     : ads_.animation("@" + w->animation.substr(1));
                a && !a->frames.empty())
                instances_.push_back({a, 0, mx, my, 28});
        if (w->children.empty()) break;
        std::uniform_int_distribution<std::size_t> pick(0, w->children.size() - 1);
        w = weapons_.find(w->children[pick(random_)]);
    }
    return sound;
}

const assets::UnitAnimation* Effects::animationOf(const std::string& name) const {
    if (name.size() < 2) return nullptr;
    const assets::UnitAnimation* a = ads_.animation(name);
    return a ? a : ads_.animation("@" + name.substr(1));
}

void Effects::step() {
    for (auto it = instances_.begin(); it != instances_.end();) {
        if (++it->frame >= int(it->animation->frames.size()))
            it = instances_.erase(it);
        else
            ++it;
    }
}

} // namespace oc2::render
