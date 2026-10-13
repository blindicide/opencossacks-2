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

    void setTitle(const std::string& title);

    // Взводится при изменении размера; сбрасывается вызывающим кодом.
    bool resized = false;
    // Клавиши (SDL_Keycode), нажатые с прошлого вызова pumpEvents().
    std::vector<std::uint32_t> pressedKeys;

    // Положение мыши в пикселях области рисования; false, пока мышь не входила в окно.
    bool mouseInside = false;
    float mouseX = 0.0f, mouseY = 0.0f;
    // Нажатия кнопок мыши с прошлого вызова pumpEvents() (1 — левая, 3 — правая).
    struct Click {
        int button;
        float x, y;
    };
    std::vector<Click> clicks;

private:
    SDL_Window* window_ = nullptr;
};

} // namespace oc2::platform
