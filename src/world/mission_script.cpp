#include "world/mission_script.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace oc2::world {

using assets::XmlNode;

namespace {

constexpr int kDefaultVoiceSteps = 125; // нет файла озвучки: 5 секунд на реплику

int resourceIndex(const std::string& name) {
    static const char* names[] = {"Wood", "Gold", "Stone", "Food", "Iron", "Coal"};
    for (int i = 0; i < 6; ++i)
        if (name == names[i]) return i;
    return -1;
}

const XmlNode* firstChild(const XmlNode* n) { return n && !n->children.empty() ? &n->children.front() : nullptr; }

void collect(const XmlNode& n, std::string_view name, std::vector<const XmlNode*>& out, int depth = 0) {
    if (n.name == name) {
        out.push_back(&n);
        return;
    }
    if (depth > 6) return;
    for (const XmlNode& c : n.children) collect(c, name, out, depth + 1);
}

bool traceEnabled() {
    static const bool on = std::getenv("OC2_SCRIPT_TRACE") != nullptr;
    return on;
}

bool boolText(const std::string& s) { return s == "true" || s == "1"; }

} // namespace

std::unique_ptr<MissionScript> MissionScript::create(const assets::Map3d& map, Env env) {
    return fromXml(map.scriptXml(), map.zonesXml(), std::move(env));
}

std::unique_ptr<MissionScript> MissionScript::fromXml(const std::string& xml, const std::string& zonesXml, Env env) {
    if (xml.empty()) return nullptr;
    std::unique_ptr<MissionScript> s(new MissionScript);
    s->env_ = std::move(env);
    try {
        s->root_ = assets::parseXml(xml);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "скрипт миссии: %s\n", e.what());
        return nullptr;
    }
    const XmlNode& root = s->root_;

    // Переменные.
    std::vector<const XmlNode*> found;
    collect(root, "V_VALUES", found);
    if (!found.empty())
        for (const XmlNode& v : found.front()->children) {
            const std::string name = v.value("Name").value_or("");
            Value val;
            if (v.name == "vvTRIGER") {
                val.num = boolText(v.value("Value").value_or("false")) ? 1 : 0;
            } else if (v.name == "vvINTEGER") {
                val.num = double(v.intValue("Value").value_or(0));
            } else if (v.name == "vvTEXT") {
                val.text = v.value("TextID").value_or("");
                val.ogg = v.value("oggFile").value_or("");
                val.speaker = v.value("SpeakerID").value_or("");
            } else if (v.name == "vvTASKS_CII") {
                std::vector<const XmlNode*> ex;
                collect(v, "vvTEXT_EX", ex);
                for (const XmlNode* e : ex)
                    s->taskText_[e->value("Name").value_or("")] = e->value("TextID").value_or("");
                continue;
            }
            if (!name.empty()) s->vars_[name] = std::move(val);
        }

    // Узлы, группы, зоны.
    found.clear();
    collect(root, "lvCNode", found);
    for (const XmlNode* n : found)
        s->nodes_[n->value("NAME").value_or("")] = {int(n->intValue("x").value_or(0)), int(n->intValue("y").value_or(0)),
                                                    int(n->intValue("R").value_or(0))};
    found.clear();
    collect(root, "lvCGroup", found);
    for (const XmlNode* g : found) {
        Group grp;
        if (const XmlNode* units = g->child("Units"))
            for (const XmlNode& e : units->children) {
                int x = 0, y = 0, idx = -1;
                if (std::sscanf(e.text.c_str(), "%d %d %d", &x, &y, &idx) == 3 && idx >= 0 &&
                    std::size_t(idx) < s->env_.mapUnits.size() && s->env_.mapUnits[std::size_t(idx)])
                    grp.units.push_back(*s->env_.mapUnits[std::size_t(idx)]);
            }
        s->groups_[g->value("NAME").value_or("")] = std::move(grp);
    }
    try {
        const std::string& zones = zonesXml;
        if (!zones.empty()) {
            const XmlNode z = assets::parseXml("<>" + zones + "</>");
            std::vector<const XmlNode*> list;
            collect(z, "ActiveZone", list);
            for (const XmlNode* a : list)
                s->zones_[a->value("Name").value_or("")] = {int(a->intValue("x").value_or(0)),
                                                            int(a->intValue("y").value_or(0)),
                                                            int(a->intValue("R").value_or(0))};
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "зоны миссии: %s\n", e.what());
    }

    // Меню и чёрный экран: начальная видимость.
    for (const char* cls : {"lvCDeffFilmMenu", "lvCBlackScreen"}) {
        found.clear();
        collect(root, cls, found);
        for (const XmlNode* n : found) s->fg_[n->value("Name").value_or("")] = boolText(n->value("Visible").value_or(""));
    }
    s->black_ = s->fg_.count("BS") && s->fg_["BS"];

    // Правила и фильмы.
    found.clear();
    collect(root, "lvCMainScript", found);
    if (!found.empty()) {
        s->init_ = found.front()->child("MAIN_INIT");
        s->scripts_ = found.front()->child("MAIN_SCRIPTS");
        if (s->scripts_)
            for (const XmlNode& r : s->scripts_->children)
                if (r.name == "lvCCondForOper") s->rules_.push_back({&r});
    }
    found.clear();
    collect(root, "lvCFilm", found);
    for (const XmlNode* f : found) s->films_.push_back({f});
    if (s->rules_.empty() && s->films_.empty() && !s->init_) return nullptr;
    return s;
}

std::vector<std::string> MissionScript::tasks() const {
    std::vector<std::string> out;
    for (const std::string& n : taskNames_) {
        const auto it = taskText_.find(n);
        out.push_back(it != taskText_.end() && !it->second.empty() ? it->second : "#" + n);
    }
    return out;
}

double MissionScript::var(const std::string& name) const {
    const auto it = vars_.find(name);
    return it == vars_.end() ? 0 : it->second.num;
}

bool MissionScript::filmRunning() const {
    return std::any_of(films_.begin(), films_.end(), [](const Film& f) { return f.state == Film::State::Running; });
}

std::string MissionScript::refName(const XmlNode& n, std::string_view field) {
    const XmlNode* c = n.child(field);
    return c ? c->value("RefName").value_or("") : "";
}

int MissionScript::intField(const XmlNode& n, std::string_view field, int fallback) const {
    return int(n.intValue(field).value_or(fallback));
}

const MissionScript::Group* MissionScript::group(const std::string& name) const {
    const auto it = groups_.find(name);
    return it == groups_.end() ? nullptr : &it->second;
}

const MissionScript::Zone* MissionScript::zone(const std::string& name) const {
    if (auto it = zones_.find(name); it != zones_.end()) return &it->second;
    if (auto it = nodes_.find(name); it != nodes_.end()) return &it->second;
    return nullptr;
}

void MissionScript::note(const std::string& op) {
    if (skippedOps_[op]++ == 0) std::fprintf(stderr, "скрипт миссии: не поддержано «%s»\n", op.c_str());
}

int MissionScript::countUnits(int player, const std::string* type, const Group* grp, const Zone* zn) const {
    const World& w = *env_.world;
    int n = 0;
    const auto matches = [&](const Unit& u) {
        if (!u.alive() || !u.desc) return false;
        if (player >= 0 && player < 8 && u.player != player) return false;
        if (type && !type->empty() && u.desc->typeName != *type) return false;
        if (zn) {
            const long dx = u.x() - zn->x, dy = u.y() - zn->y;
            if (dx * dx + dy * dy > long(zn->r) * zn->r) return false;
        }
        return true;
    };
    if (grp) {
        for (std::size_t i : grp->units)
            if (i < w.units().size() && matches(w.units()[i])) ++n;
    } else {
        for (const Unit& u : w.units())
            if (matches(u)) ++n;
    }
    return n;
}

double MissionScript::eval(const XmlNode& n) {
    const std::string name = n.name.rfind("lvC", 0) == 0 ? n.name.substr(3) : n.name;
    const auto sub = [&](std::string_view field) { return firstChild(n.child(field)) ? eval(*firstChild(n.child(field))) : 0.0; };
    if (name == "Compare") {
        const double a = sub("COND1"), b = sub("COND2");
        const std::string op = n.value("OPER").value_or("equal");
        if (op == "equal") return a == b;
        if (op == "not equal") return a != b;
        if (op == "less") return a < b;
        if (op == "less equal") return a <= b;
        if (op == "more") return a > b;
        if (op == "more equal") return a >= b;
        if (op == "And") return a != 0 && b != 0;
        if (op == "Or") return a != 0 || b != 0;
        note("Compare " + op);
        return 0;
    }
    if (name == "Logical") {
        const bool all = n.value("OPER").value_or("And") != "Or";
        const XmlNode* conds = n.child("CONDS");
        if (!conds || conds->children.empty()) return all;
        for (const XmlNode& c : conds->children) {
            const bool v = eval(c) != 0;
            if (all && !v) return 0;
            if (!all && v) return 1;
        }
        return all;
    }
    if (name == "Functional") return sub("Function");
    if (name == "Int") return double(intField(n, "lvI"));
    if (name == "Bool") return boolText(n.value("lvB").value_or("false"));
    if (name == "GetValue") {
        const XmlNode* v = n.child("Value");
        return v ? var(v->value("RefName").value_or("")) : 0;
    }
    if (name == "GetDiff") return env_.difficulty;
    if (name == "GetResource") {
        const int r = resourceIndex(n.value("parID").value_or(""));
        const int p = intField(n, "parNat");
        return r < 0 || p < 0 || p > 7 ? 0 : env_.world->resources[std::size_t(p)][std::size_t(r)];
    }
    if (name == "GetTotalAmount1") {
        const std::string type = n.value("UnitType").value_or("");
        return countUnits(intField(n, "parNat"), &type, nullptr, nullptr);
    }
    if (name == "GetTotalAmount0") return countUnits(-1, nullptr, group(n.value("parGrp").value_or("")), nullptr);
    if (name == "GetUnitsAmount0") {
        const Zone* z = zone(n.value("parZn").value_or(""));
        return z ? countUnits(intField(n, "parNat", -1), nullptr, nullptr, z) : 0;
    }
    if (name == "GetUnitsAmount1") {
        const Zone* z = zone(n.value("parZn").value_or(""));
        return z ? countUnits(-1, nullptr, group(n.value("parGrp").value_or("")), z) : 0;
    }
    if (name == "GetNofMyVillage") {
        const int owner = intField(n, "Owner");
        return env_.settlements && owner >= 0 && owner < 8
                   ? env_.settlements->owned(owner, resourceIndex(n.value("ResType").value_or("")))
                   : 0;
    }
    if (name == "TimerDone") {
        const auto it = timers_.find(refName(n, "TimerID"));
        return it == timers_.end() || env_.world->stepCount() >= it->second;
    }
    if (name == "CameraSTOP") return 1;
    if (name == "CheckButton") return skip_;
    if (name == "ogSTOP") return env_.world->stepCount() >= voiceEnd_;
    if (name == "FG_Visible" || name == "FG_InVisible") {
        const auto it = fg_.find(refName(n, "GraphObj"));
        const bool visible = it != fg_.end() && it->second;
        return name == "FG_InVisible" ? !visible : visible;
    }
    // Бригады ИИ-отрядов и усталость пока не ведутся: условия по ним ложны.
    if (name == "IsBrigade" || name == "BrigadesAmount" || name == "GetNofBrigInNode" || name == "IsTired" ||
        name == "GetReadyAmount")
        return 0;
    note("выражение " + name);
    return 0;
}

bool MissionScript::holds(const XmlNode* conditions) {
    if (!conditions) return true;
    for (const XmlNode& c : conditions->children)
        if (eval(c) == 0) return false;
    return true;
}

void MissionScript::runOps(const XmlNode* ops) {
    if (!ops) return;
    for (const XmlNode& op : ops->children) runOp(op);
}

void MissionScript::playVoice(const std::string& textVar) {
    const auto it = vars_.find(textVar);
    const std::string ogg = it != vars_.end() ? it->second.ogg : "";
    int steps = 0;
    if (!ogg.empty()) {
        voices.push_back(ogg);
        if (env_.voiceSteps) steps = env_.voiceSteps(ogg);
    }
    voiceEnd_ = env_.world->stepCount() + std::uint64_t(steps > 0 ? steps : kDefaultVoiceSteps);
}

void MissionScript::runOp(const XmlNode& op) {
    World& w = *env_.world;
    std::string name = op.name.rfind("lvC", 0) == 0 ? op.name.substr(3) : op.name;
    const auto str = [&](std::string_view f) { return op.value(f).value_or(""); };
    const auto num = [&](std::string_view f, int fallback = 0) { return intField(op, f, fallback); };
    const auto player = [](int p) { return p >= 0 && p < 8; };
    const auto resIndexOfSet = [](const std::string& tag) {
        // Поля SetRessource: Wood, Gold, Stown (камень), Food, Iron, Coal.
        if (tag == "Wood") return 0;
        if (tag == "Gold") return 1;
        if (tag == "Stown") return 2;
        if (tag == "Food") return 3;
        if (tag == "Iron") return 4;
        return 5;
    };

    if (traceEnabled())
        std::fprintf(stderr, "[%llu] %s\n", static_cast<unsigned long long>(w.stepCount()), name.c_str());
    if (name == "SetValue") {
        const std::string type = str("TypeID");
        if (type == "Triger") {
            vars_[refName(op, "ValueTG")].num = boolText(str("SetTG")) ? 1 : 0;
        } else if (type == "Integer") {
            vars_[refName(op, "ValueIN")].num = num("SetIN");
        } else {
            note("SetValue " + type);
        }
    } else if (name == "AddToInt") {
        vars_[refName(op, "IntValue")].num += num("IntAdd");
    } else if (name == "RunTimer") {
        timers_[refName(op, "TimerID")] = w.stepCount() + std::uint64_t(std::max(0, num("Time")));
    } else if (name == "SetMyNation") {
        myNation = num("Nat");
    } else if (name == "SetRessource") {
        const int p = num("Nation");
        if (player(p))
            for (const char* r : {"Wood", "Gold", "Stown", "Food", "Iron", "Coal"})
                w.resources[std::size_t(p)][std::size_t(resIndexOfSet(r))] = num(r);
    } else if (name == "AddRessource") {
        const int p = num("Nat"), r = resourceIndex(str("RessType"));
        if (player(p) && r >= 0) w.resources[std::size_t(p)][std::size_t(r)] += num("Value");
    } else if (name == "SetFriends") {
        const int a = num("fstNat"), b = num("secNat");
        if (player(a) && player(b)) w.team[std::size_t(b)] = w.team[std::size_t(a)];
    } else if (name == "ChangeNationG") {
        if (const Group* g = group(str("GrpID")))
            for (std::size_t i : g->units)
                if (i < w.units().size() && player(num("nwNat"))) w.units()[i].player = num("nwNat");
    } else if (name == "EraseNUnits" || name == "KillNUnits") {
        int left = num("UCount");
        if (const Group* g = group(str("GrpID")))
            for (std::size_t i : g->units) {
                if (left <= 0 || i >= w.units().size()) continue;
                Unit& u = w.units()[i];
                if (!u.alive()) continue;
                u.dead = true;
                u.inside = true;
                --left;
            }
    } else if (name == "GroupSendTo") {
        const Zone* z = zone(str("parZn"));
        if (!z) z = zone(str("parNode"));
        if (const Group* g = group(str("parGrp")); g && z)
            for (std::size_t i : g->units)
                if (i < w.units().size() && w.units()[i].alive()) w.orderMove(i, z->x, z->y);
    } else if (name == "SetUnitEnableState") {
        const int p = num("Nat");
        if (player(p)) {
            if (num("State") < 0)
                w.blocked[std::size_t(p)].insert(str("TypeID"));
            else
                w.blocked[std::size_t(p)].erase(str("TypeID"));
        }
    } else if (name == "SetUpgradeEnableStatus") {
        // Запрет и разрешение исследования: тот же набор blocked, что и у типов юнитов (имена не пересекаются).
        const int p = num("Nat");
        if (player(p)) {
            if (num("State") < 0)
                w.blocked[std::size_t(p)].insert(str("UpgradeID"));
            else
                w.blocked[std::size_t(p)].erase(str("UpgradeID"));
        }
    } else if (name == "TakeWood" || name == "TakeFood" || name == "TakeStone") {
        // Поля не проверены по данным: игрок — Nat или Nation, количество — Value или Count.
        const int p = op.child("Nat") ? num("Nat") : num("Nation");
        const int amount = op.child("Value") ? num("Value") : num("Count");
        const int r = name == "TakeWood" ? 0 : name == "TakeStone" ? 2 : 3;
        if (player(p)) w.resources[std::size_t(p)][std::size_t(r)] = std::max(0, w.resources[std::size_t(p)][std::size_t(r)] - amount);
    } else if (name == "ChangeFriends") {
        const int a = num("fstNat"), b = num("secNat");
        if (player(a) && player(b)) w.team[std::size_t(b)] = w.team[std::size_t(a)];
    } else if (name == "SetUpgradeDone") {
        const int p = num("Nat");
        if (player(p)) w.researched[std::size_t(p)].insert(str("UpgradeID"));
    } else if (name == "StartAIEx") {
        aiStarts.push_back({num("Nation"), num("Difficulty"), str("NameXML")});
    } else if (name == "ShowVictory") {
        if (!ending) ending = Ending{true, str("TextID")};
    } else if (name == "LooseGame") {
        if (!ending) ending = Ending{false, str("TextID")};
    } else if (name == "ShowMessageII") {
        messages.push_back({str("TextID"), str("FileID"), num("SpriteID")});
    } else if (name == "AddElemTHE_CII") {
        if (boolText(str("add_TASK")) && !str("TASK").empty() &&
            std::find(taskNames_.begin(), taskNames_.end(), str("TASK")) == taskNames_.end())
            taskNames_.push_back(str("TASK"));
    } else if (name == "DelElemTHE_CII") {
        if (boolText(str("TASK_USE"))) taskNames_.erase(std::remove(taskNames_.begin(), taskNames_.end(), str("TASK")), taskNames_.end());
    } else if (name == "SetText") {
        pendingText_ = refName(op, "TEXT");
        const auto it = vars_.find(pendingText_);
        if (it != vars_.end()) {
            dialog_.text = it->second.text;
            dialog_.speaker = it->second.speaker;
        }
    } else if (name == "PlayText") {
        playPending_ = true; // играет текст, поставленный SetText за этот шаг (порядок операций не важен)
    } else if (name == "SetLeftPort" || name == "SetRightPort") {
        Portrait& p = name == "SetLeftPort" ? dialog_.left : dialog_.right;
        p.file = str("FileID");
        p.sprite = num("SpriteID");
        const auto it = vars_.find(refName(op, "HName"));
        p.nameText = it != vars_.end() ? it->second.text : "";
    } else if (name == "SetActivFrame") {
        dialog_.rightActive = str("STATE") == "Right";
    } else if (name == "SetFGV") {
        const XmlNode* obj = op.child("GraphObj");
        const std::string ref = obj ? obj->value("RefName").value_or("") : "";
        const bool on = boolText(str("State"));
        fg_[ref] = on;
        if (obj && obj->value("ClassName").value_or("") == "lvCBlackScreen") black_ = on;
        if (obj && obj->value("ClassName").value_or("") == "lvCDeffFilmMenu") dialog_.visible = on;
    } else if (name == "StopOGMiss" || name == "FreeCamera" || name == "MoveCamera" || name == "AttachCameraToGroup" ||
               name == "SetGameMode" || name == "SetScrollLimit" || name == "SetSilence" || name == "PlayOGMiss" ||
               name == "SelectUnits" || name == "SetLightSpot" || name == "BlackScreen" || name == "SetFogMode" ||
               name == "ClearLightSpot" || name == "ClearOrders" || name == "ArtAttack" || name == "GroupHoldNode" ||
               name == "SetUnitStateCII" || name == "BrigPanelSet" || name == "SetStartPoint" || name == "ClearSG" ||
               name == "SpotGrpByUType") {
        // Камера, звук и режимы — на стороне окна; пока не нужны для хода миссии.
    } else {
        note(name);
    }
}

void MissionScript::runRules(const XmlNode* list) {
    if (!list) return;
    for (const XmlNode& r : list->children)
        if (r.name == "lvCCondForOper" && holds(r.child("Condition"))) runOps(r.child("Operation"));
}

void MissionScript::stepFilm(Film& f) {
    const XmlNode& n = *f.node;
    switch (f.state) {
    case Film::State::Idle:
        if (holds(n.child("StartCondition"))) {
            f.state = Film::State::Running;
            runOps(n.child("PrepareOperation"));
        }
        break;
    case Film::State::Running: {
        const XmlNode* stop = n.child("StopCondition");
        if (stop && !stop->children.empty() && holds(stop)) {
            runOps(n.child("DestroyOperation"));
            f.state = Film::State::Done;
        } else {
            runRules(n.child("STEP_CON_OPR"));
        }
        break;
    }
    case Film::State::Done: break;
    }
}

void MissionScript::step() {
    if (!inited_) {
        inited_ = true;
        runRules(init_);
    }
    if (skip_) voiceEnd_ = 0;
    runRules(scripts_);
    for (Film& f : films_) stepFilm(f);
    if (playPending_) {
        playPending_ = false;
        playVoice(pendingText_);
    }
    skip_ = false;
}

} // namespace oc2::world
