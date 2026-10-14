#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oc2::assets {

// Файл спрайтов типа юнита: «USERLC номер путь SHADOW dx dy».
// (dx, dy) — сдвиг левого верхнего угла кадра от точки юнита (FUN_0187abd0
// кладёт его в каждый кадр анимации).
struct UnitSpriteFile {
    int id = 0;
    std::string path; // имя GP («UnitsG17\\AnmOvcC»)
    int dx = 0, dy = 0;
};

struct UnitAnimFrame {
    int file = 0;  // номер USERLC
    int frame = 0; // кадр внутри одного направления
};

// Анимация: «#ИМЯ поворотов кадров (файл кадр)…» или «@ИМЯ поворотов файл первый последний».
// Кадры одного направления в файле идут подряд: направление d — кадры
// d·(всего / поворотов) + кадр. (dx, dy) — действовавший при чтении SETANMPARAM.
struct UnitAnimation {
    std::string name; // с префиксом: «#STAND», «@MOTION_L»
    int rotations = 1;
    std::vector<UnitAnimFrame> frames;
    int dx = 0, dy = 0;
};

// Описание типа юнита (.md): пока — то, что нужно для вывода. Тем же разбором читается
// WEAPON.ADS (файлы и анимации эффектов оружия).
// Ключевые слова оригинала — классы MD<КЛЮЧ> с разбором по строке; здесь
// читаются NAME, BUILDING, GEOMETRY, UNITRADIUS, LOCKPOINTS, USERLC, SETANMPARAM, LIFE, DAMAGE,
// ATTACK_RADIUS, ATTACK_PAUSE, SEARCH_ENEMY_RADIUS, SOUND, SELECT_SOUND, ORDER_SOUND и строки анимаций,
// остальное пропускается.
struct UnitDesc {
    std::string name;
    bool building = false;
    // GEOMETRY r1 r2 шаг (FUN_018af490): за кадр ходьбы юнит смещается на шаг/16
    // мировых единиц (таблицы cos·шаг >> 8 в единицах 1/16).
    int radius1 = 0, radius2 = 0, step = 0;
    int unitRadius = 0; // UNITRADIUS
    int razbros = 0;    // RAZBROS n — разброс выстрела (FUN_00f2c8c0)
    int rotate = 16;    // ROTATE — поворот за шаг в единицах направления (поле 0x15D9, по умолчанию 0x10)
    // LOCKPOINTS n x1 y1 …: занятые зданием клетки 16×16 мировых единиц от угла
    // картинки (OneObject::GetCornerXY: x + ax, y + 2·ay, где (ax, ay) — SETANMPARAM).
    std::vector<std::pair<int, int>> lockPoints;
    int pictureDx = 0, pictureDy = 0; // первый SETANMPARAM

    // Бой. Оружие 0..11 (0 — обычно рукопашное, 1 — выстрел), как поля типа в
    // MDDAMAGE / MDATTACK_RADIUS / MDATTACK_PAUSE (FUN_018a0010, FUN_0188a7e0, FUN_01889cc0).
    static constexpr int kWeapons = 12;
    int life = 0;                              // LIFE
    std::array<int, kWeapons> damage{};        // DAMAGE w урон
    std::array<int, kWeapons> attackMin{};     // ATTACK_RADIUS w мин макс
    std::array<int, kWeapons> attackMax{};
    std::array<int, kWeapons> attackPause{};   // ATTACK_PAUSE w шаги перезарядки
    int searchEnemyRadius = 0;                 // SEARCH_ENEMY_RADIUS
    std::array<std::string, kWeapons> weaponName; // WEAPON w ИМЯ — снаряд/эффект из weapon.nds

    // Добыча. Ресурсы: 0 WOOD, 1 GOLD, 2 STONE, 3 FOOD, 4 IRON, 5 COAL (порядок
    // RESOURCEBASE и первых чисел TAKERESSTAGES).
    static constexpr int kResources = 6;
    static int resourceId(std::string_view name); // −1 — неизвестный
    // RASTRATA_NA_VISTREL w n РЕС кол … — расход ресурсов на выстрел оружием w.
    std::array<std::array<int, kResources>, kWeapons> shotCost{};
    std::array<int, kResources> portion{};     // PORTION n РЕС кол-во … — сколько несёт за ходку
    std::array<int, kResources> workCycles{};  // TAKERESSTAGES РЕС id циклы x — циклов работы на ходку
    std::uint8_t resourceBase = 0;             // RESOURCEBASE n РЕС… — какие ресурсы принимает (биты)
    bool peasant = false;                      // PEASANT
    // SPRITEOBJECT + EXSPRITES ID …: «юнит» — посев объектов complex.lst (поле у мельницы).
    bool spriteObject = false;
    std::string exSprite;
    // Население: MFARM n — мест у готового здания (поле 0x350); NOFARM — юнит места не
    // занимает (флаг 0x340 & 0x20; у крестьян, пушек, кораблей).
    int farm = 0;
    bool noFarm = false;
    // RESCONSUMER РЕС n — расход ресурса (поля 0x15cb/0x15c9; суммы по игроку — пересчёт
    // thunk_FUN_00dcec00, поле 0x15df5). Поле одно: при нескольких строках (у гвардии
    // FOOD 8 и GOLD 8) остаётся последняя, как в оригинале (FUN_018d43c0 перезаписывает).
    int consumeResource = -1, consumeAmount = 0;

    // Шахта: PEASANTABSORBER n — сколько крестьян вмещает (поле 0x372); PRODUCER k РЕС… a b —
    // какие ресурсы даёт (биты 0x36c) и доход (a + b·крестьян)·GameSpeed/8 тысячных за шаг
    // (FUN_01596b80; 0x36e = a, 0x370 = b).
    int peasantAbsorber = 0;
    std::uint8_t producerResources = 0;
    int producerBase = 0, producerPerPeasant = 0;

    // Строительство и производство.
    std::array<int, kResources> price{};       // PRICE n РЕС кол-во …
    // BUILDSTAGES: у здания — стадий стройки (OneObject::NextStage: жизнь = стадия·LIFE/стадий,
    // картинка #BUILDLO_k, k = стадия·4/стадий); у юнита — время производства.
    int buildStages = 0;
    std::vector<std::pair<int, int>> buildLockPoints; // BUILDLOCKPOINTS — клетки стройки (как LOCKPOINTS)
    std::vector<std::pair<int, int>> buildPoints;     // BUILDPOINTS — где стоят строители (клетки от угла)
    // Где появляется произведённый юнит, в мировых единицах от угла (клетка угла ·16):
    // BORNPOINTS2 x y — (x, 2y), как MDBORNPOINTS2 (поле 0x410/0x41c); BORNPOINTS — в
    // клетках (·16, своё прочтение). Направление нового юнита — от первой точки ко второй.
    std::vector<std::pair<int, int>> bornPoints;
    std::string bornSound;                     // BORN_SOUND
    std::string bigIconFile;                   // BIGICON файл кадр — значок на панели
    int bigIconFrame = 0;

    // Звуки: «SOUND кадр #АНИМАЦИЯ ГРУППА» — на кадре анимации (номер в animations).
    struct Sound {
        int frame = 0;
        std::size_t animation = 0;
        std::string group;
    };
    std::vector<Sound> sounds;
    std::string selectSound, orderSound; // SELECT_SOUND, ORDER_SOUND
    std::vector<UnitSpriteFile> files;
    std::vector<UnitAnimation> animations;

    const UnitSpriteFile* file(int id) const;
    const UnitAnimation* animation(std::string_view name) const;

    // Текст файла в Windows-1251. Бросает FormatError на испорченной строке анимации.
    static UnitDesc parse(std::string_view text);
};

// Раздел [MEMBERS] файла нации .nds: пары «тип юнита на карте → имя .md».
std::vector<std::pair<std::string, std::string>> parseNationMembers(std::string_view text);

// [FIXED_PRODUCE] файла нации (разбор — FUN_015c2040, раздел 10):
// «ПРОИЗВОДИТЕЛЬ n», затем n строк «ЧЛЕН столбец строка клавиша»; значок —
// строка·12 + столбец на панели (столбец < 12, строка ≤ 8), клавиша NONE/---- — нет.
struct ProduceItem {
    std::string member;
    int column = 0, row = 0;
    char hotkey = 0;
};
std::vector<std::pair<std::string, std::vector<ProduceItem>>> parseNationProduce(std::string_view text);

} // namespace oc2::assets
