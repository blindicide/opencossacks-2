#pragma once

#include <array>
#include <map>
#include <memory>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "assets/campaign_list.hpp"
#include "world/savegame.hpp"
#include "assets/mission_list.hpp"
#include "core/settings.hpp"
#include "ui/dialogs.hpp"

namespace oc2::ui {

// Меню игры: окна по именам из Dialogs\MainMenu.xml («Main», «Single», …),
// открытые стопкой, как вложенные циклы FUN_014a4a20 оригинала.
class Menus {
public:
    explicit Menus(UiContext& ctx);

    // Добавляет окно с именем name из файла path (относительно данных).
    void addFile(const std::string& name, const std::string& path) { files_[name] = path; }
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

    // Настройки игрока (окно Options); применяет main.cpp каждый кадр.
    core::Settings settings;
    // Ползунок под курсором: нажатие и перетаскивание (vx — виртуальная x курсора).
    void dragSlider(Widget& w, float vx);

    // Имя открытой «модальной панели» (cva_M_ModalDeskSet / cva_M_ModalDesk).
    // Своя у каждого открытого меню: при открытии — «Main», при возврате — прежняя.
    const std::string& modalDesk() const { return stack_.back().modalDesk; }

    // Миссии (Missions\Missions.txt) и выборки для списков меню сражений:
    // va_LD_Skirmish — SingleMiss.txt, va_LD_Battle — SingleBatl.txt.
    const assets::MissionList& missions() const { return missions_; }
    // Выбранная миссия (номер в missions()); по умолчанию — первая из SingleMiss.txt.
    std::optional<std::size_t> selectedMission;

    // Итог игры для окна EndGameMessage (cva_VI_EndGame): true — победа.
    // messageId — текст итога из сценария миссии (ShowVictory / LooseGame); пусто — стандартный.
    void setGameResult(bool victory, std::string messageId = {}) {
        gameResult_ = victory;
        resultMessage_ = std::move(messageId);
        if (victory) campaignWon();
        resultShown_ = false;
    }
    // Итоговая статистика (окно PostGameStat): два столбца — победитель и проигравший.
    struct GameStats {
        struct Row {
            std::string name; // UTF-8
            int built = 0, killed = 0, lost = 0;
        };
        struct Side {
            std::string name, race; // UTF-8
            // Траты по строкам таблицы (всего, улучшения, содержание, юниты, здания, покупка, продажа) × ресурсы
            // в порядке колонок окна: дерево, еда, камень, золото, железо, уголь.
            std::array<std::array<int, 6>, 7> table{};
            std::vector<Row> units, buildings;
        };
        Side winner, loser;
    };
    void setGameStats(GameStats stats) {
        stats_ = std::move(stats);
        statsShown_ = false;
    }
    // cva_MM_BackToMain: вернуться в главное меню (игра заканчивается). Возвращается один раз.
    bool takeBackToMain() {
        const bool r = backToMain_;
        backToMain_ = false;
        return r;
    }

    // Запрос начать игру (cva_MM_SinglStart): путь карты относительно данных.
    // Возвращается один раз.
    std::optional<std::string> takeStartRequest() {
        auto r = std::move(startRequest_);
        startRequest_.reset();
        if (r) lastStart_ = r;
        return r;
    }

    // Запрос сохранить игру (cva_SaveInGame_Accept): имя сохранения (UTF-8). Возвращается один раз.
    std::optional<std::string> takeSaveRequest() {
        auto r = std::move(saveRequest_);
        saveRequest_.reset();
        return r;
    }
    // Запрос загрузить игру (cva_LoadInGame_Accept): выбранное сохранение. Возвращается один раз.
    // «Начать кампанию» в «Битве за Европу»: открыть глобальную карту.
    bool takeGlobalMapRequest() {
        const bool r = globalMapRequest_;
        globalMapRequest_ = false;
        return r;
    }
    std::optional<world::SaveGame> takeLoadRequest() {
        auto r = std::move(loadRequest_);
        loadRequest_.reset();
        return r;
    }
    // Миссия кампании, запущенная из меню: (кампания, номер миссии) — для записи в сохранение.
    std::optional<std::pair<std::size_t, int>> activeMission() const { return activeMission_; }
    // Игра загружена из сохранения: победа в ней по-прежнему открывает следующую миссию кампании.
    void restoreStart(const world::SaveGame& g);

    // Игроки стычки (таблица cva_BR_*): строка 0 — человек, 1..6 — компьютер или «нет игрока».
    struct Slot {
        int nation = 0;   // 0 — по карте, иначе номер в kNations
        int team = 0;     // 0 — как на карте, иначе 1..7
        int color = 0;
        bool active = true;
    };
    static constexpr int kSlots = 7;
    static constexpr int kNationCount = 9;
    // Коды наций (Nations.lst) в порядке пунктов списка «Нация» после «По карте».
    static const char* nationCode(int index);
    // Название нации по номеру 0..kNationCount-1 (UTF-8).
    static const char* nationName(int index);
    // Ввод с клавиатуры в InputBox в фокусе: текст UTF-8, возврат на шаг назад.
    void typeText(const std::string& utf8, bool backspace);
    // Номер игрока в таблице → имя профиля (UTF-8) или пусто.
    std::string profileName() const { return settings.currentProfile() ? settings.currentProfile()->name : std::string(); }
    std::array<Slot, kSlots> slots;
    // Сложность миссий 0..2 (cva_CampDiff, cva_ProfAdd_Diff); игра берёт её при запуске.
    int difficulty = 1;
    // Последний запуск — стычка из меню (применять таблицу игроков), а не миссия кампании.
    bool setupStart() const { return setupStart_; }

private:
    void syncPlayerTable(Dialog& dialog);
    void syncProfiles(Dialog& dialog);
    void syncSaves(Dialog& dialog);
    void syncStats(Dialog& dialog);
    void syncCredits(Dialog& dialog);
    bool saveClick(const Widget& w, const Widget::Action& a);
    bool profileClick(const Widget& w, const Widget::Action& a);
    std::string resultMessage_;
    UiContext& ctx_;
    std::map<std::string, std::string> files_; // имя меню → путь к XML
    std::map<std::string, std::unique_ptr<Dialog>> loaded_;
    struct Open {
        std::string name;
        std::string modalDesk;
    };
    std::vector<Open> stack_;
    const std::vector<std::size_t>* listFor(const Widget& list) const;
    void campaignWon();
    void syncOptions(bool toWidgets);
    static bool hasAction(const Widget& w, const char* type);

    assets::MissionList missions_;
    std::vector<std::size_t> skirmish_, battles_;
    std::optional<std::size_t> shownDescription_, shownPreview_; // что уже показано в панелях
    std::map<std::size_t, render::TextureId> previews_;          // мини-карты VIE2 по миссиям
    std::map<std::string, std::string> activeTab_; // меню → группа нажатой вкладки
    // Кампании (Dialogs\campaign.xml): выбранная в списке и открытая, пройденные миссии по кампаниям,
    // запущенная из меню миссия. Прогресс — в памяти (сохранения профиля ещё нет).
    assets::CampaignList campaigns_;
    std::size_t campaignPick_ = 0;
    std::optional<std::size_t> campaign_;
    std::vector<int> campaignDone_;
    std::optional<std::pair<std::size_t, int>> activeMission_;
    std::optional<std::string> startRequest_;
    std::chrono::steady_clock::time_point creditsStart_{}; // начало прокрутки титров
    std::string loadingTip_; // совет на экране загрузки (cp1251)
    // Сохранения (окно «Загрузить», меню игры F10): список файлов, выбранная строка, запросы для main.cpp.
    std::vector<world::SaveGame> saves_;
    int savePick_ = -1;
    bool savesDirty_ = true;
    std::string deskBeforeDelete_;
    std::optional<std::string> saveRequest_;
    std::optional<world::SaveGame> loadRequest_;
    bool globalMapRequest_ = false;
    std::optional<bool> gameResult_;
    bool resultShown_ = false;
    std::optional<GameStats> stats_;
    bool statsShown_ = false;
    bool backToMain_ = false;
    bool setupStart_ = false;
    int profilePick_ = 0;                    // выбранная строка списка профилей
    bool deleteAsk_ = false;                 // открыто подтверждение удаления профиля
    bool profileApplied_ = false;            // сложность взята из профиля
    int addRace_ = 0, addDiff_ = 1;          // поля окна «Новый профиль»
    int animQuality_ = 2, arcadeMode_ = 0, autosave_ = 0; // прочие выпадающие списки окон
    bool optionsShown_ = false;
    std::string mainDesk_ = "SinBattles"; // вариант панели «Main» меню игры
    std::optional<std::string> lastStart_; // последняя начатая карта (cva_MM_ReplayCurMiss)
};

} // namespace oc2::ui
