#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "assets/xml_tree.hpp"
#include "render/gp_images.hpp"

namespace oc2::ui {

// Шрифт интерфейса: изображение с 256 кадрами-символами (кадр = байт Windows-1251)
// и цвет, на который умножаются белые символы.
struct Font {
    render::GpId image = 0;
    std::uint32_t color = 0xFFFFFFFF;
};

// Шрифты по именам, как их регистрирует оригинал (FUN_00e76ca0, перечисление FONTS):
// «MenuTitleWhite», «SpecialGrayFont» и т. д.
class Fonts {
public:
    explicit Fonts(render::GpImages& images);

    // nullptr, если такого имени нет.
    const Font* find(std::string_view name) const;

    // Шрифты для тега {F<имя>} разметки текста — из Dialogs\fonts.xml (OneFontParam).
    void loadTagFonts(const assets::XmlNode& root);
    const Font* findTagged(std::string_view name) const;

    // Ширина строки в пикселях: сумма ширин кадров; пробел — как «c», табуляция — 4 пробела.
    int textWidth(const Font& font, std::string_view cp1251) const;
    // Высота строки — высота кадра символа «W».
    int lineHeight(const Font& font) const;

    // Выводит строку: (x, y) — левый верхний угол первого символа. color 0 — цвет шрифта.
    void draw(const Font& font, std::string_view cp1251, float x, float y, std::uint32_t color = 0,
              float scale = 1.0f) const;

    // Ширина одного символа (пробел — как «c», табуляция — 4 пробела, управляющие — 0).
    int glyphWidth(const Font& font, unsigned char c) const;

private:

    render::GpImages& images_;
    std::unordered_map<std::string, Font> fonts_;
    std::unordered_map<std::string, Font> tagFonts_;
};

} // namespace oc2::ui
