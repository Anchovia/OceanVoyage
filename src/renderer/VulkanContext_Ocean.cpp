#include "VulkanContext.h"
#include "VulkanContext_Private.h"

#include <stdexcept>

// ============================================================
//  FFT ocean (Tessendorf) — compute foundation
// ============================================================
// Phase 0: generate the time-independent ocean spectrum h0(k) once on the GPU.
// This stands up the whole compute path (storage image + compute pipeline + descriptor
// + dispatch on the graphics queue) without changing the rendered ocean yet. Later
// phases add the per-frame spectrum update, the butterfly IFFT, and the
// displacement/normal/foam assembly that the ocean vertex/fragment shaders sample.

namespace {
constexpr uint32_t kFftLocal = 16; // compute local size per axis (matches the .comp shaders)
struct OceanFFTPush { float time; };
}

void VulkanContext::createOceanFFT() {
    const uint32_t N = OCEAN_FFT_N;

    // ---- h0 spectrum storage image: rg = h0(k), ba = conj(h0(-k)) ----
    createImage(N, N, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oceanH0Image, m_oceanH0Memory);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = m_oceanH0Image;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                      = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
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

    // ---- descriptor pool (sized for the full FFT pipeline added in later phases) ----
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 16;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &poolSize;
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
    vkAllocateCommandBuffers(m_device, &cba, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier toGeneral{};
    toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image               = m_oceanH0Image;
    toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneral.subresourceRange.levelCount = 1;
    toGeneral.subresourceRange.layerCount = 1;
    toGeneral.srcAccessMask       = 0;
    toGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toGeneral);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanSpectrumPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanSpectrumPipelineLayout,
        0, 1, &m_oceanSpectrumDescriptorSet, 0, nullptr);
    vkCmdDispatch(cmd, N / kFftLocal, N / kFftLocal, 1);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCreateFence(m_device, &fi, nullptr, &fence);
    vkQueueSubmit(m_graphicsQueue, 1, &si, fence);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
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

    // ---- animated spectrum storage image: rg = Dy+i*Dx, ba = Dz ----
    createImage(N, N, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_oceanSpectrumImage, m_oceanSpectrumMemory);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = m_oceanSpectrumImage;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                      = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
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
        vkAllocateCommandBuffers(m_device, &cba, &cmd);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = m_oceanSpectrumImage;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        vkCreateFence(m_device, &fi, nullptr, &fence);
        vkQueueSubmit(m_graphicsQueue, 1, &si, fence);
        vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
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
}

// Records the per-frame FFT ocean compute work into the frame's command buffer (before
// the render passes). Step 1: animate the spectrum. Later steps add the IFFT and assembly.
void VulkanContext::recordOceanFFT(VkCommandBuffer cmd) {
    const uint32_t N = OCEAN_FFT_N;

    // h0 was written once at init on this same queue; make that write visible to the read.
    VkImageMemoryBarrier h0Avail{};
    h0Avail.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    h0Avail.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    h0Avail.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    h0Avail.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    h0Avail.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    h0Avail.image               = m_oceanH0Image;
    h0Avail.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    h0Avail.subresourceRange.levelCount = 1;
    h0Avail.subresourceRange.layerCount = 1;
    h0Avail.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    h0Avail.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &h0Avail);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanUpdatePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oceanUpdatePipelineLayout,
        0, 1, &m_oceanUpdateDescriptorSet, 0, nullptr);
    OceanFFTPush push{ m_oceanTime };
    vkCmdPushConstants(cmd, m_oceanUpdatePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, N / kFftLocal, N / kFftLocal, 1);

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
    specAvail.subresourceRange.layerCount = 1;
    specAvail.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    specAvail.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &specAvail);
}

void VulkanContext::destroyOceanFFT() {
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
