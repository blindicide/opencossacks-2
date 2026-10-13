#pragma once

#include <string>
#include <string_view>
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
};

struct SpriteList {
    std::string gpName;
    bool complex = false;
    std::vector<SpriteListEntry> entries;

    // Текст файла в Windows-1251 (имена — латиница). Бросает FormatError, если
    // нет заголовка.
    static SpriteList parse(std::string_view text);
};

} // namespace oc2::assets
