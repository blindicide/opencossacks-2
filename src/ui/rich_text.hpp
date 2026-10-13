#pragma once

#include <cstdint>
#include <string_view>

#include "ui/fonts.hpp"

namespace oc2::ui {

// Выравнивание строк (TEXTALIGN оригинала).
enum class Align { Left = 0, Center = 1, Right = 2 };

// Перевод виртуальных координат интерфейса (1024×768) в пиксели экрана.
struct View {
    float ox = 0.0f, oy = 0.0f, scale = 1.0f;
    float x(float vx) const { return ox + vx * scale; }
    float y(float vy) const { return oy + vy * scale; }
};

struct TextSize {
    int width = 0;  // самая длинная строка
    int height = 0; // сумма высот строк
};

// Упрощённый DrawMultilineText оригинала: текст в Windows-1251 с разметкой.
// Поддерживается:
//  «\» и «\r\n» — перевод строки; перенос по пробелам, если строка шире maxWidth;
//  {C AARRGGBB} / {C 0xAARRGGBB} / {CR} {CG} {CB} {CD} {CN} {CW} {CY} — цвет, {C} — прежний цвет;
//  {F<имя>} — шрифт из Dialogs\fonts.xml, {F} — исходный шрифт;
//  {AC} {AR} {AL} — выравнивание, {A} — исходное.
// Прочие теги ({R …} — ссылки, {I…}, {P …}, {G …} — картинки) пропускаются.
// Строки выравниваются внутри maxWidth, если она меньше 2000, иначе — внутри
// ширины самой длинной строки, как в оригинале.
class RichText {
public:
    explicit RichText(const Fonts& fonts) : fonts_(fonts) {}

    TextSize measure(const Font& font, std::string_view text, int maxWidth, Align align) const;
    // (x, y) — левый верхний угол блока текста в виртуальных координатах.
    // alphaOverride != 0 заменяет альфу всех цветов (недоступные кнопки).
    TextSize draw(const Font& font, std::string_view text, int x, int y, int maxWidth, Align align,
                  const View& view, std::uint32_t alphaOverride = 0) const;

private:
    TextSize run(const Font& font, std::string_view text, int x, int y, int maxWidth, Align align,
                 const View* view, std::uint32_t alphaOverride) const;

    const Fonts& fonts_;
};

} // namespace oc2::ui
