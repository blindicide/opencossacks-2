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
    lastFired_.assign(data_->quests.quests.size(), -100);
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
    recordHistory();
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
        if (sectors_[std::size_t(s.id)].owner != nation || sectors_[std::size_t(s.id)].blocked > 0) continue;
        r[0] += k.integer("SECT_INCOME_WOOD");
        r[1] += k.integer("SECT_INCOME_GOLD");
        r[2] += k.integer("SECT_INCOME_STONE");
        r[3] += k.integer("SECT_INCOME_FOOD");
    }
    return r;
}

int BigGame::recruitIncome(int nation) const {
    int n = 0;
    for (const SectorState& s : sectors_) n += s.owner == nation && s.dry == 0;
    return n * data_->k.integer("SECT_REG_RECRTS", 30);
}

int BigGame::diversionCost(int type, int sector) const {
    const int level = std::clamp(sectors_[std::size_t(sector)].level, 0, 3);
    return data_->k.integer("SECT_SABOTAGE" + std::to_string(type), 1000, std::size_t(level));
}

int BigGame::diversionPercent(int type, int sector) const {
    const int level = std::clamp(sectors_[std::size_t(sector)].level, 0, 3);
    return data_->k.integer("SECT_SAB_PERCENT" + std::to_string(type), 50, std::size_t(level));
}

bool BigGame::canDiversion(int type, int sector) const {
    if (type < 0 || type >= kDiversions || sector < 0 || sector >= sectorCount()) return false;
    if (sectors_[std::size_t(sector)].owner == player_) return false;
    return nations_[std::size_t(player_)].res[1] >= diversionCost(type, sector);
}

bool BigGame::diversion(int type, int sector) {
    if (!canDiversion(type, sector)) return false;
    SectorState& s = sectors_[std::size_t(sector)];
    nations_[std::size_t(player_)].res[1] -= diversionCost(type, sector);
    const std::string n = std::to_string(type);
    if (int(rnd() % 100) >= diversionPercent(type, sector)) {
        // обычно провал по типу диверсии, иногда — гибель командира группы
        messages.push_back({"CWM_DiversionFail" + (rnd() % 5 == 0 ? std::string("4") : n), {}});
        return true;
    }
    switch (type) {
    case 0: s.dry = std::max(s.dry, 1); break; // начало хода начисляет доход и затем уменьшает счётчик
    case 1: s.blocked = std::max(s.blocked, 1); break;
    case 2:
        if (s.level > 0) --s.level;
        s.garrison = std::min(s.garrison, data_->k.integer("SectGarrisonSquads" + std::to_string(s.level), 4));
        s.garrison = std::max(0, s.garrison - 1);
        break;
    default: loseSector(sector); break;
    }
    messages.push_back({"CWM_DiversionDone" + n, {}});
    return true;
}

int BigGame::marketValue(int res) const {
    static const int v[kRes] = {200, 500, 300, 100, 400, 400}; // сотые доли условной цены: дерево, золото, камень, еда, железо, уголь
    const int r = std::clamp(res, 0, kRes - 1);
    return v[r] * priceMod_[std::size_t(r)] / 100;
}

int BigGame::exchangeGet(int from, int to, int give) const {
    return int(std::int64_t(give) * marketValue(from) * 4 / (std::int64_t(std::max(1, marketValue(to))) * 5)); // комиссия рынка 20%
}

bool BigGame::exchange(int from, int to, int give) {
    if (from == to || give <= 0 || from < 0 || to < 0 || from >= kRes || to >= kRes) return false;
    NationState& n = nations_[std::size_t(player_)];
    if (n.res[std::size_t(from)] < give) return false;
    n.res[std::size_t(from)] -= give;
    n.res[std::size_t(to)] += exchangeGet(from, to, give);
    return true;
}

std::vector<int> BigGame::shortages(int nation) const {
    std::vector<int> out;
    const NationState& n = nations_[std::size_t(nation)];
    static const char* const key[kRes] = {"SECT_INCOME_WOOD", "SECT_INCOME_GOLD", "SECT_INCOME_STONE",
                                          "SECT_INCOME_FOOD", "SECT_INCOME_IRON", "SECT_INCOME_COAL"};
    for (int i = 0; i < kRes; ++i) {
        const int base = data_->k.integer(key[i], 0);
        if (base > 0 && n.res[std::size_t(i)] < base) out.push_back(i);
    }
    return out;
}

int BigGame::treatyPrice(Treaty t, int nation) const {
    static const int base[4] = {3, 2, 2, 5};
    return base[int(t)] + ownedBy(nation) / 2;
}

bool BigGame::canPropose(Treaty t, int nation) const {
    if (nation == player_ || !nations_[std::size_t(nation)].alive) return false;
    const Relation& r = relation(player_, nation);
    switch (t) {
    case Treaty::Peace: return r.war;
    case Treaty::NonAggression: return !r.war && r.nonAggressionUntil < turn_;
    case Treaty::Passage: return !r.war && r.passageUntil < turn_;
    case Treaty::Alliance: return !r.war && (r.nonAggressionUntil < turn_ || r.passageUntil < turn_);
    }
    return false;
}

void BigGame::declareWar(int nation) {
    if (!canDeclareWar(nation)) return;
    setRelation(player_, nation, Relation{});
}

bool BigGame::propose(Treaty t, int nation) {
    if (!canPropose(t, nation)) return false;
    NationState& me = nations_[std::size_t(player_)];
    const int price = treatyPrice(t, nation);
    if (me.res[1] < price * 1000) return false;
    // ИИ соглашается чаще, когда игрок слабее его по числу секторов; мир труднее, чем пакты.
    int chance = 55 + (ownedBy(nation) - ownedBy(player_)) * 3;
    if (t == Treaty::Peace) chance -= 15;
    chance = std::clamp(chance, 15, 85);
    if (int(rnd() % 100) >= chance) {
        messages.push_back({"CDM_DontWantContract", {assets::bigNationName(nation)}});
        return true;
    }
    me.res[1] -= price * 1000;
    nations_[std::size_t(nation)].res[1] += price * 1000;
    Relation r = relation(player_, nation);
    r.war = false;
    if (t == Treaty::NonAggression || t == Treaty::Alliance) r.nonAggressionUntil = turn_ + 10;
    if (t == Treaty::Passage || t == Treaty::Alliance) r.passageUntil = turn_ + 10;
    setRelation(player_, nation, r);
    messages.push_back({"CDT_Accepted", {}});
    return true;
}

// Конец сроков договоров и послания ИИ: предложение мира тем, кто воюет с игроком, и война от соседей.
void BigGame::diplomacyTurn() {
    for (int n = 0; n < kBigNations; ++n) {
        if (n == player_) continue;
        Relation r = relation(player_, n);
        if (r.war) continue;
        const bool nonaggEnded = r.nonAggressionUntil != 0 && r.nonAggressionUntil < turn_;
        const bool passEnded = r.passageUntil != 0 && r.passageUntil < turn_;
        if (nonaggEnded) r.nonAggressionUntil = 0, messages.push_back({"CDM_EndOfContr2", {assets::bigNationName(n)}});
        if (passEnded) r.passageUntil = 0, messages.push_back({"CDM_EndOfContr3", {assets::bigNationName(n)}});
        setRelation(player_, n, r);
    }
    for (int n = 0; n < kBigNations; ++n) {
        if (n == player_ || !nations_[std::size_t(n)].alive) continue;
        const Relation r = relation(player_, n);
        bool neighbour = false;
        for (const assets::BigSector& s : data_->map.sectors)
            if (sectors_[std::size_t(s.id)].owner == n && attackBase(player_, s.id)) neighbour = true;
        bool queued = false;
        for (const Offer& o : offers) queued = queued || (o.kind == Offer::Kind::Treaty && o.giver == n);
        if (queued) continue;
        if (r.war && rnd() % 100 < 6) { // соседи устают воевать и просят мира за золото
            Offer o;
            o.kind = Offer::Kind::Treaty;
            o.giver = n;
            o.gold = (1 + int(rnd() % 3)) * 1000;
            o.quest = "Peace";
            o.text = {"CDM_ProposeContr1", {assets::bigNationName(n), std::to_string(o.gold / 1000)}};
            offers.push_back(o);
        } else if (!r.war && r.nonAggressionUntil < turn_ && neighbour && rnd() % 100 < 3) {
            setRelation(player_, n, Relation{});
            messages.push_back({"CDM_ProposeContr0", {assets::bigNationName(n)}});
        }
    }
}

bool BigGame::adjacent(int a, int b) const {
    const auto& nb = data_->map.sectors[std::size_t(a)].neighbors;
    return std::find(nb.begin(), nb.end(), b) != nb.end();
}

std::optional<int> BigGame::attackBase(int nation, int target) const {
    for (int n : data_->map.sectors[std::size_t(target)].neighbors)
        if (sectors_[std::size_t(n)].owner == nation) return n;
    // Право прохода: армия идёт через сектор союзника.
    for (int n : data_->map.sectors[std::size_t(target)].neighbors)
        if (hasPassage(nation, sectors_[std::size_t(n)].owner)) return n;
    return std::nullopt;
}

bool BigGame::canAttack(int target) const {
    if (target < 0 || target >= sectorCount()) return false;
    if (sectors_[std::size_t(target)].owner == player_ || !atWar(player_, sectors_[std::size_t(target)].owner)) return false;
    return attackBase(player_, target) && !nations_[std::size_t(player_)].army.empty() && nations_[std::size_t(player_)].weatherSkip == 0;
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
    a.from = attackBase(player_, target).value_or(-1);
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
    if ((o.attackerWon ? a.nation : defender) == player_ && rnd() % 100 < 40) heroAward(player_);
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
    if (a.nation == player_) completeQuests(a, o, defender);
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
    for (SectorState& s : sectors_) { // гарнизон восстанавливается по отряду в ход до нормы уровня
        s.garrison = std::min(s.garrison + 1, data_->k.integer("SectGarrisonSquads" + std::to_string(s.level), 4));
        if (s.blocked > 0) --s.blocked;
        if (s.dry > 0) --s.dry;
    }
    // Принятые задания: защита засчитывается по сроку, остальные просто истекают.
    for (std::size_t i = 0; i < quests_.size();) {
        const ActiveQuest& q = quests_[i];
        bool done = false;
        if (q.name == "DefendSector") {
            if (sectors_[std::size_t(q.sector)].owner != player_) {
                messages.push_back({"CQM_DefendSector2", {assets::bigNationName(q.giver), sectorKey(q.sector)}});
                done = true;
            } else if (turn_ >= q.deadline) {
                nations_[std::size_t(player_)].res[1] += q.gold;
                messages.push_back({"CQM_DefendSector1", {assets::bigNationName(q.giver), sectorKey(q.sector), std::to_string(q.gold)}});
                done = true;
            }
        } else if (turn_ > q.deadline) {
            done = true;
        }
        if (done) quests_.erase(quests_.begin() + std::ptrdiff_t(i));
        else ++i;
    }
    for (NationState& n : nations_)
        if (n.weatherSkip > 0) --n.weatherSkip;
    recordHistory();
    if (nations_[std::size_t(player_)].alive) {
        worldEvents();
        diplomacyTurn();
        rollEvents();
    }
}

// Рынок и погода: цены сами уходят к 100%, раз в несколько ходов случается скачок цены на один ресурс,
// а в дождь нация не нападает ход (шансы — догадка).
void BigGame::recordHistory() {
    std::array<int, kBigNations> row{};
    for (int n = 0; n < kBigNations; ++n) row[std::size_t(n)] = ownedBy(n);
    history_.push_back(row);
}

void BigGame::worldEvents() {
    for (int& p : priceMod_) p += p < 100 ? std::min(5, 100 - p) : -std::min(5, p - 100);
    if (rnd() % 100 < 12) {
        static const int res[5] = {0, 2, 3, 4, 5};
        const int r = res[rnd() % 5];
        const bool rise = rnd() % 2 == 0;
        const int pct = 10 + int(rnd() % 4) * 5;
        priceMod_[std::size_t(r)] = std::clamp(priceMod_[std::size_t(r)] + (rise ? pct : -pct), 50, 200);
        messages.push_back({std::string(rise ? "CQM_RisePrices" : "CQM_FallPrices") + std::to_string(r) + std::to_string(rnd() % 2), {std::to_string(pct)}});
    }
    if (rnd() % 100 < 5) {
        const int n = int(rnd() % kBigNations);
        NationState& ns = nations_[std::size_t(n)];
        if (ns.alive && ns.weatherSkip == 0) {
            ns.weatherSkip = 1;
            messages.push_back({rnd() % 2 ? "CWM_Bad_Weather" : "CWM_Bad_Weather2", {assets::bigNationName(n)}});
        }
    }
}

// Награда герою за победу: прибавка опыта и счётчик наград; героев по именам только у наций с HERO_xx.
void BigGame::heroAward(int nation) {
    static const char* const code[kBigNations] = {"EG", "FR", "AS", "PR", "RS", "EP", nullptr, nullptr, nullptr};
    if (!code[nation]) return;
    NationState& n = nations_[std::size_t(nation)];
    ++n.awards;
    const int exp = data_->k.integer("QuestCompliteExp", 100);
    messages.push_back({"CWM_HeroGetAward" + std::to_string(rnd() % 5),
                        {std::string("$HERO_") + code[nation] + "_" + std::to_string(std::min(n.rank, 7)), std::to_string(n.awards), std::to_string(exp)}});
    giveExp(nation, exp);
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
    if (n.weatherSkip > 0) return std::nullopt;
    if (n.army.empty() || rnd() % 100 >= 65) return std::nullopt;
    // Цель: самый слабый чужой сектор рядом со своим, который армия берёт с запасом.
    int best = -1;
    double bestRatio = 1.15;
    const double power = attackPower(n.army);
    for (const assets::BigSector& s : data_->map.sectors) {
        if (sectors_[std::size_t(s.id)].owner == nation || !atWar(nation, sectors_[std::size_t(s.id)].owner) ||
            !attackBase(nation, s.id))
            continue;
        const double ratio = power / std::max(1, defencePower(s.id));
        if (ratio > bestRatio) bestRatio = ratio, best = s.id;
    }
    if (best < 0) return std::nullopt;
    Attack a{nation, best, n.army, attackBase(nation, best).value_or(-1)};
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

// ---------------------------------------------------------------- события и задания

bool BigGame::isPort(int sector) const {
    const auto& p = data_->quests.ports;
    return std::find(p.begin(), p.end(), sector) != p.end();
}

int BigGame::randomOwned(int nation) {
    std::vector<int> v;
    for (int i = 0; i < sectorCount(); ++i)
        if (sectors_[std::size_t(i)].owner == nation) v.push_back(i);
    return v.empty() ? -1 : v[rnd() % v.size()];
}

// Мятеж: сектор уходит соседней нации (или прежней «защитной»).
void BigGame::loseSector(int id) {
    SectorState& s = sectors_[std::size_t(id)];
    const int me = s.owner;
    int to = data_->map.sectors[std::size_t(id)].defOwner;
    if (to == me || to < 0 || to >= kBigNations || !nations_[std::size_t(to)].alive) {
        to = -1;
        for (int n : data_->map.sectors[std::size_t(id)].neighbors)
            if (sectors_[std::size_t(n)].owner != me) {
                to = sectors_[std::size_t(n)].owner;
                break;
            }
    }
    if (to < 0) return;
    s.owner = to;
    s.garrison = std::max(1, s.garrison / 2);
    if (ownedBy(me) == 0) {
        nations_[std::size_t(me)].alive = false;
        nations_[std::size_t(me)].army.clear();
    }
}

// Случайные события и задания для игрока (Quests.dat). Шанс за ход — probability/2 %, не чаще
// turn_limit ходов на одно определение и не больше двух за ход: частота подобрана на глаз.
void BigGame::rollEvents() {
    const auto& defs = data_->quests.quests;
    if (defs.empty()) return;
    if (lastFired_.size() != defs.size()) lastFired_.assign(defs.size(), -100);
    NationState& me = nations_[std::size_t(player_)];
    auto has = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    auto busy = [&](const std::string& name) {
        for (const ActiveQuest& q : quests_)
            if (q.name == name) return true;
        for (const Offer& o : offers)
            if (o.quest == name) return true;
        return false;
    };
    int fired = 0;
    const std::size_t off = rnd() % defs.size();
    for (std::size_t j = 0; j < defs.size() && fired < 2; ++j) {
        const std::size_t i = (off + j) % defs.size();
        const assets::BigQuest& d = defs[i];
        if (!has(d.recipients, player_) || turn_ - lastFired_[i] < std::max(3, d.turnLimit)) continue;
        if (int(rnd() % 100) >= d.probability / 2) continue;
        std::vector<int> givers;
        for (int n : d.owners)
            if (n != player_ && nations_[std::size_t(n)].alive) givers.push_back(n);
        const int giver = givers.empty() ? -1 : givers[rnd() % givers.size()];
        const std::string& nm = d.name;
        const int sector = randomOwned(player_);
        if (sector < 0) return;
        auto amount = [&](int base) { return (base * int(60 + rnd() % 41) / 100) / 100 * 100; };
        bool ok = false;
        if (nm == "AddGold" || nm == "DeleteGold") {
            int g = amount(d.gold);
            if (nm == "DeleteGold") g = std::min(g, me.res[1]);
            if (g > 0) {
                me.res[1] += nm == "AddGold" ? g : -g;
                const std::string kind = nm == "AddGold" ? (isPort(sector) ? "CQM_SeaAddGold" : "CQM_LandAddGold")
                                                         : (isPort(sector) ? "CQM_SeaDelGold" : "CQM_LandDelGold");
                messages.push_back({kind + std::to_string(rnd() % 5), {std::to_string(g)}});
                ok = true;
            }
        } else if (nm == "AddRecruits" || nm == "DeleteRecruits") {
            int r = 20 + int(rnd() % 41);
            if (nm == "DeleteRecruits") r = std::min(r, me.recruits);
            if (r > 0) {
                me.recruits += nm == "AddRecruits" ? r : -r;
                messages.push_back({"CQM_" + nm, {sectorKey(sector), std::to_string(r)}});
                ok = true;
            }
        } else if (nm == "DeleteResources") {
            if (sectors_[std::size_t(sector)].blocked == 0) {
                sectors_[std::size_t(sector)].blocked = std::max(2, d.turnLimit / 2);
                messages.push_back({"CQM_DeleteResources", {sectorKey(sector)}});
                ok = true;
            }
        } else if (nm == "DeleteDefence") {
            SectorState& s = sectors_[std::size_t(sector)];
            if (s.level > 0) {
                --s.level;
                s.garrison = std::min(s.garrison, data_->k.integer("SectGarrisonSquads" + std::to_string(s.level), 4));
                messages.push_back({"CQM_DeleteDefence", {sectorKey(sector)}});
                ok = true;
            }
        } else if (nm == "Rebellion") {
            if (ownedBy(player_) >= 2 && !busy(nm)) {
                Offer o;
                o.kind = Offer::Kind::Rebellion;
                o.sector = sector;
                o.gold = d.gold;
                o.quest = nm;
                o.text = {"CQM_SectorRebellion0", {sectorKey(sector), std::to_string(d.gold)}};
                offers.push_back(o);
                ok = true;
            }
        } else if (nm == "AddSquad") {
            const std::vector<std::string> pool = recruitable(player_);
            if (!pool.empty() && int(me.army.size()) < armyCapacity(player_) && !busy(nm)) {
                Offer o;
                o.kind = Offer::Kind::AddSquad;
                o.gold = d.gold;
                o.squad = pool[rnd() % pool.size()];
                o.quest = nm;
                o.text = {"CQM_AddSquad" + std::to_string(rnd() % 3), {std::to_string(d.gold), "@" + o.squad}};
                offers.push_back(o);
                ok = true;
            }
        } else if (nm == "SellSector" && giver >= 0 && ownedBy(player_) >= 2 && !busy(nm)) {
            int pick = -1;
            for (int s = 0; s < sectorCount() && pick < 0; ++s)
                if (sectors_[std::size_t(s)].owner == player_ && attackBase(giver, s)) pick = s;
            if (pick >= 0) {
                Offer o;
                o.kind = Offer::Kind::SellSector;
                o.sector = pick, o.gold = d.gold, o.giver = giver, o.quest = nm;
                o.text = {"CQM_SellSector0", {assets::bigNationName(giver), sectorKey(pick), std::to_string(d.gold)}};
                offers.push_back(o);
                ok = true;
            }
        } else if (nm == "AttackNation" && giver >= 0 && !busy(nm)) {
            std::vector<int> targets;
            for (int s = 0; s < sectorCount(); ++s) {
                const int ow = sectors_[std::size_t(s)].owner;
                if (ow != player_ && ow != giver && attackBase(player_, s) && !has(targets, ow)) targets.push_back(ow);
            }
            if (!targets.empty()) {
                Offer o;
                o.kind = Offer::Kind::Quest;
                o.quest = nm, o.giver = giver, o.gold = d.gold, o.deadline = turn_ + std::max(3, d.turnLimit);
                o.target = targets[rnd() % targets.size()];
                o.text = {"CQM_AttackNation0", {assets::bigNationName(giver), assets::bigNationName(o.target), std::to_string(d.gold)}};
                offers.push_back(o);
                ok = true;
            }
        } else if (nm == "CaptureSector" && giver >= 0 && !busy(nm)) {
            std::vector<int> targets;
            for (int s = 0; s < sectorCount(); ++s) {
                const int ow = sectors_[std::size_t(s)].owner;
                if (ow != player_ && ow != giver && attackBase(player_, s)) targets.push_back(s);
            }
            if (!targets.empty()) {
                Offer o;
                o.kind = Offer::Kind::Quest;
                o.quest = nm, o.giver = giver, o.gold = d.gold, o.deadline = turn_ + std::max(3, d.turnLimit);
                o.sector = targets[rnd() % targets.size()];
                o.text = {"CQM_CaptureSector0", {assets::bigNationName(giver), sectorKey(o.sector), std::to_string(d.gold)}};
                offers.push_back(o);
                ok = true;
            }
        } else if (nm == "DefendSector" && giver >= 0 && !busy(nm)) {
            std::vector<int> mine;
            for (int s = 0; s < sectorCount(); ++s)
                if (sectors_[std::size_t(s)].owner == player_)
                    for (int nb : data_->map.sectors[std::size_t(s)].neighbors)
                        if (sectors_[std::size_t(nb)].owner != player_) {
                            mine.push_back(s);
                            break;
                        }
            if (!mine.empty()) {
                Offer o;
                o.kind = Offer::Kind::Quest;
                o.quest = nm, o.giver = giver, o.gold = d.gold, o.deadline = turn_ + 5;
                o.sector = mine[rnd() % mine.size()];
                o.text = {"CQM_DefendSector0", {assets::bigNationName(giver), sectorKey(o.sector), assets::bigNationName(giver), std::to_string(d.gold)}};
                offers.push_back(o);
                ok = true;
            }
        }
        if (ok) lastFired_[i] = turn_, ++fired;
    }
}

bool BigGame::canAccept(const Offer& o) const {
    const NationState& me = nations_[std::size_t(player_)];
    switch (o.kind) {
    case Offer::Kind::Rebellion: return me.res[1] >= o.gold;
    case Offer::Kind::AddSquad: return me.res[1] >= o.gold && int(me.army.size()) < armyCapacity(player_);
    default: return true;
    }
}

bool BigGame::answer(bool accept) {
    if (offers.empty()) return true;
    const Offer o = offers.front();
    if (accept && !canAccept(o)) return false;
    offers.erase(offers.begin());
    NationState& me = nations_[std::size_t(player_)];
    switch (o.kind) {
    case Offer::Kind::Rebellion:
        if (sectors_[std::size_t(o.sector)].owner != player_) break;
        if (accept) {
            me.res[1] -= o.gold;
            if (rnd() % 100 < 80) {
                messages.push_back({"CQM_SectorRebellion1", {sectorKey(o.sector)}});
            } else {
                messages.push_back({"CQM_SectorRebellion2", {}});
                loseSector(o.sector);
            }
        } else {
            messages.push_back({"CQM_SectorRebellion4", {sectorKey(o.sector)}});
            loseSector(o.sector);
        }
        break;
    case Offer::Kind::AddSquad:
        if (!accept) break;
        me.res[1] -= o.gold;
        me.army.push_back(o.squad);
        messages.push_back({"CQM_AddSquadD", {"@" + o.squad}});
        break;
    case Offer::Kind::SellSector:
        if (!accept) {
            messages.push_back({"CQM_SellSector2", {}});
        } else if (sectors_[std::size_t(o.sector)].owner == player_ && ownedBy(player_) >= 2) {
            sectors_[std::size_t(o.sector)].owner = o.giver;
            me.res[1] += o.gold;
            messages.push_back({"CQM_SellSector1", {assets::bigNationName(o.giver), std::to_string(o.gold)}});
        }
        break;
    case Offer::Kind::Treaty:
        if (accept && atWar(player_, o.giver)) {
            Relation r;
            r.war = false;
            setRelation(player_, o.giver, r);
            me.res[1] += o.gold;
            messages.push_back({"CDT_Accepted", {}});
        }
        break;
    case Offer::Kind::Quest:
        if (accept) quests_.push_back({o.quest, o.giver, o.target, o.sector, o.gold, o.deadline});
        break;
    }
    return true;
}

void BigGame::completeQuests(const Attack&, const Outcome& o, int defender) {
    NationState& me = nations_[std::size_t(player_)];
    for (std::size_t i = 0; i < quests_.size();) {
        const ActiveQuest& q = quests_[i];
        bool done = false;
        if (q.name == "AttackNation" && q.target == defender) {
            messages.push_back({"CQM_AttackNation1", {assets::bigNationName(q.giver), assets::bigNationName(q.target), std::to_string(q.gold)}});
            done = true;
        } else if (q.name == "CaptureSector" && o.attackerWon && sectors_[std::size_t(q.sector)].owner == player_) {
            messages.push_back({"CQM_CaptureSector1", {assets::bigNationName(q.giver), sectorKey(q.sector), std::to_string(q.gold)}});
            done = true;
        }
        if (done) {
            me.res[1] += q.gold;
            quests_.erase(quests_.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
}

int BigGame::result() const {
    if (ownedBy(player_) == 0) return -1;
    if (ownedBy(player_) == sectorCount()) return 1;
    return 0;
}

std::string BigGame::serialize() const {
    std::ostringstream o;
    o << "BIG4 " << player_ << ' ' << turn_ << ' ' << seed_ << '\n';
    for (std::size_t i = 0; i < sectors_.size(); ++i)
        o << "S " << i << ' ' << sectors_[i].owner << ' ' << sectors_[i].garrison << ' ' << sectors_[i].level << ' ' << sectors_[i].blocked << ' ' << sectors_[i].dry << '\n';
    for (int i = 0; i < kBigNations; ++i) {
        const NationState& n = nations_[std::size_t(i)];
        o << "N " << i << ' ' << n.alive << ' ' << n.rank << ' ' << n.exp << ' ' << n.recruits;
        for (int r : n.res) o << ' ' << r;
        o << ' ' << n.army.size();
        for (const std::string& t : n.army) o << ' ' << t;
        o << '\n';
    }
    for (const auto& row : history_) {
        o << "H";
        for (int v : row) o << ' ' << v;
        o << '\n';
    }
    o << "P";
    for (int p : priceMod_) o << ' ' << p;
    o << '\n';
    for (int i = 0; i < kBigNations; ++i)
        if (nations_[std::size_t(i)].awards || nations_[std::size_t(i)].weatherSkip)
            o << "A " << i << ' ' << nations_[std::size_t(i)].awards << ' ' << nations_[std::size_t(i)].weatherSkip << '\n';
    for (const ActiveQuest& q : quests_)
        o << "Q " << q.name << ' ' << q.giver << ' ' << q.target << ' ' << q.sector << ' ' << q.gold << ' ' << q.deadline << '\n';
    for (int a = 0; a < kBigNations; ++a)
        for (int b = a + 1; b < kBigNations; ++b) {
            const Relation& r = relation(a, b);
            o << "D " << a << ' ' << b << ' ' << r.war << ' ' << r.nonAggressionUntil << ' ' << r.passageUntil << '\n';
        }
    o << "L " << lastFired_.size();
    for (int v : lastFired_) o << ' ' << v;
    o << '\n';
    return o.str();
}

std::optional<BigGame> BigGame::deserialize(std::shared_ptr<const Data> data, const std::string& text) {
    std::istringstream in(text);
    std::string tag;
    int player = 0, turn = 1;
    std::uint32_t seed = 1;
    if (!(in >> tag >> player >> turn >> seed) || (tag != "BIG1" && tag != "BIG2" && tag != "BIG3" && tag != "BIG4") || player < 0 || player >= kBigNations) return std::nullopt;
    const bool v2 = tag != "BIG1", v3 = tag == "BIG3" || tag == "BIG4";
    BigGame g(std::move(data), player, seed);
    g.turn_ = turn;
    g.history_.clear();
    std::size_t sectorsSeen = 0;
    while (in >> tag) {
        if (tag == "S") {
            std::size_t i;
            SectorState s;
            if (!(in >> i >> s.owner >> s.garrison >> s.level) || i >= g.sectors_.size()) return std::nullopt;
            if (v2 && !(in >> s.blocked)) return std::nullopt;
            if (v3 && !(in >> s.dry)) return std::nullopt;
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
        } else if (tag == "Q") {
            ActiveQuest q;
            if (!(in >> q.name >> q.giver >> q.target >> q.sector >> q.gold >> q.deadline) || q.sector >= g.sectorCount() ||
                q.giver < 0 || q.giver >= kBigNations)
                return std::nullopt;
            if (q.sector < 0 && q.name == "DefendSector") return std::nullopt;
            g.quests_.push_back(q);
        } else if (tag == "H") {
            std::array<int, kBigNations> row{};
            for (int& v : row) in >> v;
            if (g.history_.size() < 2000) g.history_.push_back(row);
        } else if (tag == "P") {
            for (int& p : g.priceMod_) in >> p;
        } else if (tag == "A") {
            int i, aw, ws;
            if (!(in >> i >> aw >> ws) || i < 0 || i >= kBigNations) return std::nullopt;
            g.nations_[std::size_t(i)].awards = aw;
            g.nations_[std::size_t(i)].weatherSkip = ws;
        } else if (tag == "D") {
            int a, b, war;
            Relation r;
            if (!(in >> a >> b >> war >> r.nonAggressionUntil >> r.passageUntil) || a < 0 || b < 0 || a >= kBigNations ||
                b >= kBigNations || a == b)
                return std::nullopt;
            r.war = war != 0;
            g.setRelation(a, b, r);
        } else if (tag == "L") {
            std::size_t count;
            if (!(in >> count) || count > 1000) return std::nullopt;
            std::vector<int> v(count);
            for (int& x : v) in >> x;
            if (count == g.lastFired_.size()) g.lastFired_ = std::move(v);
        } else {
            return std::nullopt;
        }
    }
    if (sectorsSeen != g.sectors_.size()) return std::nullopt;
    return g;
}

} // namespace oc2::world
