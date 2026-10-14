#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/map3d.hpp"
#include "assets/xml_tree.hpp"
#include "world/settlements.hpp"
#include "world/unit_catalog.hpp"
#include "world/world.hpp"

namespace oc2::world {

// Сценарий миссии: XML внутри карты (раздел SXML, корень lvCMainScript и lvCFilm) и зоны (ZON2).
// Правило lvCCondForOper срабатывает каждый шаг, пока все его Condition истинны, и выполняет
// Operation по порядку; MAIN_INIT идёт один раз. Фильм (lvCFilm) — сценка: StartCondition →
// PrepareOperation → правила STEP_CON_OPR, пока не истинно StopCondition → DestroyOperation.
// Триггеры — одноразовые флаги: первым действием правило обычно сбрасывает собственный.
// Семантика прочитана по скриптам миссий, кода оригинала (lua-генератор) не переносим:
//   время таймеров (RunTimer Time) — шаги по 40 мс — своё допущение;
//   показ меню диалога включается сразу, без плавного появления (AppearTime);
//   ИИ запускает StartAIEx — файл AI\*.ai.xml заменён сценарием нации игрока (как без скрипта).
class MissionScript {
public:
    struct Env {
        World* world = nullptr;
        UnitCatalog* catalog = nullptr;
        Settlements* settlements = nullptr;
        // Номер юнита карты (UNI3) → номер юнита в мире (nullopt — тип не найден).
        std::vector<std::optional<std::size_t>> mapUnits;
        int difficulty = 1; // 0..2
        // Длительность озвучки в шагах по имени файла; нет — 0.
        std::function<int(const std::string&)> voiceSteps;
    };

    // nullptr, если у карты нет скрипта или в нём нет правил и фильмов.
    static std::unique_ptr<MissionScript> create(const assets::Map3d& map, Env env);

    // То же из готовых XML скрипта и зон (для проверок).
    static std::unique_ptr<MissionScript> fromXml(const std::string& xml, const std::string& zonesXml, Env env);

    // Один шаг: MAIN_INIT (в первый раз), правила, фильмы.
    void step();

    // Игрок нажал «пропустить» диалог (CheckButton).
    void skipDialog() { skip_ = true; }

    struct Ending {
        bool victory = false;
        std::string textId;
    };
    std::optional<Ending> ending;

    struct AiStart {
        int player = 0, difficulty = 0;
        std::string file;
    };
    std::vector<AiStart> aiStarts; // забирает хозяин
    std::vector<std::string> voices; // файлы озвучки, начатые за шаги (забирает хозяин)

    struct Message {
        std::string textId;
        std::string spriteFile;
        int sprite = 0;
    };
    std::vector<Message> messages; // ShowMessageII (забирает хозяин)

    std::optional<int> myNation; // SetMyNation
    // Куда смотрит камера по сценарию (мировые координаты); нет — камера игрока. Ставят MoveCamera
    // и AttachCameraToGroup (следит за первым живым юнитом группы), снимает FreeCamera.
    std::optional<std::pair<int, int>> cameraPoint;

    // Задачи на панели: идентификаторы текстов по порядку добавления.
    std::vector<std::string> tasks() const;

    struct Portrait {
        std::string file;
        int sprite = 0;
        std::string nameText; // TextID имени
    };
    struct Dialog {
        bool visible = false;
        Portrait left, right;
        bool rightActive = false;
        std::string text; // TextID
        std::string speaker;
    };
    const Dialog& dialog() const { return dialog_; }
    // Идёт ли фильм (юнитами лучше не управлять — хозяин решает).
    bool filmRunning() const;
    // Затемнение экрана (lvCBlackScreen): 0..1.
    float blackScreen() const { return black_ ? 1.0f : 0.0f; }

    // Для проверок.
    double var(const std::string& name) const;
    bool hasRules() const { return !rules_.empty() || !films_.empty(); }
    std::size_t skippedOps() const { return skippedOps_.size(); }

private:
    struct Rule {
        const assets::XmlNode* node = nullptr;
    };
    struct Film {
        const assets::XmlNode* node = nullptr;
        enum class State { Idle, Running, Done } state = State::Idle;
    };
    struct Group {
        std::vector<std::size_t> units;
    };
    struct Zone {
        int x = 0, y = 0, r = 0;
    };
    struct Value {
        double num = 0;
        std::string text, ogg, speaker; // vvTEXT: TextID, файл озвучки, имя говорящего
    };

    MissionScript() = default;

    double eval(const assets::XmlNode& n);
    bool holds(const assets::XmlNode* conditions);
    void runOps(const assets::XmlNode* ops);
    void runOp(const assets::XmlNode& op);
    void runRules(const assets::XmlNode* list);
    void stepFilm(Film& f);

    static std::string refName(const assets::XmlNode& n, std::string_view field);
    int intField(const assets::XmlNode& n, std::string_view field, int fallback = 0) const;
    int countUnits(int player, const std::string* type, const Group* group, const Zone* zone) const;
    const Group* group(const std::string& name) const;
    const Zone* zone(const std::string& name) const;
    void playVoice(const std::string& textVar);
    void note(const std::string& op);

    Env env_;
    assets::XmlNode root_;
    const assets::XmlNode* init_ = nullptr;
    const assets::XmlNode* scripts_ = nullptr;
    std::vector<Rule> rules_;
    std::vector<Film> films_;
    bool inited_ = false;
    bool skip_ = false;
    bool playPending_ = false;
    std::unordered_map<std::string, Value> vars_;
    std::unordered_map<std::string, std::string> taskText_; // имя задачи → TextID
    std::unordered_map<std::string, Group> groups_;
    std::unordered_map<std::string, std::pair<int, int>> vectors_; // vvVector3D: клетка карты (mX, mY)
    std::string cameraGroup_; // AttachCameraToGroup
    std::unordered_map<std::string, Zone> zones_;
    std::unordered_map<std::string, Zone> nodes_;
    std::unordered_map<std::string, std::uint64_t> timers_; // конец таймера в шагах
    std::vector<std::string> taskNames_;
    std::string pendingText_; // последний SetText: переменная vvTEXT
    std::unordered_map<std::string, bool> fg_; // видимость графических объектов
    Dialog dialog_;
    std::uint64_t voiceEnd_ = 0; // шаг, когда озвучка (канал 0) закончится
    bool black_ = false;
    std::map<std::string, int> skippedOps_;
};

} // namespace oc2::world
