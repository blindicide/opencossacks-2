#include "render/terrain_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

#include "render/shaders/terrain_frag.h"
#include "render/shaders/terrain_vert.h"

namespace oc2::render {

namespace {

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + ": ошибка Vulkan " + std::to_string(r));
}

VkShaderModule createShader(VkDevice device, const std::uint32_t* code, std::size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &ci, nullptr, &m), "не удалось создать шейдер");
    return m;
}

// Следующий уровень мип-карты: среднее четырёх пикселей.
assets::Image halve(const assets::Image& src) {
    assets::Image dst(std::max(1u, src.width / 2), std::max(1u, src.height / 2));
    for (std::uint32_t y = 0; y < dst.height; ++y)
        for (std::uint32_t x = 0; x < dst.width; ++x)
            for (int c = 0; c < 4; ++c) {
                const auto at = [&](std::uint32_t sx, std::uint32_t sy) {
                    return int(src.rgba[(std::size_t(std::min(sy, src.height - 1)) * src.width +
                                         std::min(sx, src.width - 1)) * 4 + std::size_t(c)]);
                };
                const int sum = at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1);
                dst.rgba[(std::size_t(y) * dst.width + x) * 4 + std::size_t(c)] = std::uint8_t((sum + 2) / 4);
            }
    return dst;
}

struct Staging {
    VmaAllocator allocator;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation memory = VK_NULL_HANDLE;
    void* mapped = nullptr;

    Staging(VmaAllocator a, VkDeviceSize size) : allocator(a) {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        check(vmaCreateBuffer(allocator, &bci, &ai, &buffer, &memory, &info), "не удалось создать промежуточный буфер");
        mapped = info.pMappedData;
    }
    ~Staging() { vmaDestroyBuffer(allocator, buffer, memory); }
    Staging(const Staging&) = delete;
    Staging& operator=(const Staging&) = delete;
};

} // namespace

TerrainRenderer::TerrainRenderer(VulkanContext& vulkan) : vulkan_(vulkan) {
    VkDevice device = vulkan_.device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device, &li, nullptr, &setLayout_), "не удалось создать раскладку дескрипторов");

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 6};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout_), "не удалось создать раскладку конвейера");

    // В оригинале (Facture4) — «ближайший пиксель»; фактуры уменьшены в 4 раза и
    // без сглаживания рябят, поэтому здесь линейная фильтрация с мип-картами.
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = VK_LOD_CLAMP_NONE;
    check(vkCreateSampler(device, &si, nullptr, &sampler_), "не удалось создать сэмплер");

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device, &pi, nullptr, &pool_), "не удалось создать пул дескрипторов");
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &setLayout_;
    check(vkAllocateDescriptorSets(device, &ai, &set_), "не удалось выделить дескриптор");

    createPipeline();
}

TerrainRenderer::~TerrainRenderer() {
    VkDevice device = vulkan_.device();
    vkDeviceWaitIdle(device);
    destroyTextures();
    if (vertices_) vmaDestroyBuffer(vulkan_.allocator(), vertices_, verticesMemory_);
    vkDestroyDescriptorPool(device, pool_, nullptr);
    vkDestroySampler(device, sampler_, nullptr);
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
}

void TerrainRenderer::destroyTextures() {
    if (view_) vkDestroyImageView(vulkan_.device(), view_, nullptr);
    if (image_) vmaDestroyImage(vulkan_.allocator(), image_, imageMemory_);
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
}

void TerrainRenderer::createPipeline() {
    VkDevice device = vulkan_.device();
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    pipelinePass_ = vulkan_.renderPass();

    VkShaderModule vert = createShader(device, kTerrainVert, sizeof(kTerrainVert));
    VkShaderModule frag = createShader(device, kTerrainFrag, sizeof(kTerrainFrag));
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    using V = world::TerrainVertex;
    VkVertexInputBindingDescription binding{0, sizeof(V), VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription, 5> attrs{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(V, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(V, u)},
        {2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(V, factures1)},
        {3, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(V, factures2)},
        {4, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(V, light)},
    }};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // склоны, обращённые от зрителя, перекрываются ближними рядами
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    std::array<VkDynamicState, 2> dynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = static_cast<std::uint32_t>(dynamic.size());
    ds.pDynamicStates = dynamic.data();

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = static_cast<std::uint32_t>(stages.size());
    ci.pStages = stages.data();
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &ds;
    ci.layout = pipelineLayout_;
    ci.renderPass = pipelinePass_;
    VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    check(r, "не удалось создать конвейер рельефа");
}

void TerrainRenderer::setFactures(const std::vector<assets::Image>& factures, int waterLayer) {
    waterLayer_ = waterLayer;
    if (factures.empty()) throw std::runtime_error("нет фактур рельефа");
    const std::uint32_t size = factures.front().width;
    for (const assets::Image& f : factures)
        if (f.width != size || f.height != size) throw std::runtime_error("фактуры рельефа разного размера");
    std::uint32_t levels = 1;
    while ((size >> levels) > 0) ++levels;

    // Все уровни всех слоёв — в одном промежуточном буфере.
    std::vector<assets::Image> mips;
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize total = 0;
    for (std::uint32_t layer = 0; layer < factures.size(); ++layer) {
        assets::Image level = factures[layer];
        for (std::uint32_t m = 0; m < levels; ++m) {
            VkBufferImageCopy region{};
            region.bufferOffset = total;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, layer, 1};
            region.imageExtent = {level.width, level.height, 1};
            regions.push_back(region);
            total += level.rgba.size();
            const assets::Image next = halve(level);
            mips.push_back(std::move(level));
            level = next;
        }
    }
    Staging staging(vulkan_.allocator(), total);
    for (std::size_t i = 0; i < mips.size(); ++i)
        std::memcpy(static_cast<std::uint8_t*>(staging.mapped) + regions[i].bufferOffset, mips[i].rgba.data(),
                    mips[i].rgba.size());
    vmaFlushAllocation(vulkan_.allocator(), staging.memory, 0, VK_WHOLE_SIZE);

    vkDeviceWaitIdle(vulkan_.device());
    destroyTextures();
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_SRGB;
    ici.extent = {size, size, 1};
    ici.mipLevels = levels;
    ici.arrayLayers = static_cast<std::uint32_t>(factures.size());
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo iai{};
    iai.usage = VMA_MEMORY_USAGE_AUTO;
    check(vmaCreateImage(vulkan_.allocator(), &ici, &iai, &image_, &imageMemory_, nullptr),
          "не удалось создать массив фактур");

    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, ici.arrayLayers};
    vulkan_.submitNow([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image_;
        b.subresourceRange = all;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);
        vkCmdCopyBufferToImage(cmd, staging.buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(regions.size()), regions.data());
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
    });

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = image_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.format = ici.format;
    vci.subresourceRange = all;
    check(vkCreateImageView(vulkan_.device(), &vci, nullptr, &view_), "не удалось создать вид массива фактур");

    VkDescriptorImageInfo info{sampler_, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set_;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(vulkan_.device(), 1, &w, 0, nullptr);
}

void TerrainRenderer::setMesh(const std::vector<world::TerrainVertex>& vertices) {
    vkDeviceWaitIdle(vulkan_.device());
    if (vertices_) vmaDestroyBuffer(vulkan_.allocator(), vertices_, verticesMemory_);
    vertices_ = VK_NULL_HANDLE;
    vertexCount_ = 0;
    if (vertices.empty()) return;

    const VkDeviceSize bytes = vertices.size() * sizeof(world::TerrainVertex);
    Staging staging(vulkan_.allocator(), bytes);
    std::memcpy(staging.mapped, vertices.data(), bytes);
    vmaFlushAllocation(vulkan_.allocator(), staging.memory, 0, VK_WHOLE_SIZE);

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    check(vmaCreateBuffer(vulkan_.allocator(), &bci, &ai, &vertices_, &verticesMemory_, nullptr),
          "не удалось создать буфер рельефа");
    vulkan_.submitNow([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{0, 0, bytes};
        vkCmdCopyBuffer(cmd, staging.buffer, vertices_, 1, &copy);
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = vertices_;
        b.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr,
                             1, &b, 0, nullptr);
    });
    vertexCount_ = static_cast<std::uint32_t>(vertices.size());
}

void TerrainRenderer::draw(VkCommandBuffer cmd, const MapCamera& camera, float time) {
    if (!vertices_ || !view_) return;
    if (pipelinePass_ != vulkan_.renderPass()) createPipeline();
    const VkExtent2D extent = vulkan_.extent();
    VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    const float push[6] = {camera.x, camera.y, 2.0f * camera.zoom / float(extent.width),
                           2.0f * camera.zoom / float(extent.height), time, float(waterLayer_)};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(push), push);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &set_, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertices_, &offset);
    vkCmdDraw(cmd, vertexCount_, 1, 0, 0);
}

} // namespace oc2::render
