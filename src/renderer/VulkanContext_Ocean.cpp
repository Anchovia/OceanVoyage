#include "VulkanContext.h"
#include "VulkanContext_Private.h"

#include <stdexcept>
#include <vector>
#include <cmath>
#include <cstring>

// ============================================================
//  FFT ocean (Tessendorf) — compute foundation
// ============================================================
// Phase 0: generate the time-independent ocean spectrum h0(k) once on the GPU.
// This stands up the whole compute path (storage image + compute pipeline + descriptor
// + dispatch on the graphics queue) without changing the rendered ocean yet. Later
// phases add the per-frame spectrum update, the butterfly IFFT, and the
// displacement and slope assembly that the ocean vertex/fragment shaders sample.

namespace {
constexpr uint32_t kFftLocal = 16; // compute local size per axis (matches the .comp shaders)
struct OceanFFTPush     { float time; };
struct OceanFFTPassPush { int32_t stage; int32_t direction; }; // direction: 0 = horizontal, 1 = vertical
}

void VulkanContext::createOceanFFT() {
    const uint32_t N = OCEAN_FFT_N;

    // ---- h0 spectrum storage image: rg = h0(k), ba = conj(h0(-k)). One array layer per cascade. ----
    createImage(N, N, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oceanH0Image, m_oceanH0Memory, 1, OCEAN_CASCADES);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = m_oceanH0Image;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format                      = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = OCEAN_CASCADES;
    if (vkCreateImageView(m_device, &vi, nullptr, &m_oceanH0View) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean h0 image view");

    // ---- descriptor set layout: binding 0 = h0 storage image ----
    VkDescriptorSetLayoutBinding b0{};
    b0.binding         = 0;
    b0.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b0.descriptorCount = 1;
    b0.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings    = &b0;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_oceanSpectrumDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean spectrum descriptor set layout");

    // ---- descriptor pool (sized for the full FFT pipeline added across phases) ----
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 24;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // butterfly texture
    poolSizes[1].descriptorCount = 4;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 2;
    pi.pPoolSizes    = poolSizes;
    pi.maxSets       = 8;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_oceanFFTDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean FFT descriptor pool");

    // ---- allocate + write the spectrum descriptor set ----
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_oceanFFTDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_oceanSpectrumDescriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_oceanSpectrumDescriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate ocean spectrum descriptor set");

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = m_oceanH0View;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = m_oceanSpectrumDescriptorSet;
    w.dstBinding      = 0;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.descriptorCount = 1;
    w.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);

    // ---- compute pipeline ----
    VkPipelineLayoutCreateInfo pli{};
    pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts    = &m_oceanSpectrumDescriptorSetLayout;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_oceanSpectrumPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean spectrum pipeline layout");

    auto code = readFile("shaders/ocean_spectrum.comp.spv");
    VkShaderModule mod = createShaderModule(code);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName  = "main";
    VkComputePipelineCreateInfo cpi{};
    cpi.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage  = stage;
    cpi.layout = m_oceanSpectrumPipelineLayout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_oceanSpectrumPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean spectrum pipeline");
    vkDestroyShaderModule(m_device, mod, nullptr);

    // ---- one-time dispatch: UNDEFINED -> GENERAL, generate h0, leave in GENERAL ----
    VkCommandBufferAllocateInfo cba{};
    cba.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandPool        = m_commandPool;
    cba.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkCheck(vkAllocateCommandBuffers(m_device, &cba, &cmd),
        "Failed to allocate ocean h0 command buffer");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &bi),
        "Failed to begin ocean h0 command buffer");

    VkImageMemoryBarrier toGeneral{};
    toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image               = m_oceanH0Image;
    toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneral.subresourceRange.levelCount = 1;
    toGeneral.subresourceRange.layerCount = OCEAN_CASCADES;
    toGeneral.srcAccessMask       = 0;
    toGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toGeneral);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanSpectrumPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanSpectrumPipelineLayout,
        0, 1, &m_oceanSpectrumDescriptorSet, 0, nullptr);
    vkCmdDispatch(cmd, N / kFftLocal, N / kFftLocal, OCEAN_CASCADES);

    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end ocean h0 command buffer");
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCheck(vkCreateFence(m_device, &fi, nullptr, &fence),
        "Failed to create ocean h0 fence");
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &si, fence),
        "Failed to submit ocean h0 command buffer");
    vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
        "Failed to wait for ocean h0 fence");
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);

    createOceanFFTSim();
}

// ------------------------------------------------------------
//  Phase 1 (step 1): per-frame spectrum animation h0(k) -> H(k,t).
//  Produces the displacement spectrum each frame. The butterfly IFFT and the
//  displacement/normal assembly that the ocean shaders sample come in later steps.
// ------------------------------------------------------------
void VulkanContext::createOceanFFTSim() {
    const uint32_t N = OCEAN_FFT_N;

    // ---- animated spectrum storage image: rg = Dy+i*Dx, ba = Dz. One array layer per cascade. ----
    createImage(N, N, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oceanSpectrumImage, m_oceanSpectrumMemory, 1, OCEAN_CASCADES);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = m_oceanSpectrumImage;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format                      = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = OCEAN_CASCADES;
    if (vkCreateImageView(m_device, &vi, nullptr, &m_oceanSpectrumView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean spectrum image view");

    // ---- one-time UNDEFINED -> GENERAL transition ----
    {
        VkCommandBufferAllocateInfo cba{};
        cba.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cba.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cba.commandPool        = m_commandPool;
        cba.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkCheck(vkAllocateCommandBuffers(m_device, &cba, &cmd),
            "Failed to allocate ocean spectrum transition command buffer");
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(cmd, &bi),
            "Failed to begin ocean spectrum transition command buffer");
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_oceanSpectrumImage;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = OCEAN_CASCADES;
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        vkCheck(vkEndCommandBuffer(cmd),
            "Failed to end ocean spectrum transition command buffer");
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        vkCheck(vkCreateFence(m_device, &fi, nullptr, &fence),
            "Failed to create ocean spectrum transition fence");
        vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &si, fence),
            "Failed to submit ocean spectrum transition command buffer");
        vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
            "Failed to wait for ocean spectrum transition fence");
        vkDestroyFence(m_device, fence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }

    // ---- descriptor set layout: 0 = h0 (storage read), 1 = spectrum (storage write) ----
    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding         = 0;
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding         = 1;
    binds[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings    = binds;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_oceanUpdateDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean update descriptor set layout");

    // ---- pipeline layout with a time push constant ----
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset     = 0;
    pc.size       = sizeof(OceanFFTPush);
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &m_oceanUpdateDescriptorSetLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pc;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_oceanUpdatePipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean update pipeline layout");

    auto code = readFile("shaders/ocean_spectrum_update.comp.spv");
    VkShaderModule mod = createShaderModule(code);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName  = "main";
    VkComputePipelineCreateInfo cpi{};
    cpi.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage  = stage;
    cpi.layout = m_oceanUpdatePipelineLayout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_oceanUpdatePipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean update pipeline");
    vkDestroyShaderModule(m_device, mod, nullptr);

    // ---- descriptor set (shares the FFT descriptor pool) ----
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_oceanFFTDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_oceanUpdateDescriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_oceanUpdateDescriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate ocean update descriptor set");

    VkDescriptorImageInfo h0Info{};
    h0Info.imageView   = m_oceanH0View;
    h0Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo specInfo{};
    specInfo.imageView   = m_oceanSpectrumView;
    specInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_oceanUpdateDescriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo      = &h0Info;
    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = m_oceanUpdateDescriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo      = &specInfo;
    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);

    createOceanFFTTransform();
}

// ------------------------------------------------------------
//  Phase 1 (step 2): butterfly IFFT. A CPU-built butterfly texture drives a radix-2
//  Cooley-Tukey transform (log2(N) horizontal + log2(N) vertical passes) that ping-pongs
//  between the spectrum image and a second work image.
// ------------------------------------------------------------
void VulkanContext::createOceanFFTTransform() {
    const uint32_t N     = OCEAN_FFT_N;
    const uint32_t log2n = OCEAN_FFT_LOG2N;

    // ---- second ping-pong work image (one array layer per cascade) ----
    createImage(N, N, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oceanFFTPongImage, m_oceanFFTPongMemory, 1, OCEAN_CASCADES);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = m_oceanFFTPongImage;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format                      = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = OCEAN_CASCADES;
    if (vkCreateImageView(m_device, &vi, nullptr, &m_oceanFFTPongView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean FFT pong image view");

    // ---- one-time UNDEFINED -> GENERAL transition for the pong image ----
    {
        VkCommandBufferAllocateInfo cba{};
        cba.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cba.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cba.commandPool        = m_commandPool;
        cba.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkCheck(vkAllocateCommandBuffers(m_device, &cba, &cmd),
            "Failed to allocate ocean FFT pong transition command buffer");
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(cmd, &bi),
            "Failed to begin ocean FFT pong transition command buffer");
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_oceanFFTPongImage;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = OCEAN_CASCADES;
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        vkCheck(vkEndCommandBuffer(cmd),
            "Failed to end ocean FFT pong transition command buffer");
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        vkCheck(vkCreateFence(m_device, &fi, nullptr, &fence),
            "Failed to create ocean FFT pong transition fence");
        vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &si, fence),
            "Failed to submit ocean FFT pong transition command buffer");
        vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
            "Failed to wait for ocean FFT pong transition fence");
        vkDestroyFence(m_device, fence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }

    // ---- butterfly texture (CPU): width = log2(N) stages, height = N indices ----
    // Standard radix-2 Cooley-Tukey butterfly (Flügge / Tessendorf GPU FFT): each texel
    // holds the twiddle factor and the two source indices for that stage/index. Stage 0
    // folds in the bit-reversal permutation so no separate reorder pass is needed.
    std::vector<uint32_t> reversed(N);
    for (uint32_t i = 0; i < N; i++) {
        uint32_t r = 0;
        for (uint32_t b = 0; b < log2n; b++)
            if (i & (1u << b)) r |= 1u << (log2n - 1u - b);
        reversed[i] = r;
    }

    const float TWO_PI = 6.28318530718f;
    std::vector<float> bf(log2n * N * 4);
    for (uint32_t stage = 0; stage < log2n; stage++) {
        for (uint32_t y = 0; y < N; y++) {
            float k = std::fmod((float)y * (float)N / std::pow(2.0f, (float)(stage + 1)), (float)N);
            float ang = TWO_PI * k / (float)N;
            float twRe = std::cos(ang);
            float twIm = std::sin(ang);

            uint32_t span    = 1u << stage;
            bool     topWing = (y % (1u << (stage + 1))) < span;

            int topI, botI;
            if (stage == 0) {
                if (topWing) { topI = (int)reversed[y];     botI = (int)reversed[y + 1]; }
                else         { topI = (int)reversed[y - 1]; botI = (int)reversed[y];     }
            } else {
                if (topWing) { topI = (int)y;        botI = (int)(y + span); }
                else         { topI = (int)(y - span); botI = (int)y;        }
            }

            // Texel (col = stage, row = y); row-major width = log2n.
            float* t = &bf[(y * log2n + stage) * 4];
            t[0] = twRe; t[1] = twIm; t[2] = (float)topI; t[3] = (float)botI;
        }
    }
    m_oceanButterflyTex = createTexture(log2n, N, VK_FORMAT_R32G32B32A32_SFLOAT,
        bf.data(), (VkDeviceSize)bf.size() * sizeof(float), /*withSampler*/ true);

    // ---- FFT descriptor set layout: 0 = butterfly (sampler), 1 = src, 2 = dst ----
    VkDescriptorSetLayoutBinding binds[3]{};
    binds[0].binding         = 0;
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding         = 1;
    binds[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[2].binding         = 2;
    binds[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 3;
    li.pBindings    = binds;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_oceanFFTDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean FFT descriptor set layout");

    // ---- pipeline (stage + direction push constant) ----
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(OceanFFTPassPush);
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &m_oceanFFTDescriptorSetLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_oceanFFTPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean FFT pipeline layout");

    auto code = readFile("shaders/ocean_fft.comp.spv");
    VkShaderModule mod = createShaderModule(code);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName  = "main";
    VkComputePipelineCreateInfo cpi{};
    cpi.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage  = stage;
    cpi.layout = m_oceanFFTPipelineLayout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_oceanFFTPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean FFT pipeline");
    vkDestroyShaderModule(m_device, mod, nullptr);

    // ---- two descriptor sets that ping-pong spectrum <-> pong ----
    VkDescriptorSetLayout layouts[2] = { m_oceanFFTDescriptorSetLayout, m_oceanFFTDescriptorSetLayout };
    VkDescriptorSet sets[2];
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_oceanFFTDescriptorPool;
    ai.descriptorSetCount = 2;
    ai.pSetLayouts        = layouts;
    if (vkAllocateDescriptorSets(m_device, &ai, sets) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate ocean FFT descriptor sets");
    m_oceanFFTSetPingToPong = sets[0];
    m_oceanFFTSetPongToPing = sets[1];

    VkDescriptorImageInfo bfInfo{};
    bfInfo.imageView   = m_oceanButterflyTex.view;
    bfInfo.sampler     = m_oceanButterflyTex.sampler;
    bfInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo specInfo{};
    specInfo.imageView   = m_oceanSpectrumView;
    specInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo pongInfo{};
    pongInfo.imageView   = m_oceanFFTPongView;
    pongInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    auto writeSet = [&](VkDescriptorSet set, const VkDescriptorImageInfo& src, const VkDescriptorImageInfo& dst) {
        VkWriteDescriptorSet w[3]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = set; w[0].dstBinding = 0;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[0].descriptorCount = 1; w[0].pImageInfo = &bfInfo;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = set; w[1].dstBinding = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[1].descriptorCount = 1; w[1].pImageInfo = &src;
        w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2].dstSet = set; w[2].dstBinding = 2;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[2].descriptorCount = 1; w[2].pImageInfo = &dst;
        vkUpdateDescriptorSets(m_device, 3, w, 0, nullptr);
    };
    writeSet(m_oceanFFTSetPingToPong, specInfo, pongInfo); // src = spectrum(ping), dst = pong
    writeSet(m_oceanFFTSetPongToPing, pongInfo, specInfo); // src = pong, dst = spectrum(ping)

    createOceanFFTAssemble();
}

// ------------------------------------------------------------
//  Phase 1 (steps 3-4): assemble the IFFT output into a world-space displacement map that
//  the ocean vertex shader samples. R16F so linear filtering is guaranteed on the target.
// ------------------------------------------------------------
void VulkanContext::createOceanFFTAssemble() {
    const uint32_t N = OCEAN_FFT_N;

    // ---- displacement map (TRANSFER_SRC so it can be copied back to host for buoyancy).
    //      One array layer per cascade; the ocean shaders sum the layers. ----
    createImage(N, N, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oceanDisplacementImage, m_oceanDisplacementMemory, 1, OCEAN_CASCADES);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = m_oceanDisplacementImage;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format                      = VK_FORMAT_R16G16B16A16_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = OCEAN_CASCADES;
    if (vkCreateImageView(m_device, &vi, nullptr, &m_oceanDisplacementView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean displacement image view");

    // ---- one-time UNDEFINED -> GENERAL transition ----
    {
        VkCommandBufferAllocateInfo cba{};
        cba.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cba.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cba.commandPool        = m_commandPool;
        cba.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkCheck(vkAllocateCommandBuffers(m_device, &cba, &cmd),
            "Failed to allocate ocean displacement transition command buffer");
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(cmd, &bi),
            "Failed to begin ocean displacement transition command buffer");
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_oceanDisplacementImage;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = OCEAN_CASCADES;
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        vkCheck(vkEndCommandBuffer(cmd),
            "Failed to end ocean displacement transition command buffer");
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        vkCheck(vkCreateFence(m_device, &fi, nullptr, &fence),
            "Failed to create ocean displacement transition fence");
        vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &si, fence),
            "Failed to submit ocean displacement transition command buffer");
        vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
            "Failed to wait for ocean displacement transition fence");
        vkDestroyFence(m_device, fence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }

    // ---- per-cascade surface-slope map (RGBA16F, .rg = world height gradient). One layer per
    //      cascade; written by the assemble pass, sampled by the water shader for smooth normals. ----
    createImage(N, N, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oceanSlopeImage, m_oceanSlopeMemory, 1, OCEAN_CASCADES);

    vi.image = m_oceanSlopeImage; // vi still configured as 2D_ARRAY, RGBA16F, layerCount = cascades
    if (vkCreateImageView(m_device, &vi, nullptr, &m_oceanSlopeView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean slope image view");

    {
        VkCommandBufferAllocateInfo cba{};
        cba.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cba.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cba.commandPool        = m_commandPool;
        cba.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkCheck(vkAllocateCommandBuffers(m_device, &cba, &cmd),
            "Failed to allocate ocean slope transition command buffer");
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(cmd, &bi),
            "Failed to begin ocean slope transition command buffer");
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_oceanSlopeImage;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = OCEAN_CASCADES;
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        vkCheck(vkEndCommandBuffer(cmd),
            "Failed to end ocean slope transition command buffer");
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        vkCheck(vkCreateFence(m_device, &fi, nullptr, &fence),
            "Failed to create ocean slope transition fence");
        vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &si, fence),
            "Failed to submit ocean slope transition command buffer");
        vkCheck(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX),
            "Failed to wait for ocean slope transition fence");
        vkDestroyFence(m_device, fence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }

    // ---- sampler for the ocean vertex shader (linear, world-tiling repeat) ----
    VkSamplerCreateInfo sci{};
    sci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter     = VK_FILTER_LINEAR;
    sci.minFilter     = VK_FILTER_LINEAR;
    sci.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (vkCreateSampler(m_device, &sci, nullptr, &m_oceanDisplacementSampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean displacement sampler");

    // ---- descriptor set layout: 0 = fft result (read), 1 = displacement (write), 2 = slope (write) ----
    VkDescriptorSetLayoutBinding binds[3]{};
    binds[0].binding         = 0;
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding         = 1;
    binds[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[2].binding         = 2;
    binds[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 3;
    li.pBindings    = binds;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_oceanAssembleDescriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean assemble descriptor set layout");

    VkPipelineLayoutCreateInfo pli{};
    pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts    = &m_oceanAssembleDescriptorSetLayout;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_oceanAssemblePipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean assemble pipeline layout");

    auto code = readFile("shaders/ocean_assemble.comp.spv");
    VkShaderModule mod = createShaderModule(code);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName  = "main";
    VkComputePipelineCreateInfo cpi{};
    cpi.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage  = stage;
    cpi.layout = m_oceanAssemblePipelineLayout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_oceanAssemblePipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ocean assemble pipeline");
    vkDestroyShaderModule(m_device, mod, nullptr);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_oceanFFTDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_oceanAssembleDescriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_oceanAssembleDescriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate ocean assemble descriptor set");

    VkDescriptorImageInfo srcInfo{};
    srcInfo.imageView   = m_oceanSpectrumView; // final IFFT result lives here
    srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo dstInfo{};
    dstInfo.imageView   = m_oceanDisplacementView;
    dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo slopeInfo{};
    slopeInfo.imageView   = m_oceanSlopeView;
    slopeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[3]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_oceanAssembleDescriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo      = &srcInfo;
    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = m_oceanAssembleDescriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo      = &dstInfo;
    writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet          = m_oceanAssembleDescriptorSet;
    writes[2].dstBinding      = 2;
    writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo      = &slopeInfo;
    vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);

    // ---- host-visible readback buffers (one per frame in flight) for ship buoyancy.
    //      Holds every cascade layer (tightly packed) so the CPU can sum the surface. ----
    VkDeviceSize rbSize = (VkDeviceSize)N * N * 8 * OCEAN_CASCADES; // RGBA16F = 8 bytes/texel
    m_oceanReadbackBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (auto& b : m_oceanReadbackBuffers) {
        b = createBuffer(rbSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkCheck(vkMapMemory(m_device, b.memory, 0, rbSize, 0, &b.mapped),
            "Failed to map ocean readback buffer");
        memset(b.mapped, 0, (size_t)rbSize); // clean height (0) until the first copy lands
    }
}

// Records the per-frame FFT ocean compute work into the frame's command buffer (before
// the render passes). Step 1: animate the spectrum. Later steps add the IFFT and assembly.
void VulkanContext::recordOceanFFT(VkCommandBuffer cmd) {
    const uint32_t N = OCEAN_FFT_N;

    // Order the previous frame's FFT-image accesses (compute writes + the vertex shader
    // sampling the displacement map) before this frame's compute overwrites them. Barrier
    // scopes extend to earlier submissions in submission order, so this also makes the
    // init-time h0 write visible to the first frame's read.
    VkMemoryBarrier pre{};
    pre.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    pre.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &pre, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanUpdatePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanUpdatePipelineLayout,
        0, 1, &m_oceanUpdateDescriptorSet, 0, nullptr);
    OceanFFTPush push{ m_oceanTime };
    vkCmdPushConstants(cmd, m_oceanUpdatePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, N / kFftLocal, N / kFftLocal, OCEAN_CASCADES);

    // Make the spectrum write available to subsequent shader reads (future IFFT passes).
    VkImageMemoryBarrier specAvail{};
    specAvail.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    specAvail.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    specAvail.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    specAvail.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    specAvail.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    specAvail.image               = m_oceanSpectrumImage;
    specAvail.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    specAvail.subresourceRange.levelCount = 1;
    specAvail.subresourceRange.layerCount = OCEAN_CASCADES;
    specAvail.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    specAvail.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &specAvail);

    // Butterfly IFFT: log2(N) horizontal + log2(N) vertical passes, ping-ponging between
    // the spectrum image (ping) and the pong image. After 2·log2(N) passes the transformed
    // field is back in the spectrum image.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanFFTPipeline);
    for (uint32_t pass = 0; pass < 2u * OCEAN_FFT_LOG2N; pass++) {
        OceanFFTPassPush pp{};
        pp.stage     = (int32_t)(pass % OCEAN_FFT_LOG2N);
        pp.direction = (pass < OCEAN_FFT_LOG2N) ? 0 : 1;
        VkDescriptorSet set = (pass % 2u == 0u) ? m_oceanFFTSetPingToPong : m_oceanFFTSetPongToPing;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanFFTPipelineLayout,
            0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, m_oceanFFTPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(pp), &pp);
        vkCmdDispatch(cmd, N / kFftLocal, N / kFftLocal, OCEAN_CASCADES);

        // Order each pass's writes before the next pass's reads (storage images, GENERAL).
        VkMemoryBarrier mb{};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    // Assemble the transformed field into the world-space displacement map.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanAssemblePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanAssemblePipelineLayout,
        0, 1, &m_oceanAssembleDescriptorSet, 0, nullptr);
    vkCmdDispatch(cmd, N / kFftLocal, N / kFftLocal, OCEAN_CASCADES);

    // Make the displacement + slope writes visible to the ocean vertex/fragment shaders in the
    // render passes (and the displacement copy to the readback buffer).
    VkImageMemoryBarrier availBarriers[2]{};
    availBarriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    availBarriers[0].oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    availBarriers[0].newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    availBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    availBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    availBarriers[0].image               = m_oceanDisplacementImage;
    availBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    availBarriers[0].subresourceRange.levelCount = 1;
    availBarriers[0].subresourceRange.layerCount = OCEAN_CASCADES;
    availBarriers[0].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    availBarriers[0].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    availBarriers[1] = availBarriers[0];
    availBarriers[1].image               = m_oceanSlopeImage;
    availBarriers[1].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 2, availBarriers);

    // Copy the displacement back to a host-visible buffer so the CPU can float the ship on the
    // real FFT surface. The result is read ~2 frames later (after this slot's fence), so the
    // copy never stalls the GPU.
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = OCEAN_CASCADES; // copies all cascade layers, tightly packed
    region.imageExtent                 = { N, N, 1 };
    vkCmdCopyImageToBuffer(cmd, m_oceanDisplacementImage, VK_IMAGE_LAYOUT_GENERAL,
        m_oceanReadbackBuffers[m_currentFrame].buffer, 1, &region);

    // Make the copied data visible to host reads.
    VkMemoryBarrier toHost{};
    toHost.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &toHost, 0, nullptr, 0, nullptr);
}

void VulkanContext::destroyOceanFFT() {
    m_oceanReadbackBuffers.clear(); // GpuBuffer RAII frees + unmaps (device still alive here)
    vkDestroyPipeline(m_device, m_oceanAssemblePipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_oceanAssemblePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_oceanAssembleDescriptorSetLayout, nullptr);
    vkDestroySampler(m_device, m_oceanDisplacementSampler, nullptr);
    vkDestroyImageView(m_device, m_oceanDisplacementView, nullptr);
    vkDestroyImage(m_device, m_oceanDisplacementImage, nullptr);
    vkFreeMemory(m_device, m_oceanDisplacementMemory, nullptr);
    vkDestroyImageView(m_device, m_oceanSlopeView, nullptr);
    vkDestroyImage(m_device, m_oceanSlopeImage, nullptr);
    vkFreeMemory(m_device, m_oceanSlopeMemory, nullptr);

    vkDestroyPipeline(m_device, m_oceanFFTPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_oceanFFTPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_oceanFFTDescriptorSetLayout, nullptr);
    m_oceanButterflyTex.destroy();
    vkDestroyImageView(m_device, m_oceanFFTPongView, nullptr);
    vkDestroyImage(m_device, m_oceanFFTPongImage, nullptr);
    vkFreeMemory(m_device, m_oceanFFTPongMemory, nullptr);

    vkDestroyPipeline(m_device, m_oceanUpdatePipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_oceanUpdatePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_oceanUpdateDescriptorSetLayout, nullptr);
    vkDestroyImageView(m_device, m_oceanSpectrumView, nullptr);
    vkDestroyImage(m_device, m_oceanSpectrumImage, nullptr);
    vkFreeMemory(m_device, m_oceanSpectrumMemory, nullptr);

    vkDestroyPipeline(m_device, m_oceanSpectrumPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_oceanSpectrumPipelineLayout, nullptr);
    vkDestroyDescriptorPool(m_device, m_oceanFFTDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_oceanSpectrumDescriptorSetLayout, nullptr);
    vkDestroyImageView(m_device, m_oceanH0View, nullptr);
    vkDestroyImage(m_device, m_oceanH0Image, nullptr);
    vkFreeMemory(m_device, m_oceanH0Memory, nullptr);
}
