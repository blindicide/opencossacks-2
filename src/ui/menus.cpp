#include "ui/menus.hpp"

#include <cstdio>
#include <fstream>
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
    return true;
}

void Menus::close() {
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
    dialog.forEach([&](Widget& w) {
        if (w.type == "TabDesk") w.visible = w.group == tab;
        if (w.type == "TabButton") w.state = w.group == tab ? 1 : 0;
    });
    dialog.forEach([&](Widget& w) {
        for (const Widget::Action& a : w.actions) {
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
                if (Widget* m = ref("dMessage")) m->text = text(vic ? "VicMessage" : "DefMessage");
                if (Widget* b = ref("dButton")) b->text = text(vic ? "VicButton" : "DefButton");
                if (Widget* p = ref("dPicture"); p && p->kind == WidgetKind::GPPicture) {
                    p->gp = ctx_.images.id(a.node->value("gpFile").value_or(""));
                    p->hasGp = true;
                    p->sprite = int(a.node->intValue(vic ? "VicSprite" : "DefSprite").value_or(0));
                }
            }
            if (a.type == "cva_M_ModalDesk") w.visible = w.name == modalDesk();
            // FUN_013d8a00 (упрощённо): «Начать» доступна, если есть карта; проверки
            // состава игроков и команд оригинала ещё не перенесены.
            if (a.type == "cva_MM_SinglStart") w.enabled = selectedMission.has_value();
        }
    });
}

void Menus::click(Widget& w) {
    if (w.type == "TabButton") activeTab_[currentName()] = w.group; // дальше — её действия
    for (const Widget::Action& a : w.actions) {
        if (a.type == "cva_MM_Start" || a.type == "cva_MM_SinStart" || a.type == "cva_MM_MultiEnter") {
            // FUN_013d83e0. Проверка профиля игрока у cva_MM_SinStart (меню AddProfile)
            // пока пропущена: профилей ещё нет.
            if (!open(a.name)) std::fprintf(stderr, "меню «%s» не найдено\n", a.name.c_str());
            return;
        }
        if (a.type == "va_ListItem") { // FUN_013d4bb0
            if (Widget* list = current().listOf(w)) {
                Dialog::selectListItem(*list, w.listIndex);
                if (const std::vector<std::size_t>* items = listFor(*list); items && std::size_t(w.listIndex) < items->size())
                    selectedMission = (*items)[std::size_t(w.listIndex)];
            }
            continue;
        }
        // FUN_013ddfc0 — отмена действий панели, FUN_013e7150 — загрузка сохранения из
        // списка файлов; списка сохранений ещё нет, обе — без последствий.
        if (a.type == "cva_MM_CancelInDesk" || a.type == "cva_LoadInGame_Accept") continue;
        if (a.type == "cva_MM_SinglStart") {
            // FUN_013d94c0: начать игру на выбранной карте (без выбора оригинал создаёт
            // случайную карту — генератора пока нет). Выбора карты в меню ещё нет:
            // берётся selectedSkirmish из списка SingleMiss.txt.
            if (selectedMission)
                startRequest_ = gamePath(missions_.missions[*selectedMission].map);
            else
                std::fprintf(stderr, "не выбрана карта (нет Missions/Missions.txt?)\n");
            return;
        }
        if (a.type == "cva_MM_BackToMain") { // FUN_013d8540: конец игры, затем закрыть окно
            backToMain_ = true;
            close();
            return;
        }
        if (a.type == "cva_MM_Close") { // FUN_013d8510
            close();
            return;
        }
        if (a.type == "cva_M_ModalDeskSet") { // FUN_013ecd90
            stack_.back().modalDesk = a.name.empty() ? w.name : a.name;
            return;
        }
        std::printf("действие %s (%s) пока не реализовано\n", a.type.c_str(), a.name.c_str());
    }
}

const std::vector<std::size_t>* Menus::listFor(const Widget& list) const {
    if (!list.listElement) return nullptr;
    for (const Widget::Action& a : list.actions) {
        if (a.type == "va_LD_Skirmish") return &skirmish_;
        if (a.type == "va_LD_Battle") return &battles_;
    }
    return nullptr;
}

} // namespace oc2::ui
