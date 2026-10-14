#include "world/bigmap.hpp"

#include <algorithm>
#include <sstream>

namespace oc2::world {

namespace {

using assets::kBigNations;

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

bool isArtillery(const std::string& t) { return startsWith(t, "Art"); }
bool isGuard(const std::string& t) { return startsWith(t, "Guard"); }
bool isCavalry(const std::string& t) { return startsWith(t, "UnitK"); }

} // namespace

BigGame::BigGame(std::shared_ptr<const Data> data, int playerNation, std::uint32_t seed)
    : data_(std::move(data)), player_(playerNation), seed_(seed ? seed : 1) {
    const auto& k = data_->k;
    sectors_.resize(data_->map.sectors.size());
    for (const assets::BigSector& s : data_->map.sectors) {
        SectorState& st = sectors_[std::size_t(s.id)];
        st.owner = s.owner;
        st.level = std::clamp(s.defence, 0, 3);
        st.garrison = k.integer("SectGarrisonSquads" + std::to_string(st.level), 4);
    }
    for (int n = 0; n < kBigNations; ++n) {
        NationState& ns = nations_[std::size_t(n)];
        const bool me = n == player_;
        ns.res[0] = k.integer(me ? "PLAYER_RES0" : "ENEMY__RES0", 5000);
        ns.res[1] = k.integer(me ? "PLAYER_RES1" : "ENEMY__RES1", 5000);
        ns.res[2] = k.integer(me ? "PLAYER_RES2" : "ENEMY__RES2", 5000);
        ns.res[3] = 4000;
        ns.res[4] = 1000;
        ns.res[5] = 1000;
        ns.recruits = k.integer("SECT_REG_RECRTS", 30) * 4;
        ns.alive = ownedBy(n) > 0;
        // Стартовая армия — по таблице звания «лейтенант»: особые отряды и основная пехота.
        const assets::HeroArmyRow& row = data_->hero.rows[0][std::size_t(n)];
        int left = row.squads;
        for (const auto& [type, count] : row.specials)
            for (int i = 0; i < count && left > 0; ++i) ns.army.push_back(type), --left;
        while (left-- > 0) ns.army.push_back(basicSquad(n));
    }
}

int BigGame::ownedBy(int nation) const {
    int c = 0;
    for (const SectorState& s : sectors_) c += s.owner == nation;
    return c;
}

int BigGame::armyCapacity(int nation) const {
    return data_->hero.rows[std::size_t(nations_[std::size_t(nation)].rank)][std::size_t(nation)].squads;
}

int BigGame::nextRankExp(int nation) const {
    static const char* const keys[] = {"LTN_EXPS_LIMIT", "CPT_EXPS_LIMIT", "MJR_EXPS_LIMIT",
                                       "COL_EXPS_LIMIT", "GEN_EXPS_LIMIT"};
    const int r = nations_[std::size_t(nation)].rank;
    return r >= 5 ? 0 : data_->k.integer(keys[r], 1000000);
}

int BigGame::squadPower(const std::string& t) {
    if (isGuard(t)) return t.size() > 8 && t[8] == 'K' ? 28 : 24; // «GuardEngKDrgT» — конная гвардия
    if (isArtillery(t)) return startsWith(t, "ArtPus4G") ? 24 : 18;
    if (isCavalry(t)) return 16;
    if (startsWith(t, "UnitGre") || startsWith(t, "UnitEgr")) return 14;
    if (startsWith(t, "UnitSap")) return 11;
    if (startsWith(t, "UnitVol")) return 7;
    return 10;
}

BigGame::Cost BigGame::squadCost(const std::string& t) {
    Cost c;
    c.recruits = 12;
    auto set = [&](int gold, int food, int iron, int coal) {
        c.res[1] = gold, c.res[3] = food, c.res[4] = iron, c.res[5] = coal;
    };
    if (isGuard(t)) set(1500, 600, 600, 0);
    else if (isArtillery(t)) set(900, 0, 500, 300), c.recruits = 6;
    else if (isCavalry(t)) set(800, 500, 300, 0);
    else if (startsWith(t, "UnitGre") || startsWith(t, "UnitEgr") || startsWith(t, "UnitSap")) set(500, 400, 200, 0);
    else if (startsWith(t, "UnitVol")) set(0, 300, 0, 0);
    else set(300, 400, 0, 0);
    return c;
}

std::string BigGame::basicSquad(int nation) { return std::string("UnitFuz(") + assets::bigNationCode(nation) + ")"; }

std::array<int, BigGame::kRes> BigGame::income(int nation) const {
    const auto& k = data_->k;
    std::array<int, kRes> r{};
    for (const assets::BigSector& s : data_->map.sectors) {
        if (sectors_[std::size_t(s.id)].owner != nation) continue;
        r[0] += k.integer("SECT_INCOME_WOOD");
        r[1] += k.integer("SECT_INCOME_GOLD");
        r[2] += k.integer("SECT_INCOME_STONE");
        r[3] += k.integer("SECT_INCOME_FOOD");
    }
    return r;
}

bool BigGame::adjacent(int a, int b) const {
    const auto& nb = data_->map.sectors[std::size_t(a)].neighbors;
    return std::find(nb.begin(), nb.end(), b) != nb.end();
}

std::optional<int> BigGame::attackBase(int nation, int target) const {
    for (int n : data_->map.sectors[std::size_t(target)].neighbors)
        if (sectors_[std::size_t(n)].owner == nation) return n;
    return std::nullopt;
}

bool BigGame::canAttack(int target) const {
    if (target < 0 || target >= sectorCount()) return false;
    if (sectors_[std::size_t(target)].owner == player_) return false;
    return attackBase(player_, target) && !nations_[std::size_t(player_)].army.empty();
}

int BigGame::defencePower(int target) const {
    const SectorState& s = sectors_[std::size_t(target)];
    return s.garrison * (data_->k.integer("MinSectDefSquadPower", 11) + data_->k.integer("AddSectDefSquadPower", 2) * s.level);
}

int BigGame::attackPower(const std::vector<std::string>& squads) const {
    int p = 0;
    for (const std::string& t : squads) p += squadPower(t);
    return p;
}

std::vector<std::string> BigGame::defenders(int target) const {
    const SectorState& s = sectors_[std::size_t(target)];
    std::vector<std::string> out;
    for (int i = 0; i < s.garrison; ++i) out.push_back(i % 4 == 3 ? "UnitVol(" + std::string(assets::bigNationCode(s.owner)) + ")" : basicSquad(s.owner));
    return out;
}

BigGame::Attack BigGame::beginPlayerAttack(int target) const {
    Attack a;
    a.nation = player_;
    a.target = target;
    a.squads = nations_[std::size_t(player_)].army;
    return a;
}

std::uint32_t BigGame::rnd() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

BigGame::Outcome BigGame::autoResolve(const Attack& a) {
    Outcome o;
    const double att = std::max(1, attackPower(a.squads)) * (0.85 + double(rnd() % 31) / 100.0);
    const double def = std::max(1, defencePower(a.target));
    const double ratio = att / def;
    o.attackerWon = ratio > 1.0;
    o.survived.assign(a.squads.size(), false);
    if (o.attackerWon) {
        const std::uint32_t lossPct = std::uint32_t(std::min(70.0, 50.0 / ratio));
        bool any = false;
        for (std::size_t i = 0; i < a.squads.size(); ++i)
            if (rnd() % 100 >= lossPct) o.survived[i] = any = true;
        if (!any && !a.squads.empty()) o.survived[0] = true;
        o.defenderLost = sectors_[std::size_t(a.target)].garrison;
    } else {
        // Отступившие: часть уходит живой, если бой был близок.
        const std::uint32_t keepPct = ratio > 0.6 ? 30u : 0u;
        for (std::size_t i = 0; i < a.squads.size(); ++i) o.survived[i] = rnd() % 100 < keepPct;
        o.defenderLost = int(std::min<double>(sectors_[std::size_t(a.target)].garrison - 1, ratio * 0.6 * sectors_[std::size_t(a.target)].garrison));
        o.defenderLost = std::max(0, o.defenderLost);
    }
    return o;
}

void BigGame::giveExp(int nation, int exp) {
    NationState& n = nations_[std::size_t(nation)];
    n.exp += exp;
    while (n.rank < 5 && n.exp >= nextRankExp(nation)) {
        ++n.rank;
        log.push_back(std::string(assets::bigNationName(nation)) + ": новое звание героя");
    }
}

void BigGame::apply(const Attack& a, const Outcome& o) {
    NationState& n = nations_[std::size_t(a.nation)];
    SectorState& s = sectors_[std::size_t(a.target)];
    const auto& k = data_->k;
    const int defender = s.owner;
    for (const std::string& t : a.squads) {
        const auto it = std::find(n.army.begin(), n.army.end(), t);
        if (it != n.army.end()) n.army.erase(it);
    }
    int lostAttackers = 0;
    for (std::size_t i = 0; i < a.squads.size(); ++i) {
        if (i < o.survived.size() && o.survived[i]) n.army.push_back(a.squads[i]);
        else ++lostAttackers;
    }
    const std::string& sname = data_->map.sectors[std::size_t(a.target)].name;
    if (o.attackerWon) {
        s.owner = a.nation;
        s.garrison = std::max(1, k.integer("SectGarrisonSquads" + std::to_string(s.level), 4) / 2);
        giveExp(a.nation, k.integer("SectCaptureExp", 200) + k.integer("DisbandSquadExp", 175) * o.defenderLost);
        // Добавка ресурсов за взятый сектор (SECT_ADDING_*): вид — «Ressource» сектора.
        const int kind = data_->map.sectors[std::size_t(a.target)].resource;
        if (kind == 1) n.res[3] += k.integer("SECT_ADDING_FOOD", 4000);
        else if (kind == 2) n.res[1] += k.integer("SECT_ADDING_GOLD", 1500);
        else if (kind == 3) n.res[4] += k.integer("SECT_ADDING_IRON", 1000);
        else if (kind == 4) n.res[5] += k.integer("SECT_ADDING_COAL", 1000);
        if (ownedBy(defender) == 0) {
            nations_[std::size_t(defender)].alive = false;
            nations_[std::size_t(defender)].army.clear();
            log.push_back(std::string(assets::bigNationName(defender)) + " разгромлена");
        }
        log.push_back(std::string(assets::bigNationName(a.nation)) + " берёт " + sname + " у " + assets::bigNationName(defender));
    } else {
        s.garrison = std::max(1, s.garrison - o.defenderLost);
        giveExp(defender, k.integer("DisbandSquadExp", 175) * lostAttackers);
        log.push_back(std::string(assets::bigNationName(a.nation)) + " отбита у " + sname);
    }
}

std::vector<std::string> BigGame::recruitable(int nation) const {
    const NationState& n = nations_[std::size_t(nation)];
    std::vector<std::string> out{basicSquad(nation), "UnitVol(" + std::string(assets::bigNationCode(nation)) + ")"};
    for (int r = 0; r <= n.rank; ++r)
        for (const auto& [type, count] : data_->hero.rows[std::size_t(r)][std::size_t(nation)].specials) {
            (void)count;
            if (std::find(out.begin(), out.end(), type) == out.end()) out.push_back(type);
        }
    return out;
}

bool BigGame::recruit(int nation, const std::string& type) {
    NationState& n = nations_[std::size_t(nation)];
    if (int(n.army.size()) >= armyCapacity(nation)) return false;
    const Cost c = squadCost(type);
    if (n.recruits < c.recruits) return false;
    for (int i = 0; i < kRes; ++i)
        if (n.res[std::size_t(i)] < c.res[std::size_t(i)]) return false;
    for (int i = 0; i < kRes; ++i) n.res[std::size_t(i)] -= c.res[std::size_t(i)];
    n.recruits -= c.recruits;
    n.army.push_back(type);
    return true;
}

bool BigGame::upgradeDefence(int target) {
    SectorState& s = sectors_[std::size_t(target)];
    NationState& n = nations_[std::size_t(s.owner)];
    const auto& k = data_->k;
    const int wood = k.integer("SECT_DEFENCE_WOOD", 5000), stone = k.integer("SECT_DEFENCE_STONE", 5000),
              gold = k.integer("SECT_DEFENCE_GOLD", 5000), men = k.integer("SECT_DEFENCE_RECRTS", 240);
    if (s.level >= 3 || n.res[0] < wood || n.res[2] < stone || n.res[1] < gold || n.recruits < men) return false;
    n.res[0] -= wood, n.res[2] -= stone, n.res[1] -= gold, n.recruits -= men;
    ++s.level;
    s.garrison = k.integer("SectGarrisonSquads" + std::to_string(s.level), s.garrison);
    return true;
}

void BigGame::grow(int nation) {
    NationState& n = nations_[std::size_t(nation)];
    const int cap = ownedBy(nation) * data_->k.integer("SECT_MAX_RECRTS", 120);
    n.recruits = std::min(cap, n.recruits + recruitIncome(nation));
    const auto inc = income(nation);
    for (int i = 0; i < kRes; ++i) n.res[std::size_t(i)] += inc[std::size_t(i)];
}

void BigGame::startTurn() {
    ++turn_;
    for (int n = 0; n < kBigNations; ++n)
        if (nations_[std::size_t(n)].alive) grow(n);
    for (SectorState& s : sectors_) // гарнизон восстанавливается по отряду в ход до нормы уровня
        s.garrison = std::min(s.garrison + 1, data_->k.integer("SectGarrisonSquads" + std::to_string(s.level), 4));
}

std::optional<BigGame::Attack> BigGame::aiTurn(int nation) {
    NationState& n = nations_[std::size_t(nation)];
    if (!n.alive) return std::nullopt;
    // Пополнение армии героя.
    const std::vector<std::string> pool = recruitable(nation);
    for (int guard = 0; guard < 30 && int(n.army.size()) < armyCapacity(nation); ++guard) {
        const std::string& t = pool[rnd() % 4 == 0 ? rnd() % pool.size() : 0];
        if (!recruit(nation, t) && !recruit(nation, pool[0])) break;
    }
    if (n.army.empty() || rnd() % 100 >= 65) return std::nullopt;
    // Цель: самый слабый чужой сектор рядом со своим, который армия берёт с запасом.
    int best = -1;
    double bestRatio = 1.15;
    const double power = attackPower(n.army);
    for (const assets::BigSector& s : data_->map.sectors) {
        if (sectors_[std::size_t(s.id)].owner == nation || !attackBase(nation, s.id)) continue;
        const double ratio = power / std::max(1, defencePower(s.id));
        if (ratio > bestRatio) bestRatio = ratio, best = s.id;
    }
    if (best < 0) return std::nullopt;
    Attack a{nation, best, n.army};
    if (sectors_[std::size_t(best)].owner == player_) return a; // защиту разыгрывает игрок
    apply(a, autoResolve(a));
    return std::nullopt;
}

std::optional<BigGame::Attack> BigGame::endTurn() {
    log.clear();
    nextNation_ = 0;
    return continueTurn();
}

std::optional<BigGame::Attack> BigGame::continueTurn() {
    while (nextNation_ < kBigNations) {
        const int n = nextNation_++;
        if (n == player_) continue;
        if (auto a = aiTurn(n)) return a;
    }
    startTurn();
    return std::nullopt;
}

int BigGame::result() const {
    if (ownedBy(player_) == 0) return -1;
    if (ownedBy(player_) == sectorCount()) return 1;
    return 0;
}

std::string BigGame::serialize() const {
    std::ostringstream o;
    o << "BIG1 " << player_ << ' ' << turn_ << ' ' << seed_ << '\n';
    for (std::size_t i = 0; i < sectors_.size(); ++i)
        o << "S " << i << ' ' << sectors_[i].owner << ' ' << sectors_[i].garrison << ' ' << sectors_[i].level << '\n';
    for (int i = 0; i < kBigNations; ++i) {
        const NationState& n = nations_[std::size_t(i)];
        o << "N " << i << ' ' << n.alive << ' ' << n.rank << ' ' << n.exp << ' ' << n.recruits;
        for (int r : n.res) o << ' ' << r;
        o << ' ' << n.army.size();
        for (const std::string& t : n.army) o << ' ' << t;
        o << '\n';
    }
    return o.str();
}

std::optional<BigGame> BigGame::deserialize(std::shared_ptr<const Data> data, const std::string& text) {
    std::istringstream in(text);
    std::string tag;
    int player = 0, turn = 1;
    std::uint32_t seed = 1;
    if (!(in >> tag >> player >> turn >> seed) || tag != "BIG1" || player < 0 || player >= kBigNations) return std::nullopt;
    BigGame g(std::move(data), player, seed);
    g.turn_ = turn;
    std::size_t sectorsSeen = 0;
    while (in >> tag) {
        if (tag == "S") {
            std::size_t i;
            SectorState s;
            if (!(in >> i >> s.owner >> s.garrison >> s.level) || i >= g.sectors_.size()) return std::nullopt;
            g.sectors_[i] = s;
            ++sectorsSeen;
        } else if (tag == "N") {
            int i, alive;
            NationState n;
            std::size_t count;
            if (!(in >> i >> alive >> n.rank >> n.exp >> n.recruits) || i < 0 || i >= kBigNations) return std::nullopt;
            for (int& r : n.res) in >> r;
            if (!(in >> count) || count > 200) return std::nullopt;
            for (std::size_t k = 0; k < count; ++k) {
                std::string t;
                in >> t;
                n.army.push_back(t);
            }
            n.alive = alive != 0;
            n.rank = std::clamp(n.rank, 0, 5);
            g.nations_[std::size_t(i)] = std::move(n);
        } else {
            return std::nullopt;
        }
    }
    if (sectorsSeen != g.sectors_.size()) return std::nullopt;
    return g;
}

} // namespace oc2::world
