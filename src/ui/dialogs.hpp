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

enum class WidgetKind {
    Desk, // DialogsDesk и панели со списками (ListDesk, TabDesk) — пока только как контейнеры
    BitPicture,
    GPPicture,
    TextButton,
    VitButton,
    GPTextButton,
    CheckBox,
    ColoredBar,
    ComboBox,
    Other
};

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
    // Действия из v_Actions: класс («cva_MM_Start») и его поле Name («Single»).
    struct Action {
        std::string type;
        std::string name;
    };
    std::vector<Action> actions;
    std::string hint;   // ключ подсказки
    int id = 0;         // поле ID (у счётчиков ресурсов — номер ресурса)

    // Текст (TextButton, VitButton) — уже переведённый, в Windows-1251.
    std::string text;
    const Font* fontPassive = nullptr;
    const Font* fontActive = nullptr;
    const Font* fontDisabled = nullptr;
    Align align = Align::Left;
    int maxWidth = 0; // TextButton: MaxWidth
    int fontDx = 0, fontDy = 0;

    // GPPicture: FileID + SpriteID; VitButton: GP_File.
    render::GpId gp = 0;
    bool hasGp = false;
    int sprite = 0;

    // GP_TextButton: Sprite — под курсором, Sprite1 — обычный.
    int spriteNormal = -1;

    // CheckBox
    bool checked = false;
    bool central = false;
    int spriteActive = -1; // отмеченный; spriteOver[0] и spritePassive[0] — под курсором и обычный

    // ColoredBar: Style 0 — заливка, иначе рамка.
    int barStyle = 0;

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

    // ListDesk: образец строки (<Element>), поля, выбранная строка, сдвиг прокрутки.
    // Строки — дети-копии образца, их создаёт Dialog::setListItems().
    std::shared_ptr<const Widget> listElement;
    int marginX = 0, marginY = 0;
    int currentElement = -1;
    int scrollY = 0;
    // TabButton: Group; TabDesk: ParentDialogID — вкладка видна, когда нажата кнопка её группы.
    std::string group;
    // У строки списка — её номер (−1 у прочих элементов).
    int listIndex = -1;

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

    // Обходит все элементы (для покадровых действий SetFrameState).
    template <class F> void forEach(F&& f) { forEachIn(root_, f); }

    // Отмечает кнопку под курсором (виртуальные координаты); nullptr — курсор вне кнопок.
    Widget* updateHover(float vx, float vy);
    void draw(const View& view);

    // Заполняет ListDesk строками (текст — Windows-1251), сохраняя выбор (FUN_013d6530).
    void setListItems(Widget& list, const std::vector<std::string>& texts);
    // Выбор строки (va_ListItem, FUN_013d4bb0): у прежней State = 0, у новой — 1.
    static void selectListItem(Widget& list, int index);
    // Список, которому принадлежит строка (nullptr — не строка списка).
    Widget* listOf(const Widget& row);
    // Прокрутка списка под точкой (виртуальные координаты) на rows строк.
    void scrollListAt(float vx, float vy, int rows);

    // Типы элементов, которые встретились в файле, но пока не выводятся.
    const std::vector<std::string>& unsupported() const { return unsupported_; }

private:
    template <class F> static void forEachIn(Widget& w, F& f) {
        f(w);
        for (Widget& c : w.children) forEachIn(c, f);
    }
    Widget parse(const assets::XmlNode& node, std::string_view type);
    void drawWidget(Widget& w, int parentX, int parentY, const View& view);
    void drawVitButton(Widget& w, const View& view);
    void drawTextButton(Widget& w, const View& view);
    void drawGpTextButton(Widget& w, const View& view);
    void drawCheckBox(Widget& w, const View& view);
    int gpHeight(const Widget& w, int sprite);

    UiContext& ctx_;
    Widget root_;
    std::vector<std::string> unsupported_;
};

} // namespace oc2::ui
