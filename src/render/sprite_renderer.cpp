#include "render/sprite_renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include "render/shaders/sprite_frag.h"
#include "render/shaders/sprite_vert.h"

namespace oc2::render {

namespace {

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + ": ошибка Vulkan " + std::to_string(r));
}

constexpr std::uint32_t kSetsPerPool = 64;

VkShaderModule createShader(VkDevice device, const std::uint32_t* code, std::size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &ci, nullptr, &m), "не удалось создать шейдер");
    return m;
}

} // namespace

SpriteRenderer::SpriteRenderer(VulkanContext& vulkan) : vulkan_(vulkan) {
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

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout_), "не удалось создать раскладку конвейера");

    // Пиксельная графика: без сглаживания, за краями — крайний пиксель.
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device, &si, nullptr, &sampler_), "не удалось создать сэмплер");

    createPipeline();
}

SpriteRenderer::~SpriteRenderer() {
    VkDevice device = vulkan_.device();
    vkDeviceWaitIdle(device);
    for (Buffer& b : vertexBuffers_) destroyBuffer(b);
    for (Buffer& b : indexBuffers_) destroyBuffer(b);
    for (Texture& t : textures_) {
        vkDestroyImageView(device, t.view, nullptr);
        vmaDestroyImage(vulkan_.allocator(), t.image, t.memory);
    }
    for (VkDescriptorPool p : pools_) vkDestroyDescriptorPool(device, p, nullptr);
    vkDestroySampler(device, sampler_, nullptr);
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
}

void SpriteRenderer::createPipeline() {
    VkDevice device = vulkan_.device();
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    pipelinePass_ = vulkan_.renderPass();

    VkShaderModule vert = createShader(device, kSpriteVert, sizeof(kSpriteVert));
    VkShaderModule frag = createShader(device, kSpriteFrag, sizeof(kSpriteFrag));
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, sizeof(SpriteVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription, 3> attrs{{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteVertex, u)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(SpriteVertex, color)},
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
    rs.cullMode = VK_CULL_MODE_NONE; // в данных игры треугольники обоих направлений обхода
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
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
    check(r, "не удалось создать графический конвейер");
}

VkDescriptorSet SpriteRenderer::allocateSet() {
    if (setsLeftInPool_ == 0) {
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = kSetsPerPool;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &size;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        check(vkCreateDescriptorPool(vulkan_.device(), &pi, nullptr, &pool), "не удалось создать пул дескрипторов");
        pools_.push_back(pool);
        setsLeftInPool_ = kSetsPerPool;
    }
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pools_.back();
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &setLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(vulkan_.device(), &ai, &set), "не удалось выделить дескриптор");
    --setsLeftInPool_;
    return set;
}

TextureId SpriteRenderer::createTexture(const assets::Image& image) {
    if (image.width == 0 || image.height == 0) throw std::runtime_error("текстура нулевого размера");
    VmaAllocator allocator = vulkan_.allocator();
    const VkDeviceSize bytes = image.rgba.size();

    // Промежуточный буфер в памяти, доступной процессору.
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo bai{};
    bai.usage = VMA_MEMORY_USAGE_AUTO;
    bai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingMemory = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo{};
    check(vmaCreateBuffer(allocator, &bci, &bai, &staging, &stagingMemory, &stagingInfo),
          "не удалось создать промежуточный буфер");
    std::memcpy(stagingInfo.pMappedData, image.rgba.data(), bytes);
    vmaFlushAllocation(allocator, stagingMemory, 0, VK_WHOLE_SIZE);

    Texture t;
    t.size = {image.width, image.height};
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_SRGB; // цвета игры заданы в sRGB
    ici.extent = {image.width, image.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo iai{};
    iai.usage = VMA_MEMORY_USAGE_AUTO;
    VkResult r = vmaCreateImage(allocator, &ici, &iai, &t.image, &t.memory, nullptr);
    if (r != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, staging, stagingMemory);
        check(r, "не удалось создать текстуру");
    }

    vulkan_.submitNow([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {image.width, image.height, 1};
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
    });
    vmaDestroyBuffer(allocator, staging, stagingMemory);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(vulkan_.device(), &vci, nullptr, &t.view), "не удалось создать вид текстуры");

    t.set = allocateSet();
    VkDescriptorImageInfo info{sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = t.set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(vulkan_.device(), 1, &w, 0, nullptr);

    textures_.push_back(t);
    return static_cast<TextureId>(textures_.size() - 1);
}

void SpriteRenderer::drawTriangles(TextureId texture, std::span<const SpriteVertex> vertices,
                                   std::span<const std::uint16_t> indices) {
    if (texture >= textures_.size()) throw std::runtime_error("нет текстуры " + std::to_string(texture));
    if (indices.empty()) return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first = static_cast<std::uint32_t>(indices_.size());
    vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
    for (std::uint16_t i : indices) {
        if (i >= vertices.size()) throw std::runtime_error("индекс вершины вне массива");
        indices_.push_back(base + i);
    }
    // Подряд идущие треугольники с одной текстурой и областью отсечения рисуются одним вызовом.
    auto sameClip = [&](const std::optional<VkRect2D>& c) {
        if (c.has_value() != clip_.has_value()) return false;
        return !c || (c->offset.x == clip_->offset.x && c->offset.y == clip_->offset.y &&
                      c->extent.width == clip_->extent.width && c->extent.height == clip_->extent.height);
    };
    if (!batches_.empty() && batches_.back().texture == texture && sameClip(batches_.back().clip))
        batches_.back().indexCount += static_cast<std::uint32_t>(indices.size());
    else
        batches_.push_back({texture, first, static_cast<std::uint32_t>(indices.size()), clip_});
}

void SpriteRenderer::drawQuad(TextureId texture, float x, float y, float w, float h,
                              std::array<std::uint8_t, 4> color) {
    const std::array<SpriteVertex, 4> v{{
        {x, y, 0.0f, 0.0f, color},
        {x + w, y, 1.0f, 0.0f, color},
        {x + w, y + h, 1.0f, 1.0f, color},
        {x, y + h, 0.0f, 1.0f, color},
    }};
    const std::array<std::uint16_t, 6> idx{0, 1, 2, 0, 2, 3};
    drawTriangles(texture, v, idx);
}

void SpriteRenderer::fillRect(float x, float y, float w, float h, std::uint32_t argb) {
    if (!white_) {
        assets::Image img(1, 1);
        img.rgba = {255, 255, 255, 255};
        white_ = createTexture(img);
    }
    // Цвет вершин умножается на 2 в шейдере (0x80 — без изменений): белое × (c / 2) = c.
    const std::array<std::uint8_t, 4> c{std::uint8_t(((argb >> 16) & 0xFF) / 2), std::uint8_t(((argb >> 8) & 0xFF) / 2),
                                        std::uint8_t((argb & 0xFF) / 2), std::uint8_t(argb >> 24)};
    drawQuad(*white_, x, y, w, h, c);
}

void SpriteRenderer::setClip(int x1, int y1, int x2, int y2) {
    x1 = std::max(x1, 0);
    y1 = std::max(y1, 0);
    VkRect2D r{};
    r.offset = {x1, y1};
    r.extent = {std::uint32_t(std::max(x2 - x1, 0)), std::uint32_t(std::max(y2 - y1, 0))};
    clip_ = r;
}

void SpriteRenderer::ensureBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (b.size >= size) return;
    destroyBuffer(b);
    VkDeviceSize capacity = 64 * 1024;
    while (capacity < size) capacity *= 2;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = capacity;
    bci.usage = usage;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    check(vmaCreateBuffer(vulkan_.allocator(), &bci, &aci, &b.buffer, &b.memory, &info),
          "не удалось создать буфер вершин");
    b.mapped = info.pMappedData;
    b.size = capacity;
}

void SpriteRenderer::destroyBuffer(Buffer& b) {
    if (b.buffer) vmaDestroyBuffer(vulkan_.allocator(), b.buffer, b.memory);
    b = Buffer{};
}

void SpriteRenderer::flush(VkCommandBuffer cmd) {
    if (batches_.empty()) return;
    if (pipelinePass_ != vulkan_.renderPass()) createPipeline(); // формат экрана сменился

    // Буферы слота свободны: VulkanContext::beginFrame() дождался его прошлого кадра.
    const std::uint32_t slot = vulkan_.frameSlot();
    Buffer& vb = vertexBuffers_[slot];
    Buffer& ib = indexBuffers_[slot];
    const VkDeviceSize vbytes = vertices_.size() * sizeof(SpriteVertex);
    const VkDeviceSize ibytes = indices_.size() * sizeof(std::uint32_t);
    ensureBuffer(vb, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    ensureBuffer(ib, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    std::memcpy(vb.mapped, vertices_.data(), vbytes);
    std::memcpy(ib.mapped, indices_.data(), ibytes);
    vmaFlushAllocation(vulkan_.allocator(), vb.memory, 0, vbytes);
    vmaFlushAllocation(vulkan_.allocator(), ib.memory, 0, ibytes);

    const VkExtent2D extent = vulkan_.extent();
    VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    const float push[4] = {2.0f / float(extent.width), 2.0f / float(extent.height), -1.0f, -1.0f};

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, ib.buffer, 0, VK_INDEX_TYPE_UINT32);
    for (const Batch& b : batches_) {
        if (b.clip) {
            // Отсечение не может выходить за кадр.
            VkRect2D c = *b.clip;
            const auto right = std::min<std::int64_t>(std::int64_t(c.offset.x) + c.extent.width, extent.width);
            const auto bottom = std::min<std::int64_t>(std::int64_t(c.offset.y) + c.extent.height, extent.height);
            c.extent.width = std::uint32_t(std::max<std::int64_t>(right - c.offset.x, 0));
            c.extent.height = std::uint32_t(std::max<std::int64_t>(bottom - c.offset.y, 0));
            if (c.extent.width == 0 || c.extent.height == 0) continue;
            vkCmdSetScissor(cmd, 0, 1, &c);
        } else {
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &textures_[b.texture].set,
                                0, nullptr);
        vkCmdDrawIndexed(cmd, b.indexCount, 1, b.firstIndex, 0, 0);
    }

    vertices_.clear();
    indices_.clear();
    batches_.clear();
    clip_.reset();
}

} // namespace oc2::render
