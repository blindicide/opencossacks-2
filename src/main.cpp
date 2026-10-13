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
#include "platform/window.hpp"
#include "render/sprite_renderer.hpp"
#include "render/sprite_sheet.hpp"
#include "render/vulkan_context.hpp"
#include "assets/text_table.hpp"
#include "assets/xml_tree.hpp"
#include "render/gp_images.hpp"
#include "ui/dialogs.hpp"
#include "ui/fonts.hpp"
#include "ui/menus.hpp"
#include "ui/rich_text.hpp"

namespace fs = std::filesystem;

namespace {

// Тёмно-синий фон.
constexpr std::array<float, 4> kClearColor{0.04f, 0.06f, 0.12f, 1.0f};
constexpr double kAnimationFps = 15.0;

const char* kHelp =
    "Ключи:\n"
    "  --data <папка>        папка с данными игры (например, .../c2_battle_for_europe/app)\n"
    "  --sprite <файл.g2d>   показать пакет спрайтов (путь относительно --data, регистр не важен)\n"
    "  --frame <N>           начальный кадр\n"
    "  --frames <N>          нарисовать N кадров и выйти (для проверок)\n"
    "  --screenshot <файл>   сохранить последний кадр в PNG (по умолчанию после одного кадра)\n"
    "  --menu <имя>          открыть меню из Dialogs/MainMenu.xml (Main, Single, Options, …)\n"
    "  --mouse <x>,<y>       считать, что курсор стоит в этой точке экрана 1024×768 (для проверок)\n"
    "  --version             версия\n"
    "\n"
    "С --data без --sprite показывается главное меню.\n"
    "Просмотр спрайтов: ←/→ — кадр, ↑/↓ — на 10 кадров, Home — первый кадр,\n"
    "пробел — анимация, +/− — масштаб, Esc — выход.\n";

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
    std::string menu = "Main";
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
        } else if (a == "--menu" && hasValue) {
            o.menu = argv[++i];
        } else if (a == "--mouse" && hasValue) {
            float mx = 0, my = 0;
            if (std::sscanf(argv[++i], "%f,%f", &mx, &my) != 2)
                throw std::runtime_error("--mouse ждёт координаты вида 500,470");
            o.mouse = std::array<float, 2>{mx, my};
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
        if (!menus->open(firstMenu)) throw std::runtime_error("нет меню «" + firstMenu + "»");
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

} // namespace

int main(int argc, char** argv) {
    try {
        std::optional<Options> opts = parseArgs(argc, argv);
        if (!opts) return 0;

        oc2::platform::Window window(std::string(oc2::kProductName) + " " + oc2::kVersion, 1280, 720);
        oc2::render::VulkanContext vulkan(window);
        std::printf("Видеоустройство: %s\n", vulkan.deviceName().c_str());
        oc2::render::SpriteRenderer sprites(vulkan);

        std::optional<Viewer> viewer;
        std::unique_ptr<Screen> screen;
        oc2::render::TextureId pattern = 0;
        if (opts->sprite.empty() && !opts->dataDir.empty()) {
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
                // В меню Esc закрывает текущее окно, из главного — выход.
                if (!screen) break;
                screen->menus->close();
                if (screen->menus->empty()) break;
            }
            if (viewer) {
                for (std::uint32_t key : window.pressedKeys) viewer->handleKey(key);
                viewer->update(dt);
                if (std::string t = viewer->title(); t != shownTitle) {
                    window.setTitle(t);
                    shownTitle = std::move(t);
                }
            }

            VkCommandBuffer cmd = vulkan.beginFrame(kClearColor);
            if (!cmd) continue;
            const VkExtent2D e = vulkan.extent();
            if (screen) {
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
                for (const auto& click : window.clicks) {
                    if (click.button != 1 || !hovered) continue;
                    menus.click(*hovered);
                    break; // меню могло смениться
                }
                if (menus.empty()) break;
                menus.current().draw(screen->view);
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
    } catch (const std::exception& e) {
        showError(e.what());
        return 1;
    }
    return 0;
}
