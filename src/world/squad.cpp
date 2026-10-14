// Отряды и построения: шеренга, колонна, каре; общая скорость, дух, разгром.
#include "world/world.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace oc2::world {

namespace {

constexpr int kRoutBase = 200; // шагов бегства после разгрома (8 с)
constexpr std::size_t kMaxSquad = 36; // три шеренги по 12
constexpr int kHoldSteps = 250; // шеренга без движения 10 с держит позицию

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Slot {
    double u, v; // вперёд, вправо
};

// Места построения относительно центра: u — вперёд, v — вправо.
std::vector<Slot> slotsFor(World::Formation f, int n, int sp) {
    std::vector<Slot> out;
    out.reserve(std::size_t(n));
    if (f == World::Formation::Square) {
        int side = 2;
        while (4 * (side - 1) < n) ++side;
        const double half = (side - 1) / 2.0 * sp;
        std::vector<Slot> ring;
        for (int i = 0; i < side; ++i) ring.push_back({half, -half + i * sp});
        for (int i = 1; i < side; ++i) ring.push_back({half - i * sp, half});
        for (int i = 1; i < side; ++i) ring.push_back({-half, half - i * sp});
        for (int i = 1; i < side - 1; ++i) ring.push_back({-half + i * sp, -half});
        for (int k = 0; k < n; ++k) out.push_back(ring[std::size_t(k) * ring.size() / std::size_t(n)]);
        return out;
    }
    const int cols = f == World::Formation::Column ? std::min(n, 3) : (n + (n >= 20 ? 3 : 2) - 1) / (n >= 20 ? 3 : 2);
    for (int k = 0; k < n; ++k) {
        const int row = k / cols, col = k % cols;
        out.push_back({-double(row * sp), (col - (cols - 1) / 2.0) * sp});
    }
    return out;
}

} // namespace

std::string World::joinIds(const std::vector<std::size_t>& ids) {
    std::string s;
    for (const std::size_t id : ids) s += (s.empty() ? "" : ",") + std::to_string(id);
    return s;
}

std::vector<std::size_t> World::parseIds(const std::string& s) {
    std::vector<std::size_t> ids;
    std::size_t pos = 0;
    while (pos < s.size()) {
        std::size_t next = s.find(',', pos);
        if (next == std::string::npos) next = s.size();
        if (next > pos) ids.push_back(std::size_t(std::stoul(s.substr(pos, next - pos))));
        pos = next + 1;
    }
    return ids;
}

bool World::isCavalry(const Unit& u) {
    const std::string n = lower(u.desc->name);
    return n.size() > 4 && n[3] == 'k' && n[4] >= 'a' && n[4] <= 'z';
}

bool World::isOfficer(const Unit& u) {
    const std::string n = lower(u.desc->name);
    return n.size() > 2 && n.compare(n.size() - 2, 2, "of") == 0;
}

int World::stepOf(const Unit& u) const {
    if (u.squad < 0 || std::size_t(u.squad) >= squads.size() || squads[std::size_t(u.squad)].step <= 0) return u.desc->step;
    return squads[std::size_t(u.squad)].step;
}

bool World::holding(const Unit& u) const {
    if (u.squad < 0) return false;
    const Squad& sq = squads[std::size_t(u.squad)];
    return sq.form == Formation::Line && sq.idle >= kHoldSteps;
}

namespace {

struct Frame {
    int cx, cy, facing;
    double fx, fy; // вперёд
    void set(int f) {
        facing = f & 255;
        fx = cos256(facing) / 256.0;
        fy = sin256(facing) / 256.0;
    }
    Slot local(int x, int y) const {
        const double dx = x - cx, dy = y - cy;
        return {dx * fx + dy * fy, -dx * fy + dy * fx};
    }
    std::pair<int, int> world(const Slot& s) const {
        return {int(std::lround(cx + s.u * fx - s.v * fy)), int(std::lround(cy + s.u * fy + s.v * fx))};
    }
};

} // namespace

// Ставит отряд sq в построение с центром (cx, cy) и лицом facing: места раздаются по
// проекции на поперечную ось, чтобы строй не перекручивался.
static void layout(World& w, World::Squad& sq, int cx, int cy, int facing) {
    auto& us = w.units();
    Frame fr{cx, cy, 0, 0, 0};
    fr.set(facing);
    sq.facing = fr.facing;
    int radius = 8;
    for (const std::size_t m : sq.members) radius = std::max(radius, us[m].desc->unitRadius);
    auto slots = slotsFor(sq.form, int(sq.members.size()), radius * 2 + 6);
    std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return a.v != b.v ? a.v < b.v : a.u > b.u; });
    std::vector<std::pair<Slot, std::size_t>> mem;
    for (const std::size_t m : sq.members) mem.emplace_back(fr.local(us[m].x(), us[m].y()), m);
    std::sort(mem.begin(), mem.end(), [](const auto& a, const auto& b) {
        return a.first.v != b.first.v ? a.first.v < b.first.v : a.first.u > b.first.u;
    });
    for (std::size_t i = 0; i < mem.size(); ++i) {
        const auto [x, y] = fr.world(slots[i]);
        w.orderMove(mem[i].second, x, y);
    }
    sq.idle = 0;
}

bool World::formSquad(const std::vector<std::size_t>& ids, Formation form) {
    const auto rec = record("form", int(form), 0, 0, 0, joinIds(ids));
    std::map<const assets::UnitDesc*, std::vector<std::size_t>> byType;
    int player = -1;
    for (const std::size_t id : ids) {
        if (id >= units_.size()) continue;
        const Unit& u = units_[id];
        if (!u.alive() || u.inside || u.building() || u.desc->peasant || u.desc->step <= 0) continue;
        if (player < 0) player = u.player;
        if (u.player == player) byType[u.desc].push_back(id);
    }
    bool any = false;
    // Большие толпы режутся на отряды не больше kMaxSquad (по порядку слева направо).
    std::vector<std::vector<std::size_t>> groups;
    for (auto& [desc, all] : byType) {
        std::sort(all.begin(), all.end(), [&](std::size_t l, std::size_t r) {
            return units_[l].x() != units_[r].x() ? units_[l].x() < units_[r].x() : units_[l].y() < units_[r].y();
        });
        const std::size_t chunks = (all.size() + kMaxSquad - 1) / kMaxSquad;
        for (std::size_t c = 0; c < chunks; ++c) {
            // Поровну, чтобы не оставалось огрызков.
            const std::size_t from = all.size() * c / chunks, to = all.size() * (c + 1) / chunks;
            groups.emplace_back(all.begin() + std::ptrdiff_t(from), all.begin() + std::ptrdiff_t(to));
        }
    }
    for (auto& group : groups) {
        if (group.size() < 2) continue;
        // Весь отряд одним куском — только смена построения.
        int existing = units_[group[0]].squad;
        for (const std::size_t id : group)
            if (units_[id].squad != existing) existing = -1;
        if (existing >= 0 && squads[std::size_t(existing)].members.size() == group.size()) {
            Squad& sq = squads[std::size_t(existing)];
            sq.form = form;
            long cx = 0, cy = 0;
            for (const std::size_t m : sq.members) cx += units_[m].x(), cy += units_[m].y();
            layout(*this, sq, int(cx / long(sq.members.size())), int(cy / long(sq.members.size())), sq.facing);
            any = true;
            continue;
        }
        for (const std::size_t id : group) {
            Unit& u = units_[id];
            if (u.squad >= 0) {
                auto& old = squads[std::size_t(u.squad)].members;
                old.erase(std::remove(old.begin(), old.end(), id), old.end());
            }
        }
        Squad sq;
        sq.player = player;
        sq.form = form;
        sq.members = group;
        sq.initial = int(group.size());
        sq.facing = units_[group[0]].direction;
        const int index = int(squads.size());
        long cx = 0, cy = 0;
        for (const std::size_t id : group) {
            units_[id].squad = index;
            cx += units_[id].x(), cy += units_[id].y();
        }
        squads.push_back(std::move(sq));
        layout(*this, squads.back(), int(cx / long(group.size())), int(cy / long(group.size())), squads.back().facing);
        any = true;
    }
    updateSquads();
    return any;
}

void World::orderMoveGroup(const std::vector<std::size_t>& ids, int x, int y) {
    const auto rec = record("movegroup", x, y, 0, 0, joinIds(ids));
    std::vector<std::size_t> loose;
    std::map<int, bool> seen;
    std::vector<int> groups;
    for (const std::size_t id : ids) {
        if (id >= units_.size() || !units_[id].alive()) continue;
        const int sq = units_[id].squad;
        if (sq < 0) loose.push_back(id);
        else if (!seen[sq]) seen[sq] = true, groups.push_back(sq);
    }
    // Общий центр всех приказанных: точка назначения сдвигается на взаимное положение частей.
    long ax = 0, ay = 0, an = 0;
    const auto add = [&](std::size_t id) { ax += units_[id].x(), ay += units_[id].y(), ++an; };
    for (const std::size_t id : loose) add(id);
    for (const int g : groups)
        for (const std::size_t m : squads[std::size_t(g)].members) add(m);
    if (an == 0) return;
    ax /= an;
    ay /= an;
    for (const std::size_t id : loose) orderMove(id, x + units_[id].x() - int(ax), y + units_[id].y() - int(ay));
    for (const int g : groups) {
        Squad& sq = squads[std::size_t(g)];
        long cx = 0, cy = 0;
        for (const std::size_t m : sq.members) cx += units_[m].x(), cy += units_[m].y();
        cx /= long(sq.members.size());
        cy /= long(sq.members.size());
        const int tx = x + int(cx - ax), ty = y + int(cy - ay);
        const int dx = tx - int(cx), dy = ty - int(cy);
        const int facing = dx * dx + dy * dy > 80 * 80 ? directionOf(dx, dy) : sq.facing;
        layout(*this, sq, tx, ty, facing);
    }
}

// Раз в шаг: убрать павших, пересчитать скорость, дух и проверить разгром.
void World::updateSquads() {
    for (std::size_t g = 0; g < squads.size(); ++g) {
        Squad& sq = squads[g];
        if (sq.members.empty()) continue;
        sq.members.erase(std::remove_if(sq.members.begin(), sq.members.end(),
                                        [&](std::size_t m) { return !units_[m].alive() || units_[m].squad != int(g); }),
                         sq.members.end());
        if (sq.members.size() < 2) {
            for (const std::size_t m : sq.members) units_[m].squad = -1;
            sq.members.clear();
            continue;
        }
        int step = 1 << 20;
        bool moving = false;
        for (const std::size_t m : sq.members) {
            step = std::min(step, units_[m].desc->step);
            moving = moving || units_[m].moving;
        }
        sq.step = sq.form == Formation::Column ? step * 6 / 5 : sq.form == Formation::Square ? step * 3 / 5 : step;
        sq.idle = moving ? 0 : sq.idle + 1;
        ++sq.calm;
        if (sq.calm >= 50 && sq.spirit < 100 && steps_ % 25 == 0) ++sq.spirit;

        const int lost = sq.initial - int(sq.members.size());
        bool rout = lost * 10 >= sq.initial * 7 || sq.spirit <= 0;
        if (!rout && lost * 10 >= sq.initial && sq.spirit < 70) {
            long cx = 0, cy = 0;
            for (const std::size_t m : sq.members) cx += units_[m].x(), cy += units_[m].y();
            cx /= long(sq.members.size());
            cy /= long(sq.members.size());
            bool officer = false;
            for (const Unit& u : units_) {
                if (u.player != sq.player || !u.alive() || !isOfficer(u)) continue;
                const long dx = u.x() - cx, dy = u.y() - cy;
                if (dx * dx + dy * dy < 500L * 500L) officer = true;
            }
            rout = !officer;
        }
        if (rout) routSquad(g);
    }
}

void World::scare(std::size_t index) {
    Unit& u = units_.at(index);
    if (!u.alive() || u.inside || u.building()) return;
    long ex = 0, ey = 0, en = 0;
    for (const Unit& e : units_) {
        if (!e.alive() || e.inside || e.building() || !enemies(u.player, e.player)) continue;
        const long dx = e.x() - u.x(), dy = e.y() - u.y();
        if (dx * dx + dy * dy < 800L * 800L) ex += e.x(), ey += e.y(), ++en;
    }
    const int away = en ? directionOf(int(u.x() - ex / en), int(u.y() - ey / en)) : (u.direction + 128) & 255;
    u.squad = -1;
    u.panic = kRoutBase;
    u.target.reset();
    u.attacking = false;
    u.gather = Unit::Gather::None;
    pathTo(u, u.x() + cos256(away) * 700 / 256, u.y() + sin256(away) * 700 / 256);
}

// Разгром: отряд распадается, бойцы бегут от ближайших врагов и не ищут боя.
void World::routSquad(std::size_t index) {
    Squad& sq = squads[index];
    long cx = 0, cy = 0;
    for (const std::size_t m : sq.members) cx += units_[m].x(), cy += units_[m].y();
    cx /= long(sq.members.size());
    cy /= long(sq.members.size());
    long ex = 0, ey = 0, en = 0;
    for (const Unit& e : units_) {
        if (!e.alive() || e.inside || e.building() || !enemies(sq.player, e.player)) continue;
        const long dx = e.x() - cx, dy = e.y() - cy;
        if (dx * dx + dy * dy < 800L * 800L) ex += e.x(), ey += e.y(), ++en;
    }
    const int away = en ? directionOf(int(cx - ex / en), int(cy - ey / en)) : (sq.facing + 128) & 255;
    for (const std::size_t m : sq.members) {
        Unit& u = units_[m];
        u.squad = -1;
        u.panic = kRoutBase;
        u.target.reset();
        u.attacking = false;
        u.gather = Unit::Gather::None;
        pathTo(u, u.x() + cos256(away) * 700 / 256, u.y() + sin256(away) * 700 / 256);
    }
    sq.members.clear();
}

} // namespace oc2::world
