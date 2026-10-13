#include "world/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace oc2::world {

namespace {

// Таблица оригинала: 256·cos с отбрасыванием дробной части (236 у 22,5°, 97 у 67,5°).
const std::array<int, 256>& cosTable() {
    static const std::array<int, 256> table = [] {
        std::array<int, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[std::size_t(i)] = int(256.0 * std::cos(2.0 * std::numbers::pi * i / 256.0));
        return t;
    }();
    return table;
}

} // namespace

int cos256(int direction) { return cosTable()[std::size_t(direction & 255)]; }
int sin256(int direction) { return cosTable()[std::size_t((direction - 64) & 255)]; }

int directionOf(int dx, int dy) {
    if (dx == 0 && dy == 0) return 0;
    const double a = std::atan2(double(dy), double(dx)); // −π..π, Y вниз — по часовой
    return int(std::lround(a * 128.0 / std::numbers::pi)) & 255;
}

const assets::UnitAnimation* World::idleAnimation(const assets::UnitDesc& desc) {
    for (const char* name : {"#STANDLO", "@REST", "#REST", "#STAND", "#PSTAND"})
        if (const assets::UnitAnimation* a = desc.animation(name); a && !a->frames.empty()) return a;
    for (const assets::UnitAnimation& a : desc.animations)
        if (!a.frames.empty()) return &a;
    return nullptr;
}

const assets::UnitAnimation* World::moveAnimation(const assets::UnitDesc& desc) {
    for (const char* name : {"@MOTION_L", "#MOTION_L"})
        if (const assets::UnitAnimation* a = desc.animation(name); a && !a->frames.empty()) return a;
    return nullptr;
}

const assets::UnitAnimation* World::namedAnimation(const assets::UnitDesc& desc, const std::string& name) {
    for (const char* prefix : {"@", "#"})
        if (const assets::UnitAnimation* a = desc.animation(prefix + name); a && !a->frames.empty()) return a;
    return nullptr;
}

const assets::UnitAnimation* World::attackAnimation(const assets::UnitDesc& desc, int weapon) {
    return namedAnimation(desc, weapon == 0 ? std::string("ATTACK") : "ATTACK" + std::to_string(weapon));
}

void World::setAnimation(Unit& u, const assets::UnitAnimation* anim) {
    if (u.animation == anim) return;
    u.animation = anim;
    u.frame = 0;
}

std::size_t World::addUnit(const assets::UnitDesc& desc, int player, int x, int y, int direction) {
    Unit u;
    u.desc = &desc;
    u.player = player;
    u.x16 = x * 16;
    u.y16 = y * 16;
    u.direction = direction & 255;
    u.animation = idleAnimation(desc);
    // Чтобы стоящие рядом не шевелились в такт — разный начальный кадр покоя.
    if (u.animation && !desc.building) u.frame = int((units_.size() * 7) % u.animation->frames.size());
    u.life = desc.life;
    if (desc.building && lock_.width() > 0) lock_.addBuilding(desc, x, y);
    units_.push_back(u);
    return units_.size() - 1;
}

void World::orderMove(std::size_t index, int x, int y) {
    Unit& u = units_.at(index);
    if (!u.alive()) return;
    u.target.reset();
    u.gather = Unit::Gather::None;
    u.buildTarget.reset();
    pathTo(u, x, y);
}

void World::orderAttack(std::size_t index, std::size_t target) {
    Unit& u = units_.at(index);
    if (!u.alive() || u.building() || target >= units_.size() || target == index) return;
    u.target = target;
    u.repathIn = 0;
    u.gather = Unit::Gather::None;
    u.buildTarget.reset();
}

bool World::canPlace(const assets::UnitDesc& desc, int x, int y) const {
    if (lock_.width() == 0) return true;
    return lock_.pointsOpen(desc, x, y, desc.lockPoints) && lock_.pointsOpen(desc, x, y, desc.buildLockPoints);
}

bool World::affordable(const assets::UnitDesc& desc, int player) const {
    const auto& have = resources[std::size_t(player & 7)];
    for (std::size_t r = 0; r < have.size() && r < desc.price.size(); ++r)
        if (have[r] < desc.price[r]) return false;
    return true;
}

std::optional<std::size_t> World::placeBuilding(const assets::UnitDesc& desc, int player, int x, int y) {
    if (!desc.building || !canPlace(desc, x, y) || !affordable(desc, player)) return std::nullopt;
    auto& have = resources[std::size_t(player & 7)];
    for (std::size_t r = 0; r < have.size() && r < desc.price.size(); ++r) have[r] -= desc.price[r];
    Unit u;
    u.desc = &desc;
    u.player = player;
    u.x16 = x * 16;
    u.y16 = y * 16;
    u.underConstruction = desc.buildStages > 0;
    u.life = u.underConstruction ? 1 : desc.life;
    if (u.underConstruction) {
        u.animation = namedAnimation(desc, "BUILDLO_0");
        if (!u.animation) u.animation = idleAnimation(desc);
        if (lock_.width() > 0) lock_.setPoints(desc, x, y, desc.buildLockPoints, true);
    } else {
        u.animation = idleAnimation(desc);
        if (lock_.width() > 0) lock_.addBuilding(desc, x, y);
    }
    units_.push_back(u);
    return units_.size() - 1;
}

void World::nextStage(std::size_t index) {
    Unit& b = units_.at(index);
    const int stages = b.desc->buildStages;
    if (!b.underConstruction || b.stage >= stages) return;
    ++b.stage;
    b.life = std::max(1, b.stage * b.desc->life / stages);
    if (b.stage == stages) {
        b.underConstruction = false;
        b.life = b.desc->life;
        b.animation = idleAnimation(*b.desc);
        if (lock_.width() > 0) {
            lock_.setPoints(*b.desc, b.x(), b.y(), b.desc->buildLockPoints, false);
            lock_.addBuilding(*b.desc, b.x(), b.y());
        }
        return;
    }
    if (const auto* a = namedAnimation(*b.desc, "BUILDLO_" + std::to_string(std::min(3, b.stage * 4 / stages))))
        b.animation = a;
}

bool World::orderBuild(std::size_t index, std::size_t building) {
    Unit& u = units_.at(index);
    if (!u.alive() || u.building() || !u.desc->peasant || building >= units_.size()) return false;
    const Unit& b = units_[building];
    if (!b.underConstruction || !b.alive() || b.player != u.player) return false;
    u.target.reset();
    u.attacking = false;
    u.gather = Unit::Gather::None;
    u.buildTarget = building;
    const auto [px, py] = buildPoint(index, b);
    pathTo(u, px, py);
    return true;
}

std::pair<int, int> World::buildPoint(std::size_t builder, const Unit& b) const {
    const auto& pts = b.desc->buildPoints;
    if (pts.empty()) return {b.x(), b.y()};
    const auto [cx, cy] = LockMap::corner(*b.desc, b.x(), b.y());
    const auto& [px, py] = pts[builder % pts.size()];
    constexpr int C = LockMap::kCell;
    return {(cx + px) * C + C / 2, (cy + py) * C + C / 2};
}

void World::updateBuild(std::size_t index) {
    Unit& u = units_[index];
    if (!u.buildTarget) return;
    const Unit& b = units_[*u.buildTarget];
    if (!b.underConstruction || !b.alive()) {
        u.buildTarget.reset();
        setAnimation(u, idleAnimation(*u.desc));
        return;
    }
    if (u.moving) return;
    const auto [px, py] = buildPoint(index, b);
    const int dx = px - u.x(), dy = py - u.y();
    // Дошёл до точки (или ближе не пройти, но здание рядом) — работает.
    const int bx = b.x() - u.x(), by = b.y() - u.y();
    if (dx * dx + dy * dy > 48 * 48 && bx * bx + by * by > 600 * 600) {
        u.buildTarget.reset(); // не дойти
        return;
    }
    const auto* work = namedAnimation(*u.desc, "WORK");
    if (!work) work = idleAnimation(*u.desc);
    if (u.animation != work) {
        setAnimation(u, work);
        u.direction = directionOf(bx, by);
        return;
    }
    if (++u.frame >= int(work->frames.size())) {
        u.frame = 0;
        nextStage(*u.buildTarget);
    }
    emitSounds(u);
}

std::optional<std::size_t> World::sourceNear(int x, int y, int maxDistance) const {
    std::optional<std::size_t> best;
    long long bestD = (long long)maxDistance * maxDistance;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const long long dx = sources[i].x - x, dy = sources[i].y - y;
        if (dx * dx + dy * dy <= bestD) {
            bestD = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

bool World::orderGather(std::size_t index, std::size_t source) {
    Unit& u = units_.at(index);
    if (!u.alive() || u.building() || source >= sources.size()) return false;
    if (u.desc->portion[std::size_t(sources[source].resource)] <= 0) return false;
    u.target.reset();
    u.attacking = false;
    if (u.gather == Unit::Gather::None || u.source != source) u.carry = 0;
    u.source = source;
    u.buildTarget.reset();
    u.gather = Unit::Gather::ToSource;
    u.moving = false;
    return true;
}

// Анимации добычи по ресурсу: дерево — ATTACK1/MOTION_L1 (комментарий «добыча дерева»
// в .md крестьян), еда — 3, камень и руды — 6 (звук KRI_STONE у ATTACK6). Соответствие
// выведено из описаний крестьян, не из кода оригинала.
std::size_t World::addSources(const assets::Map3d& map, const char (&kind)[3], const assets::SpriteList& list,
                              const std::unordered_map<std::string, assets::SpriteSource>& table) {
    std::size_t added = 0;
    for (const assets::MapObject& o : map.objects()) {
        if (!o.is(kind) || o.type < 0 || std::size_t(o.type) >= list.entries.size()) continue;
        const auto it = table.find(list.entries[std::size_t(o.type)].id);
        if (it == table.end()) continue;
        const int res = assets::UnitDesc::resourceId(it->second.resource);
        if (res < 0) continue; // NONE, REMOVE
        sources.push_back({o.x, o.y, res, it->second.perWork, it->second.radius});
        ++added;
    }
    return added;
}

const assets::UnitAnimation* World::workAnimation(const assets::UnitDesc& d, int resource) {
    static constexpr int kSuffix[6] = {1, 6, 6, 3, 6, 6};
    if (const auto* a = attackAnimation(d, kSuffix[resource % 6])) return a;
    return namedAnimation(d, "WORK");
}

const assets::UnitAnimation* World::carryAnimation(const assets::UnitDesc& d, int resource) {
    static constexpr int kSuffix[6] = {1, 6, 6, 3, 6, 6};
    if (const auto* a = namedAnimation(d, "MOTION_L" + std::to_string(kSuffix[resource % 6]))) return a;
    return moveAnimation(d);
}

std::optional<std::size_t> World::nearestBase(const Unit& u, int resource) const {
    std::optional<std::size_t> best;
    long long bestD = -1;
    for (std::size_t i = 0; i < units_.size(); ++i) {
        const Unit& b = units_[i];
        if (!b.building() || b.player != u.player || b.underConstruction || !b.alive() ||
            !(b.desc->resourceBase & (1u << resource)))
            continue;
        const long long dx = b.x() - u.x(), dy = b.y() - u.y();
        if (bestD < 0 || dx * dx + dy * dy < bestD) {
            bestD = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

void World::updateGather(std::size_t index) {
    Unit& u = units_[index];
    if (u.gather == Unit::Gather::None || u.target) return;
    const Source& src = sources[u.source];
    const auto res = std::size_t(src.resource);
    const int reach = src.radius + 24;
    const int dx = src.x - u.x(), dy = src.y - u.y();
    switch (u.gather) {
    case Unit::Gather::ToSource:
        if (u.moving) return;
        if (dx * dx + dy * dy <= reach * reach) {
            u.gather = Unit::Gather::Working;
            u.workLeft = std::max(1, u.desc->workCycles[res]);
            u.direction = directionOf(dx, dy);
            u.animation = workAnimation(*u.desc, src.resource);
            u.frame = 0;
        } else {
            pathTo(u, src.x, src.y);
            if (!u.moving) u.gather = Unit::Gather::None; // не дойти
        }
        return;
    case Unit::Gather::Working:
        return; // циклы считаются при смене кадров
    case Unit::Gather::ToBase: {
        if (u.moving) return;
        if (!u.base || !units_[*u.base].alive()) u.base = nearestBase(u, src.resource);
        if (!u.base) {
            u.gather = Unit::Gather::None; // нести некуда — стоит с ношей
            return;
        }
        const Unit& b = units_[*u.base];
        const int bx = b.x() - u.x(), by = b.y() - u.y();
        if (bx * bx + by * by <= 320 * 320) { // у здания: сдать
            resources[std::size_t(u.player & 7)][res] += u.carry;
            u.carry = 0;
            u.gather = Unit::Gather::ToSource;
            pathTo(u, src.x, src.y);
        } else {
            pathTo(u, b.x(), b.y());
            if (!u.moving) u.gather = Unit::Gather::None;
        }
        return;
    }
    case Unit::Gather::None: return;
    }
}

void World::pathTo(Unit& u, int x, int y) {
    if (u.building() || !moveAnimation(*u.desc) || u.desc->step <= 0) return;
    u.path.clear();
    if (lock_.width() > 0)
        u.path = findPath(lock_, u.x(), u.y(), x, y);
    else
        u.path = {{x, y}};
    if (u.path.empty()) {
        u.moving = false;
        return;
    }
    std::reverse(u.path.begin(), u.path.end()); // берём с конца
    u.targetX16 = u.path.back().first * 16;
    u.targetY16 = u.path.back().second * 16;
    u.path.pop_back();
    u.moving = true;
}

void World::hit(Unit& t, int damage) {
    if (!t.alive()) return;
    t.life -= damage;
    if (t.life > 0) return;
    t.life = 0;
    t.dying = true;
    t.moving = false;
    t.attacking = false;
    t.selected = false;
    t.target.reset();
    t.path.clear();
    t.animation = namedAnimation(*t.desc, "DEATH");
    t.frame = 0;
    if (!t.animation) {
        t.dying = false;
        t.dead = true;
        t.animation = namedAnimation(*t.desc, "DEATHLIE1");
    }
}

void World::emitSounds(const Unit& u) {
    const assets::UnitDesc& d = *u.desc;
    if (d.sounds.empty() || !u.animation || d.animations.empty()) return;
    const std::size_t anim = std::size_t(u.animation - d.animations.data());
    for (const auto& s : d.sounds)
        if (s.animation == anim && s.frame == u.frame) soundEvents.push_back({s.group, u.x(), u.y()});
}

void World::updateCombat(std::size_t index) {
    Unit& u = units_[index];
    const assets::UnitDesc& d = *u.desc;
    if (u.target && !units_[*u.target].alive()) u.target.reset();

    // Поиск врага рядом — раз в 10 шагов, у стоящих без дела.
    if (!u.target && !u.moving && d.searchEnemyRadius > 0 && u.player != 7 && (steps_ + index) % 10 == 0) {
        long long best = (long long)d.searchEnemyRadius * d.searchEnemyRadius;
        for (std::size_t j = 0; j < units_.size(); ++j) {
            const Unit& e = units_[j];
            if (!e.alive() || e.building() || !enemies(u.player, e.player)) continue;
            const long long dx = e.x() - u.x(), dy = e.y() - u.y();
            if (dx * dx + dy * dy <= best) {
                best = dx * dx + dy * dy;
                u.target = j;
            }
        }
    }
    if (!u.target) return;
    const Unit& t = units_[*u.target];
    const int dx = t.x() - u.x(), dy = t.y() - u.y();
    const int dist = int(std::lround(std::hypot(double(dx), double(dy))));

    int chosen = -1;
    bool inRange = false;
    for (int w = 0; w < assets::UnitDesc::kWeapons; ++w) {
        const auto i = std::size_t(w);
        if (d.damage[i] <= 0 || d.attackMax[i] <= 0 || !attackAnimation(d, w)) continue;
        if (dist < d.attackMin[i] || dist > d.attackMax[i]) continue;
        inRange = true;
        if (u.reload[i] > 0) continue;
        if (chosen < 0 || d.attackMax[i] > d.attackMax[std::size_t(chosen)]) chosen = w;
    }
    if (chosen >= 0) {
        u.direction = directionOf(dx, dy);
        u.moving = false;
        u.path.clear();
        u.attacking = true;
        u.weapon = chosen;
        u.damageDone = false;
        u.animation = attackAnimation(d, chosen);
        u.frame = 0;
        return;
    }
    if (inRange) { // ждём перезарядки лицом к цели
        u.direction = directionOf(dx, dy);
        u.moving = false;
        u.path.clear();
        return;
    }
    if (--u.repathIn <= 0) {
        pathTo(u, t.x(), t.y());
        u.repathIn = 25; // цель движется — путь пересчитывается раз в секунду
    }
}

std::optional<int> World::winningTeam() const {
    std::array<bool, 8> alive{}, present{};
    for (const Unit& u : units_) {
        if (u.building() || u.player == 7 || u.desc->life <= 0) continue;
        const auto t = std::size_t(team[std::size_t(u.player & 7)] & 7);
        present[t] = true;
        if (u.alive()) alive[t] = true;
    }
    int teamsPresent = 0, teamsAlive = 0, last = -1;
    for (int t = 0; t < 8; ++t) {
        teamsPresent += present[std::size_t(t)];
        if (alive[std::size_t(t)]) {
            ++teamsAlive;
            last = t;
        }
    }
    if (teamsPresent >= 2 && teamsAlive == 1) return last;
    return std::nullopt;
}

void World::thinkAi(std::size_t index) {
    Unit& u = units_[index];
    if (humanTeam < 0 || u.player == 7 || team[std::size_t(u.player & 7)] == humanTeam) return;
    if (u.target || u.moving || u.attacking || u.desc->step <= 0) return;
    long long best = (long long)kAiRadius * kAiRadius;
    std::optional<std::size_t> target;
    for (std::size_t j = 0; j < units_.size(); ++j) {
        const Unit& e = units_[j];
        if (!e.alive() || e.building() || !enemies(u.player, e.player)) continue;
        const long long dx = e.x() - u.x(), dy = e.y() - u.y();
        if (dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            target = j;
        }
    }
    if (target) orderAttack(index, *target);
}

// Расталкивание (своё упрощение вместо карты занятости оригинала): живые юниты ближе
// суммы радиусов UNITRADIUS (по умолчанию 8) расходятся на половину перекрытия каждый;
// стреляющий/бьющий стоит на месте; в закрытую клетку не толкают.
void World::separate() {
    constexpr int kCellShift = 6; // клетки 64 мировые единицы
    cells_.clear();
    for (std::size_t i = 0; i < units_.size(); ++i) {
        const Unit& u = units_[i];
        if (u.building() || !u.alive()) continue;
        const std::int64_t key = (std::int64_t(u.y() >> kCellShift) << 32) | std::uint32_t(u.x() >> kCellShift);
        cells_.emplace_back(key, std::uint32_t(i));
    }
    std::sort(cells_.begin(), cells_.end());
    auto radius = [](const Unit& u) { return u.desc->unitRadius > 0 ? u.desc->unitRadius : 8; };
    auto canMoveTo = [&](int x16, int y16) {
        return lock_.width() == 0 || !lock_.locked((x16 >> 4) / LockMap::kCell, (y16 >> 4) / LockMap::kCell);
    };
    for (const auto& [key, ai] : cells_) {
        Unit& a = units_[ai];
        const int cx = a.x() >> kCellShift, cy = a.y() >> kCellShift;
        for (int ny = cy - 1; ny <= cy + 1; ++ny)
            for (int nx = cx - 1; nx <= cx + 1; ++nx) {
                const std::int64_t k = (std::int64_t(ny) << 32) | std::uint32_t(nx);
                auto range = std::equal_range(cells_.begin(), cells_.end(), std::make_pair(k, std::uint32_t(0)),
                                              [](const auto& l, const auto& r) { return l.first < r.first; });
                for (auto it = range.first; it != range.second; ++it) {
                    if (it->second <= ai) continue; // каждая пара — один раз
                    Unit& b = units_[it->second];
                    const int min16 = (radius(a) + radius(b)) * 16;
                    int dx = b.x16 - a.x16, dy = b.y16 - a.y16;
                    if (std::abs(dx) >= min16 || std::abs(dy) >= min16) continue;
                    double d = std::hypot(double(dx), double(dy));
                    if (d >= min16) continue;
                    if (d < 1.0) { // в одной точке — расходятся в стороны по номерам
                        dx = (int(ai + it->second) % 2) ? 16 : -16;
                        dy = 8;
                        d = std::hypot(double(dx), double(dy));
                    }
                    const double push = (min16 - d) / 2.0;
                    const int px = int(std::lround(dx / d * push)), py = int(std::lround(dy / d * push));
                    const bool aFixed = a.attacking, bFixed = b.attacking;
                    if (aFixed && bFixed) continue;
                    const int ka = bFixed ? 2 : 1, kb = aFixed ? 2 : 1;
                    if (!aFixed && canMoveTo(a.x16 - px * ka, a.y16 - py * ka)) {
                        a.x16 -= px * ka;
                        a.y16 -= py * ka;
                    }
                    if (!bFixed && canMoveTo(b.x16 + px * kb, b.y16 + py * kb)) {
                        b.x16 += px * kb;
                        b.y16 += py * kb;
                    }
                }
            }
    }
}

void World::killAllExcept(int keep) {
    for (Unit& u : units_)
        if (!u.building() && u.player != 7 && team[std::size_t(u.player & 7)] != keep && u.alive()) hit(u, u.life + 1);
}

void World::step() {
    ++steps_;
    for (std::size_t index = 0; index < units_.size(); ++index) {
        Unit& u = units_[index];
        if (u.building()) continue; // кадры зданий — слои, не время
        for (int& r : u.reload)
            if (r > 0) --r;
        if (u.dead) continue; // лежит неподвижно
        if (u.dying) {
            ++u.frame;
            emitSounds(u);
            if (u.frame >= int(u.animation->frames.size())) {
                u.dying = false;
                u.dead = true;
                u.animation = namedAnimation(*u.desc, "DEATHLIE1");
                u.frame = 0;
            }
            continue;
        }
        if (u.attacking) {
            const int frames = int(u.animation->frames.size());
            ++u.frame;
            emitSounds(u);
            if (!u.damageDone && u.frame >= frames / 2) {
                u.damageDone = true;
                const auto w = std::size_t(u.weapon);
                u.reload[w] = u.desc->attackPause[w];
                if (!u.desc->weaponName[w].empty())
                    shotEvents.push_back({u.desc->weaponName[w], u.x(), u.y(), u.direction});
                if (u.target && units_[*u.target].alive()) {
                    Unit& t = units_[*u.target];
                    const double reach = std::hypot(double(t.x() - u.x()), double(t.y() - u.y()));
                    // Цель успела уйти дальше полуторной дальности — промах.
                    if (reach <= u.desc->attackMax[w] * 1.5) hit(t, u.desc->damage[w]);
                }
            }
            if (u.frame >= frames) {
                u.attacking = false;
                u.animation = idleAnimation(*u.desc);
                u.frame = 0;
            }
            continue;
        }
        if ((steps_ + index) % 125 == 0) thinkAi(index); // раз в 5 с, вразнобой
        updateCombat(index);
        if (u.attacking) continue;
        updateBuild(index);
        if (u.buildTarget && !u.moving) continue; // работает на стройке
        updateGather(index);
        if (u.gather == Unit::Gather::Working) {
            const int frames = int(u.animation->frames.size());
            if (++u.frame >= frames) {
                u.frame = 0;
                if (--u.workLeft <= 0) {
                    const int r = sources[u.source].resource;
                    u.carry = u.desc->portion[std::size_t(r)];
                    u.gather = Unit::Gather::ToBase;
                    u.base.reset();
                    updateGather(index);
                }
            }
            emitSounds(u);
            continue;
        }
        if (u.moving) {
            const int dx = u.targetX16 - u.x16, dy = u.targetY16 - u.y16;
            // Смещение за шаг: (cos·шаг >> 8, sin·шаг >> 8) в единицах 1/16 (FUN_018af490, FUN_00f370e0).
            const double dist = std::hypot(double(dx), double(dy));
            if (dist <= double(u.desc->step)) {
                u.x16 = u.targetX16;
                u.y16 = u.targetY16;
                if (u.path.empty()) {
                    u.moving = false;
                } else {
                    u.targetX16 = u.path.back().first * 16;
                    u.targetY16 = u.path.back().second * 16;
                    u.path.pop_back();
                }
            } else {
                // Поворот не быстрее ROTATE за шаг; идти — когда отклонение не больше 45°
                // (упрощение: у оригинала поворот зависит ещё и от скорости).
                const int want = directionOf(dx, dy);
                const int diff = ((want - u.direction + 384) & 255) - 128; // −128…127
                const int turn = std::clamp(diff, -u.desc->rotate, u.desc->rotate);
                u.direction = (u.direction + turn) & 255;
                if (std::abs(diff - turn) <= 32) {
                    u.x16 += (cos256(u.direction) * u.desc->step) >> 8;
                    u.y16 += (sin256(u.direction) * u.desc->step) >> 8;
                }
            }
        }
        const bool carrying = u.carry > 0 && u.gather == Unit::Gather::ToBase;
        setAnimation(u, u.moving ? (carrying ? carryAnimation(*u.desc, sources[u.source].resource)
                                             : moveAnimation(*u.desc))
                                 : idleAnimation(*u.desc));
        // Один кадр за шаг при GameSpeed = 256 (OneObject::SetNextFrame).
        if (u.animation && !u.animation->frames.empty()) {
            u.frame = (u.frame + 1) % int(u.animation->frames.size());
            emitSounds(u);
        }
    }
    separate();
}

} // namespace oc2::world
