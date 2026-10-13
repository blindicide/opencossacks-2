#pragma once

#include <cstdint>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "assets/image.hpp"
#include "render/vulkan_context.hpp"
#include "world/terrain_mesh.hpp"

namespace oc2::render {

// Камера карты: левый верхний угол экрана в пикселях проекции оригинала
// (X = x, Y = y / 2 − z) и масштаб.
struct MapCamera {
    float x = 0.0f, y = 0.0f;
    float zoom = 1.0f;
};

// Вывод рельефа: массив текстур-фактур и один неизменный буфер вершин.
class TerrainRenderer {
public:
    explicit TerrainRenderer(VulkanContext& vulkan);
    ~TerrainRenderer();
    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    // Фактуры — квадратные изображения одного размера (у игры 256×256),
    // номер в векторе = номер фактуры в карте.
    void setFactures(const std::vector<assets::Image>& factures);
    void setMesh(const std::vector<world::TerrainVertex>& vertices);

    // Записывает вывод рельефа в буфер команд кадра (до спрайтов).
    void draw(VkCommandBuffer cmd, const MapCamera& camera);

private:
    void createPipeline();
    void destroyTextures();

    VulkanContext& vulkan_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkRenderPass pipelinePass_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation imageMemory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;

    VkBuffer vertices_ = VK_NULL_HANDLE;
    VmaAllocation verticesMemory_ = VK_NULL_HANDLE;
    std::uint32_t vertexCount_ = 0;
};

} // namespace oc2::render
