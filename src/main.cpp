#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "assets/g2d.hpp"
#include "assets/game_paths.hpp"
#include "assets/image.hpp"
#include "core/version.hpp"
#include "platform/audio.hpp"
#include "platform/sound_bank.hpp"
#include "platform/window.hpp"
#include "render/sprite_renderer.hpp"
#include "render/sprite_sheet.hpp"
#include "render/vulkan_context.hpp"
#include "assets/text_table.hpp"
#include "assets/text_encoding.hpp"
#include "assets/xml_tree.hpp"
#include "render/gp_images.hpp"
#include "assets/image_decode.hpp"
#include "assets/map3d.hpp"
#include "assets/mission_options.hpp"
#include "assets/sprite_list.hpp"
#include "render/map_objects.hpp"
#include "world/strategic_ai.hpp"
#include "world/unit_catalog.hpp"
#include "world/world.hpp"
#include "render/terrain_renderer.hpp"
#include "ui/dialogs.hpp"
#include "ui/fonts.hpp"
#include "ui/menus.hpp"
#include "ui/rich_text.hpp"

namespace fs = std::filesystem;

namespace {

// Тёмно-синий фон.
constexpr std::array<float, 4> kClearColor{0.04f, 0.06f, 0.12f, 1.0f};
// Меню, как в оригинале, — на чёрном (поля по краям и места без картинок).
constexpr std::array<float, 4> kMenuClearColor{0.0f, 0.0f, 0.0f, 1.0f};
constexpr double kAnimationFps = 15.0;

const char* kHelp =
    "Ключи:\n"
    "  --data <папка>        папка с данными игры (например, .../c2_battle_for_europe/app)\n"
    "  --sprite <файл.g2d>   показать пакет спрайтов (путь относительно --data, регистр не важен)\n"
    "  --frame <N>           начальный кадр\n"
    "  --frames <N>          нарисовать N кадров и выйти (для проверок)\n"
    "  --screenshot <файл>   сохранить последний кадр в PNG (по умолчанию после одного кадра)\n"
    "  --map <файл.m3d>      показать карту (путь относительно --data, например Missions/Skirmish/Skirmish1.m3d)\n"
    "  --menu <имя>          открыть меню из Dialogs/MainMenu.xml (Main, Single, Options, …)\n"
    "  --dialog <файл.xml>   открыть любое окно *.DialogsSystem.xml (путь относительно --data)\n"
    "  --at <x>,<y>          с --map: поставить камеру на точку карты (мировые координаты)\n"
    "  --move <и>,<x>,<y>    с --map: юниты игрока <и> идут строем в точку карты (для проверок)\n"
    "  --build <тип>         с --map: крестьяне игрока 0 закладывают рядом здание <тип> и строят (для проверок)\n"
    "  --produce <зд>,<юнит> с --map: у крестьян игрока 0 ставится готовое здание <зд>, в нём заказано 3 <юнит>\n"
    "  --mine                с --map: крестьяне игрока 0 идут в ближайшую шахту (для проверок)\n"
    "  --gather <и>          с --map: крестьяне игрока <и> добывают у ближайших источников (для проверок)\n"
    "  --mouse <x>,<y>       считать, что курсор стоит в этой точке экрана 1024×768 (в игре — пикселей окна; для проверок)\n"
    "  --click               с --mouse: нажать левую кнопку в этой точке на втором кадре\n"
    "  --locks               с --map: показать закрытые клетки (то же — клавиша L)\n"
    "  --ai-self             с --map: игроком 0 тоже управляет стратегический ИИ (для проверок)\n"
    "  --simulate <с>        с --map: до первого кадра прожить <с> секунд игры и вывести итоги ИИ (для проверок)\n"
    "  --mute                без звука и музыки\n"
    "  --win                 с --map: враги погибают сразу (проверка окна итога)\n"
    "  --version             версия\n"
    "\n"
    "С --data без --sprite показывается главное меню.\n"
    "Просмотр спрайтов: ←/→ — кадр, ↑/↓ — на 10 кадров, Home — первый кадр,\n"
    "пробел — анимация, +/− — масштаб, Esc — выход.\n"
    "Карта: стрелки или край экрана — прокрутка, +/− — масштаб.\n";

void showError(const std::string& message) {
    std::fprintf(stderr, "Ошибка: %s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, oc2::kProductName, message.c_str(), nullptr);
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("не удалось открыть " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Проверочная картинка без данных игры: шахматная доска 8×8 и полупрозрачная
// рамка поверх неё. Нужна, чтобы проверять вывод текстур и смешивание в CI.
oc2::assets::Image testPattern() {
    oc2::assets::Image img(8, 8);
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x) {
            std::uint8_t* p = img.pixel(x, y);
            const bool light = (x + y) % 2 == 0;
            p[0] = light ? 230 : 60;
            p[1] = light ? 200 : 40;
            p[2] = light ? 120 : 30;
            p[3] = (x == 0 || y == 0 || x == 7 || y == 7) ? 128 : 255;
        }
    return img;
}

struct Options {
    long frameLimit = -1;
    fs::path screenshot;
    fs::path dataDir;
    std::optional<std::array<float, 2>> mouse;
    std::optional<std::array<float, 2>> at; // точка карты в центре экрана
    std::optional<std::array<int, 3>> move;
    std::optional<int> gather;
    std::string build;
    std::string produce;
    bool mine = false;
    bool aiSelf = false;
    int simulate = 0; // секунд
    bool click = false;
    bool locks = false; // показать закрытые клетки с самого начала
    bool mute = false;  // без звука
    bool win = false;   // проверка итога: враги убиты сразу // одно нажатие левой кнопкой в точке --mouse на втором кадре  // приказ игроку: идти в точку (для проверок)
    std::string menu = "Main";
    fs::path map;
    fs::path sprite;
    long startFrame = 0;
};

// Возвращает nullopt, если программу нужно завершить (справка, версия).
std::optional<Options> parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "--version") {
            std::printf("%s, версия %s\n", oc2::kProductName, oc2::kVersion);
            return std::nullopt;
        } else if (a == "--help" || a == "-h") {
            std::printf("%s %s\n\n%s", oc2::kProductName, oc2::kVersion, kHelp);
            return std::nullopt;
        } else if (a == "--frames" && hasValue) {
            o.frameLimit = std::strtol(argv[++i], nullptr, 10);
        } else if (a == "--screenshot" && hasValue) {
            o.screenshot = argv[++i];
        } else if (a == "--data" && hasValue) {
            o.dataDir = argv[++i];
        } else if (a == "--sprite" && hasValue) {
            o.sprite = argv[++i];
        } else if (a == "--map" && hasValue) {
            o.map = argv[++i];
        } else if (a == "--menu" && hasValue) {
            o.menu = argv[++i];
        } else if (a == "--dialog" && hasValue) {
            o.menu = std::string("file:") + argv[++i];
        } else if (a == "--mouse" && hasValue) {
            float mx = 0, my = 0;
            if (std::sscanf(argv[++i], "%f,%f", &mx, &my) != 2)
                throw std::runtime_error("--mouse ждёт координаты вида 500,470");
            o.mouse = std::array<float, 2>{mx, my};
        } else if (a == "--at" && hasValue) {
            float x = 0, y = 0;
            if (std::sscanf(argv[++i], "%f,%f", &x, &y) != 2)
                throw std::runtime_error("--at ждёт мировые координаты вида 11939,14758");
            o.at = std::array<float, 2>{x, y};
        } else if (a == "--win") {
            o.win = true;
        } else if (a == "--mute") {
            o.mute = true;
        } else if (a == "--locks") {
            o.locks = true;
        } else if (a == "--click") {
            o.click = true;
        } else if (a == "--move" && hasValue) {
            int p = 0, x = 0, y = 0;
            if (std::sscanf(argv[++i], "%d,%d,%d", &p, &x, &y) != 3)
                throw std::runtime_error("--move ждёт «игрок,x,y», например 0,13000,1800");
            o.move = std::array<int, 3>{p, x, y};
        } else if (a == "--ai-self") {
            o.aiSelf = true;
        } else if (a == "--simulate" && hasValue) {
            o.simulate = int(std::strtol(argv[++i], nullptr, 10));
        } else if (a == "--mine") {
            o.mine = true;
        } else if (a == "--produce" && hasValue) {
            o.produce = argv[++i];
        } else if (a == "--build" && hasValue) {
            o.build = argv[++i];
        } else if (a == "--gather" && hasValue) {
            o.gather = int(std::strtol(argv[++i], nullptr, 10));
        } else if (a == "--frame" && hasValue) {
            o.startFrame = std::strtol(argv[++i], nullptr, 10);
        } else {
            throw std::runtime_error("неизвестный ключ «" + a + "»; список ключей: --help");
        }
    }
    if (!o.screenshot.empty() && o.frameLimit < 0) o.frameLimit = 1;
    return o;
}

fs::path resolveSprite(const Options& o) {
    if (o.dataDir.empty()) return o.sprite;
    if (auto found = oc2::assets::findCaseInsensitive(o.dataDir, o.sprite)) return *found;
    throw std::runtime_error("файл не найден: " + (o.dataDir / o.sprite).string());
}

// Просмотр одного пакета спрайтов.
struct Viewer {
    std::unique_ptr<oc2::render::SpriteSheet> sheet;
    std::string name;
    long frame = 0;
    float scale = 2.0f;
    bool playing = false;
    double clock = 0.0;

    void step(long delta) {
        const long n = static_cast<long>(sheet->frameCount());
        if (n == 0) return;
        frame = ((frame + delta) % n + n) % n;
    }

    void handleKey(std::uint32_t key) {
        switch (key) {
        case SDLK_RIGHT: step(1); break;
        case SDLK_LEFT: step(-1); break;
        case SDLK_UP: step(10); break;
        case SDLK_DOWN: step(-10); break;
        case SDLK_HOME: frame = 0; break;
        case SDLK_SPACE: playing = !playing; break;
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS: scale = std::min(scale + 1.0f, 8.0f); break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: scale = std::max(scale - 1.0f, 1.0f); break;
        default: break;
        }
    }

    void update(double dt) {
        if (!playing) return;
        clock += dt;
        while (clock >= 1.0 / kAnimationFps) {
            clock -= 1.0 / kAnimationFps;
            step(1);
        }
    }

    std::string title() const {
        return name + " — кадр " + std::to_string(frame + 1) + " из " + std::to_string(sheet->frameCount()) +
               " — " + oc2::kProductName + " " + oc2::kVersion;
    }

    void draw(VkExtent2D e) const {
        if (sheet->frameCount() == 0) return;
        const float x = (float(e.width) - float(sheet->width()) * scale) / 2.0f;
        const float y = (float(e.height) - float(sheet->height()) * scale) / 2.0f;
        sheet->draw(static_cast<std::size_t>(frame), std::floor(x), std::floor(y), scale);
    }
};

// Карта в игре: рельеф, объекты, юниты мира; прокрутка, масштаб, выбор и приказы.
struct MapView {
    std::unique_ptr<oc2::assets::Map3d> mapData;
    std::unique_ptr<oc2::render::TerrainRenderer> terrain;
    std::unique_ptr<oc2::render::GpImages> ownImages; // если общих изображений меню нет
    oc2::render::GpImages* images = nullptr;
    std::unique_ptr<oc2::world::UnitCatalog> units;
    std::vector<std::unique_ptr<oc2::world::StrategicAi>> ais;
    std::unique_ptr<oc2::render::MapObjects> objects;
    oc2::world::World world;
    std::unique_ptr<oc2::render::Effects> effects;
    bool ended = false; // бой окончен, показано окно итога

    // Мини-карта (своя; оригинал рисует её кодом MiniMap4X.cpp): картинка VIE2, точки
    // юнитов цветом игрока, рамка видимой области. M — показать или скрыть.
    std::optional<oc2::render::TextureId> minimapTexture;
    bool minimapVisible = true;
    static constexpr float kMinimapW = 256.0f, kMinimapH = 240.0f;

    struct MinimapRect {
        float x, y, w, h;
    };
    MinimapRect minimapRect(VkExtent2D e) const {
        const float scale = std::max(0.5f, float(e.height) / 768.0f) * 0.75f;
        const float w = kMinimapW * scale, h = kMinimapH * scale;
        return {8.0f, float(e.height) - h - 8.0f, w, h};
    }

    void drawMinimap(oc2::render::SpriteRenderer& sprites, VkExtent2D e) const {
        if (!minimapVisible || !minimapTexture) return;
        const MinimapRect r = minimapRect(e);
        const float worldSize = float(mapData->rows() * oc2::assets::Map3d::kCell);
        sprites.fillRect(r.x - 2.0f, r.y - 2.0f, r.w + 4.0f, r.h + 4.0f, 0xFF201810);
        sprites.drawQuad(*minimapTexture, r.x, r.y, r.w, r.h);
        for (const auto& u : world.units()) {
            if (!u.alive() || u.player == 7 || u.inside) continue;
            const float px = r.x + float(u.x()) / worldSize * r.w, py = r.y + float(u.y()) / worldSize * r.h;
            sprites.fillRect(px - 1.0f, py - 1.0f, 2.0f, 2.0f, oc2::render::playerColor(u.player));
        }
        // Видимая область: экранный Y = мировой Y / 2.
        const float vx = r.x + camera.x / worldSize * r.w, vy = r.y + camera.y * 2.0f / worldSize * r.h;
        const float vw = float(e.width) / camera.zoom / worldSize * r.w;
        const float vh = float(e.height) / camera.zoom * 2.0f / worldSize * r.h;
        const std::uint32_t c = 0xFFFFFFFF;
        sprites.fillRect(vx, vy, vw, 1.0f, c);
        sprites.fillRect(vx, vy + vh, vw, 1.0f, c);
        sprites.fillRect(vx, vy, 1.0f, vh, c);
        sprites.fillRect(vx + vw, vy, 1.0f, vh + 1.0f, c);
    }

    // Нажатие на мини-карте ставит туда камеру; true — нажатие поглощено.
    bool minimapClick(float x, float y, VkExtent2D e) {
        if (!minimapVisible || !minimapTexture) return false;
        const MinimapRect r = minimapRect(e);
        if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return false;
        const float worldSize = float(mapData->rows() * oc2::assets::Map3d::kCell);
        lookAt((x - r.x) / r.w * worldSize, (y - r.y) / r.h * worldSize, e);
        return true;
    }
    bool showLocks = false; // L — показать закрытые клетки

    // Панель строительства (своя раскладка — оригинал рисует её кодом): значки BIGICON
    // членов [FIXED_PRODUCE] выбранного производителя на сетке 12 столбцов, место
    // значка — столбец и строка из файла нации.
    struct PanelButton {
        const oc2::assets::UnitDesc* desc;
        float x, y, size;
        bool leave = false; // выпустить крестьянина из шахты
    };
    // Выбранный свой юнит или здание, у которого есть [FIXED_PRODUCE].
    std::optional<std::size_t> producer() const {
        const auto& us = world.units();
        for (std::size_t i = 0; i < us.size(); ++i) {
            const auto& u = us[i];
            if (!u.selected || u.player != player || !u.alive() || u.underConstruction) continue;
            if (!units->produces(units->typeOf(u.desc)).empty()) return i;
        }
        return std::nullopt;
    }

    std::vector<PanelButton> panel(VkExtent2D e) const {
        std::vector<PanelButton> out;
        const float cell = std::floor(48.0f * std::max(0.5f, float(e.height) / 768.0f));
        // Выбрана своя шахта с крестьянами — значок крестьянина выпускает одного.
        for (const auto& u : world.units())
            if (u.selected && u.player == player && !u.occupants.empty()) {
                out.push_back({world.units()[u.occupants.back()].desc, float(e.width) - 12.0f * cell - 8.0f,
                               float(e.height) - cell - 8.0f, cell, true});
                return out;
            }
        const auto who = producer();
        if (!who) return out;
        const std::vector<oc2::assets::ProduceItem>* items = &units->produces(units->typeOf(world.units()[*who].desc));
        int rows = 1;
        for (const auto& it : *items) rows = std::max(rows, it.row + 1);
        const float x0 = float(e.width) - 12.0f * cell - 8.0f, y0 = float(e.height) - float(rows) * cell - 8.0f;
        for (const auto& it : *items)
            if (const oc2::assets::UnitDesc* d = units->find(it.member))
                out.push_back({d, x0 + float(it.column) * cell, y0 + float(it.row) * cell, cell});
        return out;
    }

    void drawPanel(oc2::render::SpriteRenderer& sprites, VkExtent2D e) const {
        const std::vector<PanelButton> buttons = panel(e);
        if (buttons.empty()) return;
        float x0 = 1e9f, y0 = 1e9f, x1 = 0.0f, y1 = 0.0f;
        for (const auto& b : buttons) {
            x0 = std::min(x0, b.x), y0 = std::min(y0, b.y);
            x1 = std::max(x1, b.x + b.size), y1 = std::max(y1, b.y + b.size);
        }
        sprites.fillRect(x0 - 4.0f, y0 - 4.0f, x1 - x0 + 8.0f, y1 - y0 + 8.0f, 0xF0201810);
        for (const auto& b : buttons) {
            const oc2::render::GpId id = images->id(b.desc->bigIconFile);
            const auto frame = std::size_t(b.desc->bigIconFrame);
            if (!images->valid(id) || frame >= images->frameCount(id)) {
                sprites.fillRect(b.x + 1.0f, b.y + 1.0f, b.size - 2.0f, b.size - 2.0f, 0xFF504030);
                continue;
            }
            const float w = float(images->frameWidth(id, frame)), h = float(images->frameHeight(id, frame));
            const float scale = (b.size - 2.0f) / std::max({w, h, 1.0f});
            const bool can = world.affordable(*b.desc, player);
            images->draw(id, frame, b.x + 1.0f, b.y + 1.0f, can ? 0xFFFFFFFF : 0xFF707070, scale);
            // Очередь здания: полоса готовности над значком первого, точки — сколько заказано.
            if (const auto who = producer(); who && world.units()[*who].building()) {
                const auto& w = world.units()[*who];
                int queued = 0;
                for (const auto* q : w.queue) queued += q == b.desc ? 1 : 0;
                if (!w.queue.empty() && w.queue.front() == b.desc) {
                    const float part = float(w.produced) / float(std::max(1, b.desc->buildStages));
                    sprites.fillRect(b.x + 2.0f, b.y + b.size - 6.0f, (b.size - 4.0f) * part, 4.0f, 0xFF40E040);
                }
                for (int q = 0; q < std::min(queued, 10); ++q)
                    sprites.fillRect(b.x + 3.0f + float(q) * 4.0f, b.y + 3.0f, 3.0f, 3.0f, 0xFFFFE060);
            }
            if (placing == b.desc) {
                const std::uint32_t c = 0xFFFFD040;
                sprites.fillRect(b.x, b.y, b.size, 2.0f, c);
                sprites.fillRect(b.x, b.y + b.size - 2.0f, b.size, 2.0f, c);
                sprites.fillRect(b.x, b.y, 2.0f, b.size, c);
                sprites.fillRect(b.x + b.size - 2.0f, b.y, 2.0f, b.size, c);
            }
        }
    }

    // Выбор места здания: призрак под курсором, зелёный — можно, красный — нельзя.
    const oc2::assets::UnitDesc* placing = nullptr;
    void drawPlacement(oc2::render::SpriteRenderer& sprites, float mx, float my) {
        if (!placing) return;
        const auto [x, y] = screenToWorld(mx, my);
        if (placing->spriteObject) { // посев: метки клеток будущего поля
            constexpr int B = oc2::world::World::kFieldBlock, S = oc2::world::World::kFieldStep;
            const std::uint32_t c = world.affordable(*placing, player) ? 0xC080FF80 : 0xC0FF6060;
            for (int j = 0; j < B; ++j)
                for (int i = 0; i < B; ++i) {
                    const int fx = x + (i - B / 2) * S, fy = y + (j - B / 2) * S;
                    const float sx = (float(fx) - camera.x) * camera.zoom;
                    const float sy = (float((fy >> 1) - mapData->heightAt(fx, fy)) - camera.y) * camera.zoom;
                    sprites.fillRect(sx - 12.0f * camera.zoom, sy - 3.0f * camera.zoom, 24.0f * camera.zoom,
                                     6.0f * camera.zoom, c);
                }
            return;
        }
        const bool ok = world.canPlace(*placing, x, y) && world.affordable(*placing, player);
        objects->drawGhost(*placing, x, y, camera, ok ? 0xB080FF80 : 0xB0FF6060);
    }

    // Кнопка панели под курсором (для подсказки).
    std::optional<PanelButton> hoveredButton(float mx, float my, VkExtent2D e) const {
        for (const auto& b : panel(e))
            if (mx >= b.x && my >= b.y && mx < b.x + b.size && my < b.y + b.size) return b;
        return std::nullopt;
    }

    // Нажатие на панели; true — поглощено.
    bool panelClick(const oc2::platform::Window::Click& c, VkExtent2D e) {
        for (const auto& b : panel(e)) {
            if (c.x < b.x || c.y < b.y || c.x >= b.x + b.size || c.y >= b.y + b.size) continue;
            if (b.leave) {
                for (std::size_t i = 0; i < world.units().size(); ++i)
                    if (world.units()[i].selected && !world.units()[i].occupants.empty()) world.leaveMine(i);
                return true;
            }
            if (c.button == 1 && (b.desc->building || b.desc->spriteObject)) placing = b.desc;
            if (c.button == 1 && !b.desc->building && !b.desc->spriteObject)
                if (const auto who = producer(); who && world.orderProduce(*who, *b.desc) && sounds)
                    sounds->play(b.desc->selectSound);
            return true;
        }
        return false;
    }

    // Заложить выбранное здание в точке экрана; выбранные крестьяне идут строить.
    void place(float sx, float sy) {
        const auto [x, y] = screenToWorld(sx, sy);
        if (placing->spriteObject) {
            if (world.sowField(*placing, player, x, y) > 0) placing = nullptr;
            return;
        }
        const auto b = world.placeBuilding(*placing, player, x, y);
        if (!b) return; // место занято или не хватает ресурсов — выбор остаётся
        placing = nullptr;
        for (std::size_t i = 0; i < world.units().size(); ++i)
            if (world.units()[i].selected && world.units()[i].player == player) world.orderBuild(i, *b);
    }
    int player = 0;         // за кого играем: приказы слушают только его юниты
    oc2::platform::SoundBank* sounds = nullptr; // для SELECT_SOUND и ORDER_SOUND
    double stepTime = 0.0; // накопленное время до следующего шага симуляции
    oc2::render::MapCamera camera;
    float width = 0.0f, height = 0.0f; // размер карты в пикселях проекции

    // shared — изображения меню (общий кэш текстур), иначе создаются свои.
    MapView(oc2::render::VulkanContext& vulkan, oc2::render::SpriteRenderer& sprites, const fs::path& data,
            const fs::path& mapFile, oc2::render::GpImages* shared = nullptr) {
        const auto path = oc2::assets::findCaseInsensitive(data, mapFile);
        if (!path) throw std::runtime_error("нет карты " + (data / mapFile).string());
        mapData = std::make_unique<oc2::assets::Map3d>(oc2::assets::Map3d::parse(readFile(*path)));
        const oc2::assets::Map3d& map = *mapData;
        for (int p = 0; p < 8; ++p) world.resources[std::size_t(p)] = map.startResources(p);

        // Фактуры — по списку FacturesList.xml (номер в карте = номер в списке).
        std::vector<oc2::assets::Image> factures;
        if (auto list = oc2::assets::findCaseInsensitive(data, "FacturesList.xml")) {
            const std::vector<std::uint8_t> bytes = readFile(*list);
            const oc2::assets::XmlNode doc =
                oc2::assets::parseXml({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            for (const oc2::assets::XmlNode& f : doc.children) {
                std::string name = f.value("FactureID").value_or("");
                std::replace(name.begin(), name.end(), '\\', '/');
                oc2::assets::Image img(256, 256);
                if (auto file = oc2::assets::findCaseInsensitive(data, name)) {
                    try {
                        img = oc2::assets::decodeImage(readFile(*file));
                    } catch (const std::exception& e) {
                        std::fprintf(stderr, "фактура %s: %s\n", name.c_str(), e.what());
                    }
                }
                if (img.width != 256 || img.height != 256) img = oc2::assets::Image(256, 256);
                factures.push_back(std::move(img));
            }
        }
        if (factures.empty()) throw std::runtime_error("нет FacturesList.xml");
        const std::size_t groundCount = factures.size();
        // Отражение для воды — облака, как в FUN_0168e650 (Oblaka123g1.tga), последним слоем.
        int waterLayer = -1;
        if (auto file = oc2::assets::findCaseInsensitive(data, "textures/oblaka123g1.tga")) {
            try {
                oc2::assets::Image img = oc2::assets::decodeImage(readFile(*file));
                if (img.width == 256 && img.height == 256) {
                    waterLayer = int(factures.size());
                    factures.push_back(std::move(img));
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "текстура воды: %s\n", e.what());
            }
        }
        // Номера фактур в карте могут выходить за список — такие вершины берут нулевую.
        std::vector<oc2::world::TerrainVertex> mesh = oc2::world::buildTerrainMesh(map);
        for (auto& v : mesh)
            for (int k = 0; k < 3; ++k) {
                if (v.factures1[k] >= groundCount) v.factures1[k] = 0;
                if (v.factures2[k] >= groundCount) v.factures2[k] = 0;
            }
        terrain = std::make_unique<oc2::render::TerrainRenderer>(vulkan);
        terrain->setFactures(factures, waterLayer);
        terrain->setMesh(mesh);
        if (shared) {
            images = shared;
        } else {
            ownImages = std::make_unique<oc2::render::GpImages>(sprites, data);
            images = ownImages.get();
        }
        units = std::make_unique<oc2::world::UnitCatalog>(data);
        objects = std::make_unique<oc2::render::MapObjects>(sprites, *images, data, map);
        effects = std::make_unique<oc2::render::Effects>(data);
        if (const oc2::assets::Image preview = map.preview(); preview.width) minimapTexture = sprites.createTexture(preview);
        world.setLockMap(oc2::world::LockMap::fromTerrain(map));
        // Команды и одиночная игра: юниты отключённых игроков отходят союзнику (MOPT).
        oc2::assets::MissionOptions mission;
        try {
            mission = oc2::assets::MissionOptions::parse(map.missionOptionsXml());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "настройки миссии (MOPT): %s\n", e.what());
        }
        world.team = mission.teams();
        world.humanTeam = world.team[std::size_t(player)];
        std::size_t missing = 0;
        for (const oc2::assets::MapUnit& u : map.units()) {
            if (const oc2::assets::UnitDesc* desc = units->find(u.type))
                world.addUnit(*desc, mission.singleOwner(u.player), u.x, u.y, u.direction);
            else
                ++missing;
        }
        if (missing) std::printf("юниты карты: не найдено типов для %zu из %zu\n", missing, map.units().size());
        // Источники ресурсов: деревья (Treelist) и камни с рудами (stonlist), раздел [SOURCES] их .rsr.
        struct SourceKind {
            const char kind[3];
            const char* list;
            const char* rsr;
        };
        // Поля: стадии complex.lst и переходы complex.rsr; объекты «CO» карты — их номера.
        {
            const auto lst = oc2::assets::findCaseInsensitive(data, "complex.lst");
            const auto rsr = oc2::assets::findCaseInsensitive(data, "complex.rsr");
            if (lst && rsr) {
                const auto text = [](const fs::path& p) {
                    const std::vector<std::uint8_t> b = readFile(p);
                    return std::string(b.begin(), b.end());
                };
                try {
                    oc2::world::World::ObjectRules rules;
                    rules.list = oc2::assets::SpriteList::parse(text(*lst));
                    const std::string r = text(*rsr);
                    rules.sources = oc2::assets::SpriteList::parseSources(r);
                    rules.time = oc2::assets::SpriteList::parseTransforms(r, "[TIMETRANSFORM]");
                    rules.work = oc2::assets::SpriteList::parseTransforms(r, "[WORKTRANSFORM]");
                    world.setObjectRules(std::move(rules));
                    for (const oc2::assets::MapObject& o : map.objects())
                        if (o.is("CO")) world.addObject(o.type, o.x, o.y);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "complex.lst: %s\n", e.what());
                }
            }
        }
        for (const SourceKind& k : {SourceKind{"AG", "Treelist.lst", "treelist.rsr"},
                                    SourceKind{"ST", "stonlist.LST", "stonlist.rsr"}}) {
            const auto lst = oc2::assets::findCaseInsensitive(data, k.list);
            const auto rsr = oc2::assets::findCaseInsensitive(data, k.rsr);
            if (!lst || !rsr) continue;
            try {
                const auto text = [](const fs::path& p) {
                    const std::vector<std::uint8_t> b = readFile(p);
                    return std::string(b.begin(), b.end());
                };
                world.addSources(map, k.kind, oc2::assets::SpriteList::parse(text(*lst)),
                                 oc2::assets::SpriteList::parseSources(text(*rsr)));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", k.list, e.what());
            }
        }
        startAi(data, false);
        width = map.worldX(map.vertsInLine() - 1);
        height = float(map.rows() - 1) * oc2::assets::Map3d::kCell / 2.0f;
        std::printf("%s: сетка %d×%d, треугольников %zu, объектов %zu, юнитов %zu, источников ресурсов %zu\n",
                    path->string().c_str(), map.vertsInLine(), map.rows(), mesh.size() / 3, objects->staticCount(),
                    world.units().size(), world.sources.size());
    }

    // Стратегический ИИ игрокам 0..6 с юнитами (кроме нашего, если не self), по сценарию их нации.
    void startAi(const fs::path& data, bool self) {
        ais.clear();
        for (int p = 0; p < 7; ++p) {
            if (p == player && !self) continue;
            const std::string code = oc2::world::playerNation(world, *units, p);
            if (code.empty()) continue;
            if (auto script = oc2::world::loadAiScript(data, code)) {
                ais.push_back(std::make_unique<oc2::world::StrategicAi>(world, *units, p, std::move(*script), 1,
                                                                        0x9E3779B9u * std::uint32_t(p + 1)));
                std::printf("ИИ игрока %d: нация %s\n", p, code.c_str());
            } else {
                std::printf("ИИ игрока %d: нет сценария для нации %s\n", p, code.c_str());
            }
        }
    }

    // Шаг мира и раздумья ИИ.
    void stepWorld() {
        world.step();
        for (auto& ai : ais) ai->update();
    }

    void centerOn(VkExtent2D e) {
        camera.x = (width - float(e.width) / camera.zoom) / 2.0f;
        camera.y = (height - float(e.height) / camera.zoom) / 2.0f;
    }

    // Камера на юниты игрока (центр масс не-зданий, иначе всех); без них — центр карты.
    void centerOnPlayer(int player, VkExtent2D e) {
        long sx = 0, sy = 0, n = 0;
        for (int pass = 0; pass < 2 && n == 0; ++pass)
            for (const auto& u : world.units())
                if (u.player == player && (pass == 1 || !u.building())) sx += u.x(), sy += u.y(), ++n;
        if (n == 0) return centerOn(e);
        lookAt(float(sx / n), float(sy / n), e);
    }

    // Мировая точка (x, y) — в центр экрана (высота рельефа не учитывается).
    void lookAt(float x, float y, VkExtent2D e) {
        camera.x = x - float(e.width) / camera.zoom / 2.0f;
        camera.y = y / 2.0f - float(e.height) / camera.zoom / 2.0f;
    }

    void handleKey(std::uint32_t key, VkExtent2D e) {
        const float cx = camera.x + float(e.width) / camera.zoom / 2.0f;
        const float cy = camera.y + float(e.height) / camera.zoom / 2.0f;
        switch (key) {
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS: camera.zoom = std::min(camera.zoom * 2.0f, 4.0f); break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: camera.zoom = std::max(camera.zoom / 2.0f, 0.125f); break;
        case SDLK_L: showLocks = !showLocks; return;
        case SDLK_M: minimapVisible = !minimapVisible; return;
        default: return;
        }
        camera.x = cx - float(e.width) / camera.zoom / 2.0f;
        camera.y = cy - float(e.height) / camera.zoom / 2.0f;
    }

    // Звуки юнитов за кадр: только видимые, одна группа — один раз, не больше восьми;
    // панорама — по положению на экране.
    void playSounds(oc2::platform::SoundBank* bank, VkExtent2D e) {
        auto& events = world.soundEvents;
        if (bank) {
            std::vector<std::string> played;
            for (const auto& ev : events) {
                if (played.size() >= 8) break;
                const float sx = (float(ev.x) - camera.x) * camera.zoom;
                const float sy = (float(ev.y / 2 - mapData->heightAt(ev.x, ev.y)) - camera.y) * camera.zoom;
                if (sx < -64.0f || sy < -64.0f || sx > float(e.width) + 64.0f || sy > float(e.height) + 64.0f) continue;
                if (std::find(played.begin(), played.end(), ev.group) != played.end()) continue;
                played.push_back(ev.group);
                bank->play(ev.group, 1.0f, (sx / float(e.width)) * 2.0f - 1.0f);
            }
        }
        events.clear();
    }

    // Закрытые клетки на экране (отладка, клавиша L): полупрозрачные красные квадраты.
    void drawLocks(oc2::render::SpriteRenderer& sprites, VkExtent2D e) const {
        if (!showLocks) return;
        const auto& lm = world.lockMap();
        constexpr int C = oc2::world::LockMap::kCell;
        const int x0 = std::max(0, int(camera.x) / C - 1), x1 = int(camera.x + float(e.width) / camera.zoom) / C + 1;
        const int y0 = std::max(0, int(camera.y * 2.0f) / C - 8), y1 = int((camera.y + float(e.height) / camera.zoom) * 2.0f) / C + 8;
        for (int cy = y0; cy <= y1; ++cy)
            for (int cx = x0; cx <= x1; ++cx) {
                if (!lm.locked(cx, cy) || cx >= lm.width() || cy >= lm.height()) continue;
                const int wx = cx * C, wy = cy * C;
                const float sx = (float(wx) - camera.x) * camera.zoom;
                const float sy = (float(wy / 2 - mapData->heightAt(wx + C / 2, wy + C / 2)) - camera.y) * camera.zoom;
                sprites.fillRect(sx, sy, float(C) * camera.zoom, float(C / 2) * camera.zoom, 0x60FF2020);
            }
    }

    // Шаги симуляции за прошедшее время (не больше 5 за кадр, чтобы не отставать бесконечно).
    void advance(double dt) {
        stepTime += dt;
        for (int i = 0; i < 5 && stepTime >= oc2::world::kStepSeconds; ++i) {
            stepWorld();
            effects->step();
            for (const auto& shot : world.shotEvents)
                if (auto sound = effects->spawn(shot.weapon, shot.x, shot.y, shot.direction))
                    world.soundEvents.push_back({*sound, shot.x, shot.y});
            world.shotEvents.clear();
            stepTime -= oc2::world::kStepSeconds;
        }
        if (stepTime > oc2::world::kStepSeconds) stepTime = 0.0;
    }

    // Мировая точка под пикселем экрана: Y проекции = y/2 − z, z зависит от y — несколько уточнений.
    std::array<int, 2> screenToWorld(float sx, float sy) const {
        const float px = camera.x + sx / camera.zoom, py = camera.y + sy / camera.zoom;
        const int x = int(px);
        int y = int(py * 2.0f);
        for (int i = 0; i < 4; ++i) y = int((py + float(mapData->heightAt(x, y))) * 2.0f);
        return {x, y};
    }

    // Юнит под курсором: рамка примерно по фигуре пехотинца над точкой земли.
    std::optional<std::size_t> pick(float sx, float sy) const {
        std::optional<std::size_t> best;
        float bestDist = 1e9f;
        const auto& us = world.units();
        for (std::size_t i = 0; i < us.size(); ++i) {
            if (us[i].building() || !us[i].alive() || us[i].inside) continue;
            const float gx = (objects->groundX(us[i]) - camera.x) * camera.zoom;
            const float gy = (objects->groundY(us[i]) - camera.y) * camera.zoom;
            const float dx = sx - gx, dy = sy - (gy - 22.0f * camera.zoom);
            if (std::abs(dx) > 14.0f * camera.zoom || std::abs(dy) > 26.0f * camera.zoom) continue;
            const float d = dx * dx + dy * dy;
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    // Здание под курсором (своё прочтение): ближайшее, чья точка земли на экране не дальше
    // 110 пикселей от нажатия, поднятого на 50 (картинка здания выше точки земли).
    std::optional<std::size_t> pickBuilding(float sx, float sy) const {
        std::optional<std::size_t> best;
        float bestDist = 110.0f * 110.0f * camera.zoom * camera.zoom;
        const auto& us = world.units();
        for (std::size_t i = 0; i < us.size(); ++i) {
            if (!us[i].building() || !us[i].alive()) continue;
            const float gx = (objects->groundX(us[i]) - camera.x) * camera.zoom;
            const float gy = (objects->groundY(us[i]) - camera.y - 50.0f) * camera.zoom;
            const float d = (sx - gx) * (sx - gx) + (sy - gy) * (sy - gy);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    // Рамка выбора: нажатие левой кнопки начинает её, отпускание выбирает.
    std::optional<std::array<float, 2>> dragStart;

    // Левая кнопка — начало рамки; правая — идти в точку. Группа сохраняет строй:
    // каждый идёт со своим смещением от центра группы.
    void handleClick(const oc2::platform::Window::Click& c, VkExtent2D e) {
        auto& us = world.units();
        if (panelClick(c, e)) return;
        if (placing) {
            if (c.button == 1) place(c.x, c.y);
            else placing = nullptr; // правая кнопка — отмена
            return;
        }
        if (c.button == 1) {
            dragStart = std::array<float, 2>{c.x, c.y};
        } else if (c.button == 3) {
            // Правая кнопка на враге — атаковать его.
            const oc2::assets::UnitDesc* ordered = nullptr;
            for (const auto& u : us)
                if (u.selected && u.player == player && !ordered) ordered = u.desc;
            if (ordered && sounds) sounds->play(ordered->orderSound);
            if (auto t = pick(c.x, c.y); t && world.enemies(player, us[*t].player)) {
                for (std::size_t i = 0; i < us.size(); ++i)
                    if (us[i].selected && us[i].player == player) world.orderAttack(i, *t);
                return;
            }
            const auto [tx, ty] = screenToWorld(c.x, c.y);
            // Правая кнопка на шахте (своей или ничьей) — крестьяне входят в неё.
            if (auto m = pickBuilding(c.x, c.y); m && us[*m].desc->peasantAbsorber > 0) {
                bool any = false;
                for (std::size_t i = 0; i < us.size(); ++i)
                    if (us[i].selected && us[i].player == player && world.orderEnter(i, *m)) any = true;
                if (any) return;
            }
            // Правая кнопка у своей стройки — достраивать.
            for (std::size_t b = 0; b < us.size(); ++b) {
                if (!us[b].underConstruction || us[b].player != player) continue;
                const int dx = us[b].x() - tx, dy = us[b].y() - ty;
                if (dx * dx + dy * dy > 250 * 250) continue;
                bool any = false;
                for (std::size_t i = 0; i < us.size(); ++i)
                    if (us[i].selected && us[i].player == player && world.orderBuild(i, b)) any = true;
                if (any) return;
            }
            // Правая кнопка у дерева или камня — добывать (кто умеет; остальные идут туда).
            if (auto s = world.sourceNear(tx, ty, 48)) {
                bool any = false;
                for (std::size_t i = 0; i < us.size(); ++i)
                    if (us[i].selected && us[i].player == player && world.orderGather(i, *s)) any = true;
                if (any) return;
            }
            long cx = 0, cy = 0, n = 0;
            for (const auto& u : us)
                if (u.selected && u.player == player) cx += u.x(), cy += u.y(), ++n;
            if (n == 0) return;
            cx /= n;
            cy /= n;
            for (std::size_t i = 0; i < us.size(); ++i)
                if (us[i].selected && us[i].player == player)
                    world.orderMove(i, tx + int(us[i].x() - cx), ty + int(us[i].y() - cy));
        }
    }

    // Отпускание левой кнопки: короткое — выбор юнита под курсором (пустое место снимает
    // выбор), протяжка — все не-здания игрока в рамке (по точке земли).
    void handleRelease(const oc2::platform::Window::Click& c, int player) {
        if (c.button != 1 || !dragStart) return;
        const auto [x0, y0] = *dragStart;
        dragStart.reset();
        auto& us = world.units();
        if (std::abs(c.x - x0) < 5.0f && std::abs(c.y - y0) < 5.0f) {
            for (auto& u : us) u.selected = false;
            auto i = pick(c.x, c.y);
            if (!i) i = pickBuilding(c.x, c.y);
            if (i) {
                us[*i].selected = true;
                if (sounds && us[*i].player == player) sounds->play(us[*i].desc->selectSound);
            }
            return;
        }
        const float x1 = std::min(x0, c.x), x2 = std::max(x0, c.x), y1 = std::min(y0, c.y), y2 = std::max(y0, c.y);
        const oc2::assets::UnitDesc* first = nullptr;
        for (auto& u : us) {
            const float gx = (objects->groundX(u) - camera.x) * camera.zoom;
            const float gy = (objects->groundY(u) - camera.y) * camera.zoom;
            u.selected = !u.building() && u.alive() && !u.inside && u.player == player && gx >= x1 && gx <= x2 && gy >= y1 &&
                         gy <= y2;
            if (u.selected && !first) first = u.desc;
        }
        if (first && sounds) sounds->play(first->selectSound);
    }

    void drawDragBox(oc2::render::SpriteRenderer& sprites, const oc2::platform::Window& window) const {
        if (!dragStart) return;
        const float x1 = std::min((*dragStart)[0], window.mouseX), x2 = std::max((*dragStart)[0], window.mouseX);
        const float y1 = std::min((*dragStart)[1], window.mouseY), y2 = std::max((*dragStart)[1], window.mouseY);
        if (x2 - x1 < 5.0f && y2 - y1 < 5.0f) return;
        const std::uint32_t c = 0xFF30E030;
        sprites.fillRect(x1, y1, x2 - x1, 1.0f, c);
        sprites.fillRect(x1, y2, x2 - x1, 1.0f, c);
        sprites.fillRect(x1, y1, 1.0f, y2 - y1, c);
        sprites.fillRect(x2, y1, 1.0f, y2 - y1 + 1.0f, c);
    }

    // Прокрутка: стрелки (состояние клавиш) и курсор у края окна.
    void scroll(double dt, const oc2::platform::Window& window, VkExtent2D e) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float dx = 0.0f, dy = 0.0f;
        if (keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keys[SDL_SCANCODE_UP]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_DOWN]) dy += 1.0f;
        if (window.mouseInside) {
            const float edge = 4.0f;
            if (window.mouseX < edge) dx -= 1.0f;
            if (window.mouseX > float(e.width) - edge) dx += 1.0f;
            if (window.mouseY < edge) dy -= 1.0f;
            if (window.mouseY > float(e.height) - edge) dy += 1.0f;
        }
        const float speed = 1200.0f / camera.zoom; // пикселей проекции в секунду
        camera.x += dx * speed * float(dt);
        camera.y += dy * speed * float(dt);
    }
};

// Экран интерфейса из данных игры (пока — главное меню).
struct Screen {
    constexpr static float kWidth = 1024.0f, kHeight = 768.0f;

    oc2::assets::TextTable texts;
    std::unique_ptr<oc2::render::GpImages> images;
    std::unique_ptr<oc2::ui::Fonts> fonts;
    std::unique_ptr<oc2::ui::RichText> text;
    std::unique_ptr<oc2::ui::UiContext> ctx;
    std::unique_ptr<oc2::ui::Menus> menus;
    oc2::ui::View view;

    Screen(oc2::render::SpriteRenderer& renderer, const fs::path& data, const std::string& firstMenu) {
        texts.loadGame(data);
        images = std::make_unique<oc2::render::GpImages>(renderer, data);
        fonts = std::make_unique<oc2::ui::Fonts>(*images);
        if (auto f = oc2::assets::findCaseInsensitive(data, "Dialogs/fonts.xml")) {
            const std::vector<std::uint8_t> bytes = readFile(*f);
            fonts->loadTagFonts(oc2::assets::parseXml({reinterpret_cast<const char*>(bytes.data()), bytes.size()}));
        }
        text = std::make_unique<oc2::ui::RichText>(*fonts);
        ctx.reset(new oc2::ui::UiContext{renderer, *images, *fonts, *text, texts, data});
        menus = std::make_unique<oc2::ui::Menus>(*ctx);
        if (firstMenu.empty()) return; // только интерфейс игры (--map)
        if (firstMenu.rfind("file:", 0) == 0) menus->addFile(firstMenu, firstMenu.substr(5));
        if (!menus->open(firstMenu)) throw std::runtime_error("нет меню «" + firstMenu + "»");
    }

    // Панель игры (dialogs\respanel.DialogsSystem.xml): пока — верхняя полоса с ресурсами.
    std::unique_ptr<oc2::ui::Dialog> hud;
    oc2::ui::View hudView;

    // Оригинал рисует панель в настоящих пикселях экрана; здесь — с масштабом по высоте
    // 768, без полей по бокам (полоса тянется на всю ширину).
    void fitHud(VkExtent2D e) {
        hudView.scale = float(e.height) / kHeight;
        hudView.ox = 0.0f;
        hudView.oy = 0.0f;
    }

    oc2::ui::Dialog* gameHud() {
        if (!hud) {
            if (auto path = oc2::assets::findCaseInsensitive(ctx->dataRoot, "dialogs/respanel.DialogsSystem.xml")) {
                hud = std::make_unique<oc2::ui::Dialog>(*ctx, *path);
                // Видима только верхняя полоса; остальные панели оригинал показывает кодом.
                auto& kids = hud->root().children;
                for (std::size_t i = 1; i < kids.size(); ++i) kids[i].visible = false;
            }
        }
        return hud.get();
    }

    // FUN_013da8a0 (cva_ResPanel_Money): счётчики ресурсов (ID 0..7) и «Population».
    void updateHud(const oc2::world::World& world, int player) {
        oc2::ui::Dialog* d = gameHud();
        if (!d) return;
        const int population = world.population(player), limit = world.populationLimit(player);
        d->forEach([&](oc2::ui::Widget& w) {
            bool money = false;
            for (const auto& a : w.actions) money = money || a.type == "cva_ResPanel_Money";
            if (!money) return;
            for (oc2::ui::Widget& c : w.children) {
                if (c.kind != oc2::ui::WidgetKind::TextButton) continue;
                if (c.name == "Population")
                    c.text = std::to_string(population) + "/" + std::to_string(limit); // образец в XML: «{CR}120{C}/{CR}0»
                else if (c.id >= 0 && c.id < 8)
                    c.text = std::to_string(std::min(world.resources[std::size_t(player)][std::size_t(c.id)], 999999));
            }
        });
    }

    // Экран 1024×768 вписывается в окно с сохранением пропорций.
    void fit(VkExtent2D e) {
        view.scale = std::min(float(e.width) / kWidth, float(e.height) / kHeight);
        view.ox = std::floor((float(e.width) - kWidth * view.scale) / 2.0f);
        view.oy = std::floor((float(e.height) - kHeight * view.scale) / 2.0f);
    }
    float toVirtualX(float px) const { return (px - view.ox) / view.scale; }
    float toVirtualY(float py) const { return (py - view.oy) / view.scale; }
};

// Подсказка над кнопкой панели: текст из mdlist (разметка {C …}, {F…}) и строка цены.
void drawPanelHint(Screen& screen, const oc2::assets::UnitDesc& desc, float mx, float my,
                   oc2::render::SpriteRenderer& sprites) {
    std::string upper = desc.name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    const std::string key = "UNITSMD\\" + upper + ".MD.HINT";
    std::string text = screen.texts.lookup(key);
    if (text == key) text = desc.name;
    static constexpr const char* kNames[] = {"дерево", "золото", "камень", "еда", "железо", "уголь"};
    std::string price;
    for (std::size_t r = 0; r < desc.price.size(); ++r)
        if (desc.price[r] > 0) price += (price.empty() ? "" : ", ") + std::string(kNames[r]) + " " + std::to_string(desc.price[r]);
    if (!price.empty()) text += "\\{C FF203060}Цена: " + price;
    const std::string cp = oc2::assets::utf8ToCp1251(text);
    const oc2::ui::Font* font = screen.fonts->findTagged("S");
    if (!font) return;
    const oc2::ui::View& v = screen.hudView;
    constexpr int kWidth = 300;
    const auto size = screen.text->measure(*font, cp, kWidth, oc2::ui::Align::Left);
    const float w = float(size.width + 16) * v.scale, h = float(size.height + 12) * v.scale;
    const float x = std::max(4.0f, mx - w), y = std::max(4.0f, my - h - 8.0f);
    sprites.fillRect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 0xFF5A4630);
    sprites.fillRect(x, y, w, h, 0xFFE8DCB8);
    screen.text->draw(*font, cp, int((x + 8.0f * v.scale) / v.scale), int((y + 6.0f * v.scale) / v.scale), kWidth,
                      oc2::ui::Align::Left, v);
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::optional<Options> opts = parseArgs(argc, argv);
        if (!opts) return 0;

        oc2::platform::Window window(std::string(oc2::kProductName) + " " + oc2::kVersion, 1280, 720);
        oc2::render::VulkanContext vulkan(window);
        std::printf("Видеоустройство: %s\n", vulkan.deviceName().c_str());
        oc2::render::SpriteRenderer sprites(vulkan);
        oc2::platform::Audio audio;
        if (opts->mute) audio.masterVolume = 0.0f;
        std::unique_ptr<oc2::platform::SoundBank> sounds;
        if (!opts->dataDir.empty()) sounds = std::make_unique<oc2::platform::SoundBank>(audio, opts->dataDir);
        const oc2::ui::Widget* lastHovered = nullptr;

        std::optional<Viewer> viewer;
        std::unique_ptr<Screen> screen;
        std::unique_ptr<MapView> mapView;
        oc2::render::TextureId pattern = 0;
        if (!opts->map.empty()) {
            if (opts->dataDir.empty()) throw std::runtime_error("для --map нужен --data");
            screen = std::make_unique<Screen>(sprites, opts->dataDir, std::string());
            mapView = std::make_unique<MapView>(vulkan, sprites, opts->dataDir, opts->map, screen->images.get());
            mapView->centerOn(vulkan.extent());
            mapView->showLocks = opts->locks;
            mapView->sounds = sounds.get();
            if (opts->win) mapView->world.killAllExcept(mapView->world.team[0]);
            if (opts->at) mapView->lookAt((*opts->at)[0], (*opts->at)[1], vulkan.extent());
            if (opts->move) {
                auto& us = mapView->world.units();
                for (auto& u : us) u.selected = u.player == (*opts->move)[0] && !u.building();
                long cx = 0, cy = 0, n = 0;
                for (const auto& u : us)
                    if (u.selected) cx += u.x(), cy += u.y(), ++n;
                for (std::size_t i = 0; n > 0 && i < us.size(); ++i)
                    if (us[i].selected)
                        mapView->world.orderMove(i, (*opts->move)[1] + int(us[i].x() - cx / n),
                                                 (*opts->move)[2] + int(us[i].y() - cy / n));
            }
            if (!opts->build.empty() || !opts->produce.empty()) {
                auto& w = mapView->world;
                const std::string what = opts->build.empty() ? opts->produce.substr(0, opts->produce.find(',')) : opts->build;
                const oc2::assets::UnitDesc* d = mapView->units->find(what);
                long cx = 0, cy = 0, n = 0;
                for (auto& u : w.units())
                    if (u.player == 0 && u.desc->peasant) u.selected = true, cx += u.x(), cy += u.y(), ++n;
                std::optional<std::size_t> b;
                // Свободное место — по спирали от центра крестьян.
                for (int r = 200; d && n && !b && r < 3000; r += 100)
                    for (int a = 0; a < 16 && !b; ++a)
                        b = w.placeBuilding(*d, 0, int(cx / n + r * std::cos(a * 0.3927)), int(cy / n + r * std::sin(a * 0.3927)));
                if (b && !opts->produce.empty()) {
                    while (w.units()[*b].underConstruction) w.nextStage(*b);
                    for (auto& u : w.units()) u.selected = false;
                    w.units()[*b].selected = true;
                    if (const auto* unit = mapView->units->find(opts->produce.substr(opts->produce.find(',') + 1)))
                        for (int k = 0; k < 3; ++k) w.orderProduce(*b, *unit);
                }
                if (b) {
                    for (std::size_t i = 0; i < w.units().size(); ++i)
                        if (w.units()[i].selected) w.orderBuild(i, *b);
                    mapView->lookAt(float(w.units()[*b].x()), float(w.units()[*b].y()), vulkan.extent());
                    std::printf("стройка: %s в (%d,%d)\n", what.c_str(), w.units()[*b].x(), w.units()[*b].y());
                } else {
                    std::printf("стройка: %s не заложить\n", what.c_str());
                }
            }
            if (opts->mine) {
                auto& w = mapView->world;
                std::optional<std::size_t> best;
                long long bestD = 0;
                for (std::size_t i = 0; i < w.units().size(); ++i) {
                    const auto& m = w.units()[i];
                    if (m.desc->peasantAbsorber <= 0 || !m.desc->producerResources) continue;
                    for (const auto& u : w.units())
                        if (u.player == 0 && u.desc->peasant) {
                            const long long dx = m.x() - u.x(), dy = m.y() - u.y(), d = dx * dx + dy * dy;
                            if (!best || d < bestD) best = i, bestD = d;
                            break;
                        }
                }
                int sent = 0;
                for (std::size_t i = 0; best && i < w.units().size(); ++i)
                    if (w.units()[i].player == 0 && w.orderEnter(i, *best)) ++sent;
                if (best) {
                    std::printf("шахта %s (%d,%d): послано %d\n", w.units()[*best].desc->name.c_str(), w.units()[*best].x(),
                                w.units()[*best].y(), sent);
                    mapView->lookAt(float(w.units()[*best].x()), float(w.units()[*best].y()), vulkan.extent());
                }
            }
            if (opts->aiSelf) mapView->startAi(opts->dataDir, true);
            if (opts->simulate > 0) {
                const auto steps = std::uint64_t(opts->simulate) * 25;
                for (std::uint64_t k = 0; k < steps; ++k) {
                    mapView->stepWorld();
                    mapView->world.soundEvents.clear();
                    mapView->world.shotEvents.clear();
                }
                const auto& w = mapView->world;
                for (const auto& ai : mapView->ais) {
                    const int p = ai->player();
                    std::map<std::string, std::pair<int, int>> kinds; // тип → (готово, строится)
                    std::size_t queued = 0;
                    for (const auto& u : w.units())
                        if (u.player == p && u.alive()) {
                            auto& k = kinds[u.desc->name];
                            ++(u.underConstruction ? k.second : k.first);
                            queued += u.queue.size();
                        }
                    const auto& r = w.resources[std::size_t(p)];
                    std::printf("игрок %d через %d с: население %d/%d; дерево %d, золото %d, камень %d, еда %d, железо %d, уголь %d; в очередях %zu\n",
                                p, opts->simulate, w.population(p), w.populationLimit(p), r[0], r[1], r[2], r[3], r[4], r[5],
                                queued);
                    for (const auto& [name, n] : kinds)
                        std::printf("  %s: %d%s\n", name.c_str(), n.first,
                                    n.second ? (" (+" + std::to_string(n.second) + " строится)").c_str() : "");
                }
            }
            if (opts->gather) {
                auto& w = mapView->world;
                int ordered = 0;
                for (std::size_t i = 0; i < w.units().size(); ++i) {
                    const auto& u = w.units()[i];
                    if (u.player != *opts->gather) continue;
                    if (auto s = w.sourceNear(u.x(), u.y(), 3000); s && w.orderGather(i, *s)) ++ordered;
                }
                std::printf("добыча: послано %d\n", ordered);
            }
        } else if (opts->sprite.empty() && !opts->dataDir.empty()) {
            screen = std::make_unique<Screen>(sprites, opts->dataDir, opts->menu);
        } else if (!opts->sprite.empty()) {
            const fs::path path = resolveSprite(*opts);
            viewer.emplace();
            viewer->name = path.filename().string();
            viewer->sheet = std::make_unique<oc2::render::SpriteSheet>(
                sprites, oc2::assets::G2dFile::parse(readFile(path)));
            viewer->step(opts->startFrame);
            std::printf("%s: %zu кадров, %ux%u\n", path.string().c_str(), viewer->sheet->frameCount(),
                        viewer->sheet->width(), viewer->sheet->height());
        } else {
            pattern = sprites.createTexture(testPattern());
        }

        std::optional<std::string> pendingStart; // карта, выбранная в меню
        std::uint64_t last = SDL_GetTicksNS();
        std::string shownTitle;
        for (long frame = 0; window.pumpEvents(); ++frame) {
            if (opts->frameLimit >= 0 && frame >= opts->frameLimit) break;
            if (!opts->screenshot.empty() && frame == opts->frameLimit - 1) vulkan.requestScreenshot(opts->screenshot);

            const std::uint64_t now = SDL_GetTicksNS();
            const double dt = double(now - last) / 1e9;
            last = now;
            if (std::find(window.pressedKeys.begin(), window.pressedKeys.end(), SDLK_ESCAPE) !=
                window.pressedKeys.end()) {
                // В игре Esc возвращает в меню (если игра начата из него), в меню закрывает
                // текущее окно, из главного — выход.
                if (mapView && mapView->placing) {
                    mapView->placing = nullptr; // отмена выбора места
                    window.pressedKeys.clear();
                } else if (mapView && screen) {
                    if (screen->menus->empty()) break; // игра открыта ключом --map, меню нет
                    mapView.reset();
                    window.pressedKeys.clear();
                    window.clicks.clear();
                } else {
                    if (!screen) break;
                    screen->menus->close();
                    if (screen->menus->empty()) break;
                }
            }
            // Начало игры из меню — вне кадра: загрузка карты занимает секунды и пользуется GPU.
            if (pendingStart && screen) {
                try {
                    mapView = std::make_unique<MapView>(vulkan, sprites, opts->dataDir, *pendingStart,
                                                        screen->images.get());
                    mapView->centerOnPlayer(0, vulkan.extent());
                    mapView->sounds = sounds.get();
                } catch (const std::exception& ex) {
                    std::fprintf(stderr, "не удалось начать игру: %s\n", ex.what());
                }
                pendingStart.reset();
                window.clicks.clear();
                last = SDL_GetTicksNS(); // время загрузки не идёт в симуляцию
                continue;
            }
            if (viewer) {
                for (std::uint32_t key : window.pressedKeys) viewer->handleKey(key);
                viewer->update(dt);
                if (std::string t = viewer->title(); t != shownTitle) {
                    window.setTitle(t);
                    shownTitle = std::move(t);
                }
            }

            if (sounds) sounds->updateMusic();
            VkCommandBuffer cmd = vulkan.beginFrame(screen || mapView ? kMenuClearColor : kClearColor);
            if (!cmd) continue;
            const VkExtent2D e = vulkan.extent();
            // Меню поверх кадра: наведение, нажатия, вывод. false — меню закрыты, пора выходить.
            auto runMenu = [&]() -> bool {
                screen->fit(e);
                oc2::ui::Menus& menus = *screen->menus;
                menus.update();
                // Наведение считается по рамкам прошлого кадра — как в оригинале.
                float mx = -1.0f, my = -1.0f;
                if (opts->mouse) {
                    mx = (*opts->mouse)[0];
                    my = (*opts->mouse)[1];
                } else if (window.mouseInside) {
                    mx = screen->toVirtualX(window.mouseX);
                    my = screen->toVirtualY(window.mouseY);
                }
                oc2::ui::Widget* hovered = menus.current().updateHover(mx, my);
                // Звук наведения — при смене элемента под курсором.
                if (hovered != lastHovered && hovered && sounds) sounds->play(hovered->mouseSound);
                lastHovered = hovered;
                if (opts->click && opts->mouse && frame == 2) window.clicks.push_back({1, mx, my});
                if (window.wheel != 0.0f) menus.current().scrollListAt(mx, my, window.wheel > 0 ? -1 : 1);
                for (const auto& click : window.clicks) {
                    if (click.button != 1 || !hovered) continue;
                    if (sounds) sounds->play(hovered->clickSound);
                    menus.click(*hovered);
                    break; // меню могло смениться
                }
                if (auto start = menus.takeStartRequest()) pendingStart = std::move(start);
                if (menus.empty()) return false;
                menus.current().draw(screen->view);
                return true;
            };

            if (mapView) {
                oc2::ui::Menus& menus = *screen->menus;
                if (opts->mouse) { // в игре — пиксели окна
                    window.mouseX = (*opts->mouse)[0];
                    window.mouseY = (*opts->mouse)[1];
                    window.mouseInside = true;
                }
                // Итог боя показан — карта стоит, нажатия идут окну итога.
                const bool resultOpen = mapView->ended && !menus.empty() && menus.currentName() == "EndGameMessage";
                for (std::uint32_t key : window.pressedKeys) mapView->handleKey(key, e);
                if (window.wheel != 0.0f && !resultOpen)
                    mapView->handleKey(window.wheel > 0 ? SDLK_PLUS : SDLK_MINUS, e);
                if (!resultOpen) mapView->scroll(dt, window, e);
                // Нажатия на панели не доходят до карты.
                screen->fitHud(e);
                oc2::ui::Dialog* hud = screen->gameHud();
                const float hx = window.mouseX / screen->hudView.scale, hy = window.mouseY / screen->hudView.scale;
                oc2::ui::Widget* overHud = hud && window.mouseInside ? hud->updateHover(hx, hy) : nullptr;
                if (!resultOpen) {
                    for (const auto& click : window.clicks)
                        if (!overHud && !(click.button == 1 && mapView->minimapClick(click.x, click.y, e)))
                            mapView->handleClick(click, e);
                    for (const auto& release : window.releases) mapView->handleRelease(release, mapView->player);
                }
                if (!mapView->ended) mapView->advance(dt);
                mapView->playSounds(sounds.get(), e);
                // Итог: раз в секунду симуляции проверяется, осталась ли одна команда.
                if (!mapView->ended && mapView->world.stepCount() % 25 == 0)
                    if (auto winner = mapView->world.winningTeam()) {
                        mapView->ended = true;
                        mapView->dragStart.reset();
                        menus.setGameResult(*winner == mapView->world.team[std::size_t(mapView->player)]);
                        menus.open("EndGameMessage");
                    }
                mapView->terrain->draw(cmd, mapView->camera, float(double(SDL_GetTicksNS()) / 1e9));
                mapView->objects->draw(mapView->camera, float(e.width), float(e.height), mapView->world,
                                       mapView->effects.get());
                mapView->drawPlacement(sprites, window.mouseX, window.mouseY);
                mapView->drawLocks(sprites, e);
                mapView->drawDragBox(sprites, window);
                mapView->drawMinimap(sprites, e);
                mapView->drawPanel(sprites, e);
                screen->updateHud(mapView->world, mapView->player);
                if (hud) hud->draw(screen->hudView);
                // Подсказка кнопки панели: «UNITSMD\ИМЯ.MD.HINT» из Text/mdlist.txt и цена.
                if (auto b = mapView->hoveredButton(window.mouseX, window.mouseY, e); b && !b->leave)
                    drawPanelHint(*screen, *b->desc, window.mouseX, window.mouseY, sprites);
                if (resultOpen) {
                    const bool open = runMenu();
                    if (menus.takeBackToMain()) {
                        // Конец игры: к главному меню (без него, при --map, — выход).
                        mapView.reset();
                        while (!menus.empty() && menus.currentName() != "Main") menus.close();
                        window.clicks.clear();
                        if (menus.empty()) break;
                    } else if (!open) {
                        break;
                    }
                }
            } else if (screen) {
                if (!runMenu()) break;
            } else if (viewer) {
                viewer->draw(e);
            } else {
                const float size = 256.0f;
                const float x = (float(e.width) - size) / 2.0f, y = (float(e.height) - size) / 2.0f;
                sprites.drawQuad(pattern, x, y, size, size, {128, 128, 128, 255});
                sprites.drawQuad(pattern, x + size / 2, y + size / 2, size / 2, size / 2, {128, 128, 255, 128});
            }
            sprites.flush(cmd);
            vulkan.endFrame();
        }
        if (mapView && (!opts->build.empty() || !opts->produce.empty())) {
            int people = 0;
            for (const auto& u : mapView->world.units())
                if (u.player == 0 && u.building())
                    std::printf("здание %s: стадия %d из %d, жизнь %d, в очереди %zu\n", u.desc->name.c_str(), u.stage,
                                u.desc->buildStages, u.life, u.queue.size());
                else if (u.player == 0)
                    ++people;
            std::printf("юнитов игрока 0: %d\n", people);
        }
        if (mapView && (opts->gather || opts->mine)) {
            const int who = opts->gather ? *opts->gather : 0;
            const auto& r = mapView->world.resources[std::size_t(who & 7)];
            std::printf("ресурсы игрока %d: дерево %d, золото %d, камень %d, еда %d, железо %d, уголь %d\n",
                        who, r[0], r[1], r[2], r[3], r[4], r[5]);
        }
    } catch (const std::exception& e) {
        showError(e.what());
        return 1;
    }
    return 0;
}
