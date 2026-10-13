#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "assets/text_table.hpp"
#include "assets/xml_tree.hpp"
#include "render/gp_images.hpp"
#include "render/sprite_renderer.hpp"
#include "ui/fonts.hpp"
#include "ui/rich_text.hpp"

namespace oc2::ui {

// Всё, что нужно элементам интерфейса для загрузки и вывода.
struct UiContext {
    render::SpriteRenderer& renderer;
    render::GpImages& images;
    const Fonts& fonts;
    const RichText& text;
    const assets::TextTable& texts;
    std::filesystem::path dataRoot;
};

enum class WidgetKind { Desk, BitPicture, GPPicture, TextButton, VitButton, Other };

// Элемент окна из *.DialogsSystem.xml. Координаты x, y — относительно родителя,
// как в данных; абсолютные (виртуальные 1024×768) считаются при выводе.
struct Widget {
    WidgetKind kind = WidgetKind::Other;
    std::string type; // имя класса из XML
    std::string name;
    int x = 0, y = 0, width = 0, height = 0;
    bool visible = true;
    bool enabled = true;
    std::uint32_t color = 0xFFFFFFFF;
    std::string action; // первое действие из v_Actions («cva_MM_SinStart»)
    std::string hint;   // ключ подсказки

    // Текст (TextButton, VitButton) — уже переведённый, в Windows-1251.
    std::string text;
    const Font* fontPassive = nullptr;
    const Font* fontActive = nullptr;
    Align align = Align::Left;
    int maxWidth = 0; // TextButton: MaxWidth
    int fontDx = 0, fontDy = 0;

    // GPPicture: FileID + SpriteID; VitButton: GP_File.
    render::GpId gp = 0;
    bool hasGp = false;
    int sprite = 0;

    // VitButton
    int state = 0;
    std::array<int, 3> spritePassive{-1, -1, -1};
    std::array<int, 3> spriteOver{-1, -1, -1};
    bool oneSprited = false;
    bool disableCycling = false;
    bool unlimitedLx = false;

    // BitPicture
    render::TextureId bitmap = 0;
    bool hasBitmap = false;
    bool actualSize = false;
    int bitmapWidth = 0, bitmapHeight = 0;

    std::vector<Widget> children;

    // Состояние при работе.
    bool hover = false;
    int absX = 0, absY = 0; // левый верхний угол на последнем кадре
    int drawnW = 0, drawnH = 0;

    Widget* find(std::string_view widgetName);
};

// Окно, загруженное из *.DialogsSystem.xml.
class Dialog {
public:
    Dialog(UiContext& ctx, const std::filesystem::path& xmlPath);

    Widget& root() { return root_; }
    Widget* find(std::string_view name) { return root_.find(name); }

    // Отмечает кнопку под курсором (виртуальные координаты); nullptr — курсор вне кнопок.
    Widget* updateHover(float vx, float vy);
    void draw(const View& view);

    // Типы элементов, которые встретились в файле, но пока не выводятся.
    const std::vector<std::string>& unsupported() const { return unsupported_; }

private:
    Widget parse(const assets::XmlNode& node, std::string_view type);
    void drawWidget(Widget& w, int parentX, int parentY, const View& view);
    void drawVitButton(Widget& w, const View& view);
    void drawTextButton(Widget& w, const View& view);
    int gpHeight(const Widget& w, int sprite);

    UiContext& ctx_;
    Widget root_;
    std::vector<std::string> unsupported_;
};

} // namespace oc2::ui
