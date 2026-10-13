#include "assets/economy.hpp"

#include <charconv>
#include <string>
#include <vector>

#include "assets/byte_reader.hpp"

namespace oc2::assets {

namespace {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

int toInt(std::string_view s) {
    int v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

double toDouble(std::string_view s) {
    try {
        return std::stod(std::string(s));
    } catch (...) {
        return 0;
    }
}

// «BUY мин нач макс CHANGE c r TIMERETURN t».
EconomyPrice parsePrice(const std::vector<std::string_view>& w) {
    EconomyPrice p;
    if (w.size() >= 4) {
        p.min = toInt(w[1]);
        p.start = toInt(w[2]);
        p.max = toInt(w[3]);
    }
    for (std::size_t i = 4; i + 1 < w.size(); ++i) {
        if (w[i] == "CHANGE" && i + 2 < w.size()) {
            const double c = toDouble(w[i + 1]);
            if (c != 0) p.rate = toDouble(w[i + 2]) / (c * 100.0);
        } else if (w[i] == "TIMERETURN") {
            p.timeReturn = toDouble(w[i + 1]);
        }
    }
    return p;
}

} // namespace

Economy Economy::parse(std::string_view text) {
    Economy e;
    int current = -1;
    bool first = true, any = false;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        const auto w = words(line);
        if (w.empty() || w[0].front() == '/') continue;
        if (first) { // имена кнопок окна рынка
            first = false;
            continue;
        }
        if (w[0] == "BUY" || w[0] == "SELL") {
            if (current < 0) continue;
            Resource& r = e.resources[std::size_t(current)];
            (w[0] == "BUY" ? r.buy : r.sell) = parsePrice(w);
        } else {
            current = UnitDesc::resourceId(w[0]);
            if (current >= 0) {
                e.resources[std::size_t(current)].present = true;
                any = true;
            }
        }
    }
    if (!any) throw FormatError("economy.txt: нет ни одного ресурса");
    return e;
}

} // namespace oc2::assets
