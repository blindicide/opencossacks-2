#include "assets/bigmap_data.hpp"

#include <charconv>
#include <cstdlib>
#include <sstream>

#include "assets/byte_reader.hpp"

namespace oc2::assets {

namespace {

const char* const kCodes[kBigNations] = {"EN", "FR", "AU", "PR", "RU", "EG", "PO", "SP", "RE"};
const char* const kNames[kBigNations] = {"Англия", "Франция", "Австрия", "Пруссия", "Россия",
                                         "Египет", "Польша", "Испания", "Рейнский союз"};
const char* const kHeroNames[kBigNations] = {"ENGLAND", "FRANCE", "AUSTRIA", "PRUSSIA", "RUSSIA",
                                             "EGYPT", "POLAND", "SPAIN", "REIN"};
const char* const kRankNames[HeroArmy::kRanks] = {"LIEUTENANT", "CAPTAIN", "MAJOR", "COLONEL", "GENERAL", "MARSHAL"};

std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    std::istringstream in{std::string(line)};
    std::string w;
    while (in >> w) out.push_back(std::move(w));
    return out;
}

bool isInteger(const std::string& s) {
    if (s.empty()) return false;
    std::size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// Строки без «\r»; «//» и всё после него — комментарий.
std::vector<std::string> lines(std::string_view text, bool comments) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string line(text.substr(pos, end - pos));
        if (comments)
            if (const auto c = line.find("//"); c != std::string::npos) line.resize(c);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        out.push_back(std::move(line));
        if (end == text.size()) break;
        pos = end + 1;
    }
    return out;
}

int toInt(const std::string& s, int def = 0) {
    int v = def;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

} // namespace

const char* bigNationCode(int nation) { return nation >= 0 && nation < kBigNations ? kCodes[nation] : "NE"; }
const char* bigNationName(int nation) { return nation >= 0 && nation < kBigNations ? kNames[nation] : "Нейтралы"; }

BigMap BigMap::parse(std::string_view text) {
    BigMap map;
    BigSector* cur = nullptr;
    for (const std::string& line : lines(text, false)) {
        const std::vector<std::string> w = words(line);
        if (w.empty()) continue;
        const std::string& key = w[0];
        auto num = [&](std::size_t i) { return i < w.size() ? toInt(w[i]) : 0; };
        if (key == "#MAPSIZE#") {
            map.width = num(1);
            map.height = num(2);
        } else if (key == "#SECTOR#") {
            map.sectors.emplace_back();
            cur = &map.sectors.back();
        } else if (!cur) {
            continue;
        } else if (key == "ID") cur->id = num(1);
        else if (key == "Center") cur->center = {num(1), num(2)};
        else if (key == "SityXY") cur->city = {num(1), num(2)};
        else if (key == "FortXY") cur->fort = {num(1), num(2)};
        else if (key == "DefOwner") cur->defOwner = num(1);
        else if (key == "Defence") cur->defence = num(1);
        else if (key == "Population") cur->population = num(1);
        else if (key == "SectorName") cur->name = w.size() > 1 ? w[1] : "";
        else if (key == "Neighbors")
            for (int i = 0; i < num(1) && std::size_t(2 + i) < w.size(); ++i) cur->neighbors.push_back(num(std::size_t(2 + i)));
        else if (key == "Arrow")
            for (std::size_t i = 1; i + 2 < w.size(); i += 3)
                cur->arrows.push_back({num(i), num(i + 1), num(i + 2)});
        else if (key == "Villages") cur->villages = num(1);
        else if (key == "Ressource") cur->resource = num(1);
        else if (key == "Owner") cur->owner = num(1);
    }
    if (map.sectors.empty()) throw FormatError("Sectors.dat: нет секторов");
    return map;
}

BigMapConst BigMapConst::parse(std::string_view text) {
    BigMapConst c;
    for (const std::string& line : lines(text, true)) {
        std::vector<std::string> w = words(line);
        if (w.empty() || w[0] == "[END]" || w[0][0] == '/') continue;
        std::string key = w[0];
        if (key[0] == '#') key.erase(0, 1);
        w.erase(w.begin());
        c.values[key] = std::move(w);
    }
    return c;
}

int BigMapConst::integer(const std::string& key, int def, std::size_t index) const {
    const auto it = values.find(key);
    if (it == values.end() || index >= it->second.size()) return def;
    return toInt(it->second[index], def);
}

std::uint32_t BigMapConst::color(int nation, std::uint32_t def) const {
    const auto it = values.find("NATCOLOR" + std::to_string(nation));
    if (it == values.end() || it->second.empty()) return def;
    return std::uint32_t(std::strtoul(it->second[0].c_str(), nullptr, 16));
}

HeroArmy HeroArmy::parse(std::string_view text) {
    HeroArmy h;
    int rank = -1;
    for (const std::string& line : lines(text, true)) {
        const std::vector<std::string> w = words(line);
        if (w.empty() || w[0][0] != '#') continue;
        const std::string key = w[0].substr(1);
        bool isRank = false;
        for (int r = 0; r < kRanks; ++r)
            if (key == kRankNames[r]) rank = r, isRank = true;
        if (isRank || rank < 0) continue;
        for (int n = 0; n < kBigNations; ++n) {
            if (key != kHeroNames[n]) continue;
            // «всего, числа…, число особых типов, тип число тип число…»: первое нечисло — начало особых типов
            std::size_t t = 1;
            while (t < w.size() && isInteger(w[t])) ++t;
            if (t < 3) break;
            HeroArmyRow& row = h.rows[std::size_t(rank)][std::size_t(n)];
            row.squads = toInt(w[1]);
            for (std::size_t i = 2; i + 1 < t; ++i) row.layout.push_back(toInt(w[i]));
            for (std::size_t i = t; i + 1 < w.size(); i += 2) row.specials.emplace_back(w[i], toInt(w[i + 1]));
            break;
        }
    }
    return h;
}

BigDirections BigDirections::parse(std::string_view xml) {
    BigDirections d;
    auto field = [&](std::size_t from, std::string_view tag) -> std::pair<std::string_view, std::size_t> {
        const std::string open = "<" + std::string(tag) + ">", close = "</" + std::string(tag) + ">";
        const std::size_t a = xml.find(open, from);
        if (a == std::string_view::npos) return {{}, std::string_view::npos};
        const std::size_t b = xml.find(close, a);
        if (b == std::string_view::npos) return {{}, std::string_view::npos};
        return {xml.substr(a + open.size(), b - a - open.size()), b + close.size()};
    };
    std::size_t at = 0;
    while (true) {
        const std::size_t e = xml.find("<CDirElement>", at);
        if (e == std::string_view::npos) break;
        const auto [from, p1] = field(e, "FromSecID");
        const auto [to, p2] = field(e, "ToSecID");
        const auto [dir, p3] = field(e, "Direction");
        at = e + 13;
        if (p1 == std::string_view::npos || p2 == std::string_view::npos || p3 == std::string_view::npos) continue;
        const int s = dir == "North" ? North : dir == "East" ? East : dir == "South" ? South : dir == "West" ? West : -1;
        if (s >= 0) d.side[{toInt(std::string(from)), toInt(std::string(to))}] = s;
    }
    return d;
}

int BigDirections::sideOf(int from, int to) const {
    const auto it = side.find({from, to});
    return it == side.end() ? -1 : it->second;
}

BigQuests BigQuests::parse(std::string_view text) {
    BigQuests q;
    BigQuest* cur = nullptr;
    for (const std::string& line : lines(text, true)) {
        const std::vector<std::string> w = words(line);
        if (w.empty() || w[0][0] == '/') continue;
        const std::string& key = w[0];
        auto list = [&](std::vector<int>& out) { // «ключ N a b c»
            for (std::size_t i = 2; i < w.size(); ++i) out.push_back(toInt(w[i]));
        };
        if (key == "ports") {
            list(q.ports);
        } else if (key == "[QUEST]") {
            q.quests.emplace_back();
            cur = &q.quests.back();
        } else if (key == "[END]") {
            break;
        } else if (!cur) {
            continue;
        } else if (key == "name" && w.size() > 1) cur->name = w[1];
        else if (key == "owner") list(cur->owners);
        else if (key == "recipient") list(cur->recipients);
        else if (key == "probability" && w.size() > 1) cur->probability = toInt(w[1]);
        else if (key == "turn_limit" && w.size() > 1) cur->turnLimit = toInt(w[1]);
        else if (key == "gold" && w.size() > 1) cur->gold = toInt(w[1]);
    }
    return q;
}

} // namespace oc2::assets
