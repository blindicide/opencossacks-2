// Сетевая игра в меню: окна «Сетевая игра» (Multi) и «Комната сетевой игры» (MultiRoom).
#include <algorithm>

#include "assets/text_encoding.hpp"
#include "ui/menus.hpp"

namespace oc2::ui {

namespace {

constexpr std::size_t kChatRows = 7; // строк чата в окне сообщений

std::string gamePath(std::string name) {
    for (char& c : name)
        if (c == '\\') c = '/';
    return name;
}

} // namespace

bool Menus::inNetRoom() const {
    return !stack_.empty() && currentName() == "MultiRoom" && net.active();
}

// Строка статуса под заголовком окна «Сетевая игра»: образец — подпись «Имя игрока».
void Menus::syncNetMulti(Dialog& dialog) {
    auto cp = [](const std::string& utf8) { return assets::utf8ToCp1251(utf8); };
    Widget* status = dialog.find("NetStatus");
    if (!status) {
        const Widget* label = nullptr;
        dialog.forEach([&](Widget& w) {
            if (!label && w.kind == WidgetKind::TextButton && !w.text.empty() && w.fontPassive) label = &w;
        });
        if (!label) return;
        Widget s = *label;
        s.name = "NetStatus";
        s.actions.clear();
        s.x = 160, s.y = 400, s.width = 700, s.height = 40;
        s.maxWidth = 700;
        dialog.root().children.push_back(std::move(s));
        status = dialog.find("NetStatus");
    }
    dialog.forEach([&](Widget& w) {
        if (w.kind != WidgetKind::InputBox) return;
        if (hasAction(w, "cva_MU_NickInput") && !nickInit_) { // имя игрока: профиль или «Игрок»
            nickInit_ = true;
            w.text = cp(profileName().empty() ? "Игрок" : profileName());
        }
    });
    if (!netError_.empty()) status->text = cp(netError_);
    else status->text = cp("Ваш адрес в сети: " + (net::localAddress().empty() ? std::string("не найден") : net::localAddress()));
}

// Окно комнаты: таблица игроков из комнаты сессии, чат, кнопка «Начать» только у хозяина.
void Menus::syncNetRoom(Dialog& dialog) {
    auto cp = [](const std::string& utf8) { return assets::utf8ToCp1251(utf8); };
    net.pump();
    if (net.lost()) {
        netError_ = net.lostReason();
        net.leave();
        close();
        return;
    }
    const bool host = net.role() == net::Session::Role::Host;
    if (!host && net.takeStarted()) {
        difficulty = net.room.difficulty;
        startNetGame();
        return;
    }
    for (std::string& line : net.takeChat()) {
        chatLines_.push_back(std::move(line));
        chatDirty_ = true;
    }
    net.takeRoomChanged();
    // Карта: хозяин выбирает из списка, гость видит выбор хозяина.
    if (host) {
        if (selectedMission) {
            const std::string map = gamePath(missions_.missions[*selectedMission].map);
            if (net.room.map != map) {
                net.room.map = map;
                net.publishRoom();
            }
        }
    } else {
        for (std::size_t i = 0; i < missions_.missions.size(); ++i)
            if (gamePath(missions_.missions[i].map) == net.room.map) selectedMission = i;
    }
    // Таблица игроков.
    auto edit = [](Widget& w, int& value, bool editable) { // изменение пользователем попадает в value
        bool changed = false;
        if (w.selected != w.syncedSel && editable) value = w.selected, changed = true;
        else w.selected = value;
        w.syncedSel = w.selected;
        return changed;
    };
    bool changedRoom = false;
    dialog.forEach([&](Widget& w) {
        for (const Widget::Action& a : w.actions) {
            if (hasAction(w, "cva_BR_Ready")) w.visible = false;
            if (w.id < 0 || w.id >= kSlots) continue;
            net::Session::Slot& s = net.room.slots[std::size_t(w.id)];
            if (a.type == "cva_BR_PlName") {
                if (w.kind == WidgetKind::ComboBox) {
                    if (s.kind == 2) {
                        w.items = {cp(s.name)};
                        w.selected = w.syncedSel = 0;
                    } else {
                        w.items = {cp("Компьютер"), cp("Нет игрока")};
                        int v = s.kind == 1 ? 0 : 1;
                        if (edit(w, v, host)) {
                            s.kind = v == 0 ? 1 : 0;
                            changedRoom = true;
                        }
                    }
                } else {
                    w.text = cp(s.name);
                }
            } else if (a.type == "cva_BR_PlRace" && w.kind == WidgetKind::ComboBox) {
                if (w.items.empty()) {
                    w.items.push_back(cp("По карте"));
                    for (int n = 0; n < kNationCount; ++n) w.items.push_back(cp(nationName(n)));
                }
                int v = s.nation;
                if (edit(w, v, w.id == net.mySlot || (host && s.kind == 1))) {
                    s.nation = v;
                    changedRoom = true;
                    if (w.id == net.mySlot && !host) net.setMine(s.nation, s.team, s.color);
                }
                w.visible = s.kind != 0;
            } else if (a.type == "cva_BR_PlColor") {
                w.sprite = s.color;
                w.visible = s.kind != 0;
            } else if (a.type == "cva_BR_PlTeam") {
                w.text = s.team ? std::to_string(s.team) : cp("—");
                w.visible = s.kind != 0;
            } else if (a.type == "cva_BR_PlMapLoading") {
                w.text.clear();
            }
        }
        // Кнопка «Начать» — только хозяину.
        if (hasAction(w, "cva_MM_MultiStart")) {
            w.visible = host;
            w.enabled = host && selectedMission.has_value();
        }
        // Чат: последние строки, строка ввода с ограничением.
        if (hasAction(w, "cva_BR_ChatDesk") && w.listElement && chatDirty_) {
            std::vector<std::string> rows;
            for (std::size_t i = chatLines_.size() > kChatRows ? chatLines_.size() - kChatRows : 0; i < chatLines_.size(); ++i)
                rows.push_back(cp(chatLines_[i]));
            dialog.setListItems(w, rows);
        }
    });
    chatDirty_ = false;
    if (changedRoom && host) net.publishRoom();
}

void Menus::startNetGame() {
    for (int i = 0; i < kSlots; ++i) {
        const net::Session::Slot& s = net.room.slots[std::size_t(i)];
        slots[std::size_t(i)] = {s.nation, s.team, s.color, s.kind != 0};
    }
    startRequest_ = net.room.map;
    setupStart_ = true;
}

// Ввод в строку чата: Enter отправляет сообщение.
void Menus::enterPressed() {
    if (!inNetRoom()) return;
    current().forEach([&](Widget& w) {
        if (w.kind != WidgetKind::InputBox || !hasAction(w, "cva_BR_ChatInput") || w.text.empty()) return;
        net.chat(assets::cp1251ToUtf8(w.text));
        w.text.clear();
        w.focused = true;
    });
}

// Нажатия окон сети. true — действие обработано; stop — дальше по цепочке действий кнопки не идти.
bool Menus::netClick(const Widget& w, const Widget::Action& a, bool& stop) {
    auto field = [&](const char* action) {
        std::string text;
        current().forEach([&](Widget& i) {
            if (i.kind == WidgetKind::InputBox && hasAction(i, action)) text = assets::cp1251ToUtf8(i.text);
        });
        return text;
    };
    if (a.type == "cva_MM_MultiCreate" || a.type == "cva_MM_MultiJoin") {
        std::string name = field("cva_MU_NickInput"), error;
        if (name.empty()) name = "Игрок";
        netError_.clear();
        const bool ok = a.type == "cva_MM_MultiCreate"
                            ? net.host(name, net::Session::kPort, error)
                            : field("cva_Multi_ManualIPServer").empty()
                                  ? (error = "Введите IP-адрес сервера", false)
                                  : net.join(name, field("cva_Multi_ManualIPServer"), net::Session::kPort, error);
        if (!ok) {
            netError_ = error;
            stop = true;
            return true;
        }
        chatLines_.clear();
        chatDirty_ = true;
        if (a.type == "cva_MM_MultiCreate") {
            if (selectedMission) net.room.map = gamePath(missions_.missions[*selectedMission].map);
            net.room.slots[0].nation = slots[0].nation;
            net.room.slots[0].team = slots[0].team;
        }
        open(w.name.empty() ? "MultiRoom" : w.name);
        stop = true;
        return true;
    }
    if (a.type == "cva_MM_MultiBack") {
        net.leave();
        netError_.clear();
        return true;
    }
    if (a.type == "cva_M_MultRoom_Back") {
        net.leave();
        return true;
    }
    if (a.type == "cva_MM_MultiStart") {
        if (!inNetRoom() || net.role() != net::Session::Role::Host || !selectedMission) {
            stop = true;
            return true;
        }
        net.room.map = gamePath(missions_.missions[*selectedMission].map);
        net.room.difficulty = difficulty;
        net.start();
        startNetGame();
        stop = true;
        return true;
    }
    if (a.type == "cva_BR_InetGo2Chat" && inNetRoom()) {
        current().forEach([&](Widget& i) {
            if (i.kind == WidgetKind::InputBox && hasAction(i, "cva_BR_ChatInput")) i.focused = true;
        });
        return true;
    }
    if (inNetRoom() && (a.type == "cva_BR_PlColor" || a.type == "cva_BR_PlTeam") && w.id >= 0 && w.id < kSlots) {
        net::Session::Slot& s = net.room.slots[std::size_t(w.id)];
        const bool host = net.role() == net::Session::Role::Host;
        if (w.id == net.mySlot || (host && s.kind == 1)) {
            if (a.type == "cva_BR_PlColor") s.color = (s.color + 1) % 8;
            else s.team = (s.team + 1) % 8;
            if (w.id == net.mySlot) net.setMine(s.nation, s.team, s.color);
            else net.publishRoom();
        }
        return true;
    }
    return false;
}

} // namespace oc2::ui
