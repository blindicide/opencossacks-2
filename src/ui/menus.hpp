#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/mission_list.hpp"
#include "ui/dialogs.hpp"

namespace oc2::ui {

// Меню игры: окна по именам из Dialogs\MainMenu.xml («Main», «Single», …),
// открытые стопкой, как вложенные циклы FUN_014a4a20 оригинала.
class Menus {
public:
    explicit Menus(UiContext& ctx);

    // Открывает меню поверх текущего; false, если такого имени нет.
    bool open(const std::string& name);
    // Закрывает текущее меню (cva_MM_Close). Пустая стопка — выход из игры.
    void close();
    bool empty() const { return stack_.empty(); }
    Dialog& current();
    const std::string& currentName() const { return stack_.back().name; }

    // Покадровые действия (SetFrameState) текущего меню.
    void update();
    // Нажатие левой кнопкой мыши на элемент текущего меню (LeftClick действий).
    void click(Widget& w);

    // Имя открытой «модальной панели» (cva_M_ModalDeskSet / cva_M_ModalDesk).
    // Своя у каждого открытого меню: при открытии — «Main», при возврате — прежняя.
    const std::string& modalDesk() const { return stack_.back().modalDesk; }

    // Миссии (Missions\Missions.txt) и выборки для списков меню сражений:
    // va_LD_Skirmish — SingleMiss.txt, va_LD_Battle — SingleBatl.txt.
    const assets::MissionList& missions() const { return missions_; }
    // Выбранная миссия (номер в missions()); по умолчанию — первая из SingleMiss.txt.
    std::optional<std::size_t> selectedMission;

    // Запрос начать игру (cva_MM_SinglStart): путь карты относительно данных.
    // Возвращается один раз.
    std::optional<std::string> takeStartRequest() {
        auto r = std::move(startRequest_);
        startRequest_.reset();
        return r;
    }

private:
    UiContext& ctx_;
    std::map<std::string, std::string> files_; // имя меню → путь к XML
    std::map<std::string, std::unique_ptr<Dialog>> loaded_;
    struct Open {
        std::string name;
        std::string modalDesk;
    };
    std::vector<Open> stack_;
    const std::vector<std::size_t>* listFor(const Widget& list) const;

    assets::MissionList missions_;
    std::vector<std::size_t> skirmish_, battles_;
    std::optional<std::size_t> shownDescription_, shownPreview_; // что уже показано в панелях
    std::map<std::size_t, render::TextureId> previews_;          // мини-карты VIE2 по миссиям
    std::map<std::string, std::string> activeTab_; // меню → группа нажатой вкладки
    std::optional<std::string> startRequest_;
};

} // namespace oc2::ui
