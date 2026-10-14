#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/image.hpp"
#include "ui/dialogs.hpp"
#include "world/bigmap.hpp"

namespace oc2::ui {

// Экран «Завоевания Европы»: выбор нации, карта из 34 секторов, армия, найм, ходы.
// В оригинале он рисуется кодом engine.exe, не XML-окнами, поэтому здесь тоже собран кодом
// из картинок Interf3\TotalWarGraph и простых панелей.
class BigMapScreen {
public:
    struct Input {
        float mx = -1.0f, my = -1.0f; // виртуальные координаты 1024×768
        bool click = false;
        bool esc = false;
        bool enter = false;
    };

    // Бой за сектор на его карте: игрок нападает или обороняется.
    struct BattleRequest {
        world::BigGame::Attack attack;
        bool playerAttacks = true;
    };

    BigMapScreen(UiContext& ctx, std::shared_ptr<const world::BigGame::Data> data, std::filesystem::path savePath);

    // Загрузка данных игры; nullptr, если нет Sectors.dat и других файлов.
    static std::shared_ptr<const world::BigGame::Data> loadData(const std::filesystem::path& dataRoot);

    // Один кадр: ввод, затем рисование.
    void frame(const View& v, const Input& in);
    // Игрок вышел с экрана.
    bool done() const { return done_; }
    // Нация уже выбрана (для проверок).
    void start(int nation);

    std::optional<BattleRequest> takeBattle();
    // Итог боя на карте сектора: применяется к кампании и идёт дальше по ходу.
    void battleFinished(const world::BigGame::Outcome& outcome);
    // Сектор, где идёт бой на карте.
    const assets::BigSector* battleSector() const;
    // Карта боя за сектор нападения (путь внутри каталога игры).
    std::string battleMap() const;
    // Гарнизон сектора для боя.
    std::vector<std::string> defendersOf(int target) const { return game_ ? game_->defenders(target) : std::vector<std::string>{}; }
    bool inBattle() const { return battleWait_; }
    // Сторона карты сектора, откуда идёт нападение (BigDirections), -1 — неизвестна.
    int attackSide(const world::BigGame::Attack& a) const { return game_ ? game_->data().directions.sideOf(a.from, a.target) : -1; }

    // Для проверок: выбрать сектор.
    void select(int sector) { selected_ = sector; }

private:
    enum class Modal { None, Attack, Defend, Events, Offer, Recruit, GameOver, Quit, Hero, Diversion, Market, Diplomacy };

    struct Rect {
        float x, y, w, h;
    };

    // Рисование (виртуальные координаты → пиксели).
    void fill(const Rect& r, std::uint32_t argb);
    void frameRect(const Rect& r, std::uint32_t argb);
    void panel(const Rect& r);
    int say(const std::string& utf8, float x, float y, int width = 2000, Align align = Align::Left, const char* font = "M");
    bool button(const Rect& r, const std::string& label, bool enabled = true);
    bool hit(const Rect& r) const { return mx_ >= r.x && my_ >= r.y && mx_ < r.x + r.w && my_ < r.y + r.h; }

    void drawPick();
    void drawMap();
    void drawSidePanel();
    void drawSectorStrip();
    void drawModal();
    int sectorAt(float vx, float vy) const;
    Rect sectorFrame(int id) const;

    // Тексты Text\BigMapData.txt записаны с «#» в начале ключа; часть — без него (ResName…).
    std::string t(const std::string& key) const {
        return ctx_.texts.contains("#" + key) ? ctx_.texts.lookup("#" + key) : ctx_.texts.lookup(key);
    }
    // Название сектора (город) из BigMapData.txt: «SectorNN → Брюссель».
    std::string sectorName(int id) const {
        const std::string& key = data_->map.sectors[std::size_t(id)].name;
        return ctx_.texts.contains(key) ? ctx_.texts.lookup(key) : key;
    }
    std::string localize(std::string text) const;
    // Текст CQM_… с подставленными аргументами (%s, %d) и цветами.
    std::string format(const world::BigGame::Message& m) const;
    std::string resName(int res) const;
    std::string squadLabel(const std::string& type) const;
    std::string costLabel(const world::BigGame::Cost& c) const;

    void openEvents();
    void afterTurnStep(std::optional<world::BigGame::Attack> attack);
    void endTurn();
    void resolveDefence();
    void autosave();
    void checkOver();

    UiContext& ctx_;
    std::shared_ptr<const world::BigGame::Data> data_;
    std::filesystem::path savePath_;
    std::optional<world::BigGame> game_;
    const View* v_ = nullptr;
    float mx_ = -1.0f, my_ = -1.0f;
    bool click_ = false;

    render::GpId europe_ = 0, sectorsImg_ = 0, defenceImg_ = 0, flagsImg_ = 0;
    std::vector<assets::Image> masks_; // кадры секторов для выбора по точке
    bool maskLoaded_ = false;

    int selected_ = -1;
    int hover_ = -1;
    Modal modal_ = Modal::None;
    std::string dipLog_; // последний ответ в окне дипломатии
    int dipNation_ = -1, dipPending_ = -1; // дипломатия: выбранная нация и действие, ждущее подтверждения (0 война … 4 альянс)
    int marketFrom_ = 0, marketTo_ = 1; // рынок: что отдаём и что получаем (номера ресурсов мира)
    std::optional<world::BigGame::Attack> pending_; // нападение, о котором спрашивает окно
    bool battleWait_ = false;                       // бой идёт на карте сектора
    bool battlePlayerAttacks_ = true;
    std::optional<BattleRequest> battleOut_;
    std::vector<std::string> events_;
    std::string status_;
    int statusFrames_ = 0;
    int frameNo_ = 0;
    int overResult_ = 0;
    bool done_ = false;
    bool saveExists_ = false;
    int hero_ = 0; // какой из десяти героев нации показан в окне «Герои»
};

} // namespace oc2::ui
