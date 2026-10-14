#include "ui/bigmap_screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "assets/g16.hpp"
#include "assets/game_paths.hpp"
#include "assets/text_encoding.hpp"

namespace oc2::ui {

namespace {

constexpr float kMapScale = 0.6f;       // карта 1410×1068 → 846×641
constexpr float kMapW = 1410.0f * kMapScale;
constexpr float kPanelX = 852.0f;        // правая панель
constexpr float kStripY = 648.0f;        // нижняя полоса

const char* const kRankNames[6] = {"Лейтенант", "Капитан", "Майор", "Полковник", "Генерал", "Маршал"};

std::string readText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::uint32_t withAlpha(std::uint32_t argb, std::uint32_t a) { return (argb & 0x00FFFFFFu) | (a << 24); }

std::string hex(std::uint32_t argb) {
    char b[16];
    std::snprintf(b, sizeof b, "{C %08X}", argb);
    return b;
}

std::string replaceFirst(std::string s, const char* what, const std::string& with) {
    const auto at = s.find(what);
    if (at != std::string::npos) s.replace(at, std::char_traits<char>::length(what), with);
    return s;
}

} // namespace

std::shared_ptr<const world::BigGame::Data> BigMapScreen::loadData(const std::filesystem::path& root) {
    auto read = [&](const char* rel) -> std::optional<std::string> {
        if (auto p = assets::findCaseInsensitive(root, rel)) return readText(*p);
        return std::nullopt;
    };
    const auto sectors = read("Missions/StatData/Sectors.dat");
    const auto consts = read("Missions/StatData/BigMapConst.dat");
    const auto hero = read("Missions/StatData/HeroArmy.dat");
    if (!sectors || !consts || !hero) return nullptr;
    auto d = std::make_shared<world::BigGame::Data>();
    if (const auto quests = read("Missions/StatData/Quests.dat")) d->quests = assets::BigQuests::parse(*quests);
    if (const auto dirs = read("Missions/MissionEngin/DirectionDataBase.script.xml")) d->directions = assets::BigDirections::parse(*dirs);
    try {
        d->map = assets::BigMap::parse(*sectors);
        d->k = assets::BigMapConst::parse(*consts);
        d->hero = assets::HeroArmy::parse(*hero);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Завоевание Европы: %s\n", e.what());
        return nullptr;
    }
    return d;
}

BigMapScreen::BigMapScreen(UiContext& ctx, std::shared_ptr<const world::BigGame::Data> data,
                           std::filesystem::path savePath)
    : ctx_(ctx), data_(std::move(data)), savePath_(std::move(savePath)) {
    europe_ = ctx_.images.id("interf3\\TotalWarGraph\\lva_Europe00");
    sectorsImg_ = ctx_.images.id("interf3\\TotalWarGraph\\lva_Sectors");
    defenceImg_ = ctx_.images.id("interf3\\TotalWarGraph\\bmDefence");
    flagsImg_ = ctx_.images.id("interf3\\TotalWarGraph\\lva_Flags");
    std::error_code ec;
    saveExists_ = std::filesystem::exists(savePath_, ec);
}

void BigMapScreen::start(int nation) {
    game_.emplace(data_, nation, std::uint32_t(std::time(nullptr)));
    selected_ = -1;
    modal_ = Modal::None;
    events_.clear();
    battleWait_ = false;
    autosave();
}

void BigMapScreen::autosave() {
    if (!game_) return;
    std::error_code ec;
    std::filesystem::create_directories(savePath_.parent_path(), ec);
    std::ofstream f(savePath_, std::ios::binary);
    f << game_->serialize();
    saveExists_ = true;
}

std::optional<BigMapScreen::BattleRequest> BigMapScreen::takeBattle() {
    auto r = std::move(battleOut_);
    battleOut_.reset();
    return r;
}

const assets::BigSector* BigMapScreen::battleSector() const {
    if (!battleWait_ || !pending_) return nullptr;
    return &data_->map.sectors[std::size_t(pending_->target)];
}

std::string BigMapScreen::battleMap() const {
    const assets::BigSector* s = battleSector();
    if (!s) return {};
    return "Missions/StatData/" + s->name + "/" + s->name + ".m3d";
}

// ---------------------------------------------------------------- рисование

void BigMapScreen::fill(const Rect& r, std::uint32_t argb) {
    ctx_.renderer.fillRect(v_->x(r.x), v_->y(r.y), r.w * v_->scale, r.h * v_->scale, argb);
}

void BigMapScreen::frameRect(const Rect& r, std::uint32_t argb) {
    fill({r.x, r.y, r.w, 1}, argb);
    fill({r.x, r.y + r.h - 1, r.w, 1}, argb);
    fill({r.x, r.y, 1, r.h}, argb);
    fill({r.x + r.w - 1, r.y, 1, r.h}, argb);
}

void BigMapScreen::panel(const Rect& r) {
    fill({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 0xFF3A2A18);
    fill(r, 0xFFE8DCB8);
    frameRect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, 0xFF9C8660);
}

int BigMapScreen::say(const std::string& utf8, float x, float y, int width, Align align, const char* fontName) {
    const Font* font = ctx_.fonts.findTagged(fontName);
    if (!font) return 0;
    const std::string cp = assets::utf8ToCp1251(utf8);
    return ctx_.text.draw(*font, cp, int(x), int(y), width, align, *v_).height;
}

bool BigMapScreen::button(const Rect& r, const std::string& label, bool enabled) {
    const bool over = enabled && hit(r);
    fill({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, 0xFF2A1C10);
    fill(r, !enabled ? 0xFF8A8070 : over ? 0xFFF4E6B8 : 0xFFD8C8A0);
    const std::string c = enabled ? "{C FF2A1C10}" : "{C FF605848}";
    const Font* font = ctx_.fonts.findTagged("M");
    const float line = font ? float(ctx_.fonts.lineHeight(*font)) : 14.0f;
    say(c + label, r.x, r.y + (r.h - line) / 2 - 2, int(r.w), Align::Center);
    if (over && click_) {
        click_ = false;
        return true;
    }
    return false;
}

std::string BigMapScreen::resName(int res) const {
    static const int key[6] = {1, 4, 3, 2, 5, 6}; // порядок мира → ResNameN
    return t("ResName" + std::to_string(key[res]));
}

std::string BigMapScreen::squadLabel(const std::string& type) const {
    auto has = [&](const char* p) { return type.rfind(p, 0) == 0; };
    if (has("Art")) return "Артиллерия";
    if (has("Guard")) return "Гвардия";
    if (has("UnitK")) return "Кавалерия";
    if (has("UnitGre") || has("UnitEgr")) return "Гренадёры";
    if (has("UnitSap")) return "Сапёры";
    if (has("UnitVol")) return "Ополчение";
    if (has("UnitFuz")) return "Пехота";
    return type;
}

std::string BigMapScreen::costLabel(const world::BigGame::Cost& c) const {
    std::string s;
    for (int r = 0; r < world::BigGame::kRes; ++r)
        if (c.res[std::size_t(r)] > 0) s += (s.empty() ? "" : ", ") + resName(r) + " " + std::to_string(c.res[std::size_t(r)]);
    if (c.recruits > 0) s += (s.empty() ? "" : ", ") + t("ResName9") + " " + std::to_string(c.recruits);
    return s;
}

BigMapScreen::Rect BigMapScreen::sectorFrame(int id) const {
    const assets::BigSector& s = data_->map.sectors[std::size_t(id)];
    const float w = float(ctx_.images.frameWidth(sectorsImg_, std::size_t(id))) * kMapScale;
    const float h = float(ctx_.images.frameHeight(sectorsImg_, std::size_t(id))) * kMapScale;
    return {float(s.center.first) * kMapScale, float(s.center.second) * kMapScale, w, h};
}

int BigMapScreen::sectorAt(float vx, float vy) const {
    if (vx < 0 || vy < 0 || vx >= kMapW || vy >= 1068.0f * kMapScale) return -1;
    for (int i = 0; i < int(data_->map.sectors.size()); ++i) {
        const Rect f = sectorFrame(i);
        if (vx < f.x || vy < f.y || vx >= f.x + f.w || vy >= f.y + f.h) continue;
        const assets::Image& m = masks_[std::size_t(i)];
        const int px = int((vx - f.x) / kMapScale), py = int((vy - f.y) / kMapScale);
        if (px < int(m.width) && py < int(m.height) && m.pixel(std::uint32_t(px), std::uint32_t(py))[3] > 16) return i;
    }
    return -1;
}

void BigMapScreen::drawMap() {
    // Карта: девять плиток 470×470.
    for (int i = 0; i < 9; ++i)
        ctx_.images.draw(europe_, std::size_t(i), v_->x(float(i % 3) * 470.0f * kMapScale),
                         v_->y(float(i / 3) * 470.0f * kMapScale), 0xFFFFFFFF, kMapScale * v_->scale);
    const world::BigGame& g = *game_;
    for (const assets::BigSector& s : data_->map.sectors) {
        const int owner = g.sector(s.id).owner;
        const std::uint32_t col = data_->k.color(owner);
        const Rect f = sectorFrame(s.id);
        std::uint32_t a = 0x58;
        if (s.id == selected_) a = 0xA0;
        else if (s.id == hover_) a = 0x80;
        ctx_.images.draw(sectorsImg_, std::size_t(s.id), v_->x(f.x), v_->y(f.y), withAlpha(col, a), kMapScale * v_->scale);
    }
    // Значки городов: здание по уровню обороны, флаг владельца, гарнизон, цели нападения.
    for (const assets::BigSector& s : data_->map.sectors) {
        const auto& st = g.sector(s.id);
        const float cx = float(s.city.first) * kMapScale, cy = float(s.city.second) * kMapScale;
        const bool target = st.owner != g.player() && g.canAttack(s.id);
        const float k = v_->scale;
        const float bs = 0.62f;
        const std::size_t lvl = std::size_t(std::clamp(st.level, 0, 3));
        const float bw = float(ctx_.images.frameWidth(defenceImg_, lvl)) * bs, bh = float(ctx_.images.frameHeight(defenceImg_, lvl)) * bs;
        ctx_.images.draw(defenceImg_, lvl, v_->x(cx - bw / 2), v_->y(cy - bh * 0.62f), 0xFFFFFFFF, bs * k);
        // Флаг владельца на чёрной рамке справа сверху.
        const float fs = 0.56f;
        const float fw = 32 * fs, fh = 24 * fs;
        const float fx = cx + bw * 0.12f, fy = cy - bh * 0.62f - 4;
        fill({fx - 1, fy - 1, fw + 2, fh + 2}, 0xFF000000);
        ctx_.images.draw(flagsImg_, std::size_t(st.owner == 0 ? 0 : st.owner + 1), v_->x(fx), v_->y(fy), 0xFFFFFFFF, fs * k);
        // Гарнизон — тёмная плашка с рамкой цвета владельца под зданием.
        const std::string n = std::to_string(st.garrison);
        const float pw = 10.0f + 6.0f * float(n.size());
        const float py = cy + bh * 0.24f;
        fill({cx - pw / 2 - 1, py - 1, pw + 2, 16}, withAlpha(data_->k.color(st.owner), 0xFF));
        fill({cx - pw / 2, py, pw, 14}, 0xFF181410);
        say("{C FFFFFFFF}" + n, cx - pw / 2, py - 6, int(pw), Align::Center);
        if (target) {
            const float pulse = 0.5f + 0.5f * std::sin(float(frameNo_) * 0.15f);
            frameRect({cx - 28, cy - 30, 56, 56}, withAlpha(0xFFFF2020, std::uint32_t(120 + 120 * pulse)));
        }
    }
    if (selected_ >= 0) {
        const auto& s = data_->map.sectors[std::size_t(selected_)];
        const float cx = float(s.city.first) * kMapScale, cy = float(s.city.second) * kMapScale;
        frameRect({cx - 31, cy - 33, 62, 62}, 0xFFFFE060);
    }
}

void BigMapScreen::drawSidePanel() {
    const world::BigGame& g = *game_;
    const int me = g.player();
    const auto& ns = g.nation(me);
    const Rect r{kPanelX, 4, 1024 - kPanelX - 4, 640};
    panel(r);
    float y = 12;
    fill({r.x + 7, y - 1, 20, 16}, 0xFF000000);
    ctx_.images.draw(flagsImg_, std::size_t(me == 0 ? 0 : me + 1), v_->x(r.x + 8), v_->y(y), 0xFFFFFFFF, 0.6f * v_->scale);
    say("{C FF2A1C10}" + std::string(assets::bigNationName(me)), r.x + 28, y, 130);
    y += 22;
    y += say("{C FF402010}" + t("CWT_CurrentTurn") + " " + std::to_string(g.turn()), r.x + 8, y, int(r.w) - 12);
    y += say("{C FF402010}" + t("CWT_NumOfSectors") + " " + std::to_string(g.ownedBy(me)), r.x + 8, y, int(r.w) - 12);
    y += 6;
    const int next = g.nextRankExp(me);
    y += say("{C FF2A1C10}" + std::string(kRankNames[std::clamp(ns.rank, 0, 5)]), r.x + 8, y, int(r.w) - 12);
    y += say("{C FF402010}" + t("Experience") + " " + std::to_string(ns.exp) + (next ? "/" + std::to_string(next) : ""), r.x + 8, y,
             int(r.w) - 12);
    y += 8;
    const auto inc = g.income(me);
    for (int i = 0; i < world::BigGame::kRes; ++i) {
        std::string line = "{C FF2A1C10}" + resName(i) + " " + std::to_string(ns.res[std::size_t(i)]);
        if (inc[std::size_t(i)] > 0) line += " {C FF206020}+" + std::to_string(inc[std::size_t(i)]);
        y += say(line, r.x + 8, y, int(r.w) - 12);
    }
    y += say("{C FF2A1C10}" + t("ResName9") + " " + std::to_string(ns.recruits) + " {C FF206020}+" + std::to_string(g.recruitIncome(me)),
             r.x + 8, y, int(r.w) - 12);
    y += 10;
    y += say("{C FF402010}" + t("SquadsN") + " " + std::to_string(ns.army.size()) + "/" + std::to_string(g.armyCapacity(me)), r.x + 8, y,
             int(r.w) - 12);
    std::map<std::string, int> groups;
    for (const std::string& type : ns.army) ++groups[squadLabel(type)];
    for (const auto& [label, n] : groups) y += say("{C FF2A1C10}" + label + " x " + std::to_string(n), r.x + 14, y, int(r.w) - 16);
    if (!g.quests().empty()) {
        y += 10;
        y += say("{C FF402010}Задания:", r.x + 8, y, int(r.w) - 12);
        for (const auto& q : g.quests()) {
            const bool attack = q.name.find("AttackNation") != std::string::npos;
            const std::string what = attack ? std::string("Атака: ") + assets::bigNationName(q.target)
                                     : (q.name.find("Defend") != std::string::npos ? "Защита: " : "Захват: ") + sectorName(q.sector);
            std::string line = "{C FF2A1C10}" + what + " {C FF206020}+" + std::to_string(q.gold);
            if (q.deadline > 0) line += " {C FF402010}(до хода " + std::to_string(q.deadline) + ")";
            y += say(line, r.x + 14, y, int(r.w) - 16);
        }
    }
    if (button({r.x + 8, r.y + r.h - 34, r.w - 16, 26}, "Герои")) modal_ = Modal::Hero;
}

void BigMapScreen::drawSectorStrip() {
    const world::BigGame& g = *game_;
    const Rect r{4, kStripY, kMapW - 4, 768 - kStripY - 4};
    panel(r);
    if (selected_ >= 0) {
        const assets::BigSector& s = data_->map.sectors[std::size_t(selected_)];
        const auto& st = g.sector(selected_);
        float y = r.y + 8;
        y += say("{C FF2A1C10}" + sectorName(s.id) + " — " + assets::bigNationName(st.owner), r.x + 10, y, 420);
        std::string res = t("StratRes" + std::to_string(std::clamp(s.resource, 0, 4)));
        y += say("{C FF402010}" + t("MainRes") + " " + res, r.x + 10, y, 420);
        y += say("{C FF402010}" + t("Vill") + " " + std::to_string(s.villages) + "   " + t("Population#") + " " + t("CWT_PopltnLVL" + std::to_string(std::clamp(s.population, 0, 2))), r.x + 10, y, 420);
        const std::string lvl = t("CWT_DefenceLvl" + std::to_string(std::clamp(st.level, 0, 3)));
        say("{C FF402010}" + t("Defence#") + ": " + lvl + " (" + std::to_string(st.garrison) + ")", r.x + 10, y, 420);
    } else {
        say("{C FF402010}" + t("CWV_WorldMap"), r.x + 10, r.y + 8, 420);
    }
    if (statusFrames_ > 0) say("{C FF8F0909}" + status_, r.x + 10, r.y + 92, 600);

    const bool own = selected_ >= 0 && g.sector(selected_).owner == g.player();
    const bool atk = selected_ >= 0 && g.canAttack(selected_);
    const float bx = r.x + 440, bw = 190, bh = 24;
    if (button({bx, r.y + 8, bw, bh}, "Напасть", atk) && atk) {
        pending_ = g.beginPlayerAttack(selected_);
        modal_ = Modal::Attack;
    }
    const bool canUp = own && g.sector(selected_).level < 3;
    if (button({bx, r.y + 36, bw, bh}, t("UpgradeDef#"), canUp) && canUp) {
        if (game_->upgradeDefence(selected_)) autosave();
        else status_ = "Не хватает ресурсов для укрепления", statusFrames_ = 180;
    }
    if (button({bx, r.y + 64, bw, bh}, "Нанять отряд")) modal_ = Modal::Recruit;
    if (button({bx + bw + 8, r.y + 8, 150, bh}, t("CWB_EndOfTurn"))) endTurn();
    if (button({bx + bw + 8, r.y + 36, 150, bh}, t("CWB_Quit"))) modal_ = Modal::Quit;
    const bool canSab = selected_ >= 0 && !own;
    if (button({bx + bw + 8, r.y + 64, 150, bh}, t("CWB_Diversion"), canSab) && canSab) modal_ = Modal::Diversion;
}

void BigMapScreen::drawPick() {
    fill({0, 0, 1024, 768}, 0xFF1A1208);
    const Rect r{192, 90, 640, 580};
    panel(r);
    say("{C FF2A1C10}ЗАВОЕВАНИЕ ЕВРОПЫ", r.x, r.y + 14, int(r.w), Align::Center, "G18");
    say("{C FF402010}Выберите державу", r.x, r.y + 48, int(r.w), Align::Center);
    float y = r.y + 80;
    for (int n = 0; n < assets::kBigNations; ++n) {
        int count = 0;
        for (const assets::BigSector& s : data_->map.sectors) count += s.owner == n;
        const Rect row{r.x + 40, y, r.w - 80, 34};
        const bool over = hit(row);
        fill({row.x - 1, row.y - 1, row.w + 2, row.h + 2}, 0xFF2A1C10);
        fill(row, over ? 0xFFF4E6B8 : 0xFFD8C8A0);
        fill({row.x + 5, row.y + 4, 36, 28}, 0xFF000000);
        ctx_.images.draw(flagsImg_, std::size_t(n == 0 ? 0 : n + 1), v_->x(row.x + 7), v_->y(row.y + 6), 0xFFFFFFFF, v_->scale);
        say("{C FF2A1C10}" + std::string(assets::bigNationName(n)), row.x + 50, row.y + 4, 300);
        say("{C FF402010}" + t("CWT_NumOfSectors") + " " + std::to_string(count), row.x, row.y + 4, int(row.w) - 12, Align::Right);
        if (over && click_) {
            click_ = false;
            start(n);
            return;
        }
        y += 40;
    }
    if (saveExists_ && button({r.x + 40, y + 12, 250, 30}, "Продолжить кампанию")) {
        if (auto g = world::BigGame::deserialize(data_, readText(savePath_))) game_ = std::move(*g);
        else status_ = "Сохранение повреждено", statusFrames_ = 180;
    }
    if (button({r.x + r.w - 290, y + 12, 250, 30}, t("CWB_Quit"))) done_ = true;
    if (statusFrames_ > 0) say("{C FF8F0909}" + status_, r.x, y + 52, int(r.w), Align::Center);
}

void BigMapScreen::drawModal() {
    fill({0, 0, 1024, 768}, 0x90000000);
    const world::BigGame& g = *game_;
    const float h = modal_ == Modal::Attack || modal_ == Modal::Defend || modal_ == Modal::Offer ? 260.0f : modal_ == Modal::Quit || modal_ == Modal::GameOver ? 200.0f : 420.0f;
    const Rect r = modal_ == Modal::Hero ? Rect{112, 130, 800, 400} : Rect{262, (768 - h) / 2 - 40, 500, h};
    panel(r);
    float y = r.y + 14;
    switch (modal_) {
    case Modal::Attack:
    case Modal::Defend: {
        const bool attack = modal_ == Modal::Attack;
        const auto& a = *pending_;
        const std::string target = sectorName(a.target);
        if (attack) y += say("{C FF2A1C10}" + t("CWM_AttackSect") + " (" + target + ")", r.x + 14, y, int(r.w) - 28);
        else y += say("{C FF8F0909}" + std::string(assets::bigNationName(a.nation)) + " нападает на " + target + "!", r.x + 14, y, int(r.w) - 28);
        y += 8;
        y += say("{C FF402010}Отрядов нападающих: " + std::to_string(a.squads.size()) + ", сила " + std::to_string(g.attackPower(a.squads)), r.x + 14, y, int(r.w) - 28);
        y += say("{C FF402010}Оборона: " + std::to_string(g.sector(a.target).garrison) + " отр., сила " + std::to_string(g.defencePower(a.target)), r.x + 14, y, int(r.w) - 28);
        if (button({r.x + 20, r.y + r.h - 50, 140, 30}, "В бой")) {
            battleOut_ = BattleRequest{a, attack};
            battleWait_ = true;
            battlePlayerAttacks_ = attack;
            modal_ = Modal::None;
        }
        if (button({r.x + 180, r.y + r.h - 50, 140, 30}, "Расчёт")) {
            if (attack) {
                game_->log.clear();
                const auto out = game_->autoResolve(a);
                game_->apply(a, out);
                pending_.reset();
                openEvents();
                autosave();
            } else {
                resolveDefence();
            }
        }
        if (attack && button({r.x + 340, r.y + r.h - 50, 140, 30}, "Отмена")) {
            pending_.reset();
            modal_ = Modal::None;
        }
        break;
    }
    case Modal::Events: {
        say("{C FF2A1C10}" + t("CWV_Messages"), r.x, y, int(r.w), Align::Center);
        y += 28;
        int shown = 0;
        for (auto it = events_.rbegin(); it != events_.rend() && shown < 12; ++it, ++shown)
            y += say("{C FF402010}" + *it, r.x + 14, y, int(r.w) - 28) + 4;
        if (button({r.x + r.w / 2 - 70, r.y + r.h - 50, 140, 30}, "Дальше") || false) {
            modal_ = game_->offers.empty() ? Modal::None : Modal::Offer;
            if (modal_ == Modal::None) checkOver();
        }
        break;
    }
    case Modal::Offer: {
        if (game_->offers.empty()) {
            modal_ = Modal::None;
            break;
        }
        const auto& o = game_->offers.front();
        say("{C FF2A1C10}" + t("CDT_MessHead25"), r.x, y, int(r.w), Align::Center);
        y += 30;
        say("{C FF402010}" + format(o.text), r.x + 14, y, int(r.w) - 28);
        const bool can = game_->canAccept(o);
        if (button({r.x + 20, r.y + r.h - 50, 200, 30}, "Принять", can)) {
            game_->answer(true);
            autosave();
            if (game_->offers.empty()) openEvents();
        } else if (button({r.x + r.w - 220, r.y + r.h - 50, 200, 30}, "Отказаться")) {
            game_->answer(false);
            autosave();
            if (game_->offers.empty()) openEvents();
        }
        if (!can) say("{C FF8F0909}Не хватает золота или места в армии", r.x + 14, r.y + r.h - 80, int(r.w) - 28);
        break;
    }
    case Modal::Recruit: {
        say("{C FF2A1C10}Нанять отряд", r.x, y, int(r.w), Align::Center);
        y += 28;
        const auto& ns = g.nation(g.player());
        say("{C FF402010}" + t("SquadsN") + " " + std::to_string(ns.army.size()) + "/" + std::to_string(g.armyCapacity(g.player())), r.x + 14, y, 300);
        y += 22;
        for (const std::string& type : g.recruitable(g.player())) {
            const auto c = world::BigGame::squadCost(type);
            if (button({r.x + 14, y, r.w - 28, 26}, squadLabel(type) + " — " + costLabel(c))) {
                if (game_->recruit(g.player(), type)) autosave();
                else status_ = "Нет места в армии или не хватает ресурсов", statusFrames_ = 180;
            }
            y += 30;
        }
        if (statusFrames_ > 0) say("{C FF8F0909}" + status_, r.x + 14, r.y + r.h - 80, int(r.w) - 28);
        if (button({r.x + r.w / 2 - 70, r.y + r.h - 44, 140, 30}, "Готово")) modal_ = Modal::None;
        break;
    }
    case Modal::GameOver: {
        const std::string msg = overResult_ > 0 ? t("CWM_TotalVict") : t("CWM_TotalLoose");
        say("{C FF2A1C10}" + msg, r.x + 14, y + 40, int(r.w) - 28, Align::Center);
        if (button({r.x + r.w / 2 - 90, r.y + r.h - 50, 180, 30}, "К меню")) {
            std::error_code ec;
            std::filesystem::remove(savePath_, ec);
            saveExists_ = false;
            done_ = true;
        }
        break;
    }
    case Modal::Quit: {
        say("{C FF2A1C10}" + t("CWM_Quit"), r.x + 14, y + 50, int(r.w) - 28, Align::Center);
        if (button({r.x + 40, r.y + r.h - 50, 180, 30}, "Выйти")) {
            autosave();
            done_ = true;
        }
        if (button({r.x + r.w - 220, r.y + r.h - 50, 180, 30}, "Остаться")) modal_ = Modal::None;
        break;
    }
    case Modal::Hero: { // биографии героев нации: Missions\\Heroes\\heroinf<нация><номер>.txt
        // номер нации в heroinf.dat (EN FR RU AU PR EG PO SP RE) отличается от порядка кампании (EN FR AU PR RU …)
        static const int datNation[9] = {0, 1, 3, 4, 2, 5, 6, 7, 8};
        const int me = std::clamp(g.player(), 0, 8);
        char file[64];
        std::snprintf(file, sizeof file, "Missions/Heroes/heroinf%d%d.txt", datNation[me], hero_);
        std::string bio;
        if (std::ifstream in{ctx_.dataRoot / file, std::ios::binary}) bio.assign(std::istreambuf_iterator<char>(in), {});
        if (bio.empty()) bio = "{C FF402010}Нет сведений.";
        if (const Font* font = ctx_.fonts.findTagged("M"))
            ctx_.text.draw(*font, bio, int(r.x + 24), int(r.y + 20), int(r.w) - 48, Align::Left, *v_);
        say("{C FF402010}" + std::to_string(hero_ + 1) + " / 10", r.x + r.w / 2 + 120, r.y + r.h - 44, 80);
        if (button({r.x + 24, r.y + r.h - 50, 140, 30}, "Назад")) hero_ = (hero_ + 9) % 10;
        if (button({r.x + r.w - 164, r.y + r.h - 50, 140, 30}, "Дальше")) hero_ = (hero_ + 1) % 10;
        if (button({r.x + r.w / 2 - 70, r.y + r.h - 50, 140, 30}, "Закрыть")) modal_ = Modal::None;
        break;
    }
    case Modal::Diversion: {
        if (selected_ < 0) {
            modal_ = Modal::None;
            break;
        }
        say("{C FF2A1C10}" + t("CWB_Diversion") + " (" + sectorName(selected_) + ")", r.x, y, int(r.w), Align::Center);
        y += 34;
        for (int i = 0; i < world::BigGame::kDiversions; ++i) {
            const bool can = g.canDiversion(i, selected_);
            const std::string gold = (can ? "" : "{CR}") + std::to_string(g.diversionCost(i, selected_)) + "{C}";
            std::string label = t("CWM_Diversion" + std::to_string(i));
            const auto s = label.find("%s");
            if (s != std::string::npos) label.replace(s, 2, gold);
            const auto d = label.find("%d%%");
            if (d != std::string::npos) label.replace(d, 4, std::to_string(g.diversionPercent(i, selected_)) + "%");
            if (button({r.x + 14, y, r.w - 28, 30}, label, can) && can) {
                modal_ = Modal::None;
                game_->diversion(i, selected_);
                openEvents();
                autosave();
                return;
            }
            y += 38;
        }
        if (button({r.x + r.w / 2 - 70, r.y + r.h - 44, 140, 30}, "Отмена")) modal_ = Modal::None;
        break;
    }
    case Modal::None: break;
    }
}

// ---------------------------------------------------------------- ход

std::string BigMapScreen::localize(std::string text) const {
    for (const assets::BigSector& s : data_->map.sectors) {
        const std::size_t at = text.find(s.name);
        if (at != std::string::npos) text.replace(at, s.name.size(), sectorName(s.id));
    }
    return text;
}

std::string BigMapScreen::format(const world::BigGame::Message& m) const {
    std::string text = t(m.key);
    std::size_t arg = 0, at = 0;
    while ((at = text.find('%', at)) != std::string::npos && at + 1 < text.size()) {
        const char c = text[at + 1];
        if (c != 's' && c != 'd') {
            ++at;
            continue;
        }
        std::string v = arg < m.args.size() ? m.args[arg++] : std::string();
        if (!v.empty() && v[0] == '@') v = squadLabel(v.substr(1));
        text.replace(at, 2, v);
        at += v.size();
    }
    for (std::size_t p; (p = text.find("{CR}")) != std::string::npos;) text.replace(p, 4, "{C FF8F0909}");
    for (std::size_t p; (p = text.find("{C}")) != std::string::npos;) text.replace(p, 3, "{C FF402010}");
    return localize(text);
}

void BigMapScreen::openEvents() {
    events_ = game_->log;
    for (std::string& e : events_) e = localize(e);
    for (const auto& m : game_->messages) events_.push_back(format(m));
    game_->log.clear();
    game_->messages.clear();
    if (events_.empty()) {
        modal_ = game_->offers.empty() ? Modal::None : Modal::Offer;
        if (modal_ == Modal::None) checkOver();
    } else {
        modal_ = Modal::Events;
    }
}

void BigMapScreen::checkOver() {
    overResult_ = game_->result();
    if (overResult_ != 0) modal_ = Modal::GameOver;
}

void BigMapScreen::endTurn() {
    game_->log.clear();
    afterTurnStep(game_->endTurn());
}

void BigMapScreen::afterTurnStep(std::optional<world::BigGame::Attack> attack) {
    if (attack) {
        pending_ = std::move(attack);
        modal_ = Modal::Defend;
        return;
    }
    pending_.reset();
    autosave();
    openEvents();
}

// Защита сектора игрока: играть бой или посчитать.
void BigMapScreen::resolveDefence() {
    const auto a = *pending_;
    const auto out = game_->autoResolve(a);
    game_->apply(a, out);
    pending_.reset();
    afterTurnStep(game_->continueTurn());
}

void BigMapScreen::battleFinished(const world::BigGame::Outcome& outcome) {
    battleWait_ = false;
    if (!pending_ || !game_) return;
    const auto a = *pending_;
    game_->apply(a, outcome);
    pending_.reset();
    if (battlePlayerAttacks_) {
        autosave();
        openEvents();
    } else {
        afterTurnStep(game_->continueTurn());
    }
}

// ---------------------------------------------------------------- кадр

void BigMapScreen::frame(const View& v, const Input& in) {
    v_ = &v;
    mx_ = in.mx;
    my_ = in.my;
    click_ = in.click;
    ++frameNo_;
    if (statusFrames_ > 0) --statusFrames_;
    if (!maskLoaded_) {
        maskLoaded_ = true;
        masks_.resize(data_->map.sectors.size());
        const std::string name = "interf3/TotalWarGraph/lva_Sectors";
        for (const char* ext : {".g17", ".g16"}) {
            if (auto p = assets::findCaseInsensitive(ctx_.dataRoot, name + ext)) {
                try {
                    std::ifstream f(*p, std::ios::binary);
                    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    const auto g = assets::G16File::parse(std::move(bytes));
                    for (std::size_t i = 0; i < masks_.size() && i < g.frameCount(); ++i) masks_[i] = g.decodeFrame(i);
                    break;
                } catch (const std::exception&) {
                }
            }
        }
    }
    if (battleWait_) { // идёт бой на карте — экран ждёт
        fill({0, 0, 1024, 768}, 0xFF000000);
        return;
    }
    if (!game_) {
        if (in.esc) done_ = true;
        drawPick();
        return;
    }
    if (in.esc) {
        if (modal_ == Modal::None) modal_ = Modal::Quit;
        else if (modal_ == Modal::Quit || modal_ == Modal::Recruit || modal_ == Modal::Attack || modal_ == Modal::Hero || modal_ == Modal::Diversion) modal_ = Modal::None;
    }
    if (in.enter && modal_ == Modal::None) endTurn();

    fill({0, 0, 1024, 768}, 0xFF1A1208);
    if (modal_ == Modal::None) {
        hover_ = sectorAt(mx_, my_);
        if (click_ && hover_ >= 0) selected_ = hover_;
    } else {
        hover_ = -1;
    }
    drawMap();
    // Клики, пришедшие на карту, больше не нужны кнопкам нижней полосы.
    if (hover_ >= 0) click_ = false;
    if (modal_ == Modal::None) {
        drawSidePanel();
        drawSectorStrip();
    } else {
        drawSidePanel();
        const bool saved = click_;
        click_ = false;
        drawSectorStrip();
        click_ = saved;
        drawModal();
    }
}

} // namespace oc2::ui
