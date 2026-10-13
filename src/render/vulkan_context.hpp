#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

namespace oc2::platform { class Window; }

namespace oc2::render {

// Vulkan-контекст: экземпляр, устройство, распределитель памяти, цепочка обмена
// и цикл кадров. Кадр рисуется между beginFrame() и endFrame().
class VulkanContext {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;

    explicit VulkanContext(platform::Window& window);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Начинает кадр и проход рендера с очисткой экрана. Возвращает буфер команд
    // или VK_NULL_HANDLE, если кадр пропускается (окно свёрнуто, цепочка пересоздаётся);
    // тогда endFrame() вызывать не нужно.
    VkCommandBuffer beginFrame(const std::array<float, 4>& clearColor);
    // Завершает проход, отправляет команды и выводит кадр.
    void endFrame();

    // Сохранить следующий выведенный кадр в PNG (для проверок и CI).
    void requestScreenshot(std::filesystem::path path) { screenshotPath_ = std::move(path); }

    // Записывает команды, выполняет их и ждёт завершения (загрузка ресурсов).
    void submitNow(const std::function<void(VkCommandBuffer)>& record);

    VkDevice device() const { return device_; }
    VmaAllocator allocator() const { return allocator_; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkExtent2D extent() const { return extent_; }
    // Номер слота кадра (0…kFramesInFlight−1): ресурсы слота свободны после beginFrame().
    std::uint32_t frameSlot() const { return frameIndex_; }

    // Название выбранного GPU (для журнала).
    const std::string& deviceName() const { return deviceName_; }

private:
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    void createInstance();
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();
    void createSwapchain();
    void destroySwapchain();
    void recreateSwapchain();
    void createFrames();
    void recordScreenshotCopy(VkCommandBuffer cmd);
    void saveScreenshot();

    platform::Window& window_;
    bool validation_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool uploadPool_ = VK_NULL_HANDLE;
    std::string deviceName_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    bool canCopyImages_ = false; // цепочка поддерживает копирование изображений (для снимков)
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    // Семафор «рендер готов» на каждое изображение цепочки, а не на кадр:
    // так он гарантированно свободен к моменту повторного использования.
    std::vector<VkSemaphore> renderFinished_;

    std::array<Frame, kFramesInFlight> frames_{};
    std::uint32_t frameIndex_ = 0;
    std::uint32_t currentImage_ = 0;

    std::filesystem::path screenshotPath_;
    VkBuffer screenshotBuffer_ = VK_NULL_HANDLE;
    VmaAllocation screenshotMemory_ = VK_NULL_HANDLE;
    VkExtent2D screenshotExtent_{};
};

} // namespace oc2::render
