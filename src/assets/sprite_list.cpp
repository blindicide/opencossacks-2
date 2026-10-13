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
    if (list.gpName == "COMPLEX") {
        list.complex = true;
        return list;
    }

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

} // namespace oc2::assets
