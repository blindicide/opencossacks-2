#include "assets/nation_tech.hpp"

#include <cctype>
#include <charconv>

namespace oc2::assets {

namespace {

// Слова раздела [section] без «//»-комментариев, по строкам.
std::vector<std::vector<std::string_view>> sectionLines(std::string_view text, std::string_view section) {
    std::vector<std::vector<std::string_view>> out;
    bool in = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (const auto c = line.find("//"); c != std::string_view::npos) line = line.substr(0, c);
        std::vector<std::string_view> words;
        std::size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            const std::size_t s = i;
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            if (i > s) words.push_back(line.substr(s, i - s));
        }
        if (words.empty()) continue;
        if (words[0].front() == '[') {
            in = words[0] == section;
            continue;
        }
        if (in) out.push_back(std::move(words));
    }
    return out;
}

bool toInt(std::string_view w, int& out) {
    const char* b = w.data();
    if (b < w.data() + w.size() && *b == '+') ++b;
    const auto [end, ec] = std::from_chars(b, w.data() + w.size(), out);
    return ec == std::errc() && end != b;
}

int resourceIndex(std::string_view name) {
    static constexpr std::string_view kNames[6] = {"WOOD", "GOLD", "STONE", "FOOD", "IRON", "COAL"};
    for (int i = 0; i < 6; ++i)
        if (name == kNames[i]) return i;
    return -1;
}

// «ИМЯ n имя…» — имена могут идти на следующих строках.
std::vector<std::pair<std::string, std::vector<std::string>>> countedLists(std::string_view text,
                                                                         std::string_view section) {
    std::vector<std::string_view> words;
    for (const auto& line : sectionLines(text, section)) words.insert(words.end(), line.begin(), line.end());
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (std::size_t i = 0; i + 1 < words.size();) {
        int n = 0;
        if (!toInt(words[i + 1], n) || n < 0) {
            ++i; // сбой — к следующему слову
            continue;
        }
        std::vector<std::string> list;
        for (std::size_t k = 0; k < std::size_t(n) && i + 2 + k < words.size(); ++k)
            list.emplace_back(words[i + 2 + k]);
        out.emplace_back(std::string(words[i]), std::move(list));
        i += 2 + std::size_t(n);
    }
    return out;
}

} // namespace

std::vector<std::pair<std::string, std::vector<std::string>>> parseNationAccess(std::string_view text) {
    return countedLists(text, "[ACCESSCONTROL]");
}

std::vector<std::pair<std::string, std::vector<std::string>>> parseNationUpgradePlace(std::string_view text) {
    return countedLists(text, "[UPGRADEPLACE]");
}

std::vector<UpgradeDef> parseNationUpgrades(std::string_view text) {
    std::vector<UpgradeDef> out;
    for (const auto& w : sectionLines(text, "[UPGRADE]")) {
        bool header = false;
        for (const auto word : w) header = header || word == "#POSITION";
        if (!header) { // действие — к последнему улучшению
            if (!out.empty()) out.back().effects.emplace_back(w.begin(), w.end());
            continue;
        }
        UpgradeDef d;
        d.name = std::string(w[0]);
        if (w.size() > 1) d.icon = std::string(w[1]);
        for (std::size_t i = 2; i < w.size(); ++i) {
            if (w[i] == "#POSITION" && i + 2 < w.size()) {
                toInt(w[i + 1], d.column);
                toInt(w[i + 2], d.row);
            } else if (w[i] == "#LEVEL" && i + 1 < w.size()) {
                toInt(w[i + 1], d.level);
            } else if (w[i] == "#TIME" && i + 1 < w.size()) {
                toInt(w[i + 1], d.time);
            } else if (w[i] == "#COST" && i + 1 < w.size()) {
                int n = 0;
                toInt(w[i + 1], n);
                for (int k = 0; k < n && i + 3 + 2 * std::size_t(k) < w.size() + 0; ++k) {
                    const int r = resourceIndex(w[i + 2 + 2 * std::size_t(k)]);
                    int amount = 0;
                    if (r >= 0 && toInt(w[i + 3 + 2 * std::size_t(k)], amount)) d.cost[std::size_t(r)] = amount;
                }
            }
        }
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace oc2::assets
