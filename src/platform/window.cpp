#include "platform/window.hpp"

#include <stdexcept>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace oc2::platform {

Window::Window(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(std::string("не удалось инициализировать SDL: ") + SDL_GetError());
    window_ = SDL_CreateWindow(title.c_str(), width, height,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        std::string err = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("не удалось создать окно: " + err);
    }
}

Window::~Window() {
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Window::pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return false;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            resized = true;
            break;
        default:
            break;
        }
    }
    return true;
}

void Window::drawableSize(std::uint32_t& width, std::uint32_t& height) const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    width = static_cast<std::uint32_t>(w);
    height = static_cast<std::uint32_t>(h);
}

bool Window::minimized() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}

std::vector<const char*> Window::requiredInstanceExtensions() const {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names)
        throw std::runtime_error(std::string("SDL не поддерживает Vulkan: ") + SDL_GetError());
    return {names, names + count};
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface))
        throw std::runtime_error(std::string("не удалось создать Vulkan-поверхность: ") + SDL_GetError());
    return surface;
}

} // namespace oc2::platform
