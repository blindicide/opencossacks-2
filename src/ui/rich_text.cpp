#include "ui/rich_text.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace oc2::ui {

namespace {

struct Item {
    unsigned char c = 0; // 0 — перевод строки
    const Font* font = nullptr;
    std::uint32_t color = 0;
    Align align = Align::Left;
};

struct Line {
    std::size_t begin = 0, end = 0; // [begin, end) в массиве Item
    int width = 0;
    int height = 0;
    Align align = Align::Left;
};

// Цвета однобуквенных тегов {CX} (FUN_01058920).
bool namedColor(char letter, std::uint32_t& argb) {
    switch (letter) {
    case 'B': argb = 0xFF000000; return true;
    case 'R': argb = 0xFFB83B3F; return true;
    case 'D': argb = 0xFFFF0000; return true;
    case 'N': argb = 0xFF502515; return true;
    case 'W': argb = 0xFFFFFFFF; return true;
    case 'Y': argb = 0xFFFFFF00; return true;
    case 'G': argb = 0xFF60A05A; return true;
    default: return false;
    }
}

} // namespace

TextSize RichText::measure(const Font& font, std::string_view text, int maxWidth, Align align) const {
    return run(font, text, 0, 0, maxWidth, align, nullptr, 0);
}

TextSize RichText::draw(const Font& font, std::string_view text, int x, int y, int maxWidth, Align align,
                        const View& view, std::uint32_t alphaOverride) const {
    return run(font, text, x, y, maxWidth, align, &view, alphaOverride);
}

TextSize RichText::run(const Font& base, std::string_view text, int x, int y, int maxWidth, Align baseAlign,
                       const View* view, std::uint32_t alphaOverride) const {
    // 1. Разбор разметки в последовательность символов с их шрифтом, цветом и выравниванием.
    std::vector<Item> items;
    items.reserve(text.size());
    const Font* font = &base;
    std::uint32_t color = base.color;
    std::vector<std::uint32_t> colorStack;
    Align align = baseAlign;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '{') {
            const std::size_t close = text.find('}', i + 1);
            if (close != std::string_view::npos) {
                const std::string_view tag = text.substr(i + 1, close - i - 1);
                i = close;
                if (!tag.empty() && tag[0] == 'C' && (tag.size() <= 2 || tag[1] == ' ')) {
                    if (tag.size() == 1) {
                        if (!colorStack.empty()) {
                            color = colorStack.back();
                            colorStack.pop_back();
                        } else {
                            color = font->color;
                        }
                    } else {
                        std::uint32_t c = color;
                        bool ok = false;
                        if (tag[1] == ' ') {
                            const std::string hex(tag.substr(2));
                            char* endp = nullptr;
                            c = std::uint32_t(std::strtoul(hex.c_str(), &endp, 16));
                            ok = endp != hex.c_str();
                        } else {
                            ok = namedColor(tag[1], c);
                        }
                        if (ok) {
                            if (colorStack.size() < 63) colorStack.push_back(color);
                            color = c;
                        }
                    }
                } else if (!tag.empty() && tag[0] == 'F') {
                    if (tag.size() == 1) {
                        font = &base;
                        color = base.color;
                    } else if (const Font* f = fonts_.findTagged(tag.substr(1))) {
                        font = f;
                        color = f->color;
                    }
                } else if (!tag.empty() && tag[0] == 'A' && tag.size() < 3) {
                    if (tag.size() == 1) align = baseAlign;
                    else if (tag[1] == 'C') align = Align::Center;
                    else if (tag[1] == 'R') align = Align::Right;
                    else if (tag[1] == 'L') align = Align::Left;
                }
                // Остальные теги (ссылки, картинки) пока не выводятся.
                continue;
            }
        }
        if (ch == '\\' || ch == '\n') {
            items.push_back({0, font, color, align});
            continue;
        }
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            items.push_back({0, font, color, align});
            continue;
        }
        items.push_back({static_cast<unsigned char>(ch), font, color, align});
    }

    // 2. Разбиение на строки: по переводам строк и по ширине (перенос по пробелу).
    std::vector<Line> lines;
    std::size_t start = 0;
    auto finishLine = [&](std::size_t end) {
        Line line;
        line.begin = start;
        line.end = end;
        line.align = start < items.size() ? items[start].align : align;
        line.height = fonts_.lineHeight(start < items.size() ? *items[start].font : *font);
        for (std::size_t k = start; k < end; ++k) {
            line.width += fonts_.glyphWidth(*items[k].font, items[k].c);
            line.height = std::max(line.height, fonts_.lineHeight(*items[k].font));
        }
        lines.push_back(line);
    };

    int width = 0;
    std::size_t lastSpace = std::string::npos;
    for (std::size_t k = 0; k < items.size(); ++k) {
        const Item& it = items[k];
        if (it.c == 0) {
            finishLine(k);
            start = k + 1;
            width = 0;
            lastSpace = std::string::npos;
            continue;
        }
        // Пробел в начале перенесённой строки не выводится.
        if (it.c == ' ' && k == start && !lines.empty() && items[k - 1].c != 0) {
            ++start;
            continue;
        }
        const int w = fonts_.glyphWidth(*it.font, it.c);
        if (width + w > maxWidth && k > start) {
            if (lastSpace != std::string::npos && lastSpace > start) {
                finishLine(lastSpace);
                start = lastSpace + 1;
            } else {
                finishLine(k);
                start = k;
            }
            width = 0;
            for (std::size_t j = start; j < k; ++j) width += fonts_.glyphWidth(*items[j].font, items[j].c);
            lastSpace = std::string::npos;
            if (it.c == ' ' && k == start) {
                ++start;
                continue;
            }
        }
        if (it.c == ' ') lastSpace = k;
        width += w;
    }
    finishLine(items.size());

    TextSize size;
    for (const Line& l : lines) {
        size.width = std::max(size.width, l.width);
        size.height += l.height;
    }
    if (!view) return size;

    // 3. Вывод: строки выравниваются внутри блока.
    const int blockWidth = maxWidth < 2000 ? maxWidth : size.width;
    int penY = y;
    for (const Line& l : lines) {
        int penX = x;
        if (l.align == Align::Center) penX += (blockWidth - l.width) / 2;
        else if (l.align == Align::Right) penX += blockWidth - l.width;
        for (std::size_t k = l.begin; k < l.end; ++k) {
            const Item& it = items[k];
            std::uint32_t c = it.color;
            if (alphaOverride != 0) c = (c & 0x00FFFFFF) | (alphaOverride & 0xFF000000);
            const char glyph = char(it.c);
            fonts_.draw(*it.font, std::string_view(&glyph, 1), view->x(float(penX)), view->y(float(penY)), c,
                        view->scale);
            penX += fonts_.glyphWidth(*it.font, it.c);
        }
        penY += l.height;
    }
    return size;
}

} // namespace oc2::ui
