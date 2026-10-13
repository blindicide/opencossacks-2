#include "ui/fonts.hpp"

#include <array>

namespace oc2::ui {

namespace {

// Цвета PreDefColor и суффиксы имён меню-шрифтов в порядке оригинала
// («Diasble» — с опечаткой, как в данных).
struct NamedColor {
    const char* suffix;
    std::uint32_t argb;
};
constexpr std::array<NamedColor, 7> kMenuColors{{
    {"Black", 0xFF2E2317},
    {"Red", 0xFF8A1000},
    {"Yellow", 0xFFD4C19C},
    {"White", 0xFFFFF7EF},
    {"Gray", 0xFF6D6862},
    {"Orange", 0xFF6A3000},
    {"Diasble", 0xC0665F57},
}};

struct FixedFont {
    const char* name;
    const char* file;
    std::uint32_t argb;
};

// Именованные шрифты: файл и цвет (адрес объекта + 0x18) из FUN_00e76ca0.
// Цвет 0xFFFFFFFF — оригинал его не задаёт.
constexpr std::array<FixedFont, 33> kFixedFonts{{
    {"fonG12", "FontC10", 0xFF2E2317},
    {"fonSmall_01", "FontS01", 0xFF2E2317},
    {"fonBlow_A", "FontG2a", 0xFFFFFFFF},
    {"fonBlow_S", "FontG2s", 0xFFFFFFFF},
    {"SmallWhiteFont", "FontC12", 0xFFFFF7EF},
    {"SmallRedFont", "FontC12", 0xFF8A1000},
    {"SmallYellowFont", "FontC12", 0xFFD4C19C},
    {"SmallBlackFont", "FontC12", 0xFF2E2317},
    {"SmallOrangeFont", "FontC12", 0xFF6A3000},
    {"SmallGrayFont", "FontC12", 0xFF6D6862},
    {"SmallWhiteFont1", "FontC12", 0xFFFFF7EF},
    {"SmallRedFont1", "FontC12", 0xFF8A1000},
    {"SmallGrayFont1", "FontC12", 0xFF6D6862},
    {"SmallBlackFont1", "FontC12", 0xFF2E2317},
    {"SpecialWhiteFont", "FontG12", 0xFFFFF7EF},
    {"SpecialYellowFont", "FontG12", 0xFFD4C19C},
    {"SpecialRedFont", "FontG12", 0xFF8A1000},
    {"SpecialBlackFont", "FontG12", 0xFF2E2317},
    {"SpecialGrayFont", "FontG12", 0xFF6D6862},
    {"WhiteFont", "FontC14", 0xFFFFF7EF},
    {"YellowFont", "FontC14", 0xFFD4C19C},
    {"RedFont", "FontC14", 0xFF8A1000},
    {"BlackFont", "FontC14", 0xFF2E2317},
    {"OrangeFont", "FontC14", 0xFF6A3000},
    {"GrayFont", "FontC14", 0xFF6D6862},
    {"BigWhiteFont", "FontG18", 0xFFFFF7EF},
    {"BigYellowFont", "FontG18", 0xFFD4C19C},
    {"BigRedFont", "FontG18", 0xFFFF0000},
    {"BigBlackFont", "FontG18", 0xFF2E2317},
    {"MenuGold", "FontG30", 0xFFFFFFFF},
    {"MenuGoldBlack", "FontG30", 0xFF8A1000},
    {"MenuGoldBlack2", "FontG30", 0xFFEAE7DE},
    {"MenuGoldBlack3", "FontG30", 0x12000000},
}};

constexpr unsigned char kSpaceGlyph = 'c'; // ширина пробела в оригинале — ширина «c»

} // namespace

Fonts::Fonts(render::GpImages& images) : images_(images) {
    auto add = [&](const std::string& name, const char* file, std::uint32_t color) {
        fonts_[name] = Font{images_.id(std::string("interf3\\Fonts\\") + file), color};
    };
    for (const FixedFont& f : kFixedFonts) add(f.name, f.file, f.argb);
    for (const NamedColor& c : kMenuColors) {
        add(std::string("MenuText") + c.suffix, "FontG14", c.argb);
        add(std::string("MenuTitle") + c.suffix, "FontG16", c.argb);
        add(std::string("MenuTitle2") + c.suffix, "FontG18", c.argb);
    }
}

const Font* Fonts::find(std::string_view name) const {
    auto it = fonts_.find(std::string(name));
    return it == fonts_.end() ? nullptr : &it->second;
}

void Fonts::loadTagFonts(const assets::XmlNode& root) {
    const assets::XmlNode* list = root.child("Fonts");
    if (!list) return;
    for (const assets::XmlNode& f : list->children) {
        const auto name = f.value("Name");
        const auto file = f.value("gpFont");
        if (!name || !file) continue;
        tagFonts_[*name] = Font{images_.id(*file), std::uint32_t(f.hexValue("DefColor").value_or(0xFF2E2317))};
    }
}

const Font* Fonts::findTagged(std::string_view name) const {
    auto it = tagFonts_.find(std::string(name));
    return it == tagFonts_.end() ? nullptr : &it->second;
}

int Fonts::glyphWidth(const Font& font, unsigned char c) const {
    if (c == ' ') return int(images_.frameWidth(font.image, kSpaceGlyph));
    if (c == '\t') return 4 * int(images_.frameWidth(font.image, kSpaceGlyph));
    if (c < 0x20) return 0;
    return int(images_.frameWidth(font.image, c));
}

int Fonts::textWidth(const Font& font, std::string_view text) const {
    int w = 0;
    for (char c : text) w += glyphWidth(font, static_cast<unsigned char>(c));
    return w;
}

int Fonts::lineHeight(const Font& font) const { return int(images_.frameHeight(font.image, 'W')); }

void Fonts::draw(const Font& font, std::string_view text, float x, float y, std::uint32_t color,
                 float scale) const {
    const std::uint32_t tint = color != 0 ? color : font.color;
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c > ' ') images_.draw(font.image, c, x, y, tint, scale);
        x += float(glyphWidth(font, c)) * scale;
    }
}

} // namespace oc2::ui
