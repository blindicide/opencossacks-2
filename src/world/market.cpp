#include "world/market.hpp"

#include <cmath>

namespace oc2::world {

namespace {

int round(double v) { return int(std::lround(v)); }

// Цена, сдвинутая к пределу limit на объём сделки с весом k.
int drift(int price, int limit, double k) { return round((double(limit) * k + double(price)) / (1.0 + k)); }

} // namespace

Market::Market(const assets::Economy& economy) : economy_(economy) {
    for (std::size_t i = 0; i < buy_.size(); ++i) {
        buy_[i] = economy_.resources[i].buy.start;
        sell_[i] = economy_.resources[i].sell.start;
    }
}

bool Market::valid(int r) const {
    return r >= 0 && std::size_t(r) < buy_.size() && economy_.resources[std::size_t(r)].present;
}

int Market::quote(int sell, int buy, int amount) const {
    if (!valid(sell) || !valid(buy) || sell == buy || amount <= 0 || buy_[std::size_t(buy)] <= 0) return 0;
    return round(double(amount) * sell_[std::size_t(sell)] / buy_[std::size_t(buy)]);
}

int Market::exchange(int sell, int buy, int amount) {
    lastSold_ = 0;
    const int gained = quote(sell, buy, amount);
    if (gained <= 0) return 0;
    lastSold_ = amount;
    const auto& rb = economy_.resources[std::size_t(buy)];
    const auto& rs = economy_.resources[std::size_t(sell)];
    const std::size_t b = std::size_t(buy), s = std::size_t(sell);
    const double kb = gained * rb.buy.rate;
    buy_[b] = drift(buy_[b], rb.buy.max, kb);
    sell_[b] = drift(sell_[b], rb.sell.max, kb);
    const double ks = amount * rs.sell.rate;
    sell_[s] = drift(sell_[s], rs.sell.min, ks);
    buy_[s] = drift(buy_[s], rs.buy.min, ks);
    return gained;
}

} // namespace oc2::world
