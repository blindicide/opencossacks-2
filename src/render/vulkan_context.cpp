#include "render/vulkan_context.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "assets/image.hpp"
#include "assets/png.hpp"
#include "core/version.hpp"
#include "platform/window.hpp"

namespace oc2::render {

namespace {

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + ": ошибка Vulkan " + std::to_string(r));
}

#ifndef NDEBUG
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool layerAvailable(const char* name) {
    std::uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    return std::any_of(layers.begin(), layers.end(),
                       [&](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
}
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const char* level = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "ОШИБКА" : "предупреждение";
    std::fprintf(stderr, "[Vulkan, %s] %s\n", level, data->pMessage);
    return VK_FALSE;
}

} // namespace

VulkanContext::VulkanContext(platform::Window& window) : window_(window) {
    check(volkInitialize(), "не найден загрузчик Vulkan (установлен ли драйвер?)");
    createInstance();
    surface_ = window_.createSurface(instance_);
    pickPhysicalDevice();
    createDevice();
    createAllocator();
    createSwapchain();
    createFrames();
}

VulkanContext::~VulkanContext() {
    if (device_) vkDeviceWaitIdle(device_);
    if (screenshotBuffer_) vmaDestroyBuffer(allocator_, screenshotBuffer_, screenshotMemory_);
    vkDestroyCommandPool(device_, uploadPool_, nullptr);
    for (Frame& f : frames_) {
        vkDestroyFence(device_, f.inFlight, nullptr);
        vkDestroySemaphore(device_, f.imageAvailable, nullptr);
        vkDestroyCommandPool(device_, f.pool, nullptr);
    }
    destroySwapchain();
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

void VulkanContext::createInstance() {
    std::vector<const char*> extensions = window_.requiredInstanceExtensions();
    std::vector<const char*> layers;
#ifndef NDEBUG
    validation_ = layerAvailable(kValidationLayer);
    if (validation_) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
#endif

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = kProductName;
    app.pEngineName = kProductName;
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    check(vkCreateInstance(&ci, nullptr, &instance_), "не удалось создать экземпляр Vulkan");
    volkLoadInstance(instance_);

    if (validation_) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = debugCallback;
        check(vkCreateDebugUtilsMessengerEXT(instance_, &mi, nullptr, &messenger_),
              "не удалось включить отладку Vulkan");
    }
}

void VulkanContext::pickPhysicalDevice() {
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> devices(n);
    vkEnumeratePhysicalDevices(instance_, &n, devices.data());

    // Подходит устройство с Vulkan 1.2+, цепочкой обмена и очередью графики с выводом на экран.
    // Дискретная видеокарта предпочтительнее встроенной, любая — программной.
    int bestScore = -1;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.apiVersion < VK_API_VERSION_1_2) continue;

        std::uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, exts.data());
        bool hasSwapchain = std::any_of(exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
            return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        });
        if (!hasSwapchain) continue;

        std::uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, queues.data());
        for (std::uint32_t q = 0; q < qCount; ++q) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, q, surface_, &present);
            if (!(queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : 1;
            if (score > bestScore) {
                bestScore = score;
                physical_ = d;
                queueFamily_ = q;
                deviceName_ = props.deviceName;
            }
            break;
        }
    }
    if (!physical_)
        throw std::runtime_error("не найдено видеоустройство с поддержкой Vulkan 1.2");
}

void VulkanContext::createDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = queueFamily_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;

    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = &ext;
    check(vkCreateDevice(physical_, &ci, nullptr, &device_), "не удалось создать логическое устройство");
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
}

void VulkanContext::createAllocator() {
    // Функции Vulkan загружены volk; VMA получает их через два указателя.
    VmaVulkanFunctions fn{};
    fn.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    fn.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo ci{};
    ci.vulkanApiVersion = VK_API_VERSION_1_2;
    ci.physicalDevice = physical_;
    ci.device = device_;
    ci.instance = instance_;
    ci.pVulkanFunctions = &fn;
    check(vmaCreateAllocator(&ci, &allocator_), "не удалось создать распределитель памяти");
}

void VulkanContext::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps), "нет параметров поверхности");

    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, formats.data());
    VkSurfaceFormatKHR chosen = formats.at(0);
    for (const VkSurfaceFormatKHR& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }

    std::uint32_t w = 0, h = 0;
    window_.drawableSize(w, h);
    if (caps.currentExtent.width != UINT32_MAX) {
        extent_ = caps.currentExtent;
    } else {
        extent_.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    canCopyImages_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (canCopyImages_) ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // вертикальная синхронизация, поддерживается везде
    ci.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_), "не удалось создать цепочку обмена");

    if (format_ != chosen.format) {
        if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
        format_ = chosen.format;

        VkAttachmentDescription color{};
        color.format = format_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;
        check(vkCreateRenderPass(device_, &rp, nullptr, &renderPass_), "не удалось создать проход рендера");
    }

    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, images_.data());

    views_.resize(n);
    framebuffers_.resize(n);
    renderFinished_.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device_, &vi, nullptr, &views_[i]), "не удалось создать вид изображения");

        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = renderPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &views_[i];
        fi.width = extent_.width;
        fi.height = extent_.height;
        fi.layers = 1;
        check(vkCreateFramebuffer(device_, &fi, nullptr, &framebuffers_[i]), "не удалось создать кадровый буфер");

        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device_, &si, nullptr, &renderFinished_[i]), "не удалось создать семафор");
    }
}

void VulkanContext::destroySwapchain() {
    for (VkSemaphore s : renderFinished_) vkDestroySemaphore(device_, s, nullptr);
    for (VkFramebuffer f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    for (VkImageView v : views_) vkDestroyImageView(device_, v, nullptr);
    renderFinished_.clear();
    framebuffers_.clear();
    views_.clear();
    images_.clear();
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanContext::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    createSwapchain();
}

void VulkanContext::createFrames() {
    VkCommandPoolCreateInfo upi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    upi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    upi.queueFamilyIndex = queueFamily_;
    check(vkCreateCommandPool(device_, &upi, nullptr, &uploadPool_), "не удалось создать пул команд");

    for (Frame& f : frames_) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = queueFamily_;
        check(vkCreateCommandPool(device_, &pi, nullptr, &f.pool), "не удалось создать пул команд");

        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &ai, &f.cmd), "не удалось выделить буфер команд");

        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device_, &si, nullptr, &f.imageAvailable), "не удалось создать семафор");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device_, &fi, nullptr, &f.inFlight), "не удалось создать барьер");
    }
}

void VulkanContext::submitNow(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = uploadPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_, &ai, &cmd), "не удалось выделить буфер команд");

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd);
    check(vkEndCommandBuffer(cmd), "не удалось записать буфер команд");

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    check(vkCreateFence(device_, &fi, nullptr, &fence), "не удалось создать барьер");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VkResult r = vkQueueSubmit(queue_, 1, &si, fence);
    if (r == VK_SUCCESS) r = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, uploadPool_, 1, &cmd);
    check(r, "не удалось выполнить команды загрузки");
}

VkCommandBuffer VulkanContext::beginFrame(const std::array<float, 4>& clearColor) {
    if (window_.minimized()) return VK_NULL_HANDLE; // поверхность нулевого размера рисовать нельзя

    Frame& f = frames_[frameIndex_];
    vkWaitForFences(device_, 1, &f.inFlight, VK_TRUE, UINT64_MAX);

    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE,
                                       &currentImage_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return VK_NULL_HANDLE;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) check(r, "не удалось получить изображение");
    vkResetFences(device_, 1, &f.inFlight);

    vkResetCommandBuffer(f.cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);

    VkClearValue clear{};
    std::memcpy(clear.color.float32, clearColor.data(), sizeof(float) * 4);
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = renderPass_;
    rb.framebuffer = framebuffers_[currentImage_];
    rb.renderArea = {{0, 0}, extent_};
    rb.clearValueCount = 1;
    rb.pClearValues = &clear;
    vkCmdBeginRenderPass(f.cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    return f.cmd;
}

void VulkanContext::endFrame() {
    Frame& f = frames_[frameIndex_];
    vkCmdEndRenderPass(f.cmd);
    const bool screenshot = !screenshotPath_.empty() && canCopyImages_;
    if (screenshot) recordScreenshotCopy(f.cmd);
    check(vkEndCommandBuffer(f.cmd), "не удалось записать буфер команд");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.imageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished_[currentImage_];
    check(vkQueueSubmit(queue_, 1, &si, f.inFlight), "не удалось отправить команды");

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished_[currentImage_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &currentImage_;
    VkResult r = vkQueuePresentKHR(queue_, &pi);

    if (screenshot) {
        vkWaitForFences(device_, 1, &f.inFlight, VK_TRUE, UINT64_MAX);
        saveScreenshot();
    } else if (!screenshotPath_.empty()) {
        std::fprintf(stderr, "Снимок экрана недоступен: цепочка обмена не поддерживает копирование\n");
        screenshotPath_.clear();
    }

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || window_.resized) {
        window_.resized = false;
        recreateSwapchain();
    } else if (r != VK_SUCCESS) {
        check(r, "не удалось вывести кадр");
    }

    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

void VulkanContext::recordScreenshotCopy(VkCommandBuffer cmd) {
    const VkDeviceSize size = VkDeviceSize(extent_.width) * extent_.height * 4;
    if (screenshotBuffer_ && (screenshotExtent_.width != extent_.width || screenshotExtent_.height != extent_.height)) {
        vmaDestroyBuffer(allocator_, screenshotBuffer_, screenshotMemory_);
        screenshotBuffer_ = VK_NULL_HANDLE;
    }
    if (!screenshotBuffer_) {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        check(vmaCreateBuffer(allocator_, &bci, &aci, &screenshotBuffer_, &screenshotMemory_, nullptr),
              "не удалось создать буфер снимка");
        screenshotExtent_ = extent_;
    }

    // Изображение после прохода — в PRESENT_SRC; на время копирования переводим в TRANSFER_SRC.
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = images_[currentImage_];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &b);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(cmd, images_[currentImage_], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, screenshotBuffer_, 1,
                           &region);

    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = 0;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &b);

    VkBufferMemoryBarrier hb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hb.buffer = screenshotBuffer_;
    hb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hb, 0,
                         nullptr);
}

void VulkanContext::saveScreenshot() {
    assets::Image img(screenshotExtent_.width, screenshotExtent_.height);
    void* mapped = nullptr;
    check(vmaMapMemory(allocator_, screenshotMemory_, &mapped), "не удалось прочитать снимок");
    vmaInvalidateAllocation(allocator_, screenshotMemory_, 0, VK_WHOLE_SIZE);
    std::memcpy(img.rgba.data(), mapped, img.rgba.size());
    vmaUnmapMemory(allocator_, screenshotMemory_);

    const bool bgra = format_ == VK_FORMAT_B8G8R8A8_SRGB || format_ == VK_FORMAT_B8G8R8A8_UNORM;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        if (bgra) std::swap(img.rgba[i], img.rgba[i + 2]);
        img.rgba[i + 3] = 255; // окно непрозрачно
    }
    assets::writePng(screenshotPath_, img);
    std::printf("Снимок экрана: %s\n", screenshotPath_.string().c_str());
    screenshotPath_.clear();
}

} // namespace oc2::render
