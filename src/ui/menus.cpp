#include "ui/menus.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "assets/game_paths.hpp"

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
    current().forEach([&](Widget& w) {
        for (const Widget::Action& a : w.actions) {
            // FUN_013eccd0: панель видна, только когда открыта модальная панель с её именем.
            if (a.type == "cva_M_ModalDesk") w.visible = w.name == modalDesk();
        }
    });
}

void Menus::click(Widget& w) {
    for (const Widget::Action& a : w.actions) {
        if (a.type == "cva_MM_Start" || a.type == "cva_MM_SinStart" || a.type == "cva_MM_MultiEnter") {
            // FUN_013d83e0. Проверка профиля игрока у cva_MM_SinStart (меню AddProfile)
            // пока пропущена: профилей ещё нет.
            if (!open(a.name)) std::fprintf(stderr, "меню «%s» не найдено\n", a.name.c_str());
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

} // namespace oc2::ui
