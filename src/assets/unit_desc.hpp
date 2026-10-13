#pragma once

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

// Описание типа юнита (.md): пока — то, что нужно для вывода.
// Ключевые слова оригинала — классы MD<КЛЮЧ> с разбором по строке; здесь
// читаются NAME, BUILDING, GEOMETRY, UNITRADIUS, LOCKPOINTS, USERLC, SETANMPARAM и строки анимаций,
// остальное пропускается.
struct UnitDesc {
    std::string name;
    bool building = false;
    // GEOMETRY r1 r2 шаг (FUN_018af490): за кадр ходьбы юнит смещается на шаг/16
    // мировых единиц (таблицы cos·шаг >> 8 в единицах 1/16).
    int radius1 = 0, radius2 = 0, step = 0;
    int unitRadius = 0; // UNITRADIUS
    // LOCKPOINTS n x1 y1 …: занятые зданием клетки 16×16 мировых единиц от угла
    // картинки (OneObject::GetCornerXY: x + ax, y + 2·ay, где (ax, ay) — SETANMPARAM).
    std::vector<std::pair<int, int>> lockPoints;
    int pictureDx = 0, pictureDy = 0; // первый SETANMPARAM
    std::vector<UnitSpriteFile> files;
    std::vector<UnitAnimation> animations;

    const UnitSpriteFile* file(int id) const;
    const UnitAnimation* animation(std::string_view name) const;

    // Текст файла в Windows-1251. Бросает FormatError на испорченной строке анимации.
    static UnitDesc parse(std::string_view text);
};

// Раздел [MEMBERS] файла нации .nds: пары «тип юнита на карте → имя .md».
std::vector<std::pair<std::string, std::string>> parseNationMembers(std::string_view text);

} // namespace oc2::assets
