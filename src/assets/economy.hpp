#pragma once

#include <array>
#include <string_view>

#include "assets/unit_desc.hpp"

namespace oc2::assets {

// Цены рынка из economy.txt (загрузчик — FUN_014ab7c0): первая строка — 11 имён
// кнопок окна рынка, затем по ресурсу: имя, строки
// «BUY мин нач макс CHANGE c r TIMERETURN t» и «SELL …». Строки с «/» — комментарии.
// Цены — за 1000 единиц ресурса (в золоте условно: важно лишь отношение цен).
struct EconomyPrice {
    int min = 0, start = 0, max = 0;
    double rate = 0;       // r / (c·100): насколько сделка сдвигает цену
    double timeReturn = 0; // возврат к начальной цене со временем (не разобран)
};

struct Economy {
    struct Resource {
        bool present = false;
        EconomyPrice buy, sell;
    };
    // По номеру ресурса UnitDesc::resourceId (0 WOOD … 5 COAL).
    std::array<Resource, UnitDesc::kResources> resources{};

    // FormatError — если нет ни одного ресурса.
    static Economy parse(std::string_view text);
};

} // namespace oc2::assets
