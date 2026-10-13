#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "assets/image.hpp"
#include "core/version.hpp"
#include "platform/window.hpp"
#include "render/sprite_renderer.hpp"
#include "render/vulkan_context.hpp"

namespace {

// Тёмно-синий фон.
constexpr std::array<float, 4> kClearColor{0.04f, 0.06f, 0.12f, 1.0f};

void showError(const std::string& message) {
    std::fprintf(stderr, "Ошибка: %s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, oc2::kProductName, message.c_str(), nullptr);
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

void drawQuad(oc2::render::SpriteRenderer& r, oc2::render::TextureId tex, float x, float y, float w, float h,
              std::array<std::uint8_t, 4> color) {
    const std::array<oc2::render::SpriteVertex, 4> v{{
        {x, y, 0.0f, 0.0f, color},
        {x + w, y, 1.0f, 0.0f, color},
        {x + w, y + h, 1.0f, 1.0f, color},
        {x, y + h, 0.0f, 1.0f, color},
    }};
    const std::array<std::uint16_t, 6> idx{0, 1, 2, 0, 2, 3};
    r.drawTriangles(tex, v, idx);
}

} // namespace

int main(int argc, char** argv) {
    long frameLimit = -1;           // --frames N: нарисовать N кадров и выйти (для проверок)
    std::filesystem::path screenshot; // --screenshot файл.png: сохранить последний кадр
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("%s, версия %s\n", oc2::kProductName, oc2::kVersion);
            return 0;
        }
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frameLimit = std::strtol(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        }
    }
    if (!screenshot.empty() && frameLimit < 0) frameLimit = 1;

    try {
        oc2::platform::Window window(std::string(oc2::kProductName) + " " + oc2::kVersion, 1280, 720);
        oc2::render::VulkanContext vulkan(window);
        std::printf("Видеоустройство: %s\n", vulkan.deviceName().c_str());
        oc2::render::SpriteRenderer sprites(vulkan);
        const oc2::render::TextureId pattern = sprites.createTexture(testPattern());

        for (long frame = 0; window.pumpEvents(); ++frame) {
            if (frameLimit >= 0 && frame >= frameLimit) break;
            if (!screenshot.empty() && frame == frameLimit - 1) vulkan.requestScreenshot(screenshot);

            VkCommandBuffer cmd = vulkan.beginFrame(kClearColor);
            if (!cmd) continue;
            const VkExtent2D e = vulkan.extent();
            const float size = 256.0f;
            const float x = (float(e.width) - size) / 2.0f, y = (float(e.height) - size) / 2.0f;
            drawQuad(sprites, pattern, x, y, size, size, {128, 128, 128, 255});
            drawQuad(sprites, pattern, x + size / 2, y + size / 2, size / 2, size / 2, {128, 128, 255, 128});
            sprites.flush(cmd);
            vulkan.endFrame();
        }
    } catch (const std::exception& e) {
        showError(e.what());
        return 1;
    }
    return 0;
}
