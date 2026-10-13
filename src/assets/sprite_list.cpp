#include "assets/sprite_list.hpp"

#include <charconv>

#include "assets/byte_reader.hpp"

namespace oc2::assets {

namespace {

// Чтение по словам и строкам, как Gscanf("%s"/"%d") и пропуск до конца строки.
class Tokens {
public:
    explicit Tokens(std::string_view text) : text_(text) {}

    bool word(std::string& out) {
        while (pos_ < text_.size() && isSpace(text_[pos_])) ++pos_;
        if (pos_ >= text_.size()) return false;
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !isSpace(text_[pos_])) ++pos_;
        out.assign(text_.substr(start, pos_ - start));
        return true;
    }

    bool number(int& out) {
        std::string w;
        if (!word(w)) return false;
        const auto [end, ec] = std::from_chars(w.data(), w.data() + w.size(), out);
        return ec == std::errc() && end == w.data() + w.size();
    }

    // Следующее слово, только если оно на той же строке.
    bool wordOnLine(std::string& out) {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r')) ++pos_;
        if (pos_ >= text_.size() || text_[pos_] == '\n') return false;
        return word(out);
    }

    void skipLine() {
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
        if (pos_ < text_.size()) ++pos_;
    }

private:
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

SpriteList SpriteList::parse(std::string_view text) {
    Tokens in(text);
    SpriteList list;
    int count = 0;
    if (!in.word(list.gpName) || !in.number(count) || count < 0)
        throw FormatError("список спрайтов: нет заголовка «ИМЯ количество»");
    in.skipLine();
    list.complex = list.gpName == "COMPLEX";

    // Как в оригинале, строки-комментарии тоже входят в счёт объявленных объектов.
    int frame = 0;
    for (int i = 0; i < count; ++i) {
        std::string id;
        if (!in.word(id)) break;
        if (id[0] == '/' || id[0] == '#') { // «#» — диапазоны анимаций, пока не нужны
            in.skipLine();
            continue;
        }
        SpriteListEntry e;
        if (!in.number(e.dx) || !in.number(e.dy) || !in.number(e.radius))
            throw FormatError("список спрайтов: у " + id + " нет трёх чисел «dx dy радиус»");
        if (list.complex) {
            std::string anim;
            int w = 0, h = 0;
            if (in.wordOnLine(anim) && anim == "#FIELDPATH" && in.number(w) && in.number(h) && in.number(e.growStage) &&
                in.number(e.yScale))
                e.field = true;
        }
        in.skipLine();
        e.frame = frame++;
        if (id[0] == '@') {
            e.id = id.substr(1) + "F";
            list.entries.push_back(e);
            e.id = id.substr(1) + "B";
            e.dx = -e.dx;
            list.entries.push_back(e);
        } else {
            e.id = std::move(id);
            list.entries.push_back(std::move(e));
        }
    }
    return list;
}

std::unordered_map<std::string, SpriteTransform> SpriteList::parseTransforms(std::string_view rsr,
                                                                             std::string_view section) {
    std::unordered_map<std::string, SpriteTransform> out;
    std::size_t pos = 0;
    bool inside = false;
    while (pos < rsr.size()) {
        std::size_t end = rsr.find('\n', pos);
        if (end == std::string_view::npos) end = rsr.size();
        Tokens line(rsr.substr(pos, end - pos));
        pos = end + 1;
        std::string id;
        if (!line.word(id) || id.rfind("//", 0) == 0) continue;
        if (id[0] == '[') {
            inside = id == section;
            continue;
        }
        SpriteTransform t;
        if (inside && line.word(t.next) && line.number(t.amount)) out.emplace(std::move(id), std::move(t));
    }
    return out;
}

std::unordered_map<std::string, SpriteSource> SpriteList::parseSources(std::string_view rsr) {
    std::unordered_map<std::string, SpriteSource> out;
    std::size_t pos = 0;
    bool inSources = false;
    while (pos < rsr.size()) {
        std::size_t end = rsr.find('\n', pos);
        if (end == std::string_view::npos) end = rsr.size();
        Tokens line(rsr.substr(pos, end - pos));
        pos = end + 1;
        std::string id;
        if (!line.word(id) || id.rfind("//", 0) == 0) continue;
        if (id[0] == '[') {
            inSources = id == "[SOURCES]";
            continue;
        }
        if (!inSources) continue;
        SpriteSource src;
        if (!line.word(src.resource) || !line.number(src.perWork) || !line.number(src.radius)) continue;
        out.emplace(std::move(id), std::move(src));
    }
    return out;
}

} // namespace oc2::assets
