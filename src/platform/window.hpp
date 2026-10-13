#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <volk.h>

struct SDL_Window;

namespace oc2::platform {

// Окно игры на SDL3 с поддержкой Vulkan-поверхности.
class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Обрабатывает события ОС; возвращает false, когда окно закрыто.
    bool pumpEvents();

    // Размер области рисования в пикселях (с учётом масштабирования экрана).
    void drawableSize(std::uint32_t& width, std::uint32_t& height) const;
    bool minimized() const;

    // Расширения экземпляра Vulkan, которые нужны SDL для поверхности.
    std::vector<const char*> requiredInstanceExtensions() const;
    VkSurfaceKHR createSurface(VkInstance instance) const;

    // Взводится при изменении размера; сбрасывается вызывающим кодом.
    bool resized = false;

private:
    SDL_Window* window_ = nullptr;
};

} // namespace oc2::platform
