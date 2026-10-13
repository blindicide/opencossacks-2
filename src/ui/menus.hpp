#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

    // Карты одиночных сражений из Missions\SingleMiss.txt: ключи «#Skirmish18» →
    // Missions/Skirmish/Skirmish18.m3d, в порядке файла.
    const std::vector<std::string>& skirmishMaps() const { return skirmishMaps_; }
    std::size_t selectedSkirmish = 0;

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
    std::vector<std::string> skirmishMaps_;
    std::optional<std::string> startRequest_;
};

} // namespace oc2::ui
