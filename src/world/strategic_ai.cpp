#include "world/strategic_ai.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "assets/game_paths.hpp"
#include "assets/xml_tree.hpp"

namespace oc2::world {

namespace {

constexpr int kWood = 0, kStone = 2, kFood = 3;
constexpr int kStepsPerSecond = 24; // 0x1800 / GameSpeed 256

std::optional<assets::AiScript> readScript(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        return assets::AiScript::parse(assets::parseXml(text));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "сценарий ИИ %s: %s\n", file.string().c_str(), e.what());
        return std::nullopt;
    }
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

long long dist2(int ax, int ay, int bx, int by) {
    const long long dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

} // namespace

std::optional<assets::AiScript> loadAiScript(const std::filesystem::path& dataRoot, std::string_view code) {
    if (code.empty()) return std::nullopt;
    if (const auto router = assets::findCaseInsensitive(dataRoot, "AI/router.xml")) {
        std::ifstream in(*router, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        try {
            const assets::XmlNode doc = assets::parseXml(text);
            for (const assets::XmlNode& route : doc.children) {
                const assets::XmlNode* scripts = route.child("AI_scripts");
                if (!scripts) continue;
                for (const assets::XmlNode& s : scripts->children) {
                    std::string name = s.value("ScriptName").value_or("");
                    std::replace(name.begin(), name.end(), '\\', '/');
                    const auto file = assets::findCaseInsensitive(dataRoot, name);
                    if (!file) continue;
                    auto script = readScript(*file);
                    if (script && script->nationCode() == code) return script;
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "AI/router.xml: %s\n", e.what());
        }
    }
    const auto dir = assets::findCaseInsensitive(dataRoot, "AI");
    if (!dir) return std::nullopt;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(*dir)) {
        const std::string name = lower(e.path().filename().string());
        if (name.size() > 7 && name.ends_with(".ai.xml") && name != "autosave.ai.xml") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        auto script = readScript(f);
        if (script && script->nationCode() == code) return script;
    }
    return std::nullopt;
}

std::string playerNation(const World& world, const UnitCatalog& catalog, int player) {
    for (const Unit& u : world.units()) {
        if (u.player != player || !u.alive()) continue;
        const std::string_view type = catalog.typeOf(u.desc);
        const auto open = type.rfind('('), close = type.rfind(')');
        if (open != std::string_view::npos && close != std::string_view::npos && close > open + 1)
            return std::string(type.substr(open + 1, close - open - 1));
    }
    return {};
}

StrategicAi::StrategicAi(World& world, UnitCatalog& catalog, int player, assets::AiScript script, int difficulty,
                         std::uint32_t seed)
    : world_(world), catalog_(catalog), player_(player), script_(std::move(script)), difficulty_(difficulty),
      rng_(seed) {}

void StrategicAi::update() {
    // Игроки думают вразнобой, чтобы не нагружать один шаг.
    if ((world_.stepCount() + std::uint64_t(player_) * 3) % kThinkSteps == 0) think();
}

void StrategicAi::think() {
    buildStarted_ = false;
    indexSources();
    for (const assets::AiFolder& folder : script_.folders) {
        if (!chainHolds(folder.condition)) continue;
        for (const assets::AiTrigger& trigger : folder.rules) {
            if (!chainHolds(trigger.condition)) continue;
            for (const assets::AiAction& a : trigger.actions) act(a);
        }
    }
    employIdle();
    sendWave();
}

bool StrategicAi::chainHolds(const std::vector<assets::AiCondition>& chain) {
    for (const assets::AiCondition& c : chain)
        if (!holds(c)) return false; // дальше по цепочке не смотрим, как в оригинале с «и»
    return true;
}

int StrategicAi::count(const assets::UnitDesc* desc, bool readyOnly) const {
    int n = 0;
    for (const Unit& u : world_.units())
        if (u.player == player_ && u.desc == desc && u.alive() && !(readyOnly && u.underConstruction)) ++n;
    return n;
}

bool StrategicAi::holds(const assets::AiCondition& c) {
    using K = assets::AiCondition::Kind;
    const auto p = std::size_t(player_ & 7);
    switch (c.kind) {
    case K::Resources:
        return c.resource >= 0 && assets::aiCompare(world_.resources[p][std::size_t(c.resource)], c.op, c.value);
    case K::ReadyUnits:
        return assets::aiCompare(count(catalog_.find(c.unitType), true), c.op, c.value);
    case K::GamingTime:
        return assets::aiCompare(int(world_.stepCount() / kStepsPerSecond), c.op, c.value);
    case K::UnitsProduced: {
        const auto it = world_.producedCount[p].find(catalog_.find(c.unitType));
        return assets::aiCompare(it == world_.producedCount[p].end() ? 0 : it->second, c.op, c.value);
    }
    case K::UnitsLost: {
        int lost = 0;
        if (c.unitType.empty()) {
            for (const auto& [desc, n] : world_.lostCount[p]) lost += n;
        } else if (const auto it = world_.lostCount[p].find(catalog_.find(c.unitType)); it != world_.lostCount[p].end()) {
            lost = it->second;
        }
        return assets::aiCompare(lost, c.op, c.value);
    }
    case K::OnceInPeriod: {
        // FUN_015f4e80: прошло больше периода — истина; отставший больше чем на два периода
        // отсчёт переносится на «сейчас», иначе сдвигается на период.
        const std::uint64_t period = std::uint64_t(std::max(1, c.value)) * kStepsPerSecond;
        std::uint64_t& start = periodStart_[&c];
        const std::uint64_t elapsed = world_.stepCount() - start;
        if (elapsed <= period) return false;
        start = elapsed > 2 * period ? world_.stepCount() : start + period;
        return true;
    }
    case K::FreeLivePlaces:
        return assets::aiCompare(world_.populationLimit(player_) - world_.population(player_), c.op, c.value);
    case K::TotalLivePlaces:
        return assets::aiCompare(world_.populationLimit(player_), c.op, c.value);
    case K::Difficulty:
        return assets::aiCompare(difficulty_, c.op, c.value);
    case K::EnemyNearBase: {
        const auto [cx, cy] = baseCenter();
        const long long r2 = (long long)c.radius * c.radius;
        int n = 0;
        for (const Unit& u : world_.units())
            if (u.alive() && !u.inside && !u.building() && world_.enemies(player_, u.player) &&
                dist2(u.x(), u.y(), cx, cy) <= r2)
                ++n;
        return n >= c.minAmount && n <= c.maxAmount;
    }
    case K::Settlements:
        return settlements_ && assets::aiCompare(settlements_->owned(player_, c.resource, c.level), c.op, c.value);
    case K::Unsupported: return false;
    }
    return false;
}

bool StrategicAi::roll(int percent) {
    if (percent >= 100) return true;
    if (percent <= 0) return false;
    return int(rng_() % 100) < percent;
}

void StrategicAi::act(const assets::AiAction& a) {
    using K = assets::AiAction::Kind;
    switch (a.kind) {
    case K::Build: build(a); break;
    case K::Produce: produce(a); break;
    case K::AddResources:
        if (a.resource >= 0) world_.resources[std::size_t(player_ & 7)][std::size_t(a.resource)] += a.amount;
        break;
    case K::PeonsDistribution:
        onWood_ = std::max(0, a.onWood);
        onStone_ = std::max(0, a.onStone);
        onFood_ = std::max(0, a.onFood);
        break;
    case K::BuyResource: buyResource(a); break;
    case K::UpgradeSettlements: upgradeSettlement(a); break;
    case K::Unsupported: break;
    }
}

void StrategicAi::upgradeSettlement(const assets::AiAction& a) {
    if (!settlements_ || a.resource < 0 || a.level <= 0) return;
    const auto [cx, cy] = baseCenter();
    if (const auto i = settlements_->upgradeCandidate(player_, a.resource, a.level, cx, cy, a.maxDistance))
        settlements_->startUpgrade(*i, a.level, a.costPercent);
}

// BuyResource (FUN_015d2c10): продаёт Amount ресурса ResTypeToSell (сколько есть) за ResTypeToBuy.
void StrategicAi::buyResource(const assets::AiAction& a) {
    if (!world_.market || a.resource < 0 || a.sellResource < 0 || a.amount <= 0) return;
    world_.market->trade(world_.resources[std::size_t(player_ & 7)], a.sellResource, a.resource, a.amount);
}

std::pair<int, int> StrategicAi::baseCenter() const {
    // Центр базы — первое своё здание, принимающее ресурсы (на картах это ратуша), иначе любое,
    // иначе середина своих юнитов.
    const Unit* any = nullptr;
    for (const Unit& u : world_.units()) {
        if (u.player != player_ || !u.alive() || !u.building()) continue;
        if (u.desc->resourceBase && !u.underConstruction) return {u.x(), u.y()};
        if (!any) any = &u;
    }
    if (any) return {any->x(), any->y()};
    long long sx = 0, sy = 0, n = 0;
    for (const Unit& u : world_.units())
        if (u.player == player_ && u.alive() && !u.inside) sx += u.x(), sy += u.y(), ++n;
    return n ? std::pair<int, int>{int(sx / n), int(sy / n)} : std::pair<int, int>{0, 0};
}

bool StrategicAi::hasBase(int resource) const {
    for (const Unit& u : world_.units())
        if (u.player == player_ && u.building() && u.alive() && !u.underConstruction &&
            (u.desc->resourceBase & (1u << resource)))
            return true;
    return false;
}

void StrategicAi::indexSources() {
    grid_.clear();
    for (std::size_t i = 0; i < world_.sources.size(); ++i) {
        const World::Source& s = world_.sources[i];
        grid_[(std::int64_t(s.y >> kGridShift) << 32) | std::uint32_t(s.x >> kGridShift)].push_back(i);
    }
}

bool StrategicAi::sourceWithin(int x, int y, int radius, int resource) const {
    const long long r2 = (long long)radius * radius;
    for (int gy = (y - radius) >> kGridShift; gy <= (y + radius) >> kGridShift; ++gy)
        for (int gx = (x - radius) >> kGridShift; gx <= (x + radius) >> kGridShift; ++gx) {
            const auto it = grid_.find((std::int64_t(gy) << 32) | std::uint32_t(gx));
            if (it == grid_.end()) continue;
            for (const std::size_t i : it->second) {
                const World::Source& s = world_.sources[i];
                if ((resource < 0 || s.resource == resource) && dist2(s.x, s.y, x, y) <= r2) return true;
            }
        }
    return false;
}

std::optional<std::pair<int, int>> StrategicAi::findSite(const assets::UnitDesc& desc, const assets::AiAction& a) {
    const auto [cx, cy] = baseCenter();
    const int from = std::max(a.minDistFromCenter, 250);
    const int start = int(rng_() % 360);
    for (int r = from; r <= from + kBaseRadius; r += 64) {
        const int n = std::max(8, int(2.0 * 3.14159265 * r / 96.0));
        for (int k = 0; k < n; ++k) {
            const double ang = (start + 360.0 * k / n) * 3.14159265 / 180.0;
            const int x = cx + int(r * std::cos(ang)), y = cy + int(r * std::sin(ang));
            if (!world_.canPlace(desc, x, y)) continue;
            // Запас вокруг: здание не вплотную к соседним и к воде — проходы остаются.
            bool roomy = true;
            for (const auto& [ox, oy] : {std::pair{48, 48}, {-48, 48}, {48, -48}, {-48, -48}})
                if (!world_.canPlace(desc, x + ox, y + oy)) roomy = false;
            if (!roomy || sourceWithin(x, y, 160, -1)) continue;
            if (a.nearTrees && !sourceWithin(x, y, 400, kWood)) continue;
            if (a.nearStones && !sourceWithin(x, y, 400, kStone)) continue;
            return std::make_pair(x, y);
        }
    }
    return std::nullopt;
}

bool StrategicAi::build(const assets::AiAction& a) {
    const assets::UnitDesc* desc = catalog_.find(a.unitType);
    if (!desc) return false;
    // Build с юнитом (UnitPar(RU) в сценариях) — по сути производство.
    if (!desc->building) return produce(a);
    if (buildStarted_) return false;
    if (count(desc, false) >= a.maxAmount) return false;
    // Своё правило против тупика: пока нет здания, принимающего ресурсы (готового или
    // строящегося), ставится только такое — иначе добыча не начнётся никогда.
    if (!desc->resourceBase && std::none_of(world_.units().begin(), world_.units().end(), [&](const Unit& u) {
            return u.player == player_ && u.alive() && u.building() && u.desc->resourceBase;
        }))
        return false;
    if (!a.multiple)
        for (const Unit& u : world_.units())
            if (u.player == player_ && u.desc == desc && u.alive() && u.underConstruction) return false;
    if (!world_.affordable(*desc, player_)) return false;
    if (!roll(a.probability)) return false;
    // Строители: свободные крестьяне, затем добытчики — ближайшие к центру базы.
    const auto [cx, cy] = baseCenter();
    std::vector<std::pair<long long, std::size_t>> crew;
    const auto& units = world_.units();
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (u.player != player_ || !u.alive() || u.inside || u.mine || u.buildTarget || !u.desc->peasant) continue;
        const long long busy = u.gather != Unit::Gather::None ? (1LL << 40) : 0;
        crew.emplace_back(busy + dist2(u.x(), u.y(), cx, cy), i);
    }
    if (crew.empty()) return false;
    const auto site = findSite(*desc, a);
    if (!site) return false;
    const auto b = world_.placeBuilding(*desc, player_, site->first, site->second);
    if (!b) return false;
    buildStarted_ = true;
    std::sort(crew.begin(), crew.end());
    const std::size_t take = std::min(crew.size(), std::size_t(a.maxPeasants));
    for (std::size_t k = 0; k < take; ++k) world_.orderBuild(crew[k].second, *b);
    return true;
}

bool StrategicAi::produce(const assets::AiAction& a) {
    const assets::UnitDesc* desc = catalog_.find(a.unitType);
    if (!desc || desc->building) return false;
    auto& units = world_.units();
    int total = count(desc, false);
    int queuedFarm = 0;
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Unit& b = units[i];
        if (b.player != player_ || !b.building() || !b.alive()) continue;
        for (const assets::UnitDesc* q : b.queue) {
            if (q == desc) ++total;
            if (!q->noFarm) ++queuedFarm;
        }
        if (b.underConstruction) continue;
        const auto& list = catalog_.produces(catalog_.typeOf(b.desc));
        const bool makes = std::any_of(list.begin(), list.end(),
                                       [&](const assets::ProduceItem& it) { return it.member == a.unitType; });
        if (makes && (!best || b.queue.size() < units[*best].queue.size())) best = i;
    }
    if (!best || total >= a.maxAmount || !roll(a.probability)) return false;
    bool any = false;
    while (int(units[*best].queue.size()) < a.packSize && total < a.maxAmount) {
        // Мест нет — не ставим в очередь, иначе ресурсы зря лягут в здание.
        if (!desc->noFarm && world_.population(player_) + queuedFarm >= world_.populationLimit(player_)) break;
        if (!world_.orderProduce(*best, *desc)) break;
        ++total;
        if (!desc->noFarm) ++queuedFarm;
        any = true;
    }
    return any;
}

bool StrategicAi::sowNear(int x, int y) {
    // Поле сеет здание, в чьём [FIXED_PRODUCE] есть объект-посев (мельница).
    const auto& units = world_.units();
    for (const Unit& b : units) {
        if (b.player != player_ || !b.building() || !b.alive() || b.underConstruction) continue;
        if (dist2(b.x(), b.y(), x, y) > (long long)kBaseRadius * kBaseRadius) continue;
        for (const assets::ProduceItem& it : catalog_.produces(catalog_.typeOf(b.desc))) {
            const assets::UnitDesc* field = catalog_.find(it.member);
            if (!field || !field->spriteObject) continue;
            const int start = int(rng_() % 8);
            for (int r = 300; r <= 700; r += 200)
                for (int k = 0; k < 8; ++k) {
                    const double ang = (start + k) * 3.14159265 / 4.0;
                    const int fx = b.x() + int(r * std::cos(ang)), fy = b.y() + int(r * std::sin(ang));
                    if (sourceWithin(fx, fy, 200, -1)) continue;
                    if (world_.sowField(*field, player_, fx, fy) > 0) {
                        indexSources();
                        return true;
                    }
                }
        }
    }
    return false;
}

void StrategicAi::sendWave() {
    const std::uint64_t period = std::uint64_t(kWaveSeconds) * 24;
    if (world_.stepCount() < period || world_.stepCount() / period == lastWave_) return;
    auto& units = world_.units();
    std::vector<std::size_t> army;
    long long sx = 0, sy = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (u.player != player_ || !u.alive() || u.inside || u.building() || u.desc->peasant || u.attacking ||
            u.target)
            continue;
        if (std::none_of(u.desc->damage.begin(), u.desc->damage.end(), [](int d) { return d > 0; })) continue;
        army.push_back(i);
        sx += u.x();
        sy += u.y();
    }
    if (int(army.size()) < kWaveMinArmy) return;
    lastWave_ = world_.stepCount() / period;
    const long long n = (long long)army.size();
    const int cx = int(sx / n), cy = int(sy / n);
    std::optional<std::size_t> goal;
    long long best = 0;
    for (std::size_t j = 0; j < units.size(); ++j) {
        const Unit& e = units[j];
        if (!e.alive() || e.inside || !world_.enemies(player_, e.player)) continue;
        const long long d = dist2(e.x(), e.y(), cx, cy);
        if (!goal || d < best) {
            goal = j;
            best = d;
        }
    }
    if (!goal) return;
    for (const std::size_t i : army) world_.orderAttack(i, *goal);
}

void StrategicAi::employIdle() {
    auto& units = world_.units();
    const auto [cx, cy] = baseCenter();
    const long long base2 = (long long)kBaseRadius * kBaseRadius;
    // Сколько крестьян уже в шахтах и идут туда, сколько на каждом источнике и ресурсе.
    std::unordered_map<std::size_t, int> crew, onSource;
    std::array<int, assets::UnitDesc::kResources> onResource{};
    std::vector<std::size_t> idle;
    int peasants = 0, inMines = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (u.player != player_ || !u.alive() || !u.desc->peasant) continue;
        ++peasants;
        if (u.mine) {
            ++inMines;
            if (!u.inside) ++crew[*u.mine];
        } else if (u.gather != Unit::Gather::None) {
            ++onSource[u.source];
            ++onResource[std::size_t(u.gatherResource)];
        } else if (!u.buildTarget && !u.moving && !u.target && !u.attacking) {
            idle.push_back(i);
        }
    }
    if (idle.empty()) return;
    std::vector<std::size_t> mines;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Unit& m = units[i];
        if (!m.building() || !m.alive() || m.underConstruction || m.desc->peasantAbsorber <= 0 ||
            !m.desc->producerResources || (m.player != player_ && m.player != 7) ||
            dist2(m.x(), m.y(), cx, cy) > base2)
            continue;
        crew[i] += int(m.occupants.size());
        mines.push_back(i);
    }
    bool sowed = false;
    for (const std::size_t i : idle) {
        bool placed = false;
        // В шахты — не больше трети крестьян: дерево, камень и еда нужнее для роста.
        for (const std::size_t m : mines)
            if (inMines < peasants / kMineShare && crew[m] < std::min(kMineCrew, units[m].desc->peasantAbsorber) &&
                world_.orderEnter(i, m)) {
                ++crew[m];
                ++inMines;
                placed = true;
                break;
            }
        if (placed) continue;
        // Ресурс с наибольшей нехваткой против долей PeonsDistribution.
        const int weights[3] = {onWood_, onStone_, onFood_};
        const int kinds[3] = {kWood, kStone, kFood};
        const int sum = std::max(1, weights[0] + weights[1] + weights[2]);
        const int total = onResource[kWood] + onResource[kStone] + onResource[kFood] + 1;
        int order[3] = {0, 1, 2};
        std::sort(order, order + 3, [&](int a, int b) {
            const double da = double(weights[a]) * total / sum - onResource[std::size_t(kinds[a])];
            const double db = double(weights[b]) * total / sum - onResource[std::size_t(kinds[b])];
            return da > db;
        });
        const Unit& u = units[i];
        for (const int k : order) {
            const int res = kinds[k];
            if (weights[k] <= 0 || !hasBase(res) || u.desc->portion[std::size_t(res)] <= 0) continue;
            std::optional<std::size_t> best;
            long long bestD = 0;
            for (std::size_t s = 0; s < world_.sources.size(); ++s) {
                const World::Source& src = world_.sources[s];
                if (src.resource != res || dist2(src.x, src.y, cx, cy) > base2) continue;
                const auto it = onSource.find(s);
                if (it != onSource.end() && it->second >= kGatherersPerSource) continue;
                const long long d = dist2(src.x, src.y, u.x(), u.y());
                if (!best || d < bestD) best = s, bestD = d;
            }
            if (!best && res == kFood && !sowed) {
                sowed = true; // засеять — раз за раздумье; крестьянин пойдёт на поле в следующий раз
                // Пока растут уже засеянные поля, новых не сеем.
                const bool growing = std::any_of(world_.sources.begin(), world_.sources.end(), [&](const World::Source& f) {
                    const auto& rules = world_.objectRules();
                    return f.object >= 0 && f.resource != kFood && std::size_t(f.object) < rules.list.entries.size() &&
                           rules.time.count(rules.list.entries[std::size_t(f.object)].id) &&
                           dist2(f.x, f.y, cx, cy) <= base2;
                });
                if (!growing) sowNear(cx, cy);
            }
            if (best && world_.orderGather(i, *best)) {
                ++onSource[*best];
                ++onResource[std::size_t(res)];
                break;
            }
        }
    }
}

} // namespace oc2::world
