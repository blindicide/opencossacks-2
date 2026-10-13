#include "assets/xml_tree.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>

#include "assets/byte_reader.hpp"

namespace oc2::assets {

namespace {

constexpr int kMaxDepth = 256;

std::string decodeEntities(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '&') {
            if (raw.substr(i, 4) == "&lt;") {
                out.push_back('<');
                i += 3;
                continue;
            }
            if (raw.substr(i, 4) == "&gt;") {
                out.push_back('>');
                i += 3;
                continue;
            }
            if (raw.substr(i, 5) == "&amp;") {
                out.push_back('&');
                i += 4;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    return out;
}

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    XmlNode document() {
        skipSpaceAndComments();
        XmlNode root = element(0);
        skipSpaceAndComments();
        if (p_ != s_.size()) fail("лишние данные после корневого элемента");
        return root;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        const auto line = 1 + std::count(s_.begin(), s_.begin() + std::ptrdiff_t(std::min(p_, s_.size())), '\n');
        throw FormatError("XML, строка " + std::to_string(line) + ": " + what);
    }

    bool startsWith(std::string_view prefix) const { return s_.substr(p_, prefix.size()) == prefix; }

    bool atLineStart() const {
        for (std::size_t i = p_; i > 0; --i) {
            const char c = s_[i - 1];
            if (c == '\n') return true;
            if (c != ' ' && c != '\t' && c != '\r') return false;
        }
        return true;
    }

    void skipSpaceAndComments() {
        while (p_ < s_.size()) {
            const unsigned char c = static_cast<unsigned char>(s_[p_]);
            if (c <= ' ') {
                ++p_;
            } else if (startsWith("//") && atLineStart()) {
                // Комментарии встречаются в нескольких файлах между элементами, всегда с начала
                // строки; «//» внутри значения (например, «//#MM_…») комментарием не считается.
                while (p_ < s_.size() && s_[p_] != '\n') ++p_;
            } else {
                break;
            }
        }
    }

    std::string tagName() {
        const std::size_t start = p_;
        while (p_ < s_.size() && s_[p_] != '>') {
            if (s_[p_] == '<' || s_[p_] == '\n') fail("незакрытый тег");
            ++p_;
        }
        if (p_ >= s_.size()) fail("незакрытый тег");
        std::string name(s_.substr(start, p_ - start));
        ++p_; // '>'
        return name;
    }

    // Файлы состояний шейдеров — обычный XML с атрибутами: <Tag Name="value">.
    // В диалогах имена тегов сами содержат пробелы, поэтому атрибуты выделяются,
    // только если в имени есть «="».
    void splitAttributes(XmlNode& node) {
        if (node.name.find("=\"") == std::string::npos) return;
        const std::string full = node.name;
        std::size_t i = full.find(' ');
        node.name = full.substr(0, i);
        while (i != std::string::npos && i < full.size()) {
            while (i < full.size() && full[i] == ' ') ++i;
            if (i >= full.size()) break;
            const std::size_t eq = full.find("=\"", i);
            if (eq == std::string::npos) fail("ошибка в атрибутах тега <" + full + ">");
            const std::size_t close = full.find('"', eq + 2);
            if (close == std::string::npos) fail("ошибка в атрибутах тега <" + full + ">");
            node.attributes.emplace_back(full.substr(i, eq - i), decodeEntities(full.substr(eq + 2, close - eq - 2)));
            i = close + 1;
        }
    }

    XmlNode element(int depth) {
        if (depth > kMaxDepth) fail("слишком глубокая вложенность");
        if (!startsWith("<") || startsWith("</")) fail("ожидался открывающий тег");
        ++p_;
        XmlNode node;
        node.name = tagName();
        splitAttributes(node);
        const std::string closing = "</" + node.name + ">";

        // Если после пробелов идёт открывающий тег — это узел с детьми, иначе — лист.
        const std::size_t contentStart = p_;
        skipSpaceAndComments();
        if (startsWith("<") && !startsWith("</")) {
            while (true) {
                skipSpaceAndComments();
                if (p_ >= s_.size()) fail("нет закрывающего тега для <" + node.name + ">");
                if (startsWith("</")) {
                    if (!startsWith(closing)) fail("ожидался " + closing);
                    p_ += closing.size();
                    return node;
                }
                node.children.push_back(element(depth + 1));
            }
        }
        p_ = contentStart;
        const std::size_t end = s_.find(closing, p_);
        if (end == std::string_view::npos) fail("нет закрывающего тега для <" + node.name + ">");
        node.text = decodeEntities(s_.substr(p_, end - p_));
        p_ = end + closing.size();
        return node;
    }

    std::string_view s_;
    std::size_t p_ = 0;
};

std::string trimmed(std::string_view v) {
    while (!v.empty() && static_cast<unsigned char>(v.front()) <= ' ') v.remove_prefix(1);
    while (!v.empty() && static_cast<unsigned char>(v.back()) <= ' ') v.remove_suffix(1);
    return std::string(v);
}

} // namespace

const XmlNode* XmlNode::child(std::string_view childName) const {
    for (const XmlNode& c : children)
        if (c.name == childName) return &c;
    return nullptr;
}

std::optional<std::string> XmlNode::value(std::string_view childName) const {
    const XmlNode* c = child(childName);
    if (!c) return std::nullopt;
    return c->text;
}

std::optional<long> XmlNode::intValue(std::string_view childName) const {
    auto v = value(childName);
    if (!v) return std::nullopt;
    const std::string t = trimmed(*v);
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const long r = std::strtol(t.c_str(), &end, 10);
    if (errno != 0 || *end != '\0') return std::nullopt;
    return r;
}

std::optional<double> XmlNode::floatValue(std::string_view childName) const {
    auto v = value(childName);
    if (!v) return std::nullopt;
    const std::string t = trimmed(*v);
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    const double r = std::strtod(t.c_str(), &end);
    if (*end != '\0') return std::nullopt;
    return r;
}

std::optional<bool> XmlNode::boolValue(std::string_view childName) const {
    auto v = value(childName);
    if (!v) return std::nullopt;
    const std::string t = trimmed(*v);
    if (t == "true") return true;
    if (t == "false") return false;
    return std::nullopt;
}

std::optional<unsigned long> XmlNode::hexValue(std::string_view childName) const {
    auto v = value(childName);
    if (!v) return std::nullopt;
    const std::string t = trimmed(*v);
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    const unsigned long r = std::strtoul(t.c_str(), &end, 16);
    if (*end != '\0') return std::nullopt;
    return r;
}

XmlNode parseXml(std::string_view document) { return Parser(document).document(); }

} // namespace oc2::assets
