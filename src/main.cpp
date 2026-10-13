#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/version.hpp"
#include "platform/window.hpp"
#include "render/vulkan_context.hpp"

namespace {

// Тёмно-синий фон, пока рисовать больше нечего.
constexpr std::array<float, 4> kClearColor{0.04f, 0.06f, 0.12f, 1.0f};

void showError(const std::string& message) {
    std::fprintf(stderr, "Ошибка: %s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, oc2::kProductName, message.c_str(), nullptr);
}

} // namespace

int main(int argc, char** argv) {
    long frameLimit = -1; // --frames N: нарисовать N кадров и выйти (для проверок)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("%s, версия %s\n", oc2::kProductName, oc2::kVersion);
            return 0;
        }
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frameLimit = std::strtol(argv[++i], nullptr, 10);
        }
    }

    try {
        oc2::platform::Window window(std::string(oc2::kProductName) + " " + oc2::kVersion, 1280, 720);
        oc2::render::VulkanContext vulkan(window);
        std::printf("Видеоустройство: %s\n", vulkan.deviceName().c_str());

        for (long frame = 0; window.pumpEvents(); ++frame) {
            if (frameLimit >= 0 && frame >= frameLimit) break;
            vulkan.drawFrame(kClearColor);
        }
    } catch (const std::exception& e) {
        showError(e.what());
        return 1;
    }
    return 0;
}
