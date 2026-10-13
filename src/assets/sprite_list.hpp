#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace oc2::assets {

// Список спрайтов-объектов карты (Treelist.lst, stonlist.LST, walls.lst, …):
// первая строка — «ИМЯ_GP количество», дальше по строке на объект:
// «ID dx dy радиус». Кадр объекта в изображении GP = его номер в списке,
// точка привязки — (dx, dy) от левого верхнего угла кадра (разбор — в загрузчике
// списков оригинала, вызывается из FUN_00ed23a0).
// Строка «# a b ID dx dy радиус» задаёт пару зеркальных объектов IDF и IDB
// (у второго dx с обратным знаком). Строки с «/» — комментарии.
// Список «COMPLEX» (составные объекты) имеет другой формат и пока не читается:
// для него complex = true и entries пуст.
struct SpriteListEntry {
    std::string id;
    int dx = 0, dy = 0;
    int radius = 0;
    int frame = 0; // кадр в изображении GP
    // Поле complex.lst: «#FIELDPATH ширина высота рост масштабY» — рост колосьев
    // (у FG — 2…50, у сжатых FW — 255) и высота в 1/256 (у FW убывает при жатве).
    bool field = false;
    int growStage = 0, yScale = 256;
};

// Переходы объектов из .rsr: [TIMETRANSFORM] «ИЗ В время» и [WORKTRANSFORM] «ИЗ В работ»
// (разбор — FUN_00f… рядом с «[WORK/FIRE/TIMETRANSFORM]»: поля 0x77/0x67 и 0x61/0x51).
struct SpriteTransform {
    std::string next;
    int amount = 0;
};

// Раздел [SOURCES] файла .rsr: какой ресурс даёт объект и сколько за единицу работы.
struct SpriteSource {
    std::string resource; // WOOD, GOLD, STONE, FOOD, NONE, REMOVE
    int perWork = 0;
    int radius = 0;       // радиус работы
};

struct SpriteList {
    std::string gpName;
    bool complex = false;
    std::vector<SpriteListEntry> entries;

    // Текст файла в Windows-1251 (имена — латиница). Бросает FormatError, если
    // нет заголовка.
    static SpriteList parse(std::string_view text);
    // [SOURCES] из .rsr: ID объекта → источник.
    static std::unordered_map<std::string, SpriteSource> parseSources(std::string_view rsr);
    // Раздел section («[TIMETRANSFORM]», «[WORKTRANSFORM]») из .rsr: ID → переход.
    static std::unordered_map<std::string, SpriteTransform> parseTransforms(std::string_view rsr, std::string_view section);
};

} // namespace oc2::assets
