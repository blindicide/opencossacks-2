#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

namespace oc2::platform { class Window; }

namespace oc2::render {

// Минимальный Vulkan-контекст: экземпляр, устройство, цепочка обмена
// и цикл кадров, очищающий экран заданным цветом.
class VulkanContext {
public:
    explicit VulkanContext(platform::Window& window);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Рисует один кадр; при необходимости пересоздаёт цепочку обмена.
    void drawFrame(const std::array<float, 4>& clearColor);

    // Название выбранного GPU (для журнала).
    const std::string& deviceName() const { return deviceName_; }

private:
    static constexpr std::uint32_t kFramesInFlight = 2;

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
    std::string deviceName_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    // Семафор «рендер готов» на каждое изображение цепочки, а не на кадр:
    // так он гарантированно свободен к моменту повторного использования.
    std::vector<VkSemaphore> renderFinished_;

    std::array<Frame, kFramesInFlight> frames_{};
    std::uint32_t frameIndex_ = 0;
};

} // namespace oc2::render
