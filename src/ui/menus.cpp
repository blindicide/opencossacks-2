#include "ui/menus.hpp"

#include <chrono>
#include <random>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>

#include "assets/game_paths.hpp"
#include "assets/map3d.hpp"
#include "assets/text_encoding.hpp"

namespace oc2::ui {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string gamePath(std::string name) {
    for (char& c : name)
        if (c == '\\') c = '/';
    return name;
}

} // namespace

Menus::Menus(UiContext& ctx) : ctx_(ctx) {
    settings.load(core::Settings::defaultPath());
    // Как FUN_014a5460: список из Dialogs\MainMenu.xml, без него — три меню по умолчанию.
    files_ = {{"Main", "dialogs/v/M_Main.DialogsSystem.xml"},
              {"Single", "dialogs/v/M_Single.DialogsSystem.xml"},
              {"Options", "dialogs/v/M_Options.DialogsSystem.xml"}};
    if (auto path = assets::findCaseInsensitive(ctx_.dataRoot, "Dialogs/MainMenu.xml")) {
        std::ifstream in(*path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        try {
            const assets::XmlNode doc = assets::parseXml(text);
            const assets::XmlNode* list = doc.child("MainMenu");
            if (!list) list = &doc;
            files_.clear();
            for (const assets::XmlNode& m : list->children) files_[m.name] = gamePath(trim(m.text));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "MainMenu.xml: %s\n", e.what());
        }
    }
    // Missions.txt и выборки (FUN_00fd1570 и загрузчик сражений FUN около SingleBatl.txt).
    auto readText = [&](const char* name) -> std::optional<std::string> {
        auto path = assets::findCaseInsensitive(ctx_.dataRoot, name);
        if (!path) return std::nullopt;
        std::ifstream in(*path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    };
    try {
        if (auto text = readText("Missions/Missions.txt")) missions_ = assets::MissionList::parse(*text);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Missions.txt: %s\n", e.what());
    }
    if (auto text = readText("Missions/SingleMiss.txt")) skirmish_ = missions_.selection(*text);
    if (auto text = readText("Missions/SingleBatl.txt")) battles_ = missions_.selection(*text);
    if (!skirmish_.empty()) selectedMission = skirmish_.front();
    if (auto text = readText("Dialogs/campaign.xml")) {
        try {
            campaigns_ = assets::CampaignList::parse(*text);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "campaign.xml: %s\n", e.what());
        }
    }
    files_.emplace("Credits", "dialogs/mm/Credits.DialogsSystem.xml");
    files_.emplace("Loading", "dialogs/mm/M_Loading.DialogsSystem.xml");
    campaignDone_.assign(campaigns_.campaigns.size(), 0);
    for (std::size_t i = 0; i < campaignDone_.size() && i < settings.campaignDone.size(); ++i)
        campaignDone_[i] = settings.campaignDone[i];
}

bool Menus::open(const std::string& name) {
    auto file = files_.find(name);
    if (file == files_.end()) return false;
    if (!loaded_.count(name)) {
        const auto path = assets::findCaseInsensitive(ctx_.dataRoot, file->second);
        if (!path) {
            std::fprintf(stderr, "меню %s: нет файла %s\n", name.c_str(), file->second.c_str());
            return false;
        }
        loaded_[name] = std::make_unique<Dialog>(ctx_, *path);
    }
    stack_.push_back({name, "Main"});
    savesDirty_ = true;
    savePick_ = -1;
    return true;
}

void Menus::close() {
    optionsShown_ = false;
    if (!stack_.empty()) stack_.pop_back();
}

Dialog& Menus::current() {
    if (stack_.empty()) throw std::logic_error("нет открытого меню");
    return *loaded_.at(stack_.back().name);
}

void Menus::update() {
    if (stack_.empty()) return;
    Dialog& dialog = current();
    // Вкладки: нажатая кнопка (State 1) задаёт видимую TabDesk своей группы.
    std::string& tab = activeTab_[currentName()];
    if (tab.empty())
        dialog.forEach([&](Widget& w) {
            if (w.type == "TabButton" && w.state == 1 && tab.empty()) tab = w.group;
        });
    syncPlayerTable(dialog);
    syncProfiles(dialog);
    syncSaves(dialog);
    syncStats(dialog);
    if (currentName() == "Credits") syncCredits(dialog);
    if (currentName() == "Loading")
        dialog.forEach([&](Widget& w) {
            if (!hasAction(w, "cva_LoadingTips")) return;
            if (loadingTip_.empty()) { // случайный совет из Text/Add/tips.txt (#Tips0…)
                std::vector<std::string> tips;
                for (int n = 0; ctx_.texts.contains("#Tips" + std::to_string(n)); ++n)
                    tips.push_back(assets::utf8ToCp1251(ctx_.texts.lookup("#Tips" + std::to_string(n))));
                if (!tips.empty()) loadingTip_ = tips[std::size_t(std::random_device{}()) % tips.size()];
            }
            w.text = loadingTip_;
        });
    else loadingTip_.clear();
    bool hasTabs = false;
    dialog.forEach([&](Widget& w) { hasTabs = hasTabs || w.type == "TabButton"; });
    dialog.forEach([&](Widget& w) {
        if (w.type == "TabDesk") w.visible = !hasTabs || w.group == tab; // без кнопок вкладка одна
        if (w.type == "TabButton") w.state = w.group == tab ? 1 : 0;
    });
    // Окно настроек: ползунки и флажки берут значения из settings при открытии.
    const bool optionsOpen = currentName() == "Options" || modalDesk() == "Options";
    if (optionsOpen && !optionsShown_) syncOptions(true);
    optionsShown_ = optionsOpen;
    dialog.forEach([&](Widget& w) {
        // Список кампаний окна выбора (у окна загрузки CampList — список сохранений).
        if (w.name == "CampList" && w.listElement && !hasAction(w, "cva_LS_FileList") &&
            w.children.size() != campaigns_.campaigns.size()) {
            std::vector<std::string> names;
            for (const assets::Campaign& c : campaigns_.campaigns) names.push_back(assets::utf8ToCp1251(ctx_.texts.lookup(c.nameKey)));
            dialog.setListItems(w, names);
            Dialog::selectListItem(w, int(campaignPick_));
        }
        // «Camp Miss Description» — в данных статичный «#MissDesc1»; настоящий ключ у миссии кампании.
        if (w.name == "Camp Miss Description" && campaign_) {
            const auto& ms = campaigns_.campaigns[*campaign_].missions;
            if (!ms.empty()) {
                const auto next = std::size_t(std::min<int>(campaignDone_[*campaign_], int(ms.size()) - 1));
                w.text = assets::utf8ToCp1251(ctx_.texts.lookup(ms[next].descriptionKey));
            }
        }
        for (const Widget::Action& a : w.actions) {
            if (a.type == "cva_CCamp_Sectors") // окно карты своей кампании
                w.visible = campaign_ && campaigns_.campaigns[*campaign_].desk == w.name;
            if (a.type == "cva_CCamp_Name" && campaign_)
                w.text = assets::utf8ToCp1251(ctx_.texts.lookup(campaigns_.campaigns[*campaign_].nameKey));
            if (a.type == "cva_CMiss_Name" && campaign_) {
                const auto& ms = campaigns_.campaigns[*campaign_].missions;
                const auto next = std::size_t(std::min<int>(campaignDone_[*campaign_], int(ms.size()) - 1));
                w.text = ms.empty() ? std::string() : assets::utf8ToCp1251(ctx_.texts.lookup(ms[next].nameKey));
            }
            if (a.type == "cva_Mission" && a.node && campaign_) { // доступны пройденные и первая непройденная
                const int index = int(a.node->intValue("Index").value_or(0));
                w.enabled = index <= campaignDone_[*campaign_] &&
                            std::size_t(index) < campaigns_.campaigns[*campaign_].missions.size();
            }
            // FUN_013d6530: списки карт — названия по ключам миссий («#Skirmish18_TXT»).
            if (const std::vector<std::size_t>* items = listFor(w); items && w.children.size() != items->size()) {
                std::vector<std::string> names;
                for (std::size_t m : *items) names.push_back(assets::utf8ToCp1251(ctx_.texts.lookup(missions_.missions[m].nameKey)));
                for (std::size_t i = 0; i < items->size(); ++i)
                    if (selectedMission == (*items)[i]) w.currentElement = int(i);
                dialog.setListItems(w, names);
            }
            // FUN_013d7350: описание выбранной миссии — текст её .txt с разметкой.
            if (a.type == "va_MissDescription" && shownDescription_ != selectedMission && selectedMission) {
                shownDescription_ = selectedMission;
                w.text.clear();
                if (auto path = assets::findCaseInsensitive(ctx_.dataRoot,
                                                            gamePath(missions_.missions[*selectedMission].description))) {
                    std::ifstream in(*path, std::ios::binary);
                    w.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                }
            }
            // cva_Map_Preview (Canvas): мини-карта выбранной миссии из раздела VIE2 её карты.
            if (a.type == "cva_Map_Preview" && selectedMission && shownPreview_ != selectedMission) {
                shownPreview_ = selectedMission;
                auto it = previews_.find(*selectedMission);
                if (it == previews_.end()) {
                    render::TextureId tex = 0;
                    bool ok = false;
                    if (auto path = assets::findCaseInsensitive(ctx_.dataRoot,
                                                                gamePath(missions_.missions[*selectedMission].map))) {
                        try {
                            std::ifstream in(*path, std::ios::binary);
                            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                                                  std::istreambuf_iterator<char>()};
                            const assets::Image img = assets::Map3d::parse(bytes).preview();
                            if (img.width) {
                                tex = ctx_.renderer.createTexture(img);
                                ok = true;
                            }
                        } catch (const std::exception& e) {
                            std::fprintf(stderr, "мини-карта %s: %s\n", path->string().c_str(), e.what());
                        }
                    }
                    it = previews_.emplace(*selectedMission, ok ? tex : render::TextureId(-1)).first;
                }
                w.hasBitmap = it->second != render::TextureId(-1);
                w.bitmap = it->second;
            }
            // FUN_014d4060: заголовок, текст, кнопка и картинка окна итога — из полей действия.
            if (a.type == "cva_VI_EndGame" && gameResult_ && !resultShown_ && a.node) {
                resultShown_ = true;
                const bool vic = *gameResult_;
                auto text = [&](const char* field) {
                    return assets::utf8ToCp1251(ctx_.texts.lookup(a.node->value(field).value_or("")));
                };
                auto ref = [&](const char* field) -> Widget* {
                    const assets::XmlNode* r = a.node->child(field);
                    return r ? dialog.find(r->value("RefName").value_or("")) : nullptr;
                };
                if (Widget* t = ref("dTitle")) t->text = text(vic ? "VicTitle" : "DefTitle");
                if (Widget* m = ref("dMessage")) {
                    m->text = text(vic ? "VicMessage" : "DefMessage");
                    if (ctx_.texts.contains(resultMessage_)) m->text = assets::utf8ToCp1251(ctx_.texts.lookup(resultMessage_));
                }
                if (Widget* b = ref("dButton")) b->text = text(vic ? "VicButton" : "DefButton");
                if (Widget* p = ref("dPicture"); p && p->kind == WidgetKind::GPPicture) {
                    p->gp = ctx_.images.id(a.node->value("gpFile").value_or(""));
                    p->hasGp = true;
                    p->sprite = int(a.node->intValue(vic ? "VicSprite" : "DefSprite").value_or(0));
                }
            }
            if (a.type == "cva_M_ModalDesk") w.visible = w.name == modalDesk();
            // Меню игры: вариант панели «Main» по режиму игры (SinGlobalMap, SinBattles, MulBattles).
            if (a.type == "cva_InGameMenu_MainDesk" && a.node && w.visible)
                w.visible = a.node->value("ID").value_or("") == mainDesk_;
            // FUN_013d8a00 (упрощённо): «Начать» доступна, если есть карта; проверки
            // состава игроков и команд оригинала ещё не перенесены.
            if (a.type == "cva_MM_SinglStart") w.enabled = selectedMission.has_value();
        }
    });
}

const char* Menus::nationName(int index) {
    static const char* const names[kNationCount] = {"Франция", "Россия", "Англия", "Пруссия", "Австрия",
                                                    "Египет", "Польша", "Испания", "Рейнск. союз"};
    return index >= 0 && index < kNationCount ? names[index] : "";
}

const char* Menus::nationCode(int index) {
    static const char* const codes[kNationCount] = {"FR", "RU", "EN", "PR", "AU", "EG", "PO", "SP", "RE"};
    return index >= 0 && index < kNationCount ? codes[index] : "";
}

// Таблица игроков стычки (cva_BR_*): строка — ID элемента, одни и те же элементы есть во всех вкладках.
void Menus::syncPlayerTable(Dialog& dialog) {
    auto cp = [](const std::string& utf8) { return assets::utf8ToCp1251(utf8); };
    // Значение слота ↔ выбранная строка: изменённое пользователем попадает в слот, иначе слот — в элемент.
    auto sync = [](Widget& w, int& value) {
        if (w.selected != w.syncedSel) value = w.selected;
        else w.selected = value;
        w.syncedSel = w.selected;
    };
    dialog.forEach([&](Widget& w) {
        for (const Widget::Action& a : w.actions) {
            if (w.id < 0 || w.id >= kSlots) continue;
            Slot& slot = slots[std::size_t(w.id)];
            if (a.type == "cva_BR_PlName") {
                if (w.kind == WidgetKind::ComboBox) {
                    if (w.items.empty()) w.items = {cp("Компьютер"), cp("Нет игрока")};
                    int off = slot.active ? 0 : 1;
                    sync(w, off);
                    slot.active = off == 0;
                } else {
                    w.text = cp("Игрок");
                }
            } else if (a.type == "cva_BR_PlRace" && w.kind == WidgetKind::ComboBox) {
                // Строки: «По карте», нации, у компьютера ещё «Нет игрока» (последняя).
                if (w.items.empty()) {
                    w.items.push_back(cp("По карте"));
                    for (int n = 0; n < kNationCount; ++n) w.items.push_back(cp(nationName(n)));
                    if (w.id > 0) w.items.push_back(cp("Нет игрока"));
                }
                int value = slot.active ? slot.nation : kNationCount + 1;
                sync(w, value);
                slot.active = value <= kNationCount;
                if (slot.active) slot.nation = value;
                w.visible = true;
            } else if (a.type == "cva_BR_PlColor") {
                w.sprite = slot.color;
                w.visible = slot.active;
            } else if (a.type == "cva_BR_PlTeam") {
                w.text = slot.team ? std::to_string(slot.team) : cp("—");
                w.visible = slot.active;
            } else if (a.type == "cva_BR_PlMapLoading") {
                w.text.clear();
            }
        }
    });
    // Прочие выпадающие списки: строки и связанное значение.
    auto fill = [&](Widget& w, std::initializer_list<const char*> items, int& value) {
        if (w.items.empty())
            for (const char* i : items) w.items.push_back(cp(i));
        sync(w, value);
    };
    dialog.forEach([&](Widget& w) {
        if (w.kind != WidgetKind::ComboBox) return;
        for (const Widget::Action& a : w.actions) {
            if (a.type == "cva_CampDiff") fill(w, {"Лёгкая", "Нормальная", "Трудная"}, difficulty);
            else if (a.type == "cva_ProfAdd_Diff") fill(w, {"Лёгкая", "Нормальная", "Трудная"}, addDiff_);
            else if (a.type == "cva_ProfAdd_Race") {
                if (w.items.empty())
                    for (int n = 0; n < kNationCount; ++n) w.items.push_back(cp(nationName(n)));
                sync(w, addRace_);
            }
            else if (a.type == "cva_Opt_AnimQuality") fill(w, {"Низкое", "Среднее", "Высокое"}, animQuality_);
            else if (a.type == "cva_Opt_ArcadeMode") fill(w, {"Выключен", "Включён"}, arcadeMode_);
            else if (a.type == "cva_MP_Autosave") fill(w, {"Выключено", "Включено"}, autosave_);
        }
    });
}

// Окно итоговой статистики: таблица расходов и списки юнитов/зданий обоих игроков.
void Menus::syncStats(Dialog& dialog) {
    if (currentName() != "PostGameStat" || !stats_ || statsShown_) return;
    statsShown_ = true;
    auto cp = [](const std::string& utf8) { return assets::utf8ToCp1251(utf8); };
    dialog.forEach([&](Widget& pd) {
        if (!hasAction(pd, "cva_GS_Player")) return;
        const GameStats::Side& data = pd.id == 0 ? stats_->winner : stats_->loser;
        std::vector<Widget*> sections; // пары (заготовка секции) в порядке следования
        std::function<void(Widget&)> visit = [&](Widget& w) {
            if (hasAction(w, "cva_GS_PlayerName")) w.text = cp(data.name);
            else if (hasAction(w, "cva_GS_PlayerRace")) w.text = cp(data.race);
            else if (hasAction(w, "cva_GS_Resource")) {
                // Строки окна: 0 — всего, 1 — улучшения, 2/7 — содержание, 3 — юниты, 4 — здания, 5 — покупка, 6 — продажа.
                const int row = w.id == 7 ? 2 : std::clamp(w.id, 0, 6);
                std::size_t col = 0;
                for (Widget& c : w.children) {
                    if (c.type != "TextButton") continue;
                    if (col > 0 && col <= 6) c.text = std::to_string(data.table[std::size_t(row)][std::size_t(col - 1)]);
                    ++col;
                }
            } else if (hasAction(w, "cva_GS_FormationDesk") || hasAction(w, "cva_GS_UnitsDesk")) {
                sections.push_back(&w);
                return;
            }
            for (Widget& c : w.children) visit(c);
        };
        visit(pd);
        // Секции: формации (наших отрядов нет — скрыты), юниты, затем здания (клон секции юнитов).
        Widget* parent = nullptr;
        dialog.forEach([&](Widget& w) {
            for (Widget& c : w.children)
                if (!sections.empty() && &c == sections.front()) parent = &w;
        });
        if (!parent || sections.empty()) return;
        const bool formationFirst = hasAction(*sections.front(), "cva_GS_FormationDesk");
        Widget* unitsDesk = nullptr;
        for (Widget* sec : sections)
            if (hasAction(*sec, "cva_GS_UnitsDesk") && !unitsDesk) unitsDesk = sec;
        if (!unitsDesk) return;
        Widget buildingsDesk = *unitsDesk;
        for (Widget& c : buildingsDesk.children)
            if (c.name == "Title" && !c.children.empty()) c.children.front().text = cp("Здания");
        const bool hasBuildingsDesk = sections.size() > (formationFirst ? 2u : 1u);
        for (Widget* sec : sections)
            if (hasAction(*sec, "cva_GS_FormationDesk")) sec->visible = false;
        auto fillSection = [&](Widget& sec, const std::vector<GameStats::Row>& rows, int top) {
            const Widget* tmpl = nullptr;
            for (const Widget& c : sec.children)
                if (hasAction(c, "cva_GS_Unit") || hasAction(c, "cva_GS_Formation")) tmpl = &c;
            if (!tmpl) return top;
            const Widget proto = *tmpl;
            std::erase_if(sec.children, [&](const Widget& c) { return hasAction(c, "cva_GS_Unit"); });
            const int step = std::max(proto.height - 5, 20);
            int y = proto.y;
            for (const GameStats::Row& r : rows) {
                Widget row = proto;
                row.y = y;
                std::size_t col = 0;
                for (Widget& c : row.children) {
                    const std::string vals[4] = {cp(r.name), std::to_string(r.built), std::to_string(r.killed),
                                                 std::to_string(r.lost)};
                    if (col < 4) c.text = vals[col];
                    ++col;
                }
                sec.children.push_back(std::move(row));
                y += step;
            }
            sec.y = top;
            sec.height = y + 8;
            sec.visible = !rows.empty();
            return rows.empty() ? top : top + sec.height + 8;
        };
        int top = fillSection(*unitsDesk, data.units, 0);
        if (hasBuildingsDesk) {
            for (Widget* sec : sections)
                if (sec != unitsDesk && hasAction(*sec, "cva_GS_UnitsDesk")) {
                    fillSection(*sec, data.buildings, top);
                    break;
                }
        } else if (!data.buildings.empty()) {
            // В окне нет секции зданий — она клонируется из секции юнитов.
            fillSection(buildingsDesk, data.buildings, top);
            parent->children.push_back(std::move(buildingsDesk));
        }
    });
}

// Профили: тексты текущего, список выбора, подтверждение удаления.
void Menus::syncProfiles(Dialog& dialog) {
    auto cp = [](const std::string& utf8) { return assets::utf8ToCp1251(utf8); };
    static const char* const diffNames[3] = {"Лёгкая", "Нормальная", "Трудная"};
    core::Settings::Profile* cur = settings.profile >= 0 && std::size_t(settings.profile) < settings.profiles.size()
                                       ? &settings.profiles[std::size_t(settings.profile)]
                                       : nullptr;
    if (cur && !profileApplied_) {
        difficulty = cur->difficulty;
        profileApplied_ = true;
    } else if (cur && difficulty != cur->difficulty) { // сложность сменили в окне кампании
        cur->difficulty = difficulty;
        settings.save(core::Settings::defaultPath());
    }
    const std::string none = cp("—");
    dialog.forEach([&](Widget& w) {
        for (const Widget::Action& a : w.actions) {
            if (a.type == "cva_Sin_ProfName" || a.type == "cva_ProfCur_Name") w.text = cur ? cp(cur->name) : none;
            else if (a.type == "cva_ProfCur_Race") w.text = cur ? cp(nationName(cur->race)) : none;
            else if (a.type == "cva_ProfCur_Diff") w.text = cur ? cp(diffNames[std::clamp(cur->difficulty, 0, 2)]) : none;
            else if (a.type == "cva_ProfCur_Ocup" || a.type == "cva_ProfAdd_Desc") w.text.clear();
            else if (a.type == "cva_ProfList" && w.listElement &&
                     w.children.size() != settings.profiles.size()) {
                std::vector<std::string> names;
                for (const auto& p : settings.profiles) names.push_back(cp(p.name));
                profilePick_ = std::clamp(profilePick_, 0, std::max(0, int(names.size()) - 1));
                dialog.setListItems(w, names);
                Dialog::selectListItem(w, profilePick_);
            } else if (a.type == "cva_ProfAdd_PortScr") {
                w.visible = false; // выбор портрета пока не сделан
            } else if (a.type == "cva_ProfDel_Desk")
                w.visible = deleteAsk_;
        }
    });
}

// Список сохранений: строки и названия в полях ввода.
void Menus::syncSaves(Dialog& dialog) {
    const bool dirty = savesDirty_;
    if (savesDirty_) {
        saves_ = world::SaveGame::list();
        savePick_ = std::min(savePick_, int(saves_.size()) - 1);
        savesDirty_ = false;
    }
    dialog.forEach([&](Widget& w) {
        if (hasAction(w, "cva_LS_FileList") && w.listElement &&
            (w.children.size() != saves_.size() || dirty)) {
            std::vector<std::string> names;
            for (const auto& g : saves_) names.push_back(assets::utf8ToCp1251(g.name));
            dialog.setListItems(w, names);
            if (savePick_ >= 0) Dialog::selectListItem(w, savePick_);
        }
    });
}

// Действия сохранения и загрузки; true — действие обработано.
bool Menus::saveClick(const Widget& w, const Widget::Action& a) {
    auto typed = [&](const char* action) {
        std::string name;
        current().forEach([&](Widget& i) {
            if (i.kind == WidgetKind::InputBox && hasAction(i, action)) name = assets::cp1251ToUtf8(i.text);
        });
        return name;
    };
    if (a.type == "cva_LoadInGame_Accept") {
        // Кнопка «Начать» окна сражений грузит сохранение только на вкладке «Загрузить»,
        // в меню игры «Загрузить» грузит всегда.
        const bool battles = currentName() == "SingleBattles";
        if (battles && activeTab_[currentName()] != "Load") return false;
        if (savePick_ >= 0 && std::size_t(savePick_) < saves_.size()) loadRequest_ = saves_[std::size_t(savePick_)];
        return true;
    }
    if (a.type == "cva_SaveInGame_Accept") {
        std::string name = typed("cva_LS_SaveName");
        if (name.empty() && savePick_ >= 0 && std::size_t(savePick_) < saves_.size()) name = saves_[std::size_t(savePick_)].name;
        if (name.empty()) name = "Сохранение";
        saveRequest_ = std::move(name);
        savesDirty_ = true;
        close();
        return true;
    }
    if (a.type == "cva_DeleteSaveFile") {
        if (savePick_ >= 0 && std::size_t(savePick_) < saves_.size()) {
            deskBeforeDelete_ = modalDesk();
            stack_.back().modalDesk = "Delete Save";
        }
        return true;
    }
    if (a.type == "cva_DeleteButton") {
        if (a.node && a.node->value("Delete").value_or("") == "true" && savePick_ >= 0 &&
            std::size_t(savePick_) < saves_.size()) {
            world::SaveGame::remove(saves_[std::size_t(savePick_)].name);
            savePick_ = -1;
            savesDirty_ = true;
            current().forEach([](Widget& i) {
                if (i.kind == WidgetKind::InputBox) i.text.clear();
            });
        }
        stack_.back().modalDesk = deskBeforeDelete_.empty() ? "Main" : deskBeforeDelete_;
        return true;
    }
    (void)w;
    return false;
}

void Menus::restoreStart(const world::SaveGame& g) {
    setupStart_ = g.setup;
    difficulty = g.difficulty;
    if (g.campaign >= 0 && g.mission >= 0 && std::size_t(g.campaign) < campaignDone_.size())
        activeMission_ = {std::size_t(g.campaign), g.mission};
    else
        activeMission_.reset();
    lastStart_ = g.map;
}

// Действия окон профилей; true — действие обработано.
bool Menus::profileClick(const Widget& w, const Widget::Action& a) {
    auto save = [&] { settings.save(core::Settings::defaultPath()); };
    if (a.type == "cva_ProfSel_Accept") {
        if (profilePick_ >= 0 && std::size_t(profilePick_) < settings.profiles.size()) {
            settings.profile = profilePick_;
            profileApplied_ = false;
            save();
        }
        close();
    } else if (a.type == "cva_ProfSel_Add" || a.type == "cva_ProfAdd_Open") {
        if (open("AddProfile")) {
            current().forEach([](Widget& i) {
                if (i.kind == WidgetKind::InputBox) i.text.clear();
            });
            addRace_ = 0;
            addDiff_ = 1;
        }
    } else if (a.type == "cva_ProfSel_Cancel" || a.type == "cva_ProfAdd_Cancel") {
        close();
    } else if (a.type == "cva_ProfDelete") {
        deleteAsk_ = !settings.profiles.empty();
    } else if (a.type == "cva_ProfDel_Cancel") {
        deleteAsk_ = false;
    } else if (a.type == "cva_ProfDel_Accept") {
        if (std::size_t(profilePick_) < settings.profiles.size()) {
            settings.profiles.erase(settings.profiles.begin() + profilePick_);
            if (settings.profile >= int(settings.profiles.size())) settings.profile = int(settings.profiles.size()) - 1;
            else if (settings.profile > profilePick_) --settings.profile;
            profileApplied_ = false;
            save();
        }
        deleteAsk_ = false;
    } else if (a.type == "cva_ProfAdd_Accept") {
        std::string name;
        current().forEach([&](Widget& i) {
            if (i.kind == WidgetKind::InputBox) name = assets::cp1251ToUtf8(i.text);
        });
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name.empty() || settings.profiles.size() >= 32) return true;
        settings.profiles.push_back({name, addRace_, addDiff_});
        settings.profile = profilePick_ = int(settings.profiles.size()) - 1;
        profileApplied_ = false;
        save();
        close();
    } else {
        return false;
    }
    (void)w;
    return true;
}

void Menus::typeText(const std::string& utf8, bool backspace) {
    if (stack_.empty()) return;
    current().forEach([&](Widget& w) {
        if (w.kind != WidgetKind::InputBox || !w.focused) return;
        if (backspace && !w.text.empty()) w.text.pop_back(); // cp1251 — один байт на знак
        for (char c : assets::utf8ToCp1251(utf8))
            if (static_cast<unsigned char>(c) >= 32 && int(w.text.size()) < std::min(w.maxLen, 24)) w.text.push_back(c);
    });
}

bool Menus::hasAction(const Widget& w, const char* type) {
    for (const Widget::Action& a : w.actions)
        if (a.type == type) return true;
    return false;
}

// Победа в миссии кампании открывает следующую.
void Menus::campaignWon() {
    if (!activeMission_) return;
    int& done = campaignDone_[activeMission_->first];
    done = std::max(done, activeMission_->second + 1);
    activeMission_.reset();
    settings.campaignDone = campaignDone_;
    settings.save(core::Settings::defaultPath());
}

// Титры: строки ABLINE0… из Text/credits.txt ползут снизу вверх по окну Credits.
void Menus::syncCredits(Dialog& dialog) {
    using Clock = std::chrono::steady_clock;
    if (creditsStart_ == Clock::time_point{}) creditsStart_ = Clock::now();
    Widget* desk = nullptr;
    dialog.forEach([&](Widget& w) {
        if (!desk && hasAction(w, "cva_ShowAboutText")) desk = &w;
    });
    if (!desk) return;
    if (desk->children.empty()) {
        const Font* plain = ctx_.fonts.find("fonBlow_S");
        const Font* head = ctx_.fonts.find("fonBlow_A");
        for (int n = 0; ctx_.texts.contains("ABLINE" + std::to_string(n)); ++n) {
            std::string line = assets::utf8ToCp1251(ctx_.texts.lookup("ABLINE" + std::to_string(n)));
            const bool heading = !line.empty() && line[0] != ' ';
            Widget t;
            t.kind = WidgetKind::TextButton;
            t.type = "TextButton";
            t.text = std::move(line);
            t.fontActive = t.fontPassive = (heading && head) ? head : plain;
            t.align = Align::Center;
            t.x = 80;
            t.width = 343;
            t.height = 20;
            t.maxWidth = 343;
            desk->children.push_back(std::move(t));
        }
    }
    const double secs = std::chrono::duration<double>(Clock::now() - creditsStart_).count();
    const int spacing = 24, top = 203, bottom = 662;
    const int total = int(desk->children.size()) * spacing + (bottom - top);
    const int shift = int(secs * 35.0) % std::max(total, 1);
    int row = 0;
    for (Widget& t : desk->children) {
        t.y = bottom - shift + row++ * spacing;
        t.visible = t.y >= top && t.y + t.height <= bottom;
    }
}

void Menus::click(Widget& w) {
    if (currentName() == "Credits") { // любое нажатие закрывает титры
        close();
        return;
    }
    if (w.actions.empty() && w.hint == "ABLINE2" && currentName() == "Main") { // кнопка «Авторы»
        creditsStart_ = {};
        open("Credits");
        return;
    }
    if (w.kind == WidgetKind::ComboBox) {
        if (w.open && w.hoverRow >= 0) w.selected = w.hoverRow;
        w.open = !w.open && w.hoverRow < 0 && !w.items.empty();
        current().closeCombos(&w);
        return;
    }
    current().closeCombos();
    if (w.kind == WidgetKind::InputBox) { // фокус ввода
        current().blurInputs(&w);
        w.focused = true;
        return;
    }
    current().blurInputs();
    if (w.kind == WidgetKind::CheckBox) w.checked = !w.checked;
    if (w.type == "TabButton") activeTab_[currentName()] = w.group; // дальше — её действия
    for (const Widget::Action& a : w.actions) {
        if (profileClick(w, a)) return;
        if (saveClick(w, a)) return; // «Загрузить» без выбранной строки ничего не делает
        if (a.type == "cva_BR_PlColor" && w.id >= 0 && w.id < kSlots) {
            Slot& slot = slots[std::size_t(w.id)];
            slot.color = (slot.color + 1) % 8;
        }
        if (a.type == "cva_BR_PlTeam" && w.id >= 0 && w.id < kSlots) {
            Slot& slot = slots[std::size_t(w.id)];
            slot.team = (slot.team + 1) % 8;
        }
        if (a.type == "cva_MM_Start" || a.type == "cva_MM_SinStart" || a.type == "cva_MM_MultiEnter") {
            // FUN_013d83e0. Проверка профиля игрока у cva_MM_SinStart (меню AddProfile)
            // пока пропущена: профилей ещё нет.
            if (!open(a.name)) std::fprintf(stderr, "меню «%s» не найдено\n", a.name.c_str());
            // Первый запуск: сначала создаётся профиль.
            else if (a.type == "cva_MM_SinStart" && settings.profiles.empty()) {
                Widget::Action add;
                add.type = "cva_ProfAdd_Open";
                profileClick(w, add);
            }
            return;
        }
        if (a.type == "va_ListItem") { // FUN_013d4bb0
            if (Widget* list = current().listOf(w)) {
                Dialog::selectListItem(*list, w.listIndex);
                if (hasAction(*list, "cva_LS_FileList")) { // список сохранений
                    savePick_ = w.listIndex;
                    if (savePick_ >= 0 && std::size_t(savePick_) < saves_.size())
                        current().forEach([&](Widget& i) {
                            if (i.kind == WidgetKind::InputBox &&
                                (hasAction(i, "cva_LS_SaveName") || hasAction(i, "cva_LS_LoadName")))
                                i.text = assets::utf8ToCp1251(saves_[std::size_t(savePick_)].name);
                        });
                    continue;
                }
                if (hasAction(*list, "cva_ProfList")) {
                    profilePick_ = std::max(0, w.listIndex);
                    continue;
                }
                if (list->name == "CampList" && !hasAction(*list, "cva_LS_FileList")) {
                    campaignPick_ = std::size_t(std::max(0, w.listIndex));
                    continue;
                }
                if (const std::vector<std::size_t>* items = listFor(*list); items && std::size_t(w.listIndex) < items->size())
                    selectedMission = (*items)[std::size_t(w.listIndex)];
            }
            continue;
        }
        // FUN_013ddfc0 — отмена действий панели, FUN_013e7150 — загрузка сохранения из
        // списка файлов; списка сохранений ещё нет, обе — без последствий.
        if (a.type == "cva_MM_CancelInDesk") continue;
        if (a.type == "cva_MM_SinglStart") {
            // FUN_013d94c0: начать игру на выбранной карте (без выбора оригинал создаёт
            // случайную карту — генератора пока нет). Выбора карты в меню ещё нет:
            // берётся selectedSkirmish из списка SingleMiss.txt.
            if (selectedMission) {
                startRequest_ = gamePath(missions_.missions[*selectedMission].map);
                setupStart_ = true;
            } else
                std::fprintf(stderr, "не выбрана карта (нет Missions/Missions.txt?)\n");
            return;
        }
        if (a.type == "cva_MM_BackToMain") { // FUN_013d8540: конец игры, затем закрыть окно
            backToMain_ = true;
            close();
            return;
        }
        if (a.type == "cva_MM_AcceptInDesk") { // «Принять» панели настроек в меню игры
            syncOptions(false);
            settings.save(core::Settings::defaultPath());
            continue; // дальше — cva_M_ModalDeskSet:Main
        }
        if (a.type == "cva_MM_ReplayCurMiss") { // «Начать миссию заново»
            if (lastStart_) startRequest_ = *lastStart_;
            close();
            return;
        }
        if (a.type == "cva_InGameMenu_MainDesk_Set") { // режим игры для панели меню F10
            if (a.node) mainDesk_ = a.node->value("ID").value_or(mainDesk_);
            continue;
        }
        if (a.type == "cva_MM_Accept" || a.type == "cva_MM_Cancel") { // «Принять» и «Отмена» окна настроек
            if (a.type == "cva_MM_Accept" && currentName() == "Options") {
                syncOptions(false);
                settings.save(core::Settings::defaultPath());
            }
            close();
            return;
        }
        if (a.type == "cva_MM_Close") { // FUN_013d8510
            close();
            return;
        }
        if (a.type == "cva_M_ModalDeskSet") { // FUN_013ecd90
            stack_.back().modalDesk = a.name.empty() ? w.name : a.name;
            continue; // дальше по цепочке: cva_MM_Campaign, cva_Camp_Select
        }
        if (a.type == "cva_MM_Campaign") { // выбор кампании
            if (mainDesk_ == "SinGlobalMap") { // «Битва за Европу»: глобальная карта вместо списка кампаний
                globalMapRequest_ = true;
                continue;
            }
            stack_.back().modalDesk = "CampSelect";
            continue;
        }
        if (a.type == "cva_Camp_Select") { // «Выбрать» в списке кампаний
            if (campaignPick_ < campaigns_.campaigns.size()) campaign_ = campaignPick_;
            continue;
        }
        if (a.type == "cva_Mission") { // значок миссии на карте кампании
            if (campaign_ && a.node) {
                const int index = int(a.node->intValue("Index").value_or(0));
                const auto& ms = campaigns_.campaigns[*campaign_].missions;
                if (index >= 0 && std::size_t(index) < ms.size() && index <= campaignDone_[*campaign_] &&
                    !ms[std::size_t(index)].map.empty()) {
                    activeMission_ = {*campaign_, index};
                    startRequest_ = gamePath(ms[std::size_t(index)].map);
                    setupStart_ = false;
                }
            }
            return;
        }
        if (a.type == "cva_M_CrossFade" || a.type == "cva_vGameMode_Set" ||
            a.type == "cva_SPD_CampMessageCheck" || a.type == "cva_Exit2MainMenu" || a.type == "cva_ogg_Stop" || a.type == "cva_DemoDisable" || a.type == "cva_ProfLoad" || a.type == "cva_ProfAdd_Name" ||
            a.type == "cva_ClearClick" || a.type.rfind("cva_Opt_", 0) == 0)
            continue; // оформление и профиль — без последствий
        std::printf("действие %s (%s) пока не реализовано\n", a.type.c_str(), a.name.c_str());
    }
}

// Окно «Настройки»: ползунки и флажки опознаются по их действиям cva_Opt_*.
void Menus::syncOptions(bool toWidgets) {
    auto pct = [&](Widget& w, int& v) {
        if (toWidgets) w.sliderPos = v * w.sliderMax / 100;
        else v = w.sliderPos * 100 / w.sliderMax;
    };
    auto flag = [&](Widget& w, bool& v) {
        if (toWidgets) w.checked = v;
        else v = w.checked;
    };
    current().forEach([&](Widget& w) {
        for (const Widget::Action& a : w.actions) {
            if (a.type == "cva_Opt_SoundVolume") pct(w, settings.soundVolume);
            else if (a.type == "cva_Opt_MusicVolume") pct(w, settings.musicVolume);
            else if (a.type == "cva_Opt_ScrollSpeed") pct(w, settings.scrollSpeed);
            else if (a.type == "cva_Opt_MusicPlayMode") flag(w, settings.musicOn);
            else if (a.type == "cva_Opt_ArcadeMode") flag(w, settings.arcade);
        }
        if (w.name == "ShowHint") flag(w, settings.showHint);
        if (w.name == "ShowVideo") flag(w, settings.showVideo);
    });
}

void Menus::dragSlider(Widget& w, float vx) { w.sliderPos = Dialog::sliderPosAt(w, vx); }

const std::vector<std::size_t>* Menus::listFor(const Widget& list) const {
    if (!list.listElement) return nullptr;
    for (const Widget::Action& a : list.actions) {
        if (a.type == "va_LD_Skirmish") return &skirmish_;
        if (a.type == "va_LD_Battle") return &battles_;
    }
    return nullptr;
}

} // namespace oc2::ui
