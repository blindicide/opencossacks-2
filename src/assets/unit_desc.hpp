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
// читаются NAME, BUILDING, USERLC, SETANMPARAM и строки анимаций,
// остальное пропускается.
struct UnitDesc {
    std::string name;
    bool building = false;
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
