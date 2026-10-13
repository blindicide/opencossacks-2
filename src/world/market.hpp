#pragma once

#include <array>

#include "assets/economy.hpp"

namespace oc2::world {

// Рынок обмена ресурсов, общий для всех игроков (FUN_015d2c10 → FUN_014aca50 → FUN_014ac380).
// Продажа amount ресурса sell за ресурс buy даёт ROUND(amount · SELL[sell] / BUY[buy]);
// после сделки цены buy тянутся к максимуму, цены sell — к минимуму:
// цена = ROUND((предел·k + цена) / (1 + k)), k = объём · rate.
// Возврат цен со временем (TIMERETURN) не разобран и не делается.
class Market {
public:
    explicit Market(const assets::Economy& economy);

    // Обменивает до amount ресурса sell (сколько есть) на buy в запасах stock;
    // возвращает полученное количество buy (0 — сделки нет).
    template <std::size_t N> int trade(std::array<int, N>& stock, int sell, int buy, int amount) {
        if (sell < 0 || buy < 0 || std::size_t(sell) >= N || std::size_t(buy) >= N) return 0;
        int& from = stock[std::size_t(sell)];
        const int gained = exchange(sell, buy, amount < from ? amount : from);
        if (gained <= 0) return 0;
        from -= lastSold_;
        stock[std::size_t(buy)] += gained;
        return gained;
    }

    // Сколько buy дадут за amount sell по текущим ценам.
    int quote(int sell, int buy, int amount) const;
    int buyPrice(int resource) const { return valid(resource) ? buy_[std::size_t(resource)] : 0; }
    int sellPrice(int resource) const { return valid(resource) ? sell_[std::size_t(resource)] : 0; }

private:
    bool valid(int r) const;
    int exchange(int sell, int buy, int amount);

    assets::Economy economy_;
    std::array<int, assets::UnitDesc::kResources> buy_{}, sell_{};
    int lastSold_ = 0;
};

} // namespace oc2::world
