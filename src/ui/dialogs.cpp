#include "ui/dialogs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "assets/game_paths.hpp"
#include "assets/image_decode.hpp"
#include "assets/text_encoding.hpp"

namespace oc2::ui {

namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("не удалось открыть " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string gamePath(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

WidgetKind kindOf(std::string_view type) {
    if (type == "DialogsDesk" || type.empty()) return WidgetKind::Desk;
    if (type == "BitPicture") return WidgetKind::BitPicture;
    if (type == "GPPicture") return WidgetKind::GPPicture;
    if (type == "TextButton") return WidgetKind::TextButton;
    if (type == "VitButton") return WidgetKind::VitButton;
    return WidgetKind::Other;
}

Align alignOf(std::string_view s) {
    if (s == "Center") return Align::Center;
    if (s == "Right") return Align::Right;
    return Align::Left;
}

} // namespace

Widget* Widget::find(std::string_view widgetName) {
    if (name == widgetName) return this;
    for (Widget& c : children)
        if (Widget* w = c.find(widgetName)) return w;
    return nullptr;
}

Dialog::Dialog(UiContext& ctx, const fs::path& xmlPath) : ctx_(ctx) {
    const assets::XmlNode doc = assets::parseXml(readAll(xmlPath));
    root_ = parse(doc, "DialogsDesk");
}

Widget Dialog::parse(const assets::XmlNode& node, std::string_view type) {
    Widget w;
    w.type = std::string(type);
    w.kind = kindOf(type);
    w.name = node.value("Name").value_or("");
    w.x = int(node.intValue("x").value_or(0));
    w.y = int(node.intValue("y").value_or(0));
    w.width = int(node.intValue("Width").value_or(0));
    w.height = int(node.intValue("Height").value_or(0));
    w.visible = node.boolValue("Visible").value_or(true);
    w.enabled = node.boolValue("Enabled").value_or(true);
    w.color = std::uint32_t(node.hexValue("Color").value_or(0xFFFFFFFF));
    w.hint = node.value("Hint").value_or("");
    if (const assets::XmlNode* actions = node.child("v_Actions"))
        for (const assets::XmlNode& a : actions->children) w.actions.push_back({a.name, a.value("Name").value_or("")});

    auto font = [&](const char* field) -> const Font* {
        const std::string fname = node.value(field).value_or("");
        if (fname.empty()) return nullptr;
        const Font* f = ctx_.fonts.find(fname);
        if (!f) std::fprintf(stderr, "интерфейс: нет шрифта %s\n", fname.c_str());
        return f;
    };
    auto message = [&]() {
        std::string key = node.value("Message").value_or("");
        if (key.empty()) key = node.value("Text").value_or("");
        // Ключ в XML — байты Windows-1251; таблица отдаёт UTF-8.
        return key.empty() ? std::string() : assets::utf8ToCp1251(ctx_.texts.lookup(key));
    };

    switch (w.kind) {
    case WidgetKind::BitPicture: {
        const std::string file = node.value("FileName").value_or("");
        w.actualSize = node.boolValue("ActualSize").value_or(false);
        if (file.empty()) break;
        if (auto path = assets::findCaseInsensitive(ctx_.dataRoot, gamePath(file))) {
            try {
                const std::string bytes = readAll(*path);
                const assets::Image img = assets::decodeImage(
                    {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
                w.bitmapWidth = int(img.width);
                w.bitmapHeight = int(img.height);
                w.bitmap = ctx_.renderer.createTexture(img);
                w.hasBitmap = true;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "интерфейс: %s: %s\n", file.c_str(), e.what());
            }
        } else {
            std::fprintf(stderr, "интерфейс: нет картинки %s\n", file.c_str());
        }
        break;
    }
    case WidgetKind::GPPicture: {
        const std::string file = node.value("FileID").value_or("");
        if (!file.empty()) {
            w.gp = ctx_.images.id(file);
            w.hasGp = true;
        }
        w.sprite = int(node.intValue("SpriteID").value_or(0)) & 0xFFF;
        break;
    }
    case WidgetKind::TextButton:
        w.text = message();
        w.fontActive = font("ActiveFont");
        w.fontPassive = font("PassiveFont");
        w.align = alignOf(node.value("Align").value_or("Left"));
        w.maxWidth = int(node.intValue("MaxWidth").value_or(0));
        break;
    case WidgetKind::VitButton: {
        w.text = message();
        w.fontActive = font("FontOver");
        w.fontPassive = font("FontPassive");
        w.align = alignOf(node.value("Align").value_or("Left"));
        w.fontDx = int(node.intValue("FontDx").value_or(0));
        w.fontDy = int(node.intValue("FontDy").value_or(0));
        w.state = std::clamp(int(node.intValue("State").value_or(0)), 0, 2);
        w.oneSprited = node.boolValue("OneSprited").value_or(false);
        w.disableCycling = node.boolValue("DisableCycling").value_or(false);
        w.unlimitedLx = node.boolValue("UnlimitedLx").value_or(false);
        for (int i = 0; i < 3; ++i) {
            w.spritePassive[i] = int(node.intValue("SpritePassive" + std::to_string(i)).value_or(-1));
            w.spriteOver[i] = int(node.intValue("SpriteOver" + std::to_string(i)).value_or(-1));
        }
        const std::string file = node.value("GP_File").value_or("");
        if (!file.empty()) {
            w.gp = ctx_.images.id(file);
            w.hasGp = true;
        }
        break;
    }
    case WidgetKind::Other:
        if (std::find(unsupported_.begin(), unsupported_.end(), w.type) == unsupported_.end()) {
            unsupported_.push_back(w.type);
            std::fprintf(stderr, "интерфейс: элемент %s пока не поддерживается\n", w.type.c_str());
        }
        break;
    case WidgetKind::Desk:
        break;
    }

    if (const assets::XmlNode* kids = node.child("ChildDialogs"))
        for (const assets::XmlNode& c : kids->children) w.children.push_back(parse(c, c.name));
    return w;
}

Widget* Dialog::updateHover(float vx, float vy) {
    Widget* top = nullptr;
    // Обход в порядке вывода: последний подходящий — верхний.
    auto visit = [&](auto& self, Widget& w) -> void {
        w.hover = false;
        if (!w.visible) return;
        // Панели с действиями (cva_M_ModalDesk) — не кнопки: их действия покадровые.
        const bool clickable = w.kind == WidgetKind::VitButton || w.kind == WidgetKind::TextButton ||
                               (w.kind != WidgetKind::Desk && !w.actions.empty());
        if (clickable && w.enabled && vx >= float(w.absX) && vx < float(w.absX + w.drawnW) && vy >= float(w.absY) &&
            vy < float(w.absY + w.drawnH))
            top = &w;
        for (Widget& c : w.children) self(self, c);
    };
    visit(visit, root_);
    if (top) top->hover = true;
    return top;
}

void Dialog::draw(const View& view) { drawWidget(root_, 0, 0, view); }

int Dialog::gpHeight(const Widget& w, int sprite) {
    if (!w.hasGp || sprite < 0) return 0;
    return int(ctx_.images.frameHeight(w.gp, std::size_t(sprite)));
}

void Dialog::drawWidget(Widget& w, int parentX, int parentY, const View& view) {
    if (!w.visible) return;
    w.absX = parentX + w.x;
    w.absY = parentY + w.y;
    w.drawnW = w.width;
    w.drawnH = w.height;

    switch (w.kind) {
    case WidgetKind::BitPicture:
        if (w.hasBitmap) {
            const float bw = float(w.actualSize || w.width <= 0 ? w.bitmapWidth : w.width);
            const float bh = float(w.actualSize || w.height <= 0 ? w.bitmapHeight : w.height);
            ctx_.renderer.drawQuad(w.bitmap, view.x(float(w.absX)), view.y(float(w.absY)), bw * view.scale,
                                   bh * view.scale, render::tintToVertexColor(w.color));
        }
        break;
    case WidgetKind::GPPicture:
        // FUN_0103ef20: размер элемента — размер кадра.
        if (w.hasGp) {
            w.drawnW = int(ctx_.images.frameWidth(w.gp, std::size_t(w.sprite)));
            w.drawnH = int(ctx_.images.frameHeight(w.gp, std::size_t(w.sprite)));
            ctx_.images.draw(w.gp, std::size_t(w.sprite), view.x(float(w.absX)), view.y(float(w.absY)), w.color,
                             view.scale);
        }
        break;
    case WidgetKind::TextButton: drawTextButton(w, view); break;
    case WidgetKind::VitButton: drawVitButton(w, view); break;
    case WidgetKind::Desk:
    case WidgetKind::Other: break;
    }

    for (Widget& c : w.children) drawWidget(c, w.absX, w.absY, view);
}

// FUN_0103f960: ширина надписи по тексту, рамка элемента подстраивается под выравнивание.
void Dialog::drawTextButton(Widget& w, const View& view) {
    const Font* font = nullptr;
    if (!w.enabled) font = w.fontPassive; // DisabledFont пока не загружаем
    else if (w.hover && w.fontActive) font = w.fontActive;
    else font = w.fontPassive;
    if (!font || w.text.empty()) return;

    const int limit = std::max(w.maxWidth == 0 ? 100000 : w.maxWidth, 60);
    const TextSize size = ctx_.text.measure(*font, w.text, limit, Align::Left);
    const int center = w.absX + w.width / 2;
    int x = w.absX;
    switch (w.align) {
    case Align::Left: break;
    case Align::Center:
        x = center - size.width / 2;
        if (w.maxWidth != 0 && w.maxWidth < 2000) x = center - w.maxWidth / 2;
        break;
    case Align::Right: x = w.absX + w.width - size.width; break;
    }
    w.absX = x;
    w.drawnW = size.width;
    w.drawnH = size.height;
    ctx_.text.draw(*font, w.text, x, w.absY, limit, w.align == Align::Center ? Align::Center : Align::Left, view);
}

// VitButton::_Draw (0xcdbc28).
void Dialog::drawVitButton(Widget& w, const View& view) {
    // GetHeightGP: высота — наибольшая из картинок обычного состояния, иначе по тексту.
    int h = 0;
    if (!w.disableCycling) h = std::max(gpHeight(w, w.spritePassive[0]), gpHeight(w, w.spritePassive[1]));
    if (h == 0) h = w.height;
    const int lx = std::max(w.unlimitedLx ? 100000 : w.width, 24);
    w.drawnW = lx;
    w.drawnH = h;
    const int x1 = w.absX, y1 = w.absY, y2 = y1 - 1 + h;

    const bool over = w.hover && w.enabled;
    const int sprite = over ? w.spriteOver[std::size_t(w.state)] : w.spritePassive[std::size_t(w.state)];
    if (sprite >= 0 && w.hasGp) {
        auto clip = [&](int cx1, int cy1, int cx2, int cy2) {
            // Пересечение с рамкой кнопки (x2, y2 включительно).
            cx1 = std::max(cx1, x1);
            cy1 = std::max(cy1, y1);
            cx2 = std::min(cx2, x1 + lx);
            cy2 = std::min(cy2, y2 + 1);
            ctx_.renderer.setClip(int(std::floor(view.x(float(cx1)))), int(std::floor(view.y(float(cy1)))),
                                  int(std::ceil(view.x(float(cx2)))), int(std::ceil(view.y(float(cy2)))));
        };
        auto show = [&](int s, int sx, int sy) {
            if (s >= 0) ctx_.images.draw(w.gp, std::size_t(s), view.x(float(sx)), view.y(float(sy)), 0xFFFFFFFF,
                                         view.scale);
        };
        if (w.disableCycling) {
            clip(x1, y1, x1 + lx, y2 + 1);
            show(sprite, x1, y1);
        } else {
            // FUN_00e2a010: левый и правый края, середина — повтор кадров по смещению % 3.
            const int left = w.oneSprited ? -1 : sprite;
            const int right = w.oneSprited ? -1 : sprite + 1;
            const std::array<int, 3> mid = w.oneSprited ? std::array<int, 3>{sprite, sprite, sprite}
                                                        : std::array<int, 3>{sprite + 2, sprite + 3, sprite + 4};
            auto widthOf = [&](int s) {
                if (s < 0) return 0;
                const int v = int(ctx_.images.frameWidth(w.gp, std::size_t(s)));
                return v > 0x800 ? 0 : v;
            };
            const int lw = widthOf(left), rw = widthOf(right);
            clip(x1 + lw, y1 - 32, x1 + lx - rw, y1 + 128);
            for (int off = 0, n = 0; off < lx && n < 300; ++n) {
                const int s = mid[std::size_t(off % 3)];
                show(s, x1 + off, y1);
                const int step = widthOf(s);
                if (step <= 0) break;
                off += step;
            }
            clip(x1, y1 - 32, x1 + lx, y1 + 128);
            show(left, x1, y1);
            show(right, x1 + lx - rw, y1);
        }
        ctx_.renderer.resetClip();
    }

    const Font* font = nullptr;
    if (w.fontActive && w.enabled && (w.hover || w.state == 1)) font = w.fontActive;
    else font = w.fontPassive;
    if (!font || w.text.empty()) return;
    const std::uint32_t alpha = w.enabled ? 0 : 0x60000000;
    const TextSize size = ctx_.text.measure(*font, w.text, lx, w.align);
    const int ty = ((y1 + y2) - size.height) / 2;
    ctx_.text.draw(*font, w.text, x1 + w.fontDx, ty + w.fontDy, lx, w.align, view, alpha);
}

} // namespace oc2::ui
