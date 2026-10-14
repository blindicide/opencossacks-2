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
    if (window_) SDL_StartTextInput(window_);
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
    pressedKeys.clear();
    typed.clear();
    clicks.clear();
    releases.clear();
    wheel = 0.0f;
    // Координаты событий SDL — в точках окна, рисуем же в пикселях.
    const float density = SDL_GetWindowPixelDensity(window_) > 0.0f ? SDL_GetWindowPixelDensity(window_) : 1.0f;
    {
        const SDL_Keymod mods = SDL_GetModState();
        ctrl = (mods & SDL_KMOD_CTRL) != 0;
        shift = (mods & SDL_KMOD_SHIFT) != 0;
        alt = (mods & SDL_KMOD_ALT) != 0;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            mouseInside = true;
            mouseX = e.motion.x * density;
            mouseY = e.motion.y * density;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            wheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            mouseInside = true;
            mouseX = e.button.x * density;
            mouseY = e.button.y * density;
            clicks.push_back({int(e.button.button), mouseX, mouseY});
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            mouseX = e.button.x * density;
            mouseY = e.button.y * density;
            releases.push_back({int(e.button.button), mouseX, mouseY});
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            mouseInside = false;
            break;
        case SDL_EVENT_TEXT_INPUT:
            typed += e.text.text;
            break;
        case SDL_EVENT_KEY_DOWN:
            pressedKeys.push_back(e.key.key);
            ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
            shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            alt = (e.key.mod & SDL_KMOD_ALT) != 0;
            break;
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

void Window::setTitle(const std::string& title) { SDL_SetWindowTitle(window_, title.c_str()); }

void Window::setFullscreen(bool on) { SDL_SetWindowFullscreen(window_, on); }

bool Window::fullscreen() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0; }

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
