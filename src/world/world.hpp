#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "assets/map3d.hpp"
#include "assets/sprite_list.hpp"
#include "assets/nation_tech.hpp"
#include "assets/unit_desc.hpp"
#include "assets/weapon_list.hpp"
#include "world/lock_map.hpp"
#include "world/market.hpp"

namespace oc2::world {

// Длительность шага симуляции при обычной скорости игры: 40 мс (DAT_059e93ba = 0x28;
// настройка скорости даёт 120 − ползунок мс). Оригинал делает один шаг на кадр вывода
// и подстраивает GameSpeed под реальную длительность кадра; здесь шаг фиксированный.
inline constexpr double kStepSeconds = 0.040;

// Направление 0..255 → вектор ×256, как таблицы DAT_0216a818 (cos) и DAT_0216a610 (sin):
// 0 — восток, 64 — юг (мировой Y вниз).
int cos256(int direction);
int sin256(int direction);
// Направление от (dx, dy) к началу координат: 0..255.
int directionOf(int dx, int dy);

struct Unit {
    const assets::UnitDesc* desc = nullptr;
    int player = 0;
    int x16 = 0, y16 = 0; // мировые координаты ×16, как в оригинале и в UNI3
    int direction = 0;    // 0..255
    const assets::UnitAnimation* animation = nullptr;
    int frame = 0;        // кадр текущей анимации
    bool moving = false;
    int targetX16 = 0, targetY16 = 0;          // текущая точка пути
    std::vector<std::pair<int, int>> path;     // следующие точки (мировые), в обратном порядке
    bool selected = false;
    int squad = -1;   // номер отряда в World::squads (−1 — одиночка)
    int panic = 0;    // шагов бегства после разгрома отряда: не ищет врага

    // Бой.
    int life = 0;
    std::optional<std::size_t> target; // номер юнита-цели
    bool attacking = false;            // идёт анимация удара/выстрела
    int weapon = -1;                   // оружие текущей атаки
    bool damageDone = false;           // урон этой атаки уже нанесён
    std::array<int, assets::UnitDesc::kWeapons> reload{}; // шаги до готовности оружия
    int goalX = 0, goalY = 0;          // куда считался путь к цели
    int repathIn = 0;                  // шаги до пересчёта пути к цели
    bool dying = false, dead = false;

    // Добыча ресурса.
    enum class Gather : std::uint8_t { None, ToSource, Working, ToBase };
    Gather gather = Gather::None;
    std::size_t source = 0;  // номер источника в World::sources
    int gatherResource = 0;  // какой ресурс добывает
    int carry = 0;           // сколько несёт
    int workLeft = 0;        // циклов работы до полной ноши
    std::optional<std::size_t> base; // куда несёт

    // Строительство: у здания — стадия (OneObject +0x99), у строителя — что строит.
    bool underConstruction = false;
    int stage = 0;
    std::optional<std::size_t> buildTarget;
    // Производство у здания: очередь и шаги текущего (время — BUILDSTAGES юнита в шагах:
    // приказ производства прибавляет GameSpeed = 256 к счётчику до стадий·256, FUN_00e4fe60).
    std::vector<const assets::UnitDesc*> queue;
    int produced = 0;
    // Исследование улучшения в здании: имя и шагов до конца (0 — не идёт).
    std::string research;
    const assets::UpgradeDef* researchDef = nullptr;
    int researchLeft = 0;
    // Шахта: крестьяне внутри (номера юнитов) и накопитель дохода в тысячных.
    std::vector<std::size_t> occupants;
    int mineAccum = 0;
    // Крестьянин идёт в шахту (mine) или уже внутри (inside: не виден, не выбирается).
    std::optional<std::size_t> mine;
    bool inside = false;

    bool alive() const { return !dying && !dead; }
    int x() const { return x16 >> 4; }
    int y() const { return y16 >> 4; }
    bool building() const { return desc && desc->building; }
};

// Юниты мира и их шаг симуляции: анимации покоя и ходьбы, движение по пути A*
// с мгновенным поворотом, расталкивание (юниты не стоят друг в друге), бой.
// Бой (упрощённо, по данным .md): оружие w годно, если у него есть урон, дальность
// и анимация «ATTACKw» (у 0 — «ATTACK»); из годных и готовых, в чьём радиусе цель,
// берётся самое дальнобойное. Урон — на середине анимации, затем перезарядка
// ATTACK_PAUSE шагов. Убитый играет DEATH, затем лежит (DEATHLIE1). Юниты игроков
// 0..6 сами ищут врага в SEARCH_ENEMY_RADIUS; союзники — по командам (team).
class World {
public:
    // Карта непроходимости (без неё юниты ходят по прямой). Здания, добавленные
    // после, закрывают свои клетки.
    void setLockMap(LockMap lockMap) { lock_ = std::move(lockMap); }
    const LockMap& lockMap() const { return lock_; }

    // Добавляет юнит в мировой точке (x, y); возвращает его номер.
    std::size_t addUnit(const assets::UnitDesc& desc, int player, int x, int y, int direction);

    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }

    // Приказ идти в мировую точку (x, y). Здания не ходят. Снимает цель.
    void orderMove(std::size_t unit, int x, int y);
    // Отряды и построения (своё упрощение по подсказкам игры: шеренга, колонна, каре).
    // Отряд — однотипные юниты одного игрока, идущие строем с общей скоростью. Дух падает
    // от потерь и попаданий и восстанавливается в покое; отряд разбегается при потере 70 %,
    // при духе 0 или после 10 % потерь без офицера рядом и низком духе.
    enum class Formation : std::uint8_t { Line, Column, Square };
    struct Squad {
        int player = 0;
        Formation form = Formation::Line;
        int facing = 0;                    // 0..255
        std::vector<std::size_t> members;
        int initial = 0;
        int spirit = 100;
        int idle = 0;                      // шагов без движения
        int calm = 0;                      // шагов без попаданий
        int step = 0;                      // общая скорость (1/16 за шаг)
    };
    std::vector<Squad> squads;
    // Строит выбранных в отряды по типам (от 2 юнитов в типе; уже целый отряд лишь меняет построение).
    bool formSquad(const std::vector<std::size_t>& ids, Formation form);
    // Приказ идти в точку: отряды — строем лицом к цели, одиночки — толпой с прежним взаимным расположением.
    void orderMoveGroup(const std::vector<std::size_t>& ids, int x, int y);
    static std::string joinIds(const std::vector<std::size_t>& ids);
    static std::vector<std::size_t> parseIds(const std::string& s);
    // Кавалерия (имя «NatKxxx») и офицер (имя «…Of»): по именам .md.
    static bool isCavalry(const Unit& u);
    static bool isOfficer(const Unit& u);

    // Приказ уничтожить своё здание или подразделение (клавиша Delete): без счёта убитых.
    void orderDestroy(std::size_t unit);
    // Приказ стоять (клавиша S): снимает цель, путь, добычу и стройку.
    void orderStop(std::size_t unit);
    // Паника (Scare сценария): юнит бросает дела и бежит прочь от ближайшего врага.
    void scare(std::size_t unit);
    // Приказ атаковать юнит target.
    void orderAttack(std::size_t unit, std::size_t target);

    // Источники ресурсов (деревья и т. п.): ресурс 0..5, сколько за единицу работы,
    // радиус работы. Не истощаются (упрощение).
    struct Source {
        int x = 0, y = 0;
        int resource = 0; // −1 — сейчас не источник (поле растёт или сжато)
        int perWork = 0;
        int radius = 0;
        // Объект с переходами (поле complex.lst): номер в списке, шаги и работы в этой стадии.
        int object = -1;
        int timer = 0, works = 0;
    };
    std::vector<Source> sources;

    // Правила объектов с переходами (complex.lst + complex.rsr): стадии полей растут со
    // временем ([TIMETRANSFORM]) и сжинаются работой ([WORKTRANSFORM]); источником стадия
    // бывает, только если она есть в [SOURCES].
    struct ObjectRules {
        assets::SpriteList list;
        std::unordered_map<std::string, assets::SpriteSource> sources;
        std::unordered_map<std::string, assets::SpriteTransform> time, work;
        int find(std::string_view id) const;
    };
    // Время [TIMETRANSFORM] в шагах на единицу. Своё допущение: единица = шаг (рост
    // FG0 → FG24, 120 переходов по 10, — 48 с); в коде оригинала единица не найдена.
    static constexpr int kTransformTimeSteps = 1;
    void setObjectRules(ObjectRules rules) { rules_ = std::move(rules); }
    const ObjectRules& objectRules() const { return rules_; }
    // Объект с переходами в точке (стадия object — номер в rules.list); номер источника.
    std::size_t addObject(int object, int x, int y);
    // Посеять поле (SPRITEOBJECT, EXSPRITES): цена PRICE, квадрат 3×3 объектов первой
    // стадии через 64 единицы с центром в (x, y), на закрытых клетках не сеется (своя
    // раскладка: числа EXSPRITES не разобраны). Число посеянных; 0 — нельзя.
    static constexpr int kFieldBlock = 3, kFieldStep = 64;
    int sowField(const assets::UnitDesc& desc, int player, int x, int y);
    // Ближайший источник к мировой точке в пределах maxDistance.
    std::optional<std::size_t> sourceNear(int x, int y, int maxDistance) const;
    // Приказ добывать: идти к источнику, работать TAKERESSTAGES циклов анимации
    // ATTACKn, нести PORTION к ближайшему своему зданию с RESOURCEBASE, повторять.
    // Может только тот, у кого есть PORTION этого ресурса.
    bool orderGather(std::size_t unit, std::size_t source);

    // Можно ли поставить здание: свободны клетки LOCKPOINTS и BUILDLOCKPOINTS.
    bool canPlace(const assets::UnitDesc& desc, int x, int y) const;
    // Заложить здание игрока: проверяет место и цену (PRICE), списывает её; здание
    // на нулевой стадии занимает BUILDLOCKPOINTS. nullopt — нельзя.
    std::optional<std::size_t> placeBuilding(const assets::UnitDesc& desc, int player, int x, int y);
    // Хватает ли игроку ресурсов на выстрел оружием w юнита (RASTRATA_NA_VISTREL).
    bool canShoot(const Unit& u, int w) const;
    bool affordable(const assets::UnitDesc& desc, int player) const;
    // Приказ строить (крестьянину своего игрока): идёт к точке BUILDPOINTS и каждым
    // законченным циклом @WORK продвигает стройку на стадию (своё упрощение:
    // кто вызывает NextStage у оригинала, не найдено).
    bool orderBuild(std::size_t unit, std::size_t building);
    // Население игрока (как пересчёт thunk_FUN_00dcec00): занято — живые не-здания без
    // NOFARM; предел — сумма MFARM готовых зданий. Юнит без NOFARM не производится,
    // пока занято ≥ предела (приказ производства ждёт).
    int population(int player) const;
    int populationLimit(int player) const;
    // Расход ресурсов (RESCONSUMER) живых юнитов игрока за минуту. Своё допущение: числа —
    // в минуту игрового времени (код списания у оригинала не найден); списывается по
    // частям каждый шаг, запас не уходит ниже нуля.
    std::array<int, assets::UnitDesc::kResources> consumption(int player) const;
    // Голод (энциклопедия: «если запасы еды кончатся, войска будут голодать и умирать…
    // и вы не сможете обучать новых солдат»). Своё прочтение, числа оригинала не найдены:
    // игрок голодает, пока еды 0, а расход еды не покрыт; тогда раз в kStarveSteps шагов
    // каждый едок теряет 1/kStarveShare своей LIFE (не меньше 1), а производство
    // едоков стоит. Падение морали — когда будет мораль.
    static constexpr int kStarveSteps = 125;  // 5 секунд
    static constexpr int kStarveShare = 20;   // от сытого до смерти — около 100 секунд
    bool starving(int player) const { return starving_[std::size_t(player & 7)]; }
    // Крестьянину войти в шахту (PEASANTABSORBER): свою или ничью (игрок 7) — ничья
    // при входе становится своей (своё упрощение захвата; CheckMineCaptureAbility не разобран).
    bool orderEnter(std::size_t unit, std::size_t mine);
    // Выпустить одного крестьянина из шахты рядом с ней. false — внутри никого.
    bool leaveMine(std::size_t mine);
    // Заказать юнит в здании: списывает PRICE, ставит в очередь. false — нельзя.
    bool orderProduce(std::size_t building, const assets::UnitDesc& unit);
    // Доступен ли игроку тип desc: есть все требуемые [ACCESSCONTROL] готовые здания или исследованные улучшения.
    bool available(const assets::UnitDesc& desc, int player) const;
    // Типы, запрещённые сценарием миссии (SetUnitEnableState −1): имена typeName по игрокам.
    std::array<std::unordered_set<std::string>, 8> blocked;
    // Улучшения, исследованные игроком (по именам из [UPGRADE]).
    std::array<std::unordered_set<std::string>, 8> researched;
    // Эффекты исследованных улучшений по игрокам и типам (typeName): строки [UPGRADE] LIFE, SHIELD,
    // ATTRANGE, SINGLE_ATTPAUSE, BUILD. Проценты накапливаются множителями; смысл «200%» без знака
    // (двукратная жизнь) и вычитания щита из урона — допущения. Остальные строки (COST, EFFECT_*,
    // HEALING, RAZBROS, DAMAGE, ENABLE) пока не применяются.
    struct Mods {
        int lifeAdd = 0, lifePct = 100, shield = 0, buildPct = 100;
        std::array<int, assets::UnitDesc::kWeapons> pause, rangePct;
        Mods() { pause.fill(-1); rangePct.fill(100); }
    };
    std::array<std::unordered_map<std::string, Mods>, 8> mods;
    const Mods* modsOf(const Unit& u) const;
    int maxLife(const Unit& u) const;
    int attackMax(const Unit& u, int weapon) const;
    int attackPause(const Unit& u, int weapon) const;
    int buildStages(int player, const assets::UnitDesc& d) const;
    // Улучшение исследовано: запоминает его и применяет эффекты (в том числе к уже существующим юнитам).
    void finishResearch(int player, const assets::UpgradeDef& upgrade);
    // Исследовать улучшение в здании: списывает цену, ставит таймер. false — нельзя (занято, уже есть, нет денег).
    bool orderResearch(std::size_t building, const assets::UpgradeDef& upgrade, int costPercent = 100);
    // Следующая стадия стройки (OneObject::NextStage).
    void nextStage(std::size_t building);
    // Источники из объектов карты вида kind («AG», «ST»): номер объекта → ID в списке
    // .lst → строка [SOURCES] файла .rsr. Возвращает число добавленных.
    std::size_t addSources(const assets::Map3d& map, const char (&kind)[3], const assets::SpriteList& list,
                           const std::unordered_map<std::string, assets::SpriteSource>& table);

    // Команды игроков (из настроек миссии MOPT): одна команда — союзники.
    // По умолчанию у каждого своя. Нейтральный 7 ни с кем не воюет.
    std::array<int, 8> team{0, 1, 2, 3, 4, 5, 6, 7};
    bool enemies(int a, int b) const {
        return a != 7 && b != 7 && team[std::size_t(a & 7)] != team[std::size_t(b & 7)];
    }

    // Туман войны для команды team: клетки 64×64 мировых единиц — 0 не исследована, 1 исследована
    // (видна приглушённой, чужие здания стоят там, где их видели), 2 видна сейчас. В оригинале
    // GetFogSpotVisibility прячет клетку, пока её «свет» ниже порога; радиус обзора — своя
    // оценка: kBaseVision + kVisionStep × VISION юнита.
    struct Fog {
        static constexpr int kCell = 64, kBaseVision = 1100, kVisionStep = 100, kPeriod = 4;
        bool enabled = false;
        int team = 0;
        int cols = 0, rows = 0;
        std::vector<std::uint8_t> cells;
        std::uint8_t at(int x, int y) const {
            const int cx = x / kCell, cy = y / kCell;
            return cx < 0 || cy < 0 || cx >= cols || cy >= rows ? 0 : cells[std::size_t(cy) * std::size_t(cols) + std::size_t(cx)];
        }
    };
    Fog fog;
    // Включает туман для команды team (размеры — по карте закрытых клеток).
    void enableFog(int team);
    // Скрыт ли юнит от игрока: чужие и нейтральные юниты — вне зоны обзора, здания — в неисследованном.
    bool hiddenByFog(const Unit& u) const;
    // Один шаг симуляции (kStepSeconds при обычной скорости).
    void step();

    // Запись приказа в журнал. Вложенные приказы (build → move) не пишутся: на время вызова запись выключена.
    struct RecordGuard {
        World& w;
        bool was;
        explicit RecordGuard(World& world) : w(world), was(world.recording) { w.recording = false; }
        ~RecordGuard() { w.recording = was; }
        RecordGuard(const RecordGuard&) = delete;
        RecordGuard& operator=(const RecordGuard&) = delete;
    };
    [[nodiscard]] RecordGuard record(const char* kind, int a = 0, int b = 0, int c = 0, int d = 0, const std::string& s = {}) {
        if (recording) journal.push_back({steps_, kind, {a, b, c, d}, s});
        return RecordGuard(*this);
    }

    std::uint64_t stepCount() const { return steps_; }

    // Журнал приказов игрока (для сохранений: симуляция детерминирована, игра восстанавливается
    // повтором приказов с тех же шагов). Пишутся, пока recording (ввод игрока), а не приказы ИИ и сценария.
    struct Command {
        std::uint64_t step = 0;
        std::string kind;            // move, attack, gather, build, enter, leave, produce, research, place, sow, skip
        std::array<int, 4> a{};
        std::string s;               // имя типа юнита или улучшения
    };
    std::vector<Command> journal;
    // Последнее событие у команды игрока (по нему прыгает пробел): место и шаг; step −1 — не было.
    struct Alert {
        int x = 0, y = 0;
        std::int64_t step = -1;
    };
    Alert lastAlert;
    bool recording = false;

    // Победившая команда: живые боевые юниты (не здания) игроков 0..6 остались
    // только у одной команды, а в начале команд было не меньше двух. Упрощение
    // условий победы миссий (захват зон и т. п. пока не учитываются).
    std::optional<int> winningTeam() const;

    // Простой тактический ИИ (своё упрощение, стратегический ИИ оригинала не перенесён):
    // юниты команд, кроме humanTeam и нейтральных, раз в 5 секунд, если стоят без цели,
    // идут атаковать ближайшего врага в радиусе kAiRadius. humanTeam = −1 — ИИ выключен.
    static constexpr int kAiRadius = 2500;
    int humanTeam = -1;

    // Проверочное: убить всех боевых юнитов не из команды team.
    void killAllExcept(int team);

    // Звуки, случившиеся за шаги (кадр анимации с SOUND); забирает и очищает вывод.
    struct SoundEvent {
        std::string group;
        int x, y;
    };
    std::vector<SoundEvent> soundEvents;
    // Выстрелы за шаги: оружие из WEAPON w (weapon.nds) — дым, вспышка, звук выстрела.
    struct ShotEvent {
        std::string weapon;
        int x, y, direction;
    };
    std::vector<ShotEvent> shotEvents;
    // Летящие снаряды (ядра, гранаты, бомбы мортир): урон — при попадании, а не при выстреле.
    // Оружие и скорость полёта — из weapon.nds (WeaponList); без списка выстрелы мгновенные.
    struct Projectile {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // откуда и куда (точка прицеливания с разбросом)
        int age = 0, total = 1;             // шагов в полёте и всего
        int player = 0;                     // чей выстрел
        std::size_t shooter = 0, target = 0;
        int damage = 0;
        bool buildingTarget = false;
        bool visual = false; // обломки и взрывы разрушенного здания: только рисуются, урона нет
        const assets::Weapon* weapon = nullptr;
    };
    std::vector<Projectile> projectiles;
    const assets::WeaponList* weaponList = nullptr;

    // Ресурсы игроков (номера — как ID счётчиков на панели).
    std::array<std::array<int, 8>, 8> resources{};
    // Рынок, общий для всех игроков (нет — economy.txt не загружен).
    std::optional<Market> market;
    // Счётчики игроков по типам для стратегического ИИ: сколько готово произведено
    // (юниты из зданий и достроенные здания) и сколько погибло.
    std::array<std::unordered_map<const assets::UnitDesc*, int>, 8> producedCount, lostCount, killedCount;
    // Траты игрока для итоговой статистики: [игрок][статья][ресурс]. Продажа на рынке — полученное, покупка — отданное.
    enum Spend { SpendUpgrade, SpendUpkeep, SpendUnits, SpendBuildings, SpendBuy, SpendSell, SpendKinds };
    std::array<std::array<std::array<int, 6>, SpendKinds>, 8> spent{};

    // Анимация покоя (#STANDLO у зданий).
    static const assets::UnitAnimation* idleAnimation(const assets::UnitDesc& desc);

private:
    static const assets::UnitAnimation* moveAnimation(const assets::UnitDesc& desc);
    static const assets::UnitAnimation* attackAnimation(const assets::UnitDesc& desc, int weapon);
    static const assets::UnitAnimation* namedAnimation(const assets::UnitDesc& desc, const std::string& name);
    void setAnimation(Unit& u, const assets::UnitAnimation* anim);
    void pathTo(Unit& u, int x, int y);
    void updateCombat(std::size_t index);
    void hit(Unit& target, int damage, const Unit* by = nullptr);
    void spawnDebris(const Unit& building); // DESTRUCT: летящие обломки и взрывы на месте здания
    void emitSounds(const Unit& u);
    void thinkAi(std::size_t index);
    void updateGather(std::size_t index);
    void updateBuild(std::size_t index);
    void updateProduction(std::size_t index);
    void updateObjects();
    void setStage(Source& s, int object);
    void workDone(std::size_t source);
    void updateEnter(std::size_t index);
    void updateMine(std::size_t index);
    // Юниты в закрытых клетках (под новым зданием) уходят к ближайшей открытой.
    void pushOutOfLocks();
    std::pair<int, int> buildPoint(std::size_t builder, const Unit& building) const;
    std::optional<std::size_t> nearestBase(const Unit& u, int resource) const;
    static const assets::UnitAnimation* workAnimation(const assets::UnitDesc& d, int resource);
    static const assets::UnitAnimation* carryAnimation(const assets::UnitDesc& d, int resource);
    void separate();
    std::vector<std::pair<std::int64_t, std::uint32_t>> cells_;
    ObjectRules rules_;
    std::array<std::array<int, assets::UnitDesc::kResources>, 8> upkeepAccum_{}; // в 1/1500 единицы
    std::array<bool, 8> starving_{};
    void updateUpkeep();
    void starve();

    std::vector<Unit> units_;

    void updateSquads();
    void routSquad(std::size_t index);
    int stepOf(const Unit& u) const;
    bool holding(const Unit& u) const; // шеренга стоит на месте — держит позицию
    LockMap lock_;
    std::uint64_t steps_ = 0;
    std::uint32_t rng_ = 0x9e3779b9u; // свой генератор: исход боя повторяем при тех же приказах
    int random(int n); // 0..n-1
    // Куда на самом деле попал выстрел оружием w юнита u по цели t (разброс RAZBROS); nullptr — мимо.
    Unit* shotVictim(const Unit& u, Unit& t);
    std::pair<int, int> aimPoint(const Unit& u, const Unit& t);
    Unit* victimAt(int player, int ax, int ay);
    void stepProjectiles();
    void updateFog();
};

} // namespace oc2::world
