#include "assets/unit_desc.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

#include "assets/byte_reader.hpp"

namespace oc2::assets {

namespace {

// Строки файла без «//»-комментариев, разбитые на слова.
std::vector<std::vector<std::string_view>> lines(std::string_view text) {
    std::vector<std::vector<std::string_view>> out;
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
        if (!words.empty()) out.push_back(std::move(words));
    }
    return out;
}

// Как %d у sscanf оригинала: число в начале слова («73/14» в bldflag.md → 73).
bool toInt(std::string_view w, int& out) {
    const char* b = w.data();
    if (b < w.data() + w.size() && *b == '+') ++b;
    const auto [end, ec] = std::from_chars(b, w.data() + w.size(), out);
    return ec == std::errc() && end != b;
}

int intAt(const std::vector<std::string_view>& w, std::size_t i, std::string_view what) {
    int v = 0;
    if (i >= w.size() || !toInt(w[i], v))
        throw FormatError("описание юнита: в строке " + std::string(w[0]) + " нет числа (" + std::string(what) + ")");
    return v;
}

bool equalsNoCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

} // namespace

const UnitSpriteFile* UnitDesc::file(int id) const {
    for (const UnitSpriteFile& f : files)
        if (f.id == id) return &f;
    return nullptr;
}

const UnitAnimation* UnitDesc::animation(std::string_view n) const {
    for (const UnitAnimation& a : animations)
        if (equalsNoCase(a.name, n)) return &a;
    return nullptr;
}

UnitDesc UnitDesc::parse(std::string_view text) {
    UnitDesc d;
    int anmDx = 0, anmDy = 0; // SETANMPARAM действует на следующие анимации (FUN_018dbab0)
    bool sawAnmParam = false;
    for (const auto& w : lines(text)) {
        const std::string_view key = w[0];
        if (key == "[END]") break;
        if (key == "NAME" && w.size() >= 2) {
            d.name = std::string(w[1]);
        } else if (key == "BUILDING") {
            d.building = true;
        } else if (key == "GEOMETRY") {
            d.radius1 = intAt(w, 1, "радиус 1");
            d.radius2 = intAt(w, 2, "радиус 2");
            d.step = intAt(w, 3, "шаг");
        } else if (key == "UNITRADIUS") {
            d.unitRadius = intAt(w, 1, "радиус");
        } else if (key == "USERLC" && w.size() >= 3) {
            UnitSpriteFile f;
            f.id = intAt(w, 1, "номер");
            f.path = std::string(w[2]);
            if (w.size() >= 6 && equalsNoCase(w[3], "SHADOW")) {
                f.dx = intAt(w, 4, "dx");
                f.dy = intAt(w, 5, "dy");
            }
            d.files.push_back(std::move(f));
        } else if (key == "SETANMPARAM") {
            anmDx = intAt(w, 1, "dx");
            anmDy = intAt(w, 2, "dy");
            if (!sawAnmParam) {
                d.pictureDx = anmDx;
                d.pictureDy = anmDy;
                sawAnmParam = true;
            }
        } else if (key == "LOCKPOINTS") {
            const int n = intAt(w, 1, "число точек");
            d.lockPoints.clear();
            for (int i = 0; i < n; ++i)
                d.lockPoints.emplace_back(intAt(w, 2 + 2 * std::size_t(i), "x"), intAt(w, 3 + 2 * std::size_t(i), "y"));
        } else if ((key[0] == '#' || key[0] == '@') && key.size() > 1) {
            UnitAnimation a;
            a.name = std::string(key);
            a.dx = anmDx;
            a.dy = anmDy;
            a.rotations = intAt(w, 1, "повороты");
            if (key[0] == '#') {
                const int n = intAt(w, 2, "кадры");
                for (int i = 0; i < n; ++i)
                    a.frames.push_back({intAt(w, 3 + 2 * std::size_t(i), "файл"),
                                        intAt(w, 4 + 2 * std::size_t(i), "кадр")});
            } else {
                const int file = intAt(w, 2, "файл");
                const int first = intAt(w, 3, "первый кадр");
                const int last = intAt(w, 4, "последний кадр");
                // Диапазон бывает и убывающим («@UATTACK2 9 6 45 0» — кадры 45…0).
                const int step = first <= last ? 1 : -1;
                for (int f = first;; f += step) {
                    a.frames.push_back({file, f});
                    if (f == last) break;
                }
            }
            d.animations.push_back(std::move(a));
        }
    }
    return d;
}

std::vector<std::pair<std::string, std::string>> parseNationMembers(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> out;
    bool inMembers = false;
    for (const auto& w : lines(text)) {
        if (w[0].front() == '[') {
            inMembers = w[0] == "[MEMBERS]";
            continue;
        }
        if (inMembers && w.size() >= 2) out.emplace_back(std::string(w[0]), std::string(w[1]));
    }
    return out;
}

} // namespace oc2::assets
