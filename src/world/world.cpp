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
    if (!u.alive() || u.inside) return;
    u.target.reset();
    u.gather = Unit::Gather::None;
    u.buildTarget.reset();
    u.mine.reset();
    pathTo(u, x, y);
}

void World::orderAttack(std::size_t index, std::size_t target) {
    Unit& u = units_.at(index);
    if (!u.alive() || u.inside || u.building() || target >= units_.size() || target == index) return;
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

bool World::canShoot(const Unit& u, int w) const {
    const auto& have = resources[std::size_t(u.player & 7)];
    const auto& cost = u.desc->shotCost[std::size_t(w)];
    for (std::size_t r = 0; r < have.size() && r < cost.size(); ++r)
        if (have[r] < cost[r]) return false;
    return true;
}

std::optional<std::size_t> World::placeBuilding(const assets::UnitDesc& desc, int player, int x, int y) {
    if (!desc.building || !canPlace(desc, x, y) || !affordable(desc, player) || !available(desc, player)) return std::nullopt;
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
    pushOutOfLocks();
    return units_.size() - 1;
}

void World::pushOutOfLocks() {
    if (lock_.width() == 0) return;
    constexpr int C = LockMap::kCell;
    for (std::size_t i = 0; i < units_.size(); ++i) {
        const Unit& v = units_[i];
        if (v.building() || !v.alive() || !lock_.locked(v.x() / C, v.y() / C)) continue;
        if (const auto open = lock_.nearestOpen(v.x() / C, v.y() / C, 16))
            orderMove(i, open->first * C + C / 2, open->second * C + C / 2);
    }
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
        ++producedCount[std::size_t(b.player & 7)][b.desc];
        b.animation = idleAnimation(*b.desc);
        if (lock_.width() > 0) {
            lock_.setPoints(*b.desc, b.x(), b.y(), b.desc->buildLockPoints, false);
            lock_.addBuilding(*b.desc, b.x(), b.y());
            pushOutOfLocks();
        }
        return;
    }
    if (const auto* a = namedAnimation(*b.desc, "BUILDLO_" + std::to_string(std::min(3, b.stage * 4 / stages))))
        b.animation = a;
}

bool World::available(const assets::UnitDesc& desc, int player) const {
    for (const std::string& need : desc.needs) {
        if (researched[std::size_t(player & 7)].count(need)) continue;
        const bool built = std::any_of(units_.begin(), units_.end(), [&](const Unit& u) {
            return u.player == player && u.alive() && !u.underConstruction && u.desc->typeName == need;
        });
        if (!built) return false;
    }
    return true;
}

bool World::orderResearch(std::size_t index, const assets::UpgradeDef& up, int costPercent) {
    Unit& b = units_.at(index);
    if (!b.building() || b.underConstruction || !b.alive() || b.researchLeft > 0 || researched[std::size_t(b.player & 7)].count(up.name))
        return false;
    auto& have = resources[std::size_t(b.player & 7)];
    for (std::size_t r = 0; r < have.size() && r < up.cost.size(); ++r)
        if (have[r] < up.cost[r] * costPercent / 100) return false;
    for (std::size_t r = 0; r < have.size() && r < up.cost.size(); ++r) have[r] -= up.cost[r] * costPercent / 100;
    b.research = up.name;
    b.researchLeft = std::max(1, up.time);
    return true;
}

bool World::orderProduce(std::size_t index, const assets::UnitDesc& unit) {
    Unit& b = units_.at(index);
    if (!b.building() || b.underConstruction || !b.alive() || unit.building || !affordable(unit, b.player) ||
        !available(unit, b.player))
        return false;
    auto& have = resources[std::size_t(b.player & 7)];
    for (std::size_t r = 0; r < have.size() && r < unit.price.size(); ++r) have[r] -= unit.price[r];
    b.queue.push_back(&unit);
    return true;
}

int World::population(int player) const {
    int n = 0;
    for (const Unit& u : units_)
        if (u.player == player && !u.building() && u.alive() && !u.desc->noFarm) ++n;
    return n;
}

int World::populationLimit(int player) const {
    int n = 0;
    for (const Unit& u : units_)
        if (u.player == player && u.building() && u.alive() && !u.underConstruction) n += u.desc->farm;
    return n;
}

std::array<int, assets::UnitDesc::kResources> World::consumption(int player) const {
    std::array<int, assets::UnitDesc::kResources> out{};
    for (const Unit& u : units_)
        if (u.player == player && u.alive() && !u.underConstruction && u.desc->consumeResource >= 0 &&
            u.desc->consumeResource < assets::UnitDesc::kResources)
            out[std::size_t(u.desc->consumeResource)] += u.desc->consumeAmount;
    return out;
}

void World::updateUpkeep() {
    constexpr int kStepsPerMinute = 60 * 25;
    if (steps_ % 25 != 0) return; // раз в секунду — пересчёт дорог
    for (int p = 0; p < 7; ++p) {
        const auto need = consumption(p);
        for (std::size_t r = 0; r < need.size(); ++r) {
            upkeepAccum_[std::size_t(p)][r] += need[r] * 25;
            const int whole = upkeepAccum_[std::size_t(p)][r] / kStepsPerMinute;
            upkeepAccum_[std::size_t(p)][r] %= kStepsPerMinute;
            int& have = resources[std::size_t(p)][r];
            if (r == 3) starving_[std::size_t(p)] = need[r] > 0 && have <= 0;
            have = std::max(0, have - whole);
        }
    }
    if (steps_ % kStarveSteps == 0) starve();
}

void World::starve() {
    for (Unit& u : units_)
        if (u.alive() && !u.underConstruction && u.player >= 0 && u.player < 7 && starving_[std::size_t(u.player)] &&
            u.desc->consumeResource == 3 && u.desc->consumeAmount > 0)
            hit(u, std::max(1, u.desc->life / kStarveShare));
}

void World::updateProduction(std::size_t index) {
    if (Unit& r = units_[index]; r.researchLeft > 0 && r.alive() && --r.researchLeft == 0)
        researched[std::size_t(r.player & 7)].insert(r.research);
    if (units_[index].queue.empty() || units_[index].underConstruction || !units_[index].alive()) return;
    Unit& b = units_[index];
    if (!b.queue.front()->noFarm && population(b.player) >= populationLimit(b.player)) return; // нет мест
    if (starving(b.player) && b.queue.front()->consumeResource == 3) return; // голод: едоков не обучают
    if (++b.produced < std::max(1, b.queue.front()->buildStages)) return;
    const assets::UnitDesc& desc = *b.queue.front();
    b.queue.erase(b.queue.begin());
    b.produced = 0;
    const int player = b.player;
    const auto& born = b.desc->bornPoints;
    const auto [cx, cy] = LockMap::corner(*b.desc, b.x(), b.y());
    int x = b.x(), y = b.y() + 64, direction = 64;
    if (!born.empty()) {
        x = cx * LockMap::kCell + born[0].first;
        y = cy * LockMap::kCell + born[0].second;
    }
    if (born.size() >= 2) direction = directionOf(born[1].first - born[0].first, born[1].second - born[0].second);
    if (!b.desc->bornSound.empty()) soundEvents.push_back({b.desc->bornSound, b.x(), b.y()});
    const std::size_t u = addUnit(desc, player, x, y, direction); // b больше не трогаем: вектор мог переехать
    ++producedCount[std::size_t(player & 7)][&desc];
    if (born.size() >= 2) orderMove(u, cx * LockMap::kCell + born[1].first, cy * LockMap::kCell + born[1].second);
}

bool World::orderEnter(std::size_t index, std::size_t mineIndex) {
    Unit& u = units_.at(index);
    if (!u.alive() || u.inside || !u.desc->peasant || mineIndex >= units_.size()) return false;
    const Unit& m = units_[mineIndex];
    if (!m.alive() || m.underConstruction || m.desc->peasantAbsorber <= 0 || (m.player != u.player && m.player != 7))
        return false;
    u.target.reset();
    u.attacking = false;
    u.gather = Unit::Gather::None;
    u.buildTarget.reset();
    u.mine = mineIndex;
    pathTo(u, m.x(), m.y());
    return true;
}

void World::updateEnter(std::size_t index) {
    Unit& u = units_[index];
    if (!u.mine || u.inside) return;
    Unit& m = units_[*u.mine];
    const int dx = m.x() - u.x(), dy = m.y() - u.y();
    const int d2 = dx * dx + dy * dy;
    // Входит, дойдя до конца пути у шахты (центр закрыт клетками здания) или оказавшись
    // совсем рядом с центром (идущих к одной точке расталкивает — им не остановиться).
    if (u.moving && d2 > 48 * 48) return;
    const bool near = d2 <= 600 * 600;
    if (m.player == 7 && near) m.player = u.player; // ничья шахта — захват
    if (!near || !m.alive() || m.player != u.player || int(m.occupants.size()) >= m.desc->peasantAbsorber) {
        u.mine.reset();
        return;
    }
    u.inside = true;
    u.selected = false;
    u.moving = false;
    u.path.clear();
    m.occupants.push_back(index);
}

bool World::leaveMine(std::size_t mineIndex) {
    Unit& m = units_.at(mineIndex);
    if (m.occupants.empty()) return false;
    const std::size_t index = m.occupants.back();
    m.occupants.pop_back();
    Unit& u = units_[index];
    u.inside = false;
    u.mine.reset();
    constexpr int C = LockMap::kCell;
    int x = m.x(), y = m.y();
    if (lock_.width() > 0)
        if (const auto open = lock_.nearestOpen(x / C, y / C, 40)) x = open->first * C + C / 2, y = open->second * C + C / 2;
    u.x16 = x * 16;
    u.y16 = y * 16;
    u.moving = false;
    u.path.clear();
    return true;
}

void World::updateMine(std::size_t index) {
    Unit& m = units_[index];
    if (m.occupants.empty() || !m.desc->producerResources) return;
    const int n = std::min(int(m.occupants.size()), m.desc->peasantAbsorber);
    m.mineAccum += (m.desc->producerBase + m.desc->producerPerPeasant * n) * 256 / 8;
    int res = 0;
    while (!(m.desc->producerResources & (1u << res))) ++res;
    resources[std::size_t(m.player & 7)][std::size_t(res)] += m.mineAccum / 1000;
    m.mineAccum %= 1000;
}

bool World::orderBuild(std::size_t index, std::size_t building) {
    Unit& u = units_.at(index);
    if (!u.alive() || u.inside || u.building() || !u.desc->peasant || building >= units_.size()) return false;
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

int World::ObjectRules::find(std::string_view id) const {
    for (std::size_t i = 0; i < list.entries.size(); ++i)
        if (list.entries[i].id == id) return int(i);
    return -1;
}

void World::setStage(Source& s, int object) {
    s.object = object;
    s.timer = 0;
    s.works = 0;
    s.resource = -1;
    if (object < 0 || std::size_t(object) >= rules_.list.entries.size()) return;
    const auto it = rules_.sources.find(rules_.list.entries[std::size_t(object)].id);
    if (it == rules_.sources.end()) return;
    s.resource = assets::UnitDesc::resourceId(it->second.resource);
    s.perWork = it->second.perWork;
    s.radius = it->second.radius;
}

std::size_t World::addObject(int object, int x, int y) {
    Source s;
    s.x = x;
    s.y = y;
    setStage(s, object);
    sources.push_back(s);
    return sources.size() - 1;
}

int World::sowField(const assets::UnitDesc& desc, int player, int x, int y) {
    const int stage = rules_.find(desc.exSprite);
    if (!desc.spriteObject || stage < 0 || !affordable(desc, player)) return 0;
    std::vector<std::pair<int, int>> spots;
    for (int j = 0; j < kFieldBlock; ++j)
        for (int i = 0; i < kFieldBlock; ++i) {
            const int fx = x + (i - kFieldBlock / 2) * kFieldStep, fy = y + (j - kFieldBlock / 2) * kFieldStep;
            if (lock_.width() > 0 && lock_.locked(fx / LockMap::kCell, fy / LockMap::kCell)) continue;
            bool taken = false;
            for (const Source& s : sources)
                if (std::abs(s.x - fx) < kFieldStep / 2 && std::abs(s.y - fy) < kFieldStep / 2) taken = true;
            if (!taken) spots.emplace_back(fx, fy);
        }
    if (spots.empty()) return 0;
    auto& have = resources[std::size_t(player & 7)];
    for (std::size_t r = 0; r < have.size() && r < desc.price.size(); ++r) have[r] -= desc.price[r];
    for (const auto& [fx, fy] : spots) addObject(stage, fx, fy);
    return int(spots.size());
}

void World::updateObjects() {
    for (Source& s : sources) {
        if (s.object < 0) continue;
        const auto it = rules_.time.find(rules_.list.entries[std::size_t(s.object)].id);
        if (it == rules_.time.end()) continue;
        if (++s.timer >= it->second.amount * kTransformTimeSteps) setStage(s, rules_.find(it->second.next));
    }
}

void World::workDone(std::size_t index) {
    Source& s = sources[index];
    if (s.object < 0) return;
    const auto it = rules_.work.find(rules_.list.entries[std::size_t(s.object)].id);
    if (it == rules_.work.end()) return;
    if (++s.works >= it->second.amount) setStage(s, rules_.find(it->second.next));
}

std::optional<std::size_t> World::sourceNear(int x, int y, int maxDistance) const {
    std::optional<std::size_t> best;
    long long bestD = (long long)maxDistance * maxDistance;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (sources[i].resource < 0) continue;
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
    if (!u.alive() || u.inside || u.building() || source >= sources.size() || sources[source].resource < 0) return false;
    if (u.desc->portion[std::size_t(sources[source].resource)] <= 0) return false;
    u.target.reset();
    u.attacking = false;
    if (u.gather == Unit::Gather::None || u.source != source) u.carry = 0;
    u.source = source;
    u.gatherResource = sources[source].resource;
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
    // Источник иссяк (поле сжато) — ближайший другой с тем же ресурсом.
    if (u.gather == Unit::Gather::ToSource && sources[u.source].resource != u.gatherResource) {
        std::optional<std::size_t> next;
        long long bestD = 600LL * 600LL;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            if (sources[i].resource != u.gatherResource) continue;
            const long long ddx = sources[i].x - sources[u.source].x, ddy = sources[i].y - sources[u.source].y;
            if (ddx * ddx + ddy * ddy < bestD) bestD = ddx * ddx + ddy * ddy, next = i;
        }
        if (!next) {
            u.gather = Unit::Gather::None;
            return;
        }
        u.source = *next;
        pathTo(u, sources[u.source].x, sources[u.source].y);
    }
    const Source& src = sources[u.source];
    const auto res = std::size_t(u.gatherResource);
    const int reach = src.radius + 24;
    const int dx = src.x - u.x(), dy = src.y - u.y();
    switch (u.gather) {
    case Unit::Gather::ToSource:
        if (u.moving) return;
        if (dx * dx + dy * dy <= reach * reach) {
            u.gather = Unit::Gather::Working;
            u.workLeft = std::max(1, u.desc->workCycles[res]);
            u.direction = directionOf(dx, dy);
            u.animation = workAnimation(*u.desc, u.gatherResource);
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
        if (!u.base || !units_[*u.base].alive()) u.base = nearestBase(u, u.gatherResource);
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

int World::random(int n) {
    rng_ = rng_ * 1664525u + 1013904223u;
    return n > 0 ? int((rng_ >> 8) % std::uint32_t(n)) : 0;
}

// Разброс как в FUN_00f2c8c0: точка прицеливания сдвигается по каждой оси на
// (r − 512)·d·RAZBROS/32000, где r — 0..999, d = ((max(|dx|,|dy|) + |dx| + |dy|)/2)/32.
// Попадание — по ближайшему врагу к этой точке в пределах его радиуса и kHitRadius
// (снарядов с собственным столкновением ещё нет — своё допущение).
Unit* World::shotVictim(const Unit& u, Unit& t) {
    const int razbros = u.desc->razbros;
    if (razbros <= 0 || t.building()) return &t;
    const int dx = std::abs(t.x() - u.x()), dy = std::abs(t.y() - u.y());
    const int d = ((std::max(dx, dy) + dx + dy) / 2) >> 5;
    const int ax = t.x() + (random(1000) - 512) * d * razbros / 32000;
    const int ay = t.y() + (random(1000) - 512) * d * razbros / 32000;
    constexpr int kHitRadius = 24;
    Unit* best = nullptr;
    long long bestDist = 0;
    for (Unit& e : units_) {
        if (!e.alive() || e.inside || e.building() || !enemies(u.player, e.player)) continue;
        const long long ex = e.x() - ax, ey = e.y() - ay;
        const long long dist = ex * ex + ey * ey;
        const long long reach = kHitRadius + e.desc->unitRadius;
        if (dist <= reach * reach && (!best || dist < bestDist)) {
            best = &e;
            bestDist = dist;
        }
    }
    return best;
}

void World::hit(Unit& t, int damage) {
    if (!t.alive()) return;
    t.life -= damage;
    if (t.life > 0) return;
    t.life = 0;
    ++lostCount[std::size_t(t.player & 7)][t.desc];
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
            if (!e.alive() || e.inside || e.building() || !enemies(u.player, e.player)) continue;
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
        if (u.reload[i] > 0 || !canShoot(u, w)) continue;
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
        // Путь пересчитывается раз в секунду, но только если цель ушла от прежней точки
        // или юнит встал; недостижимая цель — повтор не чаще раза в 5 с (A* по всей карте дорог).
        const int moved = std::abs(t.x() - u.goalX) + std::abs(t.y() - u.goalY);
        if (!u.moving || moved > 150) {
            pathTo(u, t.x(), t.y());
            u.goalX = t.x();
            u.goalY = t.y();
            u.repathIn = u.moving ? 25 : 125;
        } else {
            u.repathIn = 25;
        }
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
    if (u.desc->peasant) return; // крестьянами распоряжается стратегический ИИ, в атаку их не шлют
    long long best = (long long)kAiRadius * kAiRadius;
    std::optional<std::size_t> target;
    for (std::size_t j = 0; j < units_.size(); ++j) {
        const Unit& e = units_[j];
        if (!e.alive() || e.inside || e.building() || !enemies(u.player, e.player)) continue;
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
        if (u.building() || !u.alive() || u.inside) continue;
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
    updateObjects();
    updateUpkeep();
    for (std::size_t index = 0; index < units_.size(); ++index) {
        Unit& u = units_[index];
        if (u.building()) { // кадры зданий — слои, не время
            updateProduction(index);
            updateMine(index);
            continue;
        }
        if (u.inside) continue; // в шахте
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
                // Ресурсы могли разобрать за время замаха — тогда выстрела нет.
                if (canShoot(u, u.weapon)) {
                    auto& have = resources[std::size_t(u.player & 7)];
                    const auto& cost = u.desc->shotCost[w];
                    for (std::size_t r = 0; r < cost.size(); ++r) have[r] -= cost[r];
                    if (!u.desc->weaponName[w].empty())
                        shotEvents.push_back({u.desc->weaponName[w], u.x(), u.y(), u.direction});
                    if (u.target && units_[*u.target].alive()) {
                        Unit& t = units_[*u.target];
                        const double reach = std::hypot(double(t.x() - u.x()), double(t.y() - u.y()));
                        // Цель успела уйти дальше полуторной дальности — промах.
                        if (reach <= u.desc->attackMax[w] * 1.5)
                            if (Unit* victim = shotVictim(u, t)) hit(*victim, u.desc->damage[w]);
                    }
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
        updateEnter(index);
        if (u.inside) continue;
        updateGather(index);
        if (u.gather == Unit::Gather::Working) {
            const int frames = int(u.animation->frames.size());
            if (++u.frame >= frames) {
                u.frame = 0;
                workDone(u.source);
                if (--u.workLeft <= 0 || sources[u.source].resource != u.gatherResource) {
                    const int r = u.gatherResource;
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
        setAnimation(u, u.moving ? (carrying ? carryAnimation(*u.desc, u.gatherResource)
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
