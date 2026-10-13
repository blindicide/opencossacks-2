#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "assets/image.hpp"
#include "render/vulkan_context.hpp"

namespace oc2::render {

// Вершина спрайта: положение в пикселях экрана, координаты текстуры (0…1),
// цвет RGBA8 (0x80 в канале — без изменения текстуры).
struct SpriteVertex {
    float x, y;
    float u, v;
    std::array<std::uint8_t, 4> color;
};

using TextureId = std::uint32_t;

// Вывод текстурированных треугольников с альфа-смешиванием. За кадр накапливает
// вызовы drawTriangles() и в flush() записывает их в буфер команд кадра.
class SpriteRenderer {
public:
    explicit SpriteRenderer(VulkanContext& vulkan);
    ~SpriteRenderer();
    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    // Загружает изображение в видеопамять (sRGB, фильтрация «ближайший пиксель»).
    TextureId createTexture(const assets::Image& image);
    VkExtent2D textureSize(TextureId id) const { return textures_.at(id).size; }

    // Ставит в очередь треугольники; индексы — номера в vertices.
    void drawTriangles(TextureId texture, std::span<const SpriteVertex> vertices,
                       std::span<const std::uint16_t> indices);

    // Прямоугольник с текстурой целиком; color — цвет вершин (0x80 — без изменений).
    void drawQuad(TextureId texture, float x, float y, float w, float h,
                  std::array<std::uint8_t, 4> color = {128, 128, 128, 255});

    // Область отсечения для следующих вызовов drawTriangles() (пиксели экрана).
    // Сбрасывается в flush().
    void setClip(int x1, int y1, int x2, int y2);
    void resetClip() { clip_.reset(); }

    // Записывает накопленное в буфер команд текущего кадра и очищает очередь.
    void flush(VkCommandBuffer cmd);

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkExtent2D size{};
    };
    struct Batch {
        TextureId texture;
        std::uint32_t firstIndex, indexCount;
        std::optional<VkRect2D> clip;
    };
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };

    void createPipeline();
    VkDescriptorSet allocateSet();
    void ensureBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& b);

    VulkanContext& vulkan_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkRenderPass pipelinePass_ = VK_NULL_HANDLE; // проход, под который собран конвейер
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> pools_;
    std::uint32_t setsLeftInPool_ = 0;

    std::vector<Texture> textures_;
    std::optional<VkRect2D> clip_;
    std::vector<SpriteVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<Batch> batches_;
    std::array<Buffer, VulkanContext::kFramesInFlight> vertexBuffers_{};
    std::array<Buffer, VulkanContext::kFramesInFlight> indexBuffers_{};
};

} // namespace oc2::render
