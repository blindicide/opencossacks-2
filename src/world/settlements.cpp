#include "world/settlements.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "assets/game_paths.hpp"

namespace oc2::world {

namespace {

// Ядро поселения по типу: префикс типа → файл описания.
struct CoreKind {
    std::string_view prefix;
    const char* dip;
};
constexpr CoreKind kCores[] = {
    {"BldMel(", "Dip/FoodNeutral.dip.xml"},
    {"BldRudCoal(", "Dip/CoalNeutral.dip.xml"},
    {"BldRudGold(", "Dip/GoldNeutral.dip.xml"},
    {"BldRudIron(", "Dip/IronNeutral.dip.xml"},
};

bool within(const Unit& u, const Unit& c, int radius) {
    const long dx = u.x() - c.x(), dy = u.y() - c.y();
    return dx * dx + dy * dy <= long(radius) * radius;
}

} // namespace

std::vector<Settlements::Settlement> Settlements::discover(const World& world, const UnitCatalog& catalog,
                                                          const std::filesystem::path& dataRoot) {
    std::vector<Settlement> found;
    std::vector<bool> taken(world.units().size(), false);
    for (std::size_t i = 0; i < world.units().size(); ++i) {
        const Unit& u = world.units()[i];
        if (u.player != 7 || !u.building() || !u.alive()) continue;
        const std::string_view type = catalog.typeOf(u.desc);
        if (!type.ends_with("(FR)")) continue;
        const CoreKind* kind = nullptr;
        for (const CoreKind& k : kCores)
            if (type.starts_with(k.prefix)) kind = &k;
        if (!kind) continue;
        const auto file = assets::findCaseInsensitive(dataRoot, kind->dip);
        if (!file) continue;
        Settlement s;
        try {
            std::ifstream in(*file, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            s.desc = assets::SettlementDesc::parse(assets::parseXml(text));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", kind->dip, e.what());
            continue;
        }
        s.core = i;
        s.buildings.push_back(i);
        taken[i] = true;
        found.push_back(std::move(s));
    }
    // Дома — к ближайшему ядру в kRadius.
    for (std::size_t i = 0; i < world.units().size(); ++i) {
        const Unit& u = world.units()[i];
        if (taken[i] || u.player != 7 || !u.building() || !catalog.typeOf(u.desc).starts_with("SelFraDom")) continue;
        Settlement* best = nullptr;
        long bestD = long(kRadius) * kRadius + 1;
        for (Settlement& s : found) {
            const Unit& c = world.units()[s.core];
            const long dx = u.x() - c.x(), dy = u.y() - c.y();
            if (dx * dx + dy * dy < bestD) bestD = dx * dx + dy * dy, best = &s;
        }
        if (best) best->buildings.push_back(i);
    }
    return found;
}

Settlements::Settlements(World& world, UnitCatalog& catalog, const std::filesystem::path& dataRoot)
    : Settlements(world, catalog, discover(world, catalog, dataRoot)) {}

Settlements::Settlements(World& world, UnitCatalog& catalog, std::vector<Settlement> list)
    : world_(world), catalog_(catalog), list_(std::move(list)) {
    for (Settlement& s : list_) {
        s.resource = s.desc.resource();
        s.owner = world_.units()[s.core].player;
        if (s.resource >= 0) s.produceIn = std::max(1, s.desc.peonTimeOnProduce[std::size_t(s.resource)]);
    }
}

void Settlements::step() {
    for (Settlement& s : list_) {
        if (!s.active) continue;
        const bool standing = std::any_of(s.buildings.begin(), s.buildings.end(),
                                          [&](std::size_t b) { return world_.units()[b].alive(); });
        if (!standing) {
            s.owner = 7;
            s.active = false;
            continue;
        }
        // Захват проверяется реже: в оригинале ProcessTribe идёт не каждый тик.
        if (world_.stepCount() % 8 == 0) capture(s);
        if (s.upgradeIn > 0 && --s.upgradeIn == 0) ++s.level;
        produce(s);
        guard(s);
    }
}

int Settlements::owned(int player, int resource, int minLevel) const {
    return int(std::count_if(list_.begin(), list_.end(), [&](const Settlement& s) {
        return s.active && s.owner == player && (resource < 0 || s.resource == resource) && s.level >= minLevel;
    }));
}

int Settlements::peons(const Settlement& s) const {
    int n = s.desc.peonMaxAmount[std::size_t(s.resource)];
    for (int i = 0; i < s.level && i < int(s.desc.upgrades.size()); ++i)
        n += s.desc.upgrades[std::size_t(i)].effect[std::size_t(s.resource)];
    return n;
}

bool Settlements::startUpgrade(std::size_t index, int level, int costPercent) {
    if (index >= list_.size()) return false;
    Settlement& s = list_[index];
    if (!s.active || s.owner == 7 || s.resource < 0 || s.upgradeIn > 0 || level != s.level + 1 ||
        level > int(s.desc.upgrades.size()))
        return false;
    const auto& up = s.desc.upgrades[std::size_t(level - 1)];
    auto& have = world_.resources[std::size_t(s.owner & 7)];
    std::array<int, assets::UnitDesc::kResources> cost{};
    for (std::size_t r = 0; r < cost.size(); ++r) {
        cost[r] = up.price[r] * costPercent / 100;
        if (have[r] < cost[r]) return false;
    }
    for (std::size_t r = 0; r < cost.size(); ++r) have[r] -= cost[r];
    s.upgradeIn = std::max(1, up.duration);
    return true;
}

std::optional<std::size_t> Settlements::upgradeCandidate(int player, int resource, int level, int x, int y,
                                                          int maxDistance) const {
    std::optional<std::size_t> best;
    long long bestDist = 0;
    for (std::size_t i = 0; i < list_.size(); ++i) {
        const Settlement& s = list_[i];
        if (!s.active || s.owner != player || s.resource != resource || s.upgradeIn > 0 || s.level + 1 != level ||
            level > int(s.desc.upgrades.size()))
            continue;
        const Unit& core = world_.units()[s.core];
        const long long dx = core.x() - x, dy = core.y() - y, d = dx * dx + dy * dy;
        if (d > (long long)maxDistance * maxDistance || (best && d >= bestDist)) continue;
        best = i;
        bestDist = d;
    }
    return best;
}

void Settlements::capture(Settlement& s) {
    const Unit& core = world_.units()[s.core];
    int defenders = 0;
    std::array<int, 8> intruders{};
    for (const Unit& u : world_.units()) {
        if (!u.alive() || u.inside || u.building() || u.player == 7 || !within(u, core, kRadius)) continue;
        if (s.owner != 7 && !world_.enemies(u.player, s.owner))
            ++defenders;
        else
            ++intruders[std::size_t(u.player)];
    }
    if (defenders >= kMinDefenders) return;
    if (std::any_of(s.cops.begin(), s.cops.end(), [&](std::size_t c) { return world_.units()[c].alive(); })) return;
    int captor = -1, team = -1;
    for (int p = 0; p < 7; ++p) {
        if (!intruders[std::size_t(p)]) continue;
        const int t = world_.team[std::size_t(p)];
        if (team >= 0 && t != team) return; // чужих команд больше одной
        team = t;
        if (captor < 0 || intruders[std::size_t(p)] > intruders[std::size_t(captor)]) captor = p;
    }
    if (captor >= 0 && captor != s.owner) setOwner(s, captor);
}

void Settlements::setOwner(Settlement& s, int player) {
    std::printf("поселение %zu (ресурс %d) захвачено игроком %d на шаге %llu\n", std::size_t(&s - list_.data()),
                s.resource, player, static_cast<unsigned long long>(world_.stepCount()));
    s.owner = player;
    s.accum = 0;
    for (std::size_t b : s.buildings)
        if (world_.units()[b].alive()) world_.units()[b].player = player;
    s.cops.clear(); // прежняя охрана мертва (иначе захвата бы не было)
    s.copIn = std::max(1, s.desc.copBuildTime / 2);
}

void Settlements::produce(Settlement& s) {
    if (s.owner == 7 || s.resource < 0) return;
    const auto r = std::size_t(s.resource);
    if (--s.produceIn > 0) return;
    s.produceIn = std::max(1, s.desc.peonTimeOnProduce[r]);
    s.accum += peons(s);
    const int load = s.desc.obozResAmount[r];
    if (load <= 0 || s.accum < load) return;
    s.accum -= load;
    int& stock = world_.resources[std::size_t(s.owner)][r];
    const int cap = s.desc.maxResAmount[r];
    if (cap <= 0 || stock < cap) stock += load;
}

void Settlements::guard(Settlement& s) {
    if (s.owner == 7 || s.desc.copId.empty()) return;
    std::erase_if(s.cops, [&](std::size_t c) { return !world_.units()[c].alive(); });
    if (int(s.cops.size()) >= s.desc.copMaxAmount) return;
    if (--s.copIn > 0) return;
    s.copIn = std::max(1, s.desc.copBuildTime / 2);
    const assets::UnitDesc* cop = catalog_.find(s.desc.copId);
    if (!cop) return;
    const Unit& core = world_.units()[s.core];
    // Встают кольцом у ядра; расталкивание разведёт их.
    const int dir = int(s.cops.size()) * 37 % 256;
    const int x = core.x() + cos256(dir) * 300 / 256, y = core.y() + sin256(dir) * 300 / 256;
    s.cops.push_back(world_.addUnit(*cop, s.owner, x, y, dir));
}

} // namespace oc2::world
