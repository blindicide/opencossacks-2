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
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "assets/economy.hpp"
#include "assets/g2d.hpp"
#include "assets/game_paths.hpp"
#include "assets/image.hpp"
#include "core/version.hpp"
#include "core/settings.hpp"
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
#include "world/mission_script.hpp"
#include "world/savegame.hpp"
#include "world/settlements.hpp"
#include "world/strategic_ai.hpp"
#include "world/unit_catalog.hpp"
#include "world/world.hpp"
#include "render/terrain_renderer.hpp"
#include "ui/dialogs.hpp"
#include "ui/fonts.hpp"
#include "ui/menus.hpp"
#include "ui/bigmap_screen.hpp"
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
    "  --type <текст>        ввод текста на 20-м кадре (для проверок меню)\n"
    "  --clicks <x,y;x,y>    нажатия мыши по очереди, раз в 3 кадра (меню 1024×768, для проверок)\n"
    "  --click               с --mouse: нажать левую кнопку в этой точке на втором кадре\n"
    "  --locks               с --map: показать закрытые клетки (то же — клавиша L)\n"
    "  --pause               с --map: игра стартует на паузе\n"
    "  --wound N             с --map: здания — N % жизни (проверка огня и дыма)\n"
    "  --ai-self             с --map: игроком 0 тоже управляет стратегический ИИ (для проверок)\n"
    "  --simulate <с>        с --map: до первого кадра прожить <с> секунд игры и вывести итоги ИИ (для проверок)\n"
    "  --mute                без звука и музыки\n"
    "  --form line|column|square  с --select-own: построить выбранных отрядом (проверка построений)\n"
    "  --select-own          с --map: выбрать все свои юниты и поставить камеру на первого (проверка панели)\n"
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

// Условия боя за сектор: SecObjNN.txt (нападающему) или SecObjDefNN.txt (обороняющемуся) из каталога сектора.
// Текст остаётся в Windows-1251 с разметкой; пусто, если файла нет.
std::string sectorObjectives(const fs::path& data, const oc2::assets::BigSector* sector, bool attacker) {
    if (!sector) return {};
    std::string digits;
    for (char c : sector->name)
        if (c >= '0' && c <= '9') digits += c;
    if (digits.empty()) return {};
    const std::string rel = "Missions/StatData/" + sector->name + "/" + (attacker ? "SecObj" : "SecObjDef") + digits + ".txt";
    const auto path = oc2::assets::findCaseInsensitive(data, rel);
    if (!path) return {};
    const auto bytes = readFile(*path);
    return std::string(bytes.begin(), bytes.end());
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
    std::string typeText; // --type: текст, «введённый» на 20-м кадре (для проверок меню)
    std::vector<std::array<float, 2>> clickSeq; // --clicks: нажатия по очереди, раз в 3 кадра с 2-го (для проверок меню)
    std::optional<std::array<float, 2>> at; // точка карты в центре экрана
    std::optional<std::array<int, 3>> move;
    std::optional<int> gather;
    std::string build;
    std::string produce;
    bool mine = false;
    bool aiSelf = false;
    bool startPaused = false; // --pause: с --map игра стартует на паузе
    int wound = -1; // --wound N: здания на карте — N % жизни (проверка огня и дыма)
    bool saveCheck = false; // с --map: сохранить, загрузить и сравнить состояние
    int simulate = 0; // секунд
    bool click = false;
    bool locks = false; // показать закрытые клетки с самого начала
    bool mute = false;  // без звука
    int form = -1;          // проверка: построить выбранных (--select-own) отрядом: 0 шеренга, 1 колонна, 2 каре
    bool selectOwn = false; // проверка: выбрать своих юнитов и встать камерой на первого
    bool esc = false; // проверка: на первом кадре нажать Esc (меню игры)
    bool f1 = false;  // проверка: на первом кадре нажать F1 (помощь)
    bool bigMap = false;   // сразу открыть «Завоевание Европы»
    int bigNation = -1;    // и выбрать эту нацию
    int bigSelect = -1;    // и этот сектор
    bool skipFilm = false; // проверка сценария: пропускать диалоги
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
        } else if (a == "--type" && hasValue) {
            o.typeText = argv[++i];
        } else if (a == "--clicks" && hasValue) {
            std::string list = argv[++i];
            for (char& ch : list)
                if (ch == ';') ch = ' ';
            const char* p = list.c_str();
            float mx = 0, my = 0;
            int used = 0;
            while (std::sscanf(p, " %f,%f%n", &mx, &my, &used) == 2) {
                o.clickSeq.push_back({mx, my});
                p += used;
            }
        } else if (a == "--at" && hasValue) {
            float x = 0, y = 0;
            if (std::sscanf(argv[++i], "%f,%f", &x, &y) != 2)
                throw std::runtime_error("--at ждёт мировые координаты вида 11939,14758");
            o.at = std::array<float, 2>{x, y};
        } else if (a == "--f1") {
            o.f1 = true;
        } else if (a == "--esc") {
            o.esc = true;
        } else if (a == "--form" && hasValue) {
            const std::string v = argv[++i];
            o.form = v == "column" ? 1 : v == "square" ? 2 : 0;
        } else if (a == "--select-own") {
            o.selectOwn = true;
        } else if (a == "--dump-script" && hasValue) {
            // проверка: вывести сценарий карты (SXML) и выйти
            const auto bytes = readFile(argv[++i]);
            std::fputs(oc2::assets::Map3d::parse(bytes).scriptXml().c_str(), stdout);
            return std::nullopt;
        } else if (a == "--dump-zones" && hasValue) {
            const auto bytes = readFile(argv[++i]);
            std::fputs(oc2::assets::Map3d::parse(bytes).zonesXml().c_str(), stdout);
            return std::nullopt;
        } else if (a == "--dump-sections" && hasValue) {
            const auto bytes = readFile(argv[++i]);
            for (const auto& sec : oc2::assets::Map3d::parse(bytes).sections())
                std::printf("%s %zu\n", sec.tag.c_str(), sec.size);
            return std::nullopt;
        } else if (a == "--bigmap") {
            o.bigMap = true;
            if (hasValue && argv[i + 1][0] != '-') o.bigNation = int(std::strtol(argv[++i], nullptr, 10));
        } else if (a == "--bigsel" && hasValue) {
            o.bigSelect = int(std::strtol(argv[++i], nullptr, 10));
        } else if (a == "--skip-film") {
            o.skipFilm = true;
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
        } else if (a == "--save-check") {
            o.saveCheck = true;
        } else if (a == "--pause") {
            o.startPaused = true;
        } else if (a == "--wound" && hasValue) {
            o.wound = std::atoi(argv[++i]);
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
    std::unique_ptr<oc2::world::Settlements> settlements;
    std::unique_ptr<oc2::render::MapObjects> objects;
    oc2::world::World world;
    std::unique_ptr<oc2::render::Effects> effects;
    bool ended = false; // бой окончен, показано окно итога

    // Мини-карта (своя; оригинал рисует её кодом MiniMap4X.cpp): картинка VIE2, точки
    // юнитов цветом игрока, рамка видимой области. M — показать или скрыть.
    std::optional<oc2::render::TextureId> minimapTexture;
    bool minimapVisible = true;
    bool paused = false;                  // клавиша Pause: симуляция стоит, приказы принимаются
    bool ctrlHeld = false, altHeld = false; // модификаторы на момент нажатия клавиши
    std::array<std::vector<std::size_t>, 10> groups; // группы Ctrl+0…9
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
        const oc2::assets::UpgradeDef* upgrade = nullptr; // кнопка улучшения вместо юнита
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
        // Улучшения, которые исследует это здание ([UPGRADEPLACE]), — в клетках из #POSITION.
        for (const std::string& name : units->upgradesAt(units->typeOf(world.units()[*who].desc)))
            if (const oc2::assets::UpgradeDef* up = units->upgrade(name))
                out.push_back({nullptr, x0 + float(up->column) * cell, y0 + float(std::min(up->row, rows - 1)) * cell,
                               cell, false, up});
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
            if (b.upgrade) { // улучшение: значка в данных нет — плитка с уровнем точками
                const auto who = producer();
                const bool done = world.researched[std::size_t(player & 7)].count(b.upgrade->name) > 0;
                const bool running = who && world.units()[*who].researchLeft > 0 && world.units()[*who].research == b.upgrade->name;
                bool can = !done && !running && who && world.units()[*who].researchLeft == 0;
                for (std::size_t r = 0; can && r < b.upgrade->cost.size(); ++r)
                    can = world.resources[std::size_t(player & 7)][r] >= b.upgrade->cost[r];
                sprites.fillRect(b.x + 1.0f, b.y + 1.0f, b.size - 2.0f, b.size - 2.0f,
                                 done ? 0xFF305830 : running ? 0xFF806020 : can ? 0xFF907048 : 0xFF504030);
                for (int l = 0; l < b.upgrade->level; ++l)
                    sprites.fillRect(b.x + 6.0f + float(l) * 8.0f, b.y + b.size - 12.0f, 6.0f, 6.0f, 0xFFFFE060);
                if (running) {
                    const float part = 1.0f - float(world.units()[*who].researchLeft) / float(std::max(1, b.upgrade->time));
                    sprites.fillRect(b.x + 2.0f, b.y + b.size - 6.0f, (b.size - 4.0f) * part, 4.0f, 0xFF40E040);
                }
                continue;
            }
            const oc2::render::GpId id = images->id(b.desc->bigIconFile);
            const auto frame = std::size_t(b.desc->bigIconFrame);
            if (!images->valid(id) || frame >= images->frameCount(id)) {
                sprites.fillRect(b.x + 1.0f, b.y + 1.0f, b.size - 2.0f, b.size - 2.0f, 0xFF504030);
                continue;
            }
            const float w = float(images->frameWidth(id, frame)), h = float(images->frameHeight(id, frame));
            const float scale = (b.size - 2.0f) / std::max({w, h, 1.0f});
            const bool can = world.affordable(*b.desc, player) && world.available(*b.desc, player);
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
            if (b.upgrade) {
                if (c.button == 1)
                    if (const auto who = producer()) world.orderResearch(*who, *b.upgrade);
                return true;
            }
            if (c.button == 1 && (b.desc->building || b.desc->spriteObject) && world.available(*b.desc, player)) placing = b.desc;
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
    std::string startMap;   // карта игры (в сохранении)
    int player = 0;         // за кого играем: приказы слушают только его юниты
    // Сценарий миссии (правила из карты). Есть правила — ИИ запускает сам сценарий.
    std::unique_ptr<oc2::world::MissionScript> script;
    static inline int difficulty = 1; // сложность миссии 0..2 (GetDiff)
    // Таблица игроков стычки из меню; пусто — миссия играется как есть.
    static inline std::optional<std::array<oc2::ui::Menus::Slot, oc2::ui::Menus::kSlots>> setup;
    fs::path dataDir;
    // Плашка ShowMessageII: текст и оставшиеся шаги.
    std::string bannerText;
    int bannerLeft = 0;
    std::string battleNotice; // условия боя за сектор (SecObj*.txt), Windows-1251 с разметкой
    int noticeLeft = 0;       // кадров, сколько ещё показывать
    oc2::platform::SoundBank* sounds = nullptr; // для SELECT_SOUND и ORDER_SOUND
    double stepTime = 0.0; // накопленное время до следующего шага симуляции
    oc2::render::MapCamera camera;
    float width = 0.0f, height = 0.0f; // размер карты в пикселях проекции

    // shared — изображения меню (общий кэш текстур), иначе создаются свои.
    MapView(oc2::render::VulkanContext& vulkan, oc2::render::SpriteRenderer& sprites, const fs::path& data,
            const fs::path& mapFile, oc2::render::GpImages* shared = nullptr) {
        startMap = mapFile.generic_string();
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
        world.weaponList = &effects->weapons();
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
        if (setup) // команды из таблицы: одинаковый номер — союзники (8+ не пересекается с «своим номером»)
            for (int p = 0; p < oc2::ui::Menus::kSlots; ++p)
                if ((*setup)[std::size_t(p)].team) world.team[std::size_t(p)] = 8 + (*setup)[std::size_t(p)].team;
        world.humanTeam = world.team[std::size_t(player)];
        dataDir = data;
        std::size_t missing = 0;
        std::vector<std::optional<std::size_t>> mapUnits;
        for (const oc2::assets::MapUnit& u : map.units()) {
            const int owner = mission.singleOwner(u.player);
            const oc2::ui::Menus::Slot* slot =
                setup && owner >= 0 && owner < oc2::ui::Menus::kSlots ? &(*setup)[std::size_t(owner)] : nullptr;
            if (slot && !slot->active) { // «Нет игрока»: его юнитов на карте нет
                mapUnits.emplace_back();
                continue;
            }
            std::string type = u.type;
            if (slot && slot->nation > 0) { // нация из таблицы: суффикс «(XX)» типа заменяется
                const auto open = type.rfind('('), close = type.rfind(')');
                if (open != std::string::npos && close != std::string::npos && close > open) {
                    const std::string changed = type.substr(0, open + 1) + oc2::ui::Menus::nationCode(slot->nation - 1) +
                                                type.substr(close);
                    if (units->find(changed)) type = changed;
                }
            }
            if (const oc2::assets::UnitDesc* desc = units->find(type))
                mapUnits.push_back(world.addUnit(*desc, owner, u.x, u.y, u.direction));
            else
                mapUnits.emplace_back(), ++missing;
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
        settlements = std::make_unique<oc2::world::Settlements>(world, *units, data);
        std::printf("поселений: %zu\n", settlements->list().size());
        {
            oc2::world::MissionScript::Env env;
            env.world = &world;
            env.catalog = units.get();
            env.settlements = settlements.get();
            env.mapUnits = std::move(mapUnits);
            env.difficulty = difficulty;
            env.voiceSteps = [this](const std::string& file) { return sounds ? sounds->playVoice(file, false) : 0; };
            script = oc2::world::MissionScript::create(map, std::move(env));
            if (script && !script->hasRules()) script.reset();
        }
        if (script)
            std::printf("сценарий миссии: правила и фильмы из карты\n");
        else
            startAi(data, false);
        width = map.worldX(map.vertsInLine() - 1);
        height = float(map.rows() - 1) * oc2::assets::Map3d::kCell / 2.0f;
        std::printf("%s: сетка %d×%d, треугольников %zu, объектов %zu, юнитов %zu, источников ресурсов %zu\n",
                    path->string().c_str(), map.vertsInLine(), map.rows(), mesh.size() / 3, objects->staticCount(),
                    world.units().size(), world.sources.size());
    }

    // Бой за сектор «Завоевания Европы»: свои отряды — игрок 0, чужие — игрок 1. Карты секторов
    // пусты (только нейтралы), поэтому отряды расставляются здесь на открытой земле: нападающие
    // у одного края, обороняющиеся у другого.
    struct BigBattle {
        oc2::world::BigGame::Attack attack;
        bool playerAttacks = true;
        std::array<std::vector<std::vector<std::size_t>>, 2> squads; // [сторона][отряд] → юниты
        std::array<std::vector<std::string>, 2> types;
        int aiTimer = 0;
    };
    std::optional<BigBattle> big;

    // side — с какой стороны карты идёт нападение (0 север, 1 восток, 2 юг, 3 запад; -1 — по форме поля);
    // notice — текст условий боя (с разметкой, Windows-1251), он показывается в начале боя.
    void setupBigBattle(const oc2::world::BigGame::Attack& attack, const std::vector<std::string>& defenders,
                        bool playerAttacks, int side = -1, std::string notice = {}) {
        battleNotice = std::move(notice);
        noticeLeft = battleNotice.empty() ? 0 : 25 * 20;
        BigBattle b;
        b.attack = attack;
        b.playerAttacks = playerAttacks;
        const std::vector<std::string>& mine = playerAttacks ? attack.squads : defenders;
        const std::vector<std::string>& theirs = playerAttacks ? defenders : attack.squads;
        b.types = {mine, theirs};
        world.team[0] = 0;
        world.team[1] = 1;
        world.humanTeam = 0;
        player = 0;
        const oc2::world::LockMap& lm = world.lockMap();
        const int W = lm.width(), H = lm.height();
        auto roomy = [&](int cx, int cy) {
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                    if (lm.locked(cx + dx, cy + dy)) return false;
            return true;
        };
        std::vector<std::pair<int, int>> open;
        int minX = W, maxX = 0, minY = H, maxY = 0;
        for (int cy = 4; cy < H - 4; cy += 4)
            for (int cx = 4; cx < W - 4; cx += 4)
                if (roomy(cx, cy)) {
                    open.emplace_back(cx, cy);
                    minX = std::min(minX, cx), maxX = std::max(maxX, cx);
                    minY = std::min(minY, cy), maxY = std::max(maxY, cy);
                }
        if (open.empty()) open.emplace_back(W / 2, H / 2), minX = maxX = W / 2, minY = maxY = H / 2;
        const bool alongX = side >= 0 ? (side == 1 || side == 3) : maxX - minX >= maxY - minY;
        auto nearestTo = [&](double fx, double fy) {
            const int tx = int(minX + fx * (maxX - minX)), ty = int(minY + fy * (maxY - minY));
            long long best = -1;
            std::pair<int, int> at = open[0];
            for (const auto& o : open) {
                const long long d = (long long)(o.first - tx) * (o.first - tx) + (long long)(o.second - ty) * (o.second - ty);
                if (best < 0 || d < best) best = d, at = o;
            }
            return at;
        };
        // Нападающие — слева (или сверху), обороняющиеся — справа; игрок и враг по ролям.
        // Известная сторона нападения: нападающие у этого края карты, обороняющиеся у противоположного.
        const double near = 0.30, far = 0.70;
        const auto from = side == 0 ? nearestTo(0.5, near) : side == 1 ? nearestTo(far, 0.5) : side == 2 ? nearestTo(0.5, far)
                          : side == 3 ? nearestTo(near, 0.5) : alongX ? nearestTo(near, 0.5) : nearestTo(0.5, near);
        const auto to = side == 0 ? nearestTo(0.5, far) : side == 1 ? nearestTo(near, 0.5) : side == 2 ? nearestTo(0.5, near)
                        : side == 3 ? nearestTo(far, 0.5) : alongX ? nearestTo(far, 0.5) : nearestTo(0.5, far);
        const std::array<std::pair<int, int>, 2> anchor = {playerAttacks ? from : to, playerAttacks ? to : from};
        for (int side = 0; side < 2; ++side) {
            // Шеренги отрядов поперёк линии боя, дальше по глубине — следующие ряды.
            const std::pair<int, int> dirAway = alongX ? std::make_pair((anchor[std::size_t(side)].first < anchor[std::size_t(1 - side)].first) ? -1 : 1, 0)
                                                       : std::make_pair(0, (anchor[std::size_t(side)].second < anchor[std::size_t(1 - side)].second) ? -1 : 1);
            const int sideCols = 4;
            for (std::size_t k = 0; k < b.types[std::size_t(side)].size(); ++k) {
                const oc2::assets::UnitDesc* desc = units->find(b.types[std::size_t(side)][k]);
                if (!desc) desc = units->find("UnitFuz(" + std::string(oc2::assets::bigNationCode(0)) + ")");
                if (!desc) {
                    b.squads[std::size_t(side)].emplace_back();
                    continue;
                }
                const bool gun = desc->name.rfind("Art", 0) == 0;
                const int count = gun ? 4 : 12;
                const int col = int(k % sideCols) - 1, row = int(k / sideCols);
                const int across = col * 260 - 100, depth = row * 190;
                const int sx = anchor[std::size_t(side)].first * 16 + (alongX ? depth * dirAway.first : across);
                const int sy = anchor[std::size_t(side)].second * 16 + (alongX ? across : depth * dirAway.second);
                const int dir = alongX ? (dirAway.first > 0 ? 192 : 64) : (dirAway.second > 0 ? 0 : 128);
                std::vector<std::size_t> ids;
                for (int u = 0; u < count; ++u) {
                    int x = sx + (u % 4) * 48, y = sy + (u / 4) * 48;
                    if (const auto o = lm.nearestOpen(x / 16, y / 16, 14)) x = o->first * 16 + 8, y = o->second * 16 + 8;
                    ids.push_back(world.addUnit(*desc, side, x, y, dir));
                }
                if (ids.size() >= 2) world.formSquad(ids, oc2::world::World::Formation::Line);
                b.squads[std::size_t(side)].push_back(std::move(ids));
            }
        }
        big = std::move(b);
    }

    // Простой ИИ боя за сектор: раз в две секунды замершие отряды врага идут на ближайшего живого
    // юнита игрока; встретив его, они вступают в бой сами (обычная реакция юнитов).
    void bigBattleAi() {
        if (noticeLeft > 0) --noticeLeft;
        if (!big || ++big->aiTimer < 50) return;
        big->aiTimer = 0;
        // Оборона стоит на месте, пока к ней не подошли; нападающий ИИ идёт вперёд.
        if (big->playerAttacks) return;
        const auto& us = world.units();
        for (const auto& ids : big->squads[1]) {
            bool busy = false;
            long long cx = 0, cy = 0;
            int alive = 0;
            for (std::size_t id : ids)
                if (us[id].alive()) {
                    ++alive;
                    cx += us[id].x(), cy += us[id].y();
                    busy = busy || us[id].moving || us[id].target;
                }
            if (!alive || busy) continue;
            cx /= alive, cy /= alive;
            long long best = -1;
            int tx = 0, ty = 0;
            for (const auto& group : big->squads[0])
                for (std::size_t id : group)
                    if (us[id].alive()) {
                        const long long d = (us[id].x() - cx) * (us[id].x() - cx) + (us[id].y() - cy) * (us[id].y() - cy);
                        if (best < 0 || d < best) best = d, tx = us[id].x(), ty = us[id].y();
                    }
            if (best < 0) return;
            std::vector<std::size_t> live;
            for (std::size_t id : ids)
                if (us[id].alive()) live.push_back(id);
            world.orderMoveGroup(live, tx, ty);
        }
    }

    // Итог боя: отряд уцелел, если жива треть его юнитов. retreat — игрок ушёл с поля боя (бой проигран).
    oc2::world::BigGame::Outcome bigOutcome(bool retreat) {
        oc2::world::BigGame::Outcome out;
        const auto& us = world.units();
        auto alive = [&](const std::vector<std::size_t>& ids) {
            int n = 0;
            for (std::size_t id : ids) n += us[id].alive();
            return n * 3 >= int(ids.size());
        };
        const int atk = big->playerAttacks ? 0 : 1, def = 1 - atk;
        const auto winner = world.winningTeam();
        const int humanTeam = world.team[0];
        bool humanWon = !retreat && winner && *winner == humanTeam;
        out.attackerWon = big->playerAttacks ? humanWon : !humanWon;
        out.survived.assign(big->attack.squads.size(), false);
        for (std::size_t i = 0; i < big->squads[std::size_t(atk)].size() && i < out.survived.size(); ++i)
            out.survived[i] = alive(big->squads[std::size_t(atk)][i]);
        // Отряды, у которых не нашлось описания юнитов, считаются уцелевшими.
        for (std::size_t i = big->squads[std::size_t(atk)].size(); i < out.survived.size(); ++i) out.survived[i] = true;
        for (const auto& ids : big->squads[std::size_t(def)]) out.defenderLost += !alive(ids);
        return out;
    }

    // Стратегический ИИ игрокам 0..6 с юнитами (кроме нашего, если не self), по сценарию их нации.
    void startAi(const fs::path& data, bool self, int only = -1) {
        if (only < 0) ais.clear();
        if (!world.market)
            if (auto f = oc2::assets::findCaseInsensitive(data, "economy.txt")) try {
                    const std::vector<std::uint8_t> b = readFile(*f);
                    world.market.emplace(oc2::assets::Economy::parse(std::string_view(
                        reinterpret_cast<const char*>(b.data()), b.size())));
                } catch (const std::exception& e) {
                    std::printf("economy.txt: %s\n", e.what());
                }
        for (int p = 0; p < 7; ++p) {
            if (only >= 0 && p != only) continue;
            if (p == player && !self) continue;
            if (setup && !(*setup)[std::size_t(p)].active) continue;
            const std::string code = oc2::world::playerNation(world, *units, p);
            if (code.empty()) continue;
            if (auto script = oc2::world::loadAiScript(data, code)) {
                ais.push_back(std::make_unique<oc2::world::StrategicAi>(world, *units, p, std::move(*script), 1,
                                                                        0x9E3779B9u * std::uint32_t(p + 1)));
                ais.back()->setSettlements(settlements.get());
                std::printf("ИИ игрока %d: нация %s\n", p, code.c_str());
            } else {
                std::printf("ИИ игрока %d: нет сценария для нации %s\n", p, code.c_str());
            }
        }
    }

    // Шаг мира и раздумья ИИ.
    void stepWorld() {
        if (script && script->gamePaused) { // PAUSE сценария: мир стоит, правила идут
            runScript();
            return;
        }
        world.step();
        if (settlements) settlements->step();
        for (auto& ai : ais) ai->update();
        if (script) runScript();
    }

    // Шаг сценария миссии и применение его событий.
    VkExtent2D viewExtent{1280, 720}; // размер окна для камеры сценария; обновляется каждый кадр
    void runScript() {
        script->step();
        if (script->myNation) {
            player = *script->myNation;
            world.humanTeam = world.team[std::size_t(player & 7)];
            script->myNation.reset();
        }
        for (const auto& a : script->aiStarts) startAi(dataDir, false, a.player);
        script->aiStarts.clear();
        if (sounds)
            for (const std::string& v : script->voices) sounds->playVoice(v);
        script->voices.clear();
        for (const auto& m : script->messages) {
            bannerText = m.textId;
            bannerLeft = 25 * 8;
        }
        script->messages.clear();
        if (bannerLeft > 0) --bannerLeft;
        if (script->cameraPoint && script->filmRunning()) lookAt(float(script->cameraPoint->first), float(script->cameraPoint->second), viewExtent);
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

    // Построение выбранных в отряд (E — шеренга, R — колонна, T — каре).
    void formSelected(oc2::world::World::Formation form) {
        std::vector<std::size_t> ids;
        auto& us = world.units();
        for (std::size_t i = 0; i < us.size(); ++i)
            if (us[i].selected && us[i].player == player) ids.push_back(i);
        if (ids.size() >= 2) world.formSquad(ids, form);
    }

    // Выбор одного бойца выбирает весь его отряд.
    void selectSquadmates() {
        auto& us = world.units();
        std::vector<bool> take(world.squads.size());
        for (const auto& u : us)
            if (u.selected && u.squad >= 0) take[std::size_t(u.squad)] = true;
        for (auto& u : us)
            if (u.squad >= 0 && take[std::size_t(u.squad)] && u.alive() && u.player == player) u.selected = true;
    }

    // Выбор по клавишам: Ctrl-A — войска, Ctrl-B — здания, Ctrl-Z — все того же типа,
    // Ctrl-P — свободные крестьяне, Ctrl-0…9 запоминает выбранных, 0…9 — вызывает группу.
    bool selectionKey(std::uint32_t key) {
        auto& us = world.units();
        const auto mine = [&](const oc2::world::Unit& u) { return u.player == player && u.alive() && !u.inside; };
        const auto pick = [&](auto&& wanted) {
            for (auto& u : us) u.selected = mine(u) && wanted(u);
        };
        if (ctrlHeld && key == SDLK_A) {
            pick([](const oc2::world::Unit& u) { return !u.building() && !u.desc->peasant; });
            return true;
        }
        if (ctrlHeld && key == SDLK_B) {
            pick([](const oc2::world::Unit& u) { return u.building() && !u.underConstruction; });
            return true;
        }
        if (ctrlHeld && key == SDLK_P) {
            pick([](const oc2::world::Unit& u) {
                return u.desc->peasant && !u.moving && u.gather == oc2::world::Unit::Gather::None && !u.buildTarget && !u.mine && !u.target;
            });
            return true;
        }
        if (ctrlHeld && key == SDLK_Z) {
            std::vector<const oc2::assets::UnitDesc*> types;
            for (const auto& u : us)
                if (u.selected && mine(u)) types.push_back(u.desc);
            if (types.empty()) return true;
            pick([&](const oc2::world::Unit& u) { return std::find(types.begin(), types.end(), u.desc) != types.end(); });
            return true;
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            auto& g = groups[std::size_t(key - SDLK_0)];
            if (ctrlHeld) {
                g.clear();
                for (std::size_t i = 0; i < us.size(); ++i)
                    if (us[i].selected && mine(us[i])) g.push_back(i);
                return true;
            }
            if (g.empty()) return true;
            for (auto& u : us) u.selected = false;
            for (std::size_t i : g)
                if (i < us.size() && mine(us[i])) us[i].selected = true;
            return true;
        }
        return false;
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
        case SDLK_S: {
            auto& us = world.units();
            for (std::size_t i = 0; i < us.size(); ++i)
                if (us[i].selected && us[i].player == player && !us[i].building()) world.orderStop(i);
            return;
        }
        case SDLK_E: formSelected(oc2::world::World::Formation::Line); return;
        case SDLK_R: formSelected(oc2::world::World::Formation::Column); return;
        case SDLK_T: formSelected(oc2::world::World::Formation::Square); return;
        case SDLK_M: minimapVisible = !minimapVisible; return;
        case SDLK_PAUSE:
        case SDLK_GRAVE: paused = !paused; return;
        case SDLK_DELETE: {
            PlayerInput input(world);
            for (std::size_t i = 0; i < world.units().size(); ++i)
                if (world.units()[i].selected && world.units()[i].player == player) world.orderDestroy(i);
            return;
        }
        case SDLK_BACKSPACE: {
            long sx = 0, sy = 0, n = 0;
            for (const auto& u : world.units())
                if (u.selected && u.alive()) sx += u.x(), sy += u.y(), ++n;
            if (n > 0) lookAt(float(sx / n), float(sy / n), e);
            return;
        }
        case SDLK_SPACE: // к месту последнего события у игрока
            if (world.lastAlert.step >= 0) lookAt(float(world.lastAlert.x), float(world.lastAlert.y), e);
            return;
        default: selectionKey(key); return;
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
            tick();
            stepTime -= oc2::world::kStepSeconds;
        }
        if (stepTime > oc2::world::kStepSeconds) stepTime = 0.0;
    }

    // Один шаг симуляции вместе с эффектами выстрелов.
    void tick() {
        stepWorld();
        effects->step();
        for (const auto& shot : world.shotEvents)
            if (auto sound = effects->spawn(shot.weapon, shot.x, shot.y, shot.direction))
                world.soundEvents.push_back({*sound, shot.x, shot.y});
        world.shotEvents.clear();
    }

    // Пока жив, приказы мира пишутся в журнал сохранения (ввод игрока, а не ИИ и сценарий).
    struct PlayerInput {
        oc2::world::World& w;
        explicit PlayerInput(oc2::world::World& world) : w(world) { w.recording = true; }
        ~PlayerInput() { w.recording = false; }
    };

    // Пропуск диалога сценария (пробел); пишется в журнал, чтобы загрузка повторила тот же ход.
    void skipDialog() {
        if (!script) return;
        world.journal.push_back({world.stepCount(), "skip", {}, {}});
        script->skipDialog();
    }

    // Приказ из журнала сохранения.
    void applyCommand(const oc2::world::World::Command& c) {
        const auto type = [&] { return units->find(c.s); };
        if (c.kind == "move") world.orderMove(std::size_t(c.a[0]), c.a[1], c.a[2]);
        else if (c.kind == "attack") world.orderAttack(std::size_t(c.a[0]), std::size_t(c.a[1]));
        else if (c.kind == "movegroup") world.orderMoveGroup(oc2::world::World::parseIds(c.s), c.a[0], c.a[1]);
        else if (c.kind == "form")
            world.formSquad(oc2::world::World::parseIds(c.s), static_cast<oc2::world::World::Formation>(c.a[0]));
        else if (c.kind == "stop") world.orderStop(std::size_t(c.a[0]));
        else if (c.kind == "destroy") world.orderDestroy(std::size_t(c.a[0]));
        else if (c.kind == "gather") world.orderGather(std::size_t(c.a[0]), std::size_t(c.a[1]));
        else if (c.kind == "build") world.orderBuild(std::size_t(c.a[0]), std::size_t(c.a[1]));
        else if (c.kind == "enter") world.orderEnter(std::size_t(c.a[0]), std::size_t(c.a[1]));
        else if (c.kind == "leave") world.leaveMine(std::size_t(c.a[0]));
        else if (c.kind == "produce") {
            if (const auto* d = type()) world.orderProduce(std::size_t(c.a[0]), *d);
        } else if (c.kind == "research") {
            if (const auto* u = units->upgrade(c.s)) world.orderResearch(std::size_t(c.a[0]), *u, c.a[1]);
        } else if (c.kind == "place") {
            if (const auto* d = type()) world.placeBuilding(*d, c.a[0], c.a[1], c.a[2]);
        } else if (c.kind == "sow") {
            if (const auto* d = type()) world.sowField(*d, c.a[0], c.a[1], c.a[2]);
        } else if (c.kind == "skip") {
            if (script) script->skipDialog();
        }
    }

    // Загрузка сохранения: игра уже построена с теми же картой и таблицей игроков — проигрываются
    // приказы журнала до сохранённого шага (без звуков). Журнал остаётся, чтобы следующие сохранения были полными.
    void replay(const oc2::world::SaveGame& g) {
        oc2::platform::SoundBank* const saved = std::exchange(sounds, nullptr);
        std::size_t next = 0;
        for (;;) {
            while (next < g.journal.size() && g.journal[next].step <= world.stepCount()) applyCommand(g.journal[next++]);
            if (world.stepCount() >= g.steps) break;
            tick();
        }
        world.soundEvents.clear();
        world.journal = g.journal;
        sounds = saved;
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
        const PlayerInput input(world);
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
            std::vector<std::size_t> ids;
            for (std::size_t i = 0; i < us.size(); ++i)
                if (us[i].selected && us[i].player == player) ids.push_back(i);
            if (!ids.empty()) world.orderMoveGroup(ids, tx, ty);
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
            selectSquadmates();
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
        selectSquadmates();
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
    float scrollFactor = 1.0f; // из настроек: 0.5 … 1.5
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
        const float speed = 1200.0f * scrollFactor / camera.zoom; // пикселей проекции в секунду
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
void drawPanelHint(Screen& screen, const oc2::assets::UnitDesc* unitDesc, const oc2::assets::UpgradeDef* upgrade,
                   float mx, float my, oc2::render::SpriteRenderer& sprites) {
    static const oc2::assets::UnitDesc kNoDesc;
    const oc2::assets::UnitDesc& desc = unitDesc ? *unitDesc : kNoDesc;
    std::string upper = desc.name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    const std::string key = "UNITSMD\\" + upper + ".MD.HINT";
    std::string text = screen.texts.lookup(key);
    if (text == key) text = desc.name;
    static constexpr const char* kNames[] = {"дерево", "золото", "камень", "еда", "железо", "уголь"};
    std::string price;
    if (upgrade) {
        text = "Улучшение " + upgrade->name + " (уровень " + std::to_string(upgrade->level) + ")";
        for (std::size_t r = 0; r < upgrade->cost.size(); ++r)
            if (upgrade->cost[r] > 0)
                price += (price.empty() ? "" : ", ") + std::string(kNames[r]) + " " + std::to_string(upgrade->cost[r]);
    } else {
        for (std::size_t r = 0; r < desc.price.size(); ++r)
            if (desc.price[r] > 0)
                price += (price.empty() ? "" : ", ") + std::string(kNames[r]) + " " + std::to_string(desc.price[r]);
    }
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

// Итоговая статистика: победитель и проигравший (у человека — он сам, у другой стороны — самый затратливый игрок).
oc2::ui::Menus::GameStats buildGameStats(Screen& screen, const MapView& view, bool humanWon) {
    using oc2::ui::Menus;
    using World = oc2::world::World;
    const oc2::world::World& world = view.world;
    // Название юнита — между «{C FFxxxxxx}» и следующей разметкой в подсказке «UNITSMD\ИМЯ.MD.HINT».
    auto unitName = [&](const oc2::assets::UnitDesc& d) {
        std::string upper = d.name;
        for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        const std::string key = "UNITSMD\\" + upper + ".MD.HINT";
        const std::string hint = screen.texts.lookup(key);
        if (hint == key) return d.name;
        const auto open = hint.find("{C ");
        const auto close = open == std::string::npos ? open : hint.find('}', open);
        if (close == std::string::npos) return d.name;
        const auto end = hint.find_first_of("{\\", close + 1);
        return hint.substr(close + 1, end == std::string::npos ? std::string::npos : end - close - 1);
    };
    auto total = [&](int p) {
        int sum = 0;
        for (int k = 0; k < World::SpendBuy; ++k)
            for (int v : world.spent[std::size_t(p)][std::size_t(k)]) sum += v;
        return sum;
    };
    auto side = [&](int p) {
        Menus::GameStats::Side out;
        std::string nation;
        for (const auto& u : world.units())
            if (u.player == p) {
                const std::string& t = u.desc->typeName;
                const auto open = t.rfind('('), close = t.rfind(')');
                if (open != std::string::npos && close != std::string::npos && close > open + 1) nation = t.substr(open + 1, close - open - 1);
                break;
            }
        for (int n = 0; n < Menus::kNationCount; ++n)
            if (nation == Menus::nationCode(n)) out.race = Menus::nationName(n);
        const std::string profile = screen.menus->profileName();
        out.name = p == view.player ? (profile.empty() ? std::string("Игрок") : profile) : "Компьютер " + std::to_string(p);
        static constexpr int kColumn[6] = {0, 3, 2, 1, 4, 5};
        const auto& sp = world.spent[std::size_t(p)];
        const int rowKind[7] = {-1, World::SpendUpgrade, World::SpendUpkeep, World::SpendUnits, World::SpendBuildings,
                                World::SpendBuy, World::SpendSell};
        for (int c = 0; c < 6; ++c)
            for (int r = 1; r < 7; ++r) {
                const int v = sp[std::size_t(rowKind[r])][std::size_t(kColumn[c])];
                out.table[std::size_t(r)][std::size_t(c)] = v;
                if (r <= 4) out.table[0][std::size_t(c)] += v;
            }
        std::map<const oc2::assets::UnitDesc*, Menus::GameStats::Row> rows;
        auto add = [&](const auto& counts, int Menus::GameStats::Row::*field) {
            for (const auto& [desc, n] : counts[std::size_t(p & 7)]) rows[desc].*field += n;
        };
        add(world.producedCount, &Menus::GameStats::Row::built);
        add(world.killedCount, &Menus::GameStats::Row::killed);
        add(world.lostCount, &Menus::GameStats::Row::lost);
        for (auto& [desc, row] : rows) {
            row.name = unitName(*desc);
            (desc->building ? out.buildings : out.units).push_back(row);
        }
        return out;
    };
    // Противник: другая команда с наибольшими тратами.
    int enemy = -1;
    for (int p = 0; p < 7; ++p)
        if (p != view.player && world.team[std::size_t(p & 7)] != world.team[std::size_t(view.player & 7)] &&
            (enemy < 0 || total(p) > total(enemy)))
            enemy = p;
    Menus::GameStats stats;
    const Menus::GameStats::Side mine = side(view.player);
    Menus::GameStats::Side theirs = enemy >= 0 ? side(enemy) : Menus::GameStats::Side{};
    stats.winner = humanWon ? mine : theirs;
    stats.loser = humanWon ? theirs : mine;
    return stats;
}

// Сценарий миссии на экране: список задач слева сверху, плашка сообщения, диалог внизу (портреты не рисуем).
// Надпись «ПАУЗА» (PAUSETEXT) по центру сверху, пока симуляция стоит по клавише Pause.
void drawPauseBanner(Screen& screen, const MapView& view, VkExtent2D e, oc2::render::SpriteRenderer& sprites) {
    if (!view.paused) return;
    const oc2::ui::Font* font = screen.fonts->findTagged("G18");
    if (!font) font = screen.fonts->findTagged("S");
    if (!font) return;
    const oc2::ui::View& v = screen.hudView;
    const std::string cp = oc2::assets::utf8ToCp1251(screen.texts.contains("PAUSETEXT") ? screen.texts.lookup("PAUSETEXT") : "ПАУЗА");
    const auto size = screen.text->measure(*font, cp, 400, oc2::ui::Align::Center);
    const float w = float(size.width + 40) * v.scale, h = float(size.height + 16) * v.scale;
    const float x = (float(e.width) - w) / 2.0f, y = 48.0f * v.scale;
    sprites.fillRect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 0xFF5A4630);
    sprites.fillRect(x, y, w, h, 0xEEE8DCB8);
    screen.text->draw(*font, cp, int(x / v.scale) + 20, int(y / v.scale) + 8, size.width + 4, oc2::ui::Align::Center, v);
}

// Условия боя за сектор (SecObj*.txt) по центру сверху в начале боя.
void drawBattleNotice(Screen& screen, const MapView& view, VkExtent2D e, oc2::render::SpriteRenderer& sprites) {
    if (view.noticeLeft <= 0 || view.battleNotice.empty()) return;
    const oc2::ui::Font* font = screen.fonts->findTagged("S");
    if (!font) return;
    const oc2::ui::View& v = screen.hudView;
    const auto size = screen.text->measure(*font, view.battleNotice, 420, oc2::ui::Align::Left);
    const float w = float(size.width + 28) * v.scale, h = float(size.height + 20) * v.scale;
    const float x = (float(e.width) - w) / 2.0f, y = 84.0f * v.scale;
    sprites.fillRect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 0xFF5A4630);
    sprites.fillRect(x, y, w, h, 0xEEE8DCB8);
    screen.text->draw(*font, view.battleNotice, int(x / v.scale) + 14, int(y / v.scale) + 10, size.width + 4, oc2::ui::Align::Left, v);
}

void drawMissionHud(Screen& screen, const MapView& view, VkExtent2D e, oc2::render::SpriteRenderer& sprites) {
    drawPauseBanner(screen, view, e, sprites);
    drawBattleNotice(screen, view, e, sprites);
    if (!view.script) return;
    const oc2::ui::Font* font = screen.fonts->findTagged("S");
    if (!font) return;
    const oc2::ui::View& v = screen.hudView;
    const auto box = [&](const std::string& utf8, float x, float y, int width, std::uint32_t border, std::uint32_t fill) {
        const std::string cp = oc2::assets::utf8ToCp1251(utf8);
        const auto size = screen.text->measure(*font, cp, width, oc2::ui::Align::Left);
        const float w = float(size.width + 16) * v.scale, h = float(size.height + 12) * v.scale;
        sprites.fillRect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, border);
        sprites.fillRect(x, y, w, h, fill);
        screen.text->draw(*font, cp, int((x + 8.0f * v.scale) / v.scale), int((y + 6.0f * v.scale) / v.scale), width,
                          oc2::ui::Align::Left, v);
        return h;
    };
    float y = 8.0f;
    for (const std::string& id : view.script->tasks())
        y += box(screen.texts.lookup(id), 8.0f, y, 260, 0xFF5A4630, 0xDDE8DCB8) + 4.0f;
    if (view.bannerLeft > 0 && !view.bannerText.empty())
        box(screen.texts.lookup(view.bannerText), float(e.width) / 2.0f - 200.0f * v.scale, 8.0f, 400, 0xFF5A4630, 0xEEE8DCB8);
    const auto& d = view.script->dialog();
    if (d.visible && !d.text.empty()) {
        std::string text = d.text;
        const std::string speaker = d.speaker.empty() ? "" : screen.texts.lookup(d.speaker) + ": ";
        box(speaker + screen.texts.lookup(text), float(e.width) / 2.0f - 400.0f * v.scale, float(e.height) - 140.0f * v.scale, 800,
            0xFF5A4630, 0xEEE8DCB8);
    }
    if (view.script->blackScreen() > 0.0f) sprites.fillRect(0.0f, 0.0f, float(e.width), float(e.height), 0xFF000000);
}

// Сводка выбранных юнитов правее мини-карты: имя («UNITSMD\\ИМЯ.MD» из mdlist), здоровье и число.
void drawSelectionInfo(Screen& screen, const MapView& view, VkExtent2D e, oc2::render::SpriteRenderer& sprites) {
    const oc2::ui::Font* font = screen.fonts->findTagged("S");
    if (!font) return;
    std::vector<std::pair<const oc2::assets::UnitDesc*, int>> kinds; // тип → сколько выбрано
    const oc2::world::Unit* first = nullptr;
    int total = 0;
    for (const auto& u : view.world.units()) {
        if (!u.selected || !u.alive() || u.inside) continue;
        if (!first) first = &u;
        ++total;
        auto it = std::find_if(kinds.begin(), kinds.end(), [&](const auto& k) { return k.first == u.desc; });
        if (it == kinds.end()) kinds.emplace_back(u.desc, 1);
        else ++it->second;
    }
    if (!first) return;
    std::string text;
    for (std::size_t i = 0; i < kinds.size() && i < 5; ++i) {
        std::string upper = kinds[i].first->name;
        for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        const std::string key = "UNITSMD\\" + upper + ".MD";
        std::string name = screen.texts.lookup(key);
        if (name == key) name = kinds[i].first->name;
        text += name + (kinds[i].second > 1 ? ": " + std::to_string(kinds[i].second) : "") + "\\";
    }
    if (kinds.size() > 5) text += "...\\";
    if (total == 1) text += "Здоровье: " + std::to_string(first->life) + " / " + std::to_string(view.world.maxLife(*first));
    const std::string cp = oc2::assets::utf8ToCp1251(text);
    const oc2::ui::View& v = screen.hudView;
    constexpr int kWidth = 220;
    const auto size = screen.text->measure(*font, cp, kWidth, oc2::ui::Align::Left);
    const auto mini = view.minimapRect(e);
    const float w = float(size.width + 16) * v.scale, h = float(size.height + 12) * v.scale;
    const float x = mini.x + mini.w + 12.0f, y = float(e.height) - h - 8.0f;
    sprites.fillRect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 0xFF5A4630);
    sprites.fillRect(x, y, w, h, 0xFFE8DCB8);
    screen.text->draw(*font, cp, int((x + 8.0f * v.scale) / v.scale), int((y + 6.0f * v.scale) / v.scale), kWidth,
                      oc2::ui::Align::Left, v);
    if (total == 1 && first->desc->life > 0) { // полоса здоровья под текстом
        const float part = std::clamp(float(first->life) / float(std::max(1, view.world.maxLife(*first))), 0.0f, 1.0f);
        sprites.fillRect(x + 4.0f, y + h - 8.0f, w - 8.0f, 4.0f, 0xFF402820);
        sprites.fillRect(x + 4.0f, y + h - 8.0f, (w - 8.0f) * part, 4.0f, part > 0.5f ? 0xFF40C040 : part > 0.25f ? 0xFFE0C040 : 0xFFE04040);
    }
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
        oc2::ui::Widget* draggedSlider = nullptr;

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
            if (opts->selectOwn)
                for (auto& u : mapView->world.units())
                    if (u.player == mapView->player && u.alive() && !u.building()) {
                        if (!opts->at) mapView->lookAt(float(u.x()), float(u.y()), vulkan.extent());
                        u.selected = true;
                    }
            if (opts->form >= 0) mapView->formSelected(static_cast<oc2::world::World::Formation>(opts->form));
            if (opts->at) mapView->lookAt((*opts->at)[0], (*opts->at)[1], vulkan.extent());
            if (opts->move) {
                auto& us = mapView->world.units();
                for (auto& u : us) u.selected = u.player == (*opts->move)[0] && !u.building();
                long cx = 0, cy = 0, n = 0;
                for (const auto& u : us)
                    if (u.selected) cx += u.x(), cy += u.y(), ++n;
                const MapView::PlayerInput input(mapView->world);
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
            mapView->paused = opts->startPaused;
            if (opts->wound >= 0)
                for (oc2::world::Unit& b : mapView->world.units())
                    if (b.building() && !b.underConstruction) b.life = std::max(1, mapView->world.maxLife(b) * opts->wound / 100);
            if (opts->simulate > 0) {
                const auto steps = std::uint64_t(opts->simulate) * 25;
                for (std::uint64_t k = 0; k < steps; ++k) {
                    if (opts->skipFilm && mapView->script && k % 4 == 0) mapView->script->skipDialog();
                    mapView->stepWorld();
                    mapView->world.soundEvents.clear();
                    mapView->world.shotEvents.clear();
                }
                const auto& w = mapView->world;
                if (const auto& sc = mapView->script) {
                    std::printf("сценарий через %d с: задач %zu, диалог %s, фильм %s, итог %s\n", opts->simulate, sc->tasks().size(),
                                sc->dialog().visible ? "виден" : "нет", sc->filmRunning() ? "идёт" : "нет",
                                sc->ending ? (sc->ending->victory ? "победа" : "поражение") : "нет");
                    for (const auto& t : sc->tasks()) std::printf("  задача %s\n", t.c_str());
                    std::printf("  MovieInt=%g FreeTrigger=%g\n", sc->var("MovieInt"), sc->var("FreeTrigger"));
                }
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
            if (opts->saveCheck) { // сохранение → файл → загрузка: состояние должно совпасть
                auto hash = [](const oc2::world::World& w) {
                    std::uint64_t h = 1469598103934665603ULL;
                    for (const auto& u : w.units())
                        for (const std::int64_t v : {std::int64_t(u.x()), std::int64_t(u.y()), std::int64_t(u.life), std::int64_t(u.alive())})
                            h = (h ^ std::uint64_t(v)) * 1099511628211ULL;
                    for (const auto& r : w.resources)
                        for (const int v : r) h = (h ^ std::uint64_t(v)) * 1099511628211ULL;
                    return h;
                };
                oc2::world::SaveGame g;
                g.name = "selfcheck";
                g.map = mapView->startMap;
                g.steps = mapView->world.stepCount();
                g.journal = mapView->world.journal;
                const bool ok = g.write();
                const auto back = oc2::world::SaveGame::read(oc2::world::SaveGame::pathFor(g.name));
                oc2::world::SaveGame::remove(g.name);
                std::printf("сохранение: файл %s, приказов %zu, шаг %llu\n", ok && back ? "записан и прочитан" : "НЕ РАБОТАЕТ",
                            g.journal.size(), (unsigned long long)g.steps);
                if (back) {
                    MapView copy(vulkan, sprites, opts->dataDir, back->map, screen->images.get());
                    copy.replay(*back);
                    const auto a = hash(mapView->world), b = hash(copy.world);
                    std::printf("состояние: %016llx и %016llx — %s\n", (unsigned long long)a, (unsigned long long)b,
                                a == b ? "совпало" : "РАСХОДИТСЯ");
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

        int loadingStage = 0; // 0 — нет, 1 — рисуется экран загрузки, 2 — загрузка идёт
        std::optional<std::string> pendingStart; // карта, выбранная в меню
        std::optional<oc2::world::SaveGame> pendingLoad; // сохранение, выбранное в меню
        std::unique_ptr<oc2::ui::BigMapScreen> bigScreen; // «Завоевание Европы»
        std::optional<oc2::ui::BigMapScreen::BattleRequest> pendingBig; // бой за сектор, который сейчас грузится
        std::optional<oc2::world::BigGame::Outcome> bigDone; // бой за сектор закончен
        bool bigRequested = opts->bigMap;
        std::uint64_t last = SDL_GetTicksNS();
        std::string shownTitle;
        for (long frame = 0; window.pumpEvents(); ++frame) {
            if (opts->frameLimit >= 0 && frame >= opts->frameLimit) break;
            if (!opts->screenshot.empty() && frame == opts->frameLimit - 1) vulkan.requestScreenshot(opts->screenshot);

            const std::uint64_t now = SDL_GetTicksNS();
            const double dt = double(now - last) / 1e9;
            last = now;
            if (!opts->typeText.empty() && frame == 20) window.typed = opts->typeText;
            if (opts->esc && frame == 1) window.pressedKeys.push_back(SDLK_ESCAPE); // проверка меню игры
            if (opts->f1 && frame == 1) window.pressedKeys.push_back(SDLK_F1); // проверка помощи
            if (mapView && screen && !mapView->ended &&
                std::find(window.pressedKeys.begin(), window.pressedKeys.end(), SDLK_F1) != window.pressedKeys.end()) {
                // Помощь (F1): окно поверх игры, повторное нажатие закрывает.
                oc2::ui::Menus& m = *screen->menus;
                if (!m.empty() && m.currentName() == "F1game") m.close();
                else if (m.empty()) m.open("F1game");
                window.pressedKeys.clear();
            }
            if (!(bigScreen && !mapView) && std::find(window.pressedKeys.begin(), window.pressedKeys.end(), SDLK_ESCAPE) !=
                                                window.pressedKeys.end()) {
                // В игре Esc возвращает в меню (если игра начата из него), в меню закрывает
                // текущее окно, из главного — выход.
                if (mapView && mapView->placing) {
                    mapView->placing = nullptr; // отмена выбора места
                    window.pressedKeys.clear();
                } else if (mapView && screen) {
                    // Меню игры (F10): Esc открывает его, повторное нажатие закрывает.
                    oc2::ui::Menus& m = *screen->menus;
                    if (!m.empty() && (m.currentName() == "F10game" || m.currentName() == "F1game")) m.close();
                    else if (m.empty() || m.currentName() != "EndGameMessage") m.open("F10game");
                    window.pressedKeys.clear();
                    window.clicks.clear();
                } else {
                    if (!screen) break;
                    screen->menus->close();
                    if (screen->menus->empty()) break;
                }
            }
            // Начало игры из меню — вне кадра: загрузка карты занимает секунды и пользуется GPU.
            // Сначала один кадр экрана загрузки (M_Loading), затем сама загрузка.
            if ((pendingStart || pendingLoad) && screen && !mapView && loadingStage == 0 && screen->menus->open("Loading")) {
                loadingStage = 1;
                window.clicks.clear();
            } else if ((pendingStart || pendingLoad) && screen && loadingStage == 1) {
                loadingStage = 2;
            } else if ((pendingStart || pendingLoad) && screen) {
                oc2::ui::Menus& menus = *screen->menus;
                if (loadingStage == 2 && !menus.empty() && menus.currentName() == "Loading") menus.close();
                loadingStage = 0;
                MapView::setup.reset();
                MapView::difficulty = menus.difficulty;
                if (pendingLoad) {
                    // Загрузка: та же карта и таблица игроков, затем повтор приказов до сохранённого шага.
                    const oc2::world::SaveGame& g = *pendingLoad;
                    menus.restoreStart(g);
                    MapView::difficulty = g.difficulty;
                    if (g.setup) {
                        oc2::ui::Menus::Slot* slot = menus.slots.data();
                        for (const auto& s : g.slots) *slot++ = {s[0], s[1], s[2], s[3] != 0};
                        MapView::setup = menus.slots;
                    }
                    pendingStart = g.map;
                    while (!menus.empty() && menus.currentName() == "F10game") menus.close();
                } else if (menus.setupStart())
                    MapView::setup = menus.slots;
                try {
                    mapView = std::make_unique<MapView>(vulkan, sprites, opts->dataDir, *pendingStart, screen->images.get());
                    mapView->centerOnPlayer(0, vulkan.extent());
                    mapView->sounds = sounds.get();
                    if (pendingBig && bigScreen) {
                        const auto& att = pendingBig->attack;
                        mapView->setupBigBattle(att, bigScreen->defendersOf(att.target), pendingBig->playerAttacks,
                                                bigScreen->attackSide(att), sectorObjectives(opts->dataDir, bigScreen->battleSector(), pendingBig->playerAttacks));
                        if (opts->win) mapView->world.killAllExcept(mapView->world.team[0]); // проверка: победа сразу
                        mapView->centerOnPlayer(0, vulkan.extent());
                    }
                    if (pendingLoad) {
                        mapView->replay(*pendingLoad);
                        mapView->centerOnPlayer(mapView->player, vulkan.extent());
                    }
                } catch (const std::exception& ex) {
                    std::fprintf(stderr, "не удалось начать игру: %s\n", ex.what());
                }
                if (pendingBig && !mapView && bigScreen) bigScreen->battleFinished(oc2::world::BigGame::Outcome{false, std::vector<bool>(pendingBig->attack.squads.size(), true), 0}); // карта не открылась — бой не состоялся
                pendingBig.reset();
                pendingStart.reset();
                pendingLoad.reset();
                window.clicks.clear();
                last = SDL_GetTicksNS(); // время загрузки не идёт в симуляцию
                continue;
            }
            if (screen && !bigScreen && !mapView && loadingStage == 0 &&
                (bigRequested || screen->menus->takeGlobalMapRequest())) {
                bigRequested = false;
                if (auto data = oc2::ui::BigMapScreen::loadData(opts->dataDir)) {
                    bigScreen = std::make_unique<oc2::ui::BigMapScreen>(
                        *screen->ctx, data, oc2::core::Settings::defaultPath().parent_path() / "bigmap.sav");
                    if (opts->bigNation >= 0) bigScreen->start(opts->bigNation);
                    if (opts->bigSelect >= 0) bigScreen->select(opts->bigSelect);
                } else {
                    std::fprintf(stderr, "Завоевание Европы: нет данных кампании\n");
                }
                window.clicks.clear();
            }
            if (bigDone && mapView && bigScreen) { // бой за сектор закончен: назад на глобальную карту
                mapView.reset();
                bigScreen->battleFinished(*bigDone);
                bigDone.reset();
                window.clicks.clear();
                last = SDL_GetTicksNS();
            }
            if (viewer) {
                for (std::uint32_t key : window.pressedKeys) viewer->handleKey(key);
                viewer->update(dt);
                if (std::string t = viewer->title(); t != shownTitle) {
                    window.setTitle(t);
                    shownTitle = std::move(t);
                }
            }

            if (screen && screen->menus) { // настройки игрока: громкость и скорость прокрутки
                const oc2::core::Settings& set = screen->menus->settings;
                audio.sfxVolume = float(set.soundVolume) / 100.0f;
                audio.musicVolume = set.musicOn ? float(set.musicVolume) / 100.0f : 0.0f;
                if (mapView) mapView->scrollFactor = 0.5f + float(set.scrollSpeed) / 100.0f;
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
                if (!opts->clickSeq.empty() && frame >= 1) { // курсор идёт к точке на кадр раньше нажатия
                    const auto i = std::min(std::size_t((frame - 1) / 3), opts->clickSeq.size() - 1);
                    mx = opts->clickSeq[i][0];
                    my = opts->clickSeq[i][1];
                }
                oc2::ui::Widget* hovered = menus.current().updateHover(mx, my);
                // Звук наведения — при смене элемента под курсором.
                if (hovered != lastHovered && hovered && sounds) sounds->play(hovered->mouseSound);
                lastHovered = hovered;
                if (opts->click && opts->mouse && frame == 2) window.clicks.push_back({1, mx, my});
                if (!opts->clickSeq.empty() && frame >= 2 && (frame - 2) % 3 == 0 &&
                    std::size_t((frame - 2) / 3) < opts->clickSeq.size())
                    window.clicks.push_back({1, mx, my});

                if (window.wheel != 0.0f) menus.current().scrollListAt(mx, my, window.wheel > 0 ? -1 : 1);
                if (!window.typed.empty() || std::find(window.pressedKeys.begin(), window.pressedKeys.end(), SDLK_BACKSPACE) !=
                                                 window.pressedKeys.end())
                    menus.typeText(window.typed, std::find(window.pressedKeys.begin(), window.pressedKeys.end(),
                                                           SDLK_BACKSPACE) != window.pressedKeys.end());
                for (const auto& click : window.clicks) {
                    if (click.button == 1 && !hovered) {
                        menus.current().closeCombos();
                        menus.current().blurInputs();
                    }
                    if (click.button != 1 || !hovered) continue;
                    if (sounds) sounds->play(hovered->clickSound);
                    if (hovered->kind == oc2::ui::WidgetKind::Slider) draggedSlider = hovered;
                    menus.click(*hovered);
                    break; // меню могло смениться
                }
                if (draggedSlider) { // ползунок следует за курсором, пока кнопка зажата
                    menus.dragSlider(*draggedSlider, mx);
                    for (const auto& r : window.releases)
                        if (r.button == 1) draggedSlider = nullptr;
                }
                if (auto start = menus.takeStartRequest()) pendingStart = std::move(start);
                if (auto load = menus.takeLoadRequest()) pendingLoad = std::move(load);
                if (menus.takeGlobalMapRequest()) bigRequested = true;
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
                // Окно итога или меню игры открыто — карта стоит, нажатия идут окну.
                const bool resultOpen = !menus.empty() && ((mapView->ended && (menus.currentName() == "EndGameMessage" || menus.currentName() == "PostGameStat")) ||
                                                           menus.currentName() == "F10game" || menus.currentName() == "F1game");
                const bool gameMenu = resultOpen && menus.currentName() != "EndGameMessage" && menus.currentName() != "PostGameStat";
                mapView->ctrlHeld = window.ctrl;
                mapView->altHeld = window.alt;
                for (std::uint32_t key : window.pressedKeys) {
                    mapView->handleKey(key, e);
                    if (key == SDLK_SPACE) mapView->skipDialog(); // пропуск диалога
                }
                if (window.wheel != 0.0f && !resultOpen)
                    mapView->handleKey(window.wheel > 0 ? SDLK_PLUS : SDLK_MINUS, e);
                mapView->viewExtent = e;
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
                if (!mapView->ended && !resultOpen && !mapView->paused) {
                    mapView->advance(dt);
                    mapView->bigBattleAi();
                }
                mapView->playSounds(sounds.get(), e);
                // Итог: раз в секунду симуляции проверяется, осталась ли одна команда.
                if (!mapView->ended && mapView->script && mapView->script->ending) {
                    mapView->ended = true;
                    mapView->dragStart.reset();
                    menus.setGameResult(mapView->script->ending->victory, mapView->script->ending->textId);
                    menus.setGameStats(buildGameStats(*screen, *mapView, mapView->script->ending->victory));
                    menus.open("EndGameMessage");
                } else if (!mapView->ended && !mapView->script && mapView->world.stepCount() % 25 == 0)
                    if (auto winner = mapView->world.winningTeam()) {
                        mapView->ended = true;
                        mapView->dragStart.reset();
                        if (mapView->big) {
                            bigDone = mapView->bigOutcome(false);
                        } else {
                            const bool won = *winner == mapView->world.team[std::size_t(mapView->player)];
                            menus.setGameResult(won);
                            menus.setGameStats(buildGameStats(*screen, *mapView, won));
                            menus.open("EndGameMessage");
                        }
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
                drawSelectionInfo(*screen, *mapView, e, sprites);
                drawMissionHud(*screen, *mapView, e, sprites);
                if (auto b = mapView->hoveredButton(window.mouseX, window.mouseY, e); b && !b->leave && (b->desc || b->upgrade))
                    drawPanelHint(*screen, b->desc, b->upgrade, window.mouseX, window.mouseY, sprites);
                if (resultOpen) {
                    const bool open = runMenu();
                    if (auto name = menus.takeSaveRequest()) {
                        oc2::world::SaveGame g;
                        g.name = *name;
                        g.time = std::time(nullptr);
                        g.map = mapView->startMap;
                        g.setup = MapView::setup.has_value();
                        if (MapView::setup)
                            for (std::size_t i = 0; i < g.slots.size(); ++i) {
                                const auto& sl = (*MapView::setup)[i];
                                g.slots[i] = {sl.nation, sl.team, sl.color, sl.active ? 1 : 0};
                            }
                        g.difficulty = MapView::difficulty;
                        g.player = mapView->player;
                        if (const auto m = menus.activeMission()) g.campaign = int(m->first), g.mission = m->second;
                        g.steps = mapView->world.stepCount();
                        g.journal = mapView->world.journal;
                        if (!g.write()) std::fprintf(stderr, "не удалось сохранить «%s»\n", name->c_str());
                    }
                    if (menus.takeBackToMain()) {
                        // Конец игры: к главному меню (без него, при --map, — выход).
                        if (mapView->big && bigScreen) bigScreen->battleFinished(mapView->bigOutcome(true)); // отступление
                        mapView.reset();
                        while (!menus.empty() && menus.currentName() != "Main") menus.close();
                        window.clicks.clear();
                        if (menus.empty()) break;
                    } else if (!open && !gameMenu) {
                        break;
                    }
                }
            } else if (bigScreen && screen && loadingStage == 0 && !pendingStart) {
                screen->fit(e);
                oc2::ui::BigMapScreen::Input in;
                if (opts->mouse) {
                    in.mx = (*opts->mouse)[0], in.my = (*opts->mouse)[1];
                } else if (window.mouseInside) {
                    in.mx = screen->toVirtualX(window.mouseX), in.my = screen->toVirtualY(window.mouseY);
                }
                if (!opts->clickSeq.empty() && frame >= 1) { // проверка: нажатия из --clicks, как в меню
                    const auto i = std::min(std::size_t((frame - 1) / 3), opts->clickSeq.size() - 1);
                    in.mx = opts->clickSeq[i][0], in.my = opts->clickSeq[i][1];
                    if (frame >= 2 && (frame - 2) % 3 == 0 && std::size_t((frame - 2) / 3) < opts->clickSeq.size()) in.click = true;
                }
                for (const auto& c : window.clicks)
                    if (c.button == 1) in.click = true, in.mx = screen->toVirtualX(c.x), in.my = screen->toVirtualY(c.y);
                auto pressed = [&](std::uint32_t key) {
                    return std::find(window.pressedKeys.begin(), window.pressedKeys.end(), key) != window.pressedKeys.end();
                };
                in.esc = pressed(SDLK_ESCAPE);
                in.enter = pressed(SDLK_RETURN);
                bigScreen->frame(screen->view, in);
                if (auto b = bigScreen->takeBattle()) {
                    pendingBig = *b;
                    pendingStart = bigScreen->battleMap();
                }
                if (bigScreen->done()) bigScreen.reset();
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
