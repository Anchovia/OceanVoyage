#include "VulkanContext.h"
#include "VulkanContext_Private.h"
#include "renderer/Types.h"
#include "platform/Window.h"
#include "game/Camera.h"

#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef PASTEL_DEV_BUILD
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#endif

#ifdef PASTEL_DEV_BUILD
void VulkanContext::beginDevFrame() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_devFrameStarted = true;
}

bool VulkanContext::devWantsMouse() const {
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}

bool VulkanContext::devWantsKeyboard() const {
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
}

void VulkanContext::toggleDevUi() {
    m_devUiVisible = !m_devUiVisible;
}

void VulkanContext::buildDevUi(const FrameRenderData& frame) {
    if (!ImGui::GetCurrentContext()) return;
    if (!m_devFrameStarted) beginDevFrame();

    if (m_devUiVisible) {
        ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("OceanVoyage Dev", &m_devUiVisible)) {
            ImGui::TextUnformatted("F3 toggles this panel");
            ImGui::Separator();
            ImGui::Text("Time of day: %.3f", frame.timeOfDay);
            ImGui::Text("Ship: %.2f, %.2f, %.2f",
                frame.shipPosition.x, frame.shipPosition.y, frame.shipPosition.z);
            ImGui::Text("Heading: %.1f deg  Thr: %.2f  Rud: %.2f",
                frame.shipHeading * 57.2957795f, frame.shipThrottle, frame.shipRudder);
            ImGui::SliderFloat("Move speed", &m_devMoveSpeedMultiplier, 1.0f, 8.0f, "%.1fx");
            ImGui::Separator();
            if (!m_devTimingSupported) {
                ImGui::TextUnformatted("GPU timing: unavailable");
            } else if (!m_devGpuTiming.valid) {
                ImGui::TextUnformatted("GPU timing: waiting for first frame");
            } else {
                ImGui::Text("GPU total:  %.3f ms", m_devGpuTiming.totalMs);
                ImGui::Text("  shadow:   %.3f ms", m_devGpuTiming.shadowMs);
                ImGui::Text("  scene:    %.3f ms", m_devGpuTiming.sceneMs);
                ImGui::Text("  post:     %.3f ms", m_devGpuTiming.postMs);
                ImGui::Text("  imgui:    %.3f ms", m_devGpuTiming.imguiMs);
            }
        }
        ImGui::End();
    }

    ImGui::Render();
    m_devFrameStarted = false;
}

void VulkanContext::readDevGpuTimings(uint32_t frameIndex) {
    if (!m_devTimingSupported || !m_devQueriesWritten[frameIndex]) return;

    uint64_t timestamps[DEV_TIMESTAMP_COUNT] = {};
    VkResult result = vkGetQueryPoolResults(
        m_device, m_devQueryPool, frameIndex * DEV_TIMESTAMP_COUNT, DEV_TIMESTAMP_COUNT,
        sizeof(timestamps), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) {
        m_devGpuTiming.valid = false;
        return;
    }

    auto ms = [&](uint32_t a, uint32_t b) {
        return (float)((double)(timestamps[b] - timestamps[a]) *
            (double)m_devTimestampPeriod / 1000000.0);
    };
    m_devGpuTiming.valid    = true;
    m_devGpuTiming.totalMs  = ms(0, 4);
    m_devGpuTiming.shadowMs = ms(0, 1);
    m_devGpuTiming.sceneMs  = ms(1, 2);
    m_devGpuTiming.postMs   = ms(2, 3);
    m_devGpuTiming.imguiMs  = ms(3, 4);
    m_devQueriesWritten[frameIndex] = false;
}

void VulkanContext::writeDevTimestamp(VkCommandBuffer cmd, uint32_t index) {
    if (!m_devTimingSupported) return;
    const uint32_t query = m_currentFrame * DEV_TIMESTAMP_COUNT + index;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_devQueryPool, query);
}
#endif

// ============================================================
//  Command buffer recording
// ============================================================
void VulkanContext::copySceneColorForWater(VkCommandBuffer cmd) {
    VkImageMemoryBarrier colorToCopy{};
    colorToCopy.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorToCopy.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    colorToCopy.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    colorToCopy.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorToCopy.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    colorToCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorToCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorToCopy.image               = m_offscreenImage[m_currentFrame];
    colorToCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorToCopy.subresourceRange.levelCount = 1;
    colorToCopy.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier copyToDst{};
    copyToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    copyToDst.srcAccessMask       = m_sceneColorCopyReady[m_currentFrame] ? VK_ACCESS_SHADER_READ_BIT : 0;
    copyToDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyToDst.oldLayout           = m_sceneColorCopyReady[m_currentFrame]
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    copyToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copyToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copyToDst.image               = m_sceneColorCopyImage[m_currentFrame];
    copyToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyToDst.subresourceRange.levelCount = 1;
    copyToDst.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier toCopyBarriers[] = { colorToCopy, copyToDst };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 2, toCopyBarriers);

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent = { m_swapchainExtent.width, m_swapchainExtent.height, 1 };
    vkCmdCopyImage(cmd,
        m_offscreenImage[m_currentFrame], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_sceneColorCopyImage[m_currentFrame], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copy);

    VkImageMemoryBarrier colorBack = colorToCopy;
    colorBack.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    colorBack.dstAccessMask = 0;
    colorBack.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    colorBack.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageMemoryBarrier copyReadable = copyToDst;
    copyReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    copyReadable.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyReadable.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageMemoryBarrier readableBarriers[] = { colorBack, copyReadable };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, readableBarriers);

    m_sceneColorCopyReady[m_currentFrame] = true;
}

void VulkanContext::copySceneDepthForWater(VkCommandBuffer cmd) {
    VkImageMemoryBarrier depthToCopy{};
    depthToCopy.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthToCopy.srcAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthToCopy.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    depthToCopy.oldLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthToCopy.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    depthToCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthToCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthToCopy.image               = m_depthImage;
    depthToCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthToCopy.subresourceRange.levelCount = 1;
    depthToCopy.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier copyToDst{};
    copyToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    copyToDst.srcAccessMask       = m_sceneDepthCopyReady[m_currentFrame] ? VK_ACCESS_SHADER_READ_BIT : 0;
    copyToDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyToDst.oldLayout           = m_sceneDepthCopyReady[m_currentFrame]
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    copyToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copyToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    copyToDst.image               = m_sceneDepthCopyImage[m_currentFrame];
    copyToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    copyToDst.subresourceRange.levelCount = 1;
    copyToDst.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier toCopyBarriers[] = { depthToCopy, copyToDst };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 2, toCopyBarriers);

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    copy.srcSubresource.layerCount     = 1;
    copy.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    copy.dstSubresource.layerCount     = 1;
    copy.extent = { m_swapchainExtent.width, m_swapchainExtent.height, 1 };
    vkCmdCopyImage(cmd,
        m_depthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_sceneDepthCopyImage[m_currentFrame], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copy);

    VkImageMemoryBarrier depthBack = depthToCopy;
    depthBack.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    depthBack.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBack.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    depthBack.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkImageMemoryBarrier copyReadable = copyToDst;
    copyReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    copyReadable.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyReadable.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageMemoryBarrier readableBarriers[] = { depthBack, copyReadable };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, readableBarriers);

    m_sceneDepthCopyReady[m_currentFrame] = true;
}

void VulkanContext::drawPortInstances(VkCommandBuffer cmd, VkDescriptorSet descriptorSet) {
    if (m_portMesh.count == 0 || m_portInstanceCount <= 0 || m_portPipeline == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_portPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_portPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    VkBuffer     bufs[] = { m_portMesh.vbuf };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);

    for (int i = 0; i < m_portInstanceCount; i++) {
        const PortRenderInstance& p = m_portInstances[(size_t)i];
        const float scale = glm::max(p.scale, 0.01f);
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(p.position.x, p.position.y, 0.0f));
        model = glm::rotate(model, p.heading, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(scale));
        vkCmdPushConstants(cmd, m_portPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(glm::mat4), &model);
        vkCmdDraw(cmd, m_portMesh.count, 1, 0, 0);
    }
}

void VulkanContext::drawPortShadows(VkCommandBuffer cmd, const glm::mat4& lightMVP) {
    if (m_portMesh.count == 0 || m_portInstanceCount <= 0 || m_portShadowPipeline == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_portShadowPipeline);
    VkBuffer     bufs[] = { m_portMesh.vbuf };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);

    for (int i = 0; i < m_portInstanceCount; i++) {
        const PortRenderInstance& p = m_portInstances[(size_t)i];
        const float scale = glm::max(p.scale, 0.01f);
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(p.position.x, p.position.y, 0.0f));
        model = glm::rotate(model, p.heading, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(scale));
        glm::mat4 portLightMVP = lightMVP * model;
        vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(glm::mat4), &portLightMVP);
        vkCmdDraw(cmd, m_portMesh.count, 1, 0, 0);
    }
}

// Islands share the port pipeline; the mesh is baked in world space so the
// model matrix is identity (one draw for all islands).
void VulkanContext::drawIslandMesh(VkCommandBuffer cmd, VkDescriptorSet descriptorSet) {
    if (m_islandMesh.count == 0 || m_portPipeline == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_portPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_portPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    VkBuffer     bufs[] = { m_islandMesh.vbuf };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);

    const glm::mat4 model(1.0f);
    vkCmdPushConstants(cmd, m_portPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(glm::mat4), &model);
    vkCmdDraw(cmd, m_islandMesh.count, 1, 0, 0);
}

void VulkanContext::drawIslandShadows(VkCommandBuffer cmd, const glm::mat4& lightMVP) {
    if (m_islandMesh.count == 0 || m_portShadowPipeline == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_portShadowPipeline);
    VkBuffer     bufs[] = { m_islandMesh.vbuf };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);

    vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(glm::mat4), &lightMVP);
    vkCmdDraw(cmd, m_islandMesh.count, 1, 0, 0);
}

void VulkanContext::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkCheck(vkBeginCommandBuffer(cmd, &begin),
        "Failed to begin frame command buffer");

#ifdef PASTEL_DEV_BUILD
    if (m_devTimingSupported) {
        const uint32_t base = m_currentFrame * DEV_TIMESTAMP_COUNT;
        vkCmdResetQueryPool(cmd, m_devQueryPool, base, DEV_TIMESTAMP_COUNT);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_devQueryPool, base);
    }
#endif

    // In menus/loading the ocean is not drawn, so skip the whole FFT sim (spectrum + IFFT +
    // assemble, ~60 dispatches) on those screens. The gate matches the ocean draw condition
    // below, so a drawn ocean always has its displacement computed this same frame.
    const bool worldVisible = !(m_mainMenuHud || m_settingsHud || m_loadingHud);

    // FFT ocean simulation (compute) — animate the wave spectrum for this frame before
    // the render passes. Runs on the graphics queue; barriers order it ahead of sampling.
    if (worldVisible)
        recordOceanFFT(cmd);

    // Shadow pass — render the scene depth into each cascade layer from the sun's view.
    for (uint32_t cascade = 0; cascade < CSM_CASCADES; cascade++) {
        const glm::mat4& lightMVP = m_lightMVPCascade[cascade];

        VkClearValue shadowClear{};
        shadowClear.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo shadowRp{};
        shadowRp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowRp.renderPass      = m_shadowRenderPass;
        shadowRp.framebuffer     = m_shadowFramebuffers[cascade];
        shadowRp.renderArea      = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
        shadowRp.clearValueCount = 1;
        shadowRp.pClearValues    = &shadowClear;

        // Always begin/end so each cascade layer is cleared (depth=1.0 → fully lit) and
        // transitioned to READ_ONLY_OPTIMAL. At night skip the geometry: the fragment
        // shaders gate shadow sampling on dayFactor > 0.01 anyway.
        vkCmdBeginRenderPass(cmd, &shadowRp, VK_SUBPASS_CONTENTS_INLINE);
        if (m_dayFactor > 0.01f) {
            // Ship casts a shadow — push lightMVP * shipModel so the tilted hull casts a
            // correct shadow into this cascade.
            if (m_shipMesh.count > 0) {
                glm::mat4 shipLightMVP = lightMVP * m_shipModel;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
                vkCmdPushConstants(cmd, m_shadowPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &shipLightMVP);
                VkBuffer     sBufs[] = { m_shipMesh.vbuf };
                VkDeviceSize sOffs[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, sBufs, sOffs);
                vkCmdDraw(cmd, m_shipMesh.count, 1, 0, 0);
            }
            drawPortShadows(cmd, lightMVP);
            drawIslandShadows(cmd, lightMVP);
        }
        vkCmdEndRenderPass(cmd);
    }
#ifdef PASTEL_DEV_BUILD
    writeDevTimestamp(cmd, 1);
#endif

    // Planar reflection pass — render the scene once from a camera mirrored across the
    // water plane. The ocean shader samples this color target with projected UVs.
    if (worldVisible && !m_reflectionFramebuffers.empty()) {
        VkClearValue reflClear[2];
        reflClear[0].color        = {{m_skyColor[0], m_skyColor[1], m_skyColor[2], 1.0f}};
        reflClear[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo reflRp{};
        reflRp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        reflRp.renderPass      = m_renderPass;
        reflRp.framebuffer     = m_reflectionFramebuffers[m_currentFrame];
        reflRp.renderArea      = {{0, 0}, m_swapchainExtent};
        reflRp.clearValueCount = 2;
        reflRp.pClearValues    = reflClear;

        vkCmdBeginRenderPass(cmd, &reflRp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport reflViewport{ 0.0f, 0.0f, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
        VkRect2D   reflScissor{ {0, 0}, m_swapchainExtent };
        vkCmdSetViewport(cmd, 0, 1, &reflViewport);
        vkCmdSetScissor(cmd, 0, 1, &reflScissor);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout, 0, 1, &m_reflectionDescriptorSets[m_currentFrame], 0, nullptr);

        // Mirrored geometry only when the planar contribution is enabled
        // (mode 2/3). The pass itself always runs: the clear is the sky color,
        // which is exactly the "planar off" content, and it keeps the image's
        // layout cycle intact.
        if (m_reflectionModeHud >= 2 && m_shipMesh.count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shipPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_shipPipelineLayout, 0, 1, &m_reflectionDescriptorSets[m_currentFrame], 0, nullptr);
            vkCmdPushConstants(cmd, m_shipPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(glm::mat4), &m_shipModel);
            VkBuffer     sBufs[] = { m_shipMesh.vbuf };
            VkDeviceSize sOffs[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, sBufs, sOffs);
            vkCmdDraw(cmd, m_shipMesh.count, 1, 0, 0);
        }
        if (m_reflectionModeHud >= 2) {
            drawPortInstances(cmd, m_reflectionDescriptorSets[m_currentFrame]);
            drawIslandMesh(cmd, m_reflectionDescriptorSets[m_currentFrame]);
        }

        vkCmdEndRenderPass(cmd);
    }

    VkClearValue clearValues[2];
    clearValues[0].color        = {{m_skyColor[0], m_skyColor[1], m_skyColor[2], 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass      = m_renderPass;
    rp.framebuffer     = m_sceneFramebuffers[m_currentFrame]; // render into offscreen color
    rp.renderArea      = {{0, 0}, m_swapchainExtent};
    rp.clearValueCount = 2;
    rp.pClearValues    = clearValues;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Dynamic viewport/scissor — track the current swapchain extent (handles window resize)
    VkViewport viewport{ 0.0f, 0.0f, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
    VkRect2D   scissor{ {0, 0}, m_swapchainExtent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (worldVisible) {
        drawPortInstances(cmd, m_descriptorSets[m_currentFrame]);
        drawIslandMesh(cmd, m_descriptorSets[m_currentFrame]);
    }

    // Refraction/depth seed for water. The ship is drawn again after the ocean for final
    // visibility, but including it in the pre-water buffers gives the water shader real
    // scene color/depth to refract around the hull instead of sampling only empty sky.
    if (worldVisible && m_shipMesh.count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shipPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_shipPipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);
        vkCmdPushConstants(cmd, m_shipPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(glm::mat4), &m_shipModel);
        VkBuffer     sBufs[] = { m_shipMesh.vbuf };
        VkDeviceSize sOffs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, sBufs, sOffs);
        vkCmdDraw(cmd, m_shipMesh.count, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd); // end pre-water opaque pass
    copySceneColorForWater(cmd);
    copySceneDepthForWater(cmd);

    VkRenderPassBeginInfo waterRp = rp;
    waterRp.renderPass      = m_sceneLoadRenderPass;
    waterRp.framebuffer     = m_sceneLoadFramebuffers[m_currentFrame];
    waterRp.clearValueCount = 0;
    waterRp.pClearValues    = nullptr;
    vkCmdBeginRenderPass(cmd, &waterRp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Water + late dynamic entities. Keeping this separate from the opaque pass creates the
    // production path for sampled scene depth while preserving the current ship-after-water
    // occlusion order.

    // Ocean surface — Gerstner-wave grid that follows the camera. Opaque + depth-tested
    // so the ship and (future) islands occlude it correctly.
    if (worldVisible && m_oceanIndexCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_oceanPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_oceanPipelineLayout, 0, 1, &m_oceanDescriptorSets[m_currentFrame], 0, nullptr);
        VkBuffer     oBufs[] = { m_oceanVertexBuffer };
        VkDeviceSize oOffs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, oBufs, oOffs);
        vkCmdBindIndexBuffer(cmd, m_oceanIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_oceanIndexCount, 1, 0, 0, 0);
    }

    // Ship (placeholder) — replaces the player cube. Drawn with the rotation-capable
    // object pipeline so the bow faces the player heading; the per-frame instance seats
    // it on the sea surface. Drawn last in the scene pass; switches the bound pipeline.
    if (worldVisible && m_shipMesh.count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shipPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_shipPipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);
        vkCmdPushConstants(cmd, m_shipPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(glm::mat4), &m_shipModel);
        VkBuffer     sBufs[] = { m_shipMesh.vbuf };
        VkDeviceSize sOffs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, sBufs, sOffs);
        vkCmdDraw(cmd, m_shipMesh.count, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd); // end scene pass (offscreen color now SHADER_READ_ONLY)

    if (worldVisible && m_lighthouseBeamPipeline != VK_NULL_HANDLE) {
        VkRenderPassBeginInfo beamRp = waterRp;
        vkCmdBeginRenderPass(cmd, &beamRp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        PostPushConstants beamPc{};
        beamPc.params = glm::vec4(
            1.0f / (float)m_swapchainExtent.width,
            1.0f / (float)m_swapchainExtent.height,
            0.0f,
            0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lighthouseBeamPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_postPipelineLayout, 0, 1, &m_postDescriptorSets[m_currentFrame], 0, nullptr);
        vkCmdPushConstants(cmd, m_postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PostPushConstants), &beamPc);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }
#ifdef PASTEL_DEV_BUILD
    writeDevTimestamp(cmd, 2);
#endif

    // TAA resolve (aaMode 3) — blend the HDR scene against the reprojected history
    // before tone mapping. Output [m_currentFrame] feeds the post pass this frame
    // and is read back as history by the other frame index next frame.
    if (m_aaModeHud == 3) {
        // Final (post-water) depth is needed for reprojection; move it to a
        // sampleable layout. Next frame's scene pass starts from UNDEFINED, so
        // no transition back is required.
        VkImageMemoryBarrier depthToRead{};
        depthToRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthToRead.srcAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        depthToRead.oldLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthToRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToRead.image               = m_depthImage;
        depthToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthToRead.subresourceRange.levelCount = 1;
        depthToRead.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthToRead);

        TaaPushConstants taaPc{};
        taaPc.reprojection = m_taaReprojection;
        taaPc.params = glm::vec4(
            1.0f / (float)m_swapchainExtent.width,
            1.0f / (float)m_swapchainExtent.height,
            (m_taaHistoryFrames > 0) ? 1.0f : 0.0f,
            0.0f);

        VkRenderPassBeginInfo taaRp{};
        taaRp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        taaRp.renderPass      = m_taaRenderPass;
        taaRp.framebuffer     = m_taaFramebuffers[m_currentFrame];
        taaRp.renderArea      = {{0, 0}, m_swapchainExtent};
        vkCmdBeginRenderPass(cmd, &taaRp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport tvp{ 0.0f, 0.0f, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
        VkRect2D   tsc{ {0, 0}, m_swapchainExtent };
        vkCmdSetViewport(cmd, 0, 1, &tvp);
        vkCmdSetScissor(cmd, 0, 1, &tsc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_taaPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_taaPipelineLayout, 0, 1, &m_taaDescriptorSets[m_currentFrame], 0, nullptr);
        vkCmdPushConstants(cmd, m_taaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(TaaPushConstants), &taaPc);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }

    PostPushConstants postPc{};
    postPc.params = glm::vec4(
        1.0f / (float)m_swapchainExtent.width,
        1.0f / (float)m_swapchainExtent.height,
        (float)m_aaModeHud,
        0.0f
    );

    if (m_aaModeHud == 2) {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

        auto recordSmaaPass = [&](VkFramebuffer framebuffer,
                                  VkPipeline pipeline,
                                  VkPipelineLayout layout,
                                  VkDescriptorSet descriptorSet)
        {
            VkRenderPassBeginInfo rp{};
            rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp.renderPass      = m_smaaRenderPass;
            rp.framebuffer     = framebuffer;
            rp.renderArea      = {{0, 0}, m_swapchainExtent};
            rp.clearValueCount = 1;
            rp.pClearValues    = &clear;
            vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp{ 0.0f, 0.0f, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
            VkRect2D sc{ {0, 0}, m_swapchainExtent };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                layout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PostPushConstants), &postPc);
            vkCmdDraw(cmd, 3, 1, 0, 0);

            vkCmdEndRenderPass(cmd);
        };

        // Tone map + grade the HDR scene into the LDR target first; SMAA then
        // operates on perceptual LDR (standard order) instead of raw HDR.
        {
            VkRenderPassBeginInfo ldrRp{};
            ldrRp.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            ldrRp.renderPass  = m_postLdrRenderPass;
            ldrRp.framebuffer = m_ldrFramebuffers[m_currentFrame];
            ldrRp.renderArea  = {{0, 0}, m_swapchainExtent};
            vkCmdBeginRenderPass(cmd, &ldrRp, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp{ 0.0f, 0.0f, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
            VkRect2D sc{ {0, 0}, m_swapchainExtent };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postLdrPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_postPipelineLayout, 0, 1, &m_postDescriptorSets[m_currentFrame], 0, nullptr);
            vkCmdPushConstants(cmd, m_postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PostPushConstants), &postPc);
            vkCmdDraw(cmd, 3, 1, 0, 0);

            vkCmdEndRenderPass(cmd);
        }

        recordSmaaPass(m_smaaEdgeFramebuffers[m_currentFrame],
            m_smaaEdgePipeline, m_smaaEdgePipelineLayout, m_smaaEdgeDescriptorSets[m_currentFrame]);
        recordSmaaPass(m_smaaBlendFramebuffers[m_currentFrame],
            m_smaaBlendPipeline, m_smaaBlendPipelineLayout, m_smaaBlendDescriptorSets[m_currentFrame]);
    }

    // Post-process pass — sample offscreen scene, output to swapchain
    {
        VkRenderPassBeginInfo postRp{};
        postRp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        postRp.renderPass      = m_postRenderPass;
        postRp.framebuffer     = m_postFramebuffers[imageIndex];
        postRp.renderArea      = {{0, 0}, m_swapchainExtent};
        postRp.clearValueCount = 0;
        vkCmdBeginRenderPass(cmd, &postRp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport pvp{ 0.0f, 0.0f, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
        VkRect2D   psc{ {0, 0}, m_swapchainExtent };
        vkCmdSetViewport(cmd, 0, 1, &pvp);
        vkCmdSetScissor(cmd, 0, 1, &psc);

        VkPipeline postPipeline = m_postPipeline;
        VkPipelineLayout postLayout = m_postPipelineLayout;
        VkDescriptorSet postDescriptorSet = m_postDescriptorSets[m_currentFrame];
        if (m_aaModeHud == 2) {
            postPipeline = m_smaaNeighborhoodPipeline;
            postLayout = m_smaaNeighborhoodPipelineLayout;
            postDescriptorSet = m_smaaNeighborhoodDescriptorSets[m_currentFrame];
        } else if (m_aaModeHud == 3) {
            postDescriptorSet = m_postTaaDescriptorSets[m_currentFrame]; // tone-map the TAA resolve
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            postLayout, 0, 1, &postDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, postLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PostPushConstants), &postPc);
        vkCmdDraw(cmd, 3, 1, 0, 0); // fullscreen triangle

        // UI overlay draws after post AA so pixel text remains crisp.
        if (m_uiVertexCount > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipeline);
            VkBuffer     uiBufs[] = { m_uiBuffer[m_currentFrame] };
            VkDeviceSize uiOffs[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, uiBufs, uiOffs);
            vkCmdDraw(cmd, m_uiVertexCount, 1, 0, 0);
        }
#ifdef PASTEL_DEV_BUILD
        writeDevTimestamp(cmd, 3);
        if (ImGui::GetCurrentContext()) {
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData && drawData->CmdListsCount > 0)
                ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
        }
        writeDevTimestamp(cmd, 4);
#endif

        vkCmdEndRenderPass(cmd);
    }

    vkCheck(vkEndCommandBuffer(cmd),
        "Failed to end frame command buffer");
}

// ============================================================
//  drawFrame
// ============================================================
void VulkanContext::drawFrame(const FrameRenderData& frame) {
    m_mainMenuHud      = frame.mainMenu;
    m_settingsHud      = frame.settings;
    m_loadingHud       = frame.loading;
    m_pausedHud        = frame.paused;
    m_vsyncHud         = frame.vsyncEnabled;
    m_aaModeHud        = frame.aaMode;
    m_reflectionModeHud = frame.reflectionMode;
    m_shipSpeedHud     = glm::length(glm::vec2(frame.shipVelocity.x, frame.shipVelocity.y));
    m_shipHeadingHud   = frame.shipHeading;
    m_shipThrottleHud  = frame.shipThrottle;
    m_shipRudderHud    = frame.shipRudder;
    m_portDistanceHud  = frame.portDistance;
    m_portDirHud       = frame.portDir;
    m_nearPortHud      = frame.nearPort;
    m_cargoUsedHud     = frame.cargoUsed;
    m_cargoCapHud      = frame.cargoCapacity;
    m_moneyHud         = frame.money;
    m_canDockHud       = frame.canDock;
    m_dockedHud        = frame.docked;
    m_portNameHud      = frame.portName;
    m_marketOpenHud    = frame.marketOpen;
    m_marketSelHud     = frame.marketSelected;
    m_nearestPortNameHud = frame.nearestPortName;
    m_windDirHud       = frame.windDir;
    m_windSpeedHud     = frame.windSpeed;
    m_routePortNameHud = frame.routePortName;
    m_routeDistanceHud = frame.routeDistance;
    m_routeDirHud      = frame.routeDir;
    m_routeArrivedHud  = frame.routeArrived;
    m_portTypeNameHud  = frame.portTypeName;
    m_marketRowsHudCount = frame.marketRows
        ? std::min(frame.marketRowCount, (int)m_marketRowsHud.size()) : 0;
    for (int i = 0; i < m_marketRowsHudCount; i++)
        m_marketRowsHud[(size_t)i] = frame.marketRows[i];
    m_portInstanceCount = frame.portInstances
        ? std::min(frame.portInstanceCount, (int)m_portInstances.size()) : 0;
    for (int i = 0; i < m_portInstanceCount; i++)
        m_portInstances[(size_t)i] = frame.portInstances[i];

    if (frame.vsyncEnabled != m_vsyncEnabled) {
        m_vsyncEnabled = frame.vsyncEnabled;
#ifdef PASTEL_DEV_BUILD
        if (m_devFrameStarted && ImGui::GetCurrentContext()) {
            ImGui::EndFrame();
            m_devFrameStarted = false;
        }
#endif
        recreateSwapchain();
    }

    // Advance frame counter and free buffers that are no longer in flight
    m_frameCount++;
    // Erase entries the GPU has finished with; GpuBuffer RAII frees them on erase.
    m_deletionQueue.erase(
        std::remove_if(m_deletionQueue.begin(), m_deletionQueue.end(),
            [&](const DeferredDelete& d) {
                return m_frameCount - d.frame > (uint64_t)MAX_FRAMES_IN_FLIGHT;
            }),
        m_deletionQueue.end()
    );

    // Sky color: 4 keyframes keyed on timeOfDay (0=midnight, 0.25=dawn, 0.5=noon, 0.75=dusk)
    static constexpr float kSkyKeys[4][3] = {
        {0.05f, 0.05f, 0.12f}, // midnight
        {0.85f, 0.55f, 0.35f}, // dawn
        {0.45f, 0.72f, 0.95f}, // noon
        {0.80f, 0.40f, 0.20f}, // dusk
    };
    const float t4  = frame.timeOfDay * 4.0f;
    const int   seg = static_cast<int>(t4) % 4;
    const float f   = t4 - static_cast<int>(t4);
    const int   next = (seg + 1) % 4;
    for (int i = 0; i < 3; ++i)
        m_skyColor[i] = kSkyKeys[seg][i] * (1.0f - f) + kSkyKeys[next][i] * f;

    // Wait for the previous frame using this slot to finish
    vkCheck(vkWaitForFences(m_device, 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX),
        "Failed to wait for in-flight frame fence");
#ifdef PASTEL_DEV_BUILD
    readDevGpuTimings(m_currentFrame);
#endif

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
#ifdef PASTEL_DEV_BUILD
        if (m_devFrameStarted && ImGui::GetCurrentContext()) {
            ImGui::EndFrame();
            m_devFrameStarted = false;
        }
#endif
        recreateSwapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    // If a previous frame is still using this image, wait on its fence first
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkCheck(vkWaitForFences(m_device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX),
            "Failed to wait for swapchain image fence");
    m_imagesInFlight[imageIndex] = m_inFlight[m_currentFrame];

    vkCheck(vkResetFences(m_device, 1, &m_inFlight[m_currentFrame]),
        "Failed to reset in-flight frame fence");

    // Sun direction + cascaded shadow map (CSM): fit one ortho box per view-frustum slice.
    {
        const float kSunAzimuth = glm::radians(225.0f); // rotate light direction in world (tune to taste)
        constexpr float kPi = 3.14159265f;
        constexpr float kTwoPi = 6.28318530f;
        float elevation = sinf(frame.timeOfDay * kTwoPi - kPi * 0.5f);
        float azimuth   = (frame.timeOfDay - 0.25f) * kTwoPi + kSunAzimuth;
        m_sunDir    = glm::normalize(glm::vec3(cosf(azimuth), sinf(azimuth), elevation));
        m_dayFactor = glm::clamp(elevation, 0.0f, 1.0f); // 0 at night, 1 at noon
        m_shadowCenter = frame.shipPosition;

        // Practical split scheme (log + uniform blend) over the shadowed view-depth range.
        // Beyond shadowFar the receivers stay lit (imperceptible at the low sailing view).
        const float shadowNear = 0.5f;
        const float shadowFar  = 400.0f;
        const float lambda     = 0.7f;
        float splitFar[CSM_CASCADES];
        for (uint32_t i = 0; i < CSM_CASCADES; i++) {
            float p    = float(i + 1) / float(CSM_CASCADES);
            float logd = shadowNear * std::pow(shadowFar / shadowNear, p);
            float unid = shadowNear + (shadowFar - shadowNear) * p;
            splitFar[i] = lambda * logd + (1.0f - lambda) * unid;
        }
        m_cascadeSplits = glm::vec4(splitFar[0], splitFar[1], splitFar[2], 0.0f);

        // Per-cascade fit: bounding sphere of the frustum slice (rotation-invariant → no
        // shimmer) + whole-texel snapping of the sphere centre.
        const glm::mat4 invCamView = glm::inverse(frame.camera.view());
        const float tanHalfV = std::tan(glm::radians(frame.camera.fov()) * 0.5f);
        const float tanHalfH = tanHalfV * frame.camera.aspect();
        const glm::vec3 up(0.0f, 0.0f, 1.0f); // sun elevation keeps |sunDir.z| <= ~0.71, never parallel
        const float casterMargin = 120.0f;    // pull the light back to catch tall casters (ship masts)

        for (uint32_t c = 0; c < CSM_CASCADES; c++) {
            float nearD = (c == 0) ? shadowNear : splitFar[c - 1];
            float farD  = splitFar[c];

            // 8 slice corners in world space (view looks down -z).
            glm::vec3 center(0.0f);
            glm::vec3 corners[8];
            int idx = 0;
            for (int s = 0; s < 2; s++) {
                float d = (s == 0) ? nearD : farD;
                float hh = d * tanHalfV, hw = d * tanHalfH;
                for (int cy = -1; cy <= 1; cy += 2)
                for (int cx = -1; cx <= 1; cx += 2) {
                    glm::vec4 wp = invCamView * glm::vec4(cx * hw, cy * hh, -d, 1.0f);
                    corners[idx] = glm::vec3(wp);
                    center += corners[idx];
                    idx++;
                }
            }
            center /= 8.0f;

            float radius = 0.0f;
            for (int i = 0; i < 8; i++) {
                float len = glm::length(corners[i] - center);
                if (len > radius) radius = len;
            }
            radius = std::ceil(radius * 16.0f) / 16.0f; // stabilize the extent

            glm::vec3 eye = center + m_sunDir * (radius + casterMargin);
            glm::mat4 lightView = glm::lookAt(eye, center, up);
            glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius,
                                             0.0f, 2.0f * radius + casterMargin + 1.0f);
            lightProj[1][1] *= -1.0f;

            // Snap the sphere centre to whole shadow texels (kills translational shimmer).
            glm::vec4 centerLS     = (lightProj * lightView) * glm::vec4(center, 1.0f);
            float     texelsPerNdc = (float)SHADOW_MAP_SIZE * 0.5f;
            glm::vec2 inTexels     = glm::vec2(centerLS) * texelsPerNdc;
            glm::vec2 snapOffset   = (glm::round(inTexels) - inTexels) / texelsPerNdc;
            lightProj[3][0] += snapOffset.x;
            lightProj[3][1] += snapOffset.y;

            m_lightMVPCascade[c] = lightProj * lightView;
        }
    }

    m_oceanTime = frame.gameTime;
    m_oceanWakeShipPosition = glm::vec2(frame.shipPosition.x, frame.shipPosition.y);
    m_oceanWakeShipVelocity = glm::vec2(frame.shipVelocity.x, frame.shipVelocity.y);
    m_oceanWakeShipHeading  = frame.shipHeading;
    m_oceanWakeDeltaTime    = m_oceanWakeHasPrevTime ? frame.gameTime - m_oceanWakePrevTime : 0.0f;
    if (m_oceanWakeDeltaTime < 0.0f || m_oceanWakeDeltaTime > 0.1f)
        m_oceanWakeDeltaTime = 0.0f;
    m_oceanWakePrevTime = frame.gameTime;
    m_oceanWakeHasPrevTime = true;
    updateUniformBuffer(m_currentFrame, frame.camera, frame.gameTime);
    updateReflectionUniformBuffer(m_currentFrame, frame.camera, frame.gameTime);
    updateOceanHistoryDescriptor(m_currentFrame);
    updateShipTransform(frame.shipPosition, frame.shipHeading, frame.gameTime);
    updateHotbar();
#ifdef PASTEL_DEV_BUILD
    buildDevUi(frame);
#endif
    vkCheck(vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0),
        "Failed to reset frame command buffer");
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);
#ifdef PASTEL_DEV_BUILD
    m_devQueriesWritten[m_currentFrame] = m_devTimingSupported;
#endif

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &m_imageAvailable[m_currentFrame];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &m_commandBuffers[m_currentFrame];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &m_renderFinished[imageIndex];
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submit, m_inFlight[m_currentFrame]),
        "Failed to submit frame command buffer");

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &m_renderFinished[imageIndex];
    present.swapchainCount     = 1;
    present.pSwapchains        = &m_swapchain;
    present.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(m_presentQueue, &present);
    if (result != VK_SUCCESS &&
        result != VK_SUBOPTIMAL_KHR &&
        result != VK_ERROR_OUT_OF_DATE_KHR) {
        throw std::runtime_error("Failed to present swapchain image");
    }
    bool swapchainRecreated = false;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window.wasResized()) {
        m_window.resetResized();
        recreateSwapchain();
        swapchainRecreated = true;
    }

    if (!swapchainRecreated && m_temporalHistoryFrames < MAX_FRAMES_IN_FLIGHT)
        m_temporalHistoryFrames++;
    // TAA history is only valid while consecutive frames keep resolving in mode 3;
    // any break (mode switch, resize) restarts accumulation from the current frame.
    if (!swapchainRecreated && m_aaModeHud == 3) {
        if (m_taaHistoryFrames < MAX_FRAMES_IN_FLIGHT) m_taaHistoryFrames++;
    } else {
        m_taaHistoryFrames = 0;
    }
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================
//  Per-frame update functions
// ============================================================
namespace {
constexpr float WATER_REFLECTION_PLANE_Z = (float)SHARED_SEA_LEVEL; // mirror across the rest water plane

glm::vec3 reflectPointAcrossWater(const glm::vec3& p) {
    return { p.x, p.y, 2.0f * WATER_REFLECTION_PLANE_Z - p.z };
}

glm::vec3 reflectVectorAcrossWater(const glm::vec3& v) {
    return { v.x, v.y, -v.z };
}

glm::mat4 planarReflectionView(const Camera& camera, glm::vec3& outCameraPos) {
    glm::mat4 invView = glm::inverse(camera.view());
    glm::vec3 camPos  = glm::vec3(invView[3]);
    glm::vec3 forward = -glm::vec3(invView[2]);
    glm::vec3 up      =  glm::vec3(invView[1]);

    outCameraPos = reflectPointAcrossWater(camPos);
    glm::vec3 reflectedForward = glm::normalize(reflectVectorAcrossWater(forward));
    glm::vec3 reflectedUp      = glm::normalize(reflectVectorAcrossWater(up));
    return glm::lookAt(outCameraPos, outCameraPos + reflectedForward, reflectedUp);
}

glm::vec3 transformPortLocalPoint(const PortRenderInstance& port, glm::vec3 local) {
    const float scale = std::max(port.scale, 0.01f);
    const float c = std::cos(port.heading);
    const float s = std::sin(port.heading);
    glm::vec2 xy = glm::vec2(local.x, local.y) * scale;
    glm::vec2 rotated{xy.x * c - xy.y * s, xy.x * s + xy.y * c};
    return {port.position.x + rotated.x, port.position.y + rotated.y, local.z * scale};
}

glm::vec3 transformPortLocalDirection(const PortRenderInstance& port, glm::vec3 local) {
    const float c = std::cos(port.heading);
    const float s = std::sin(port.heading);
    glm::vec2 rotated{local.x * c - local.y * s, local.x * s + local.y * c};
    return glm::normalize(glm::vec3(rotated.x, rotated.y, local.z));
}

void populatePortLighting(UniformBufferObject& ubo,
                          int portInstanceCount,
                          const std::array<PortRenderInstance, 16>& portInstances,
                          float gameTime) {
    for (uint32_t i = 0; i < SHARED_LOCAL_LIGHT_COUNT; i++) {
        ubo.localLightPosRadius[i] = glm::vec4(0.0f);
        ubo.localLightColorIntensity[i] = glm::vec4(0.0f);
    }
    for (uint32_t i = 0; i < SHARED_SPOT_LIGHT_COUNT; i++) {
        ubo.spotLightPosRadius[i] = glm::vec4(0.0f);
        ubo.spotLightDirAngle[i] = glm::vec4(0.0f);
        ubo.spotLightColorIntensity[i] = glm::vec4(0.0f);
    }

    uint32_t lightCount = 0;
    auto pushLight = [&](glm::vec3 position, float radius, glm::vec3 color, float intensity) {
        if (lightCount >= SHARED_LOCAL_LIGHT_COUNT) return;
        ubo.localLightPosRadius[lightCount] = glm::vec4(position, radius);
        ubo.localLightColorIntensity[lightCount] = glm::vec4(color, intensity);
        lightCount++;
    };

    uint32_t spotCount = 0;
    auto pushSpot = [&](glm::vec3 position, glm::vec3 direction, float radius,
                        float cosOuterCone, glm::vec3 color, float intensity) {
        if (spotCount >= SHARED_SPOT_LIGHT_COUNT) return;
        ubo.spotLightPosRadius[spotCount] = glm::vec4(position, radius);
        ubo.spotLightDirAngle[spotCount] = glm::vec4(direction, cosOuterCone);
        ubo.spotLightColorIntensity[spotCount] = glm::vec4(color, intensity);
        spotCount++;
    };

    constexpr float kSpotCosOuter = 0.9599f; // cos(16.25 deg)
    for (int i = 0; i < portInstanceCount; i++) {
        const PortRenderInstance& port = portInstances[(size_t)i];
        const float scale = std::max(port.scale, 0.01f);
        const glm::vec3 lanternPos = transformPortLocalPoint(port, {38.0f, 8.0f, 21.6f});
        pushLight(lanternPos,
                  180.0f * scale, {1.0f, 0.70f, 0.32f}, 4.8f);
        pushLight(transformPortLocalPoint(port, {-5.0f, -34.0f, 4.2f}),
                  85.0f * scale, {1.0f, 0.56f, 0.24f}, 2.1f);
        const float sweep = gameTime * 0.42f + (float)i * 1.73f;
        glm::vec3 localDir{std::cos(sweep), std::sin(sweep), -0.082f};
        pushSpot(lanternPos, transformPortLocalDirection(port, localDir),
                 520.0f * scale, kSpotCosOuter, {1.0f, 0.68f, 0.30f}, 7.4f);
    }
}

// Island waterline ellipses for the ocean shader (shore tint + shoreline
// foam). Zero radius marks an unused slot, mirroring the light arrays.
void populateIslandData(UniformBufferObject& ubo,
                        int islandCount,
                        const std::array<glm::vec4, SHARED_ISLAND_COUNT>& posRadius,
                        const std::array<glm::vec4, SHARED_ISLAND_COUNT>& rotation) {
    for (uint32_t i = 0; i < SHARED_ISLAND_COUNT; i++) {
        const bool used = i < (uint32_t)islandCount;
        ubo.islandPosRadius[i] = used ? posRadius[i] : glm::vec4(0.0f);
        ubo.islandRotation[i]  = used ? rotation[i]  : glm::vec4(0.0f);
    }
}
} // namespace

void VulkanContext::updateUniformBuffer(uint32_t currentFrame, const Camera& camera, float gameTime) {
    UniformBufferObject ubo{};
    glm::vec3 reflectionCameraPos;
    glm::mat4 reflectionView = planarReflectionView(camera, reflectionCameraPos);

    ubo.model    = glm::mat4(1.0f);
    ubo.view     = camera.view();
    ubo.proj     = camera.proj();
    ubo.lightDir = glm::vec4(m_sunDir, m_dayFactor); // w = dayFactor (0=night, 1=noon)
    ubo.lightMVP = m_lightMVPCascade[0]; // legacy field kept for layout; holds cascade 0
    for (uint32_t c = 0; c < CSM_CASCADES; c++) ubo.lightMVPCascade[c] = m_lightMVPCascade[c];
    ubo.cascadeSplits = m_cascadeSplits;
    ubo.fogColor = glm::vec4(m_skyColor[0], m_skyColor[1], m_skyColor[2], 1.0f);
    ubo.clipPlane = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    ubo.animationParams = glm::vec4(gameTime, 0.0f, 0.0f, 0.0f);
    ubo.cameraPos       = glm::vec4(camera.position(), 1.0f);
    ubo.reflectionViewProj = camera.proj() * reflectionView;
    ubo.invViewProj = glm::inverse(ubo.proj * ubo.view);
    glm::mat4 currentViewProj = ubo.proj * ubo.view;
    ubo.prevViewProj = (m_temporalHistoryFrames > 0) ? m_prevViewProj : currentViewProj;
    ubo.temporalParams = glm::vec4(m_temporalHistoryFrames > 0 ? 1.0f : 0.0f,
                                   (float)m_reflectionModeHud, 0.0f, 0.0f);
    populatePortLighting(ubo, m_portInstanceCount, m_portInstances, gameTime);
    populateIslandData(ubo, m_islandCount, m_islandPosRadius, m_islandRotation);
    // TAA reprojection: current NDC -> previous clip, shared with the SSR history matrices.
    m_taaReprojection = ubo.prevViewProj * ubo.invViewProj;
    m_prevViewProj = currentViewProj;
    memcpy(m_uniformBuffers[currentFrame].mapped, &ubo, sizeof(ubo));
}

void VulkanContext::updateReflectionUniformBuffer(uint32_t currentFrame, const Camera& camera, float gameTime) {
    UniformBufferObject ubo{};
    glm::vec3 reflectionCameraPos;
    glm::mat4 reflectionView = planarReflectionView(camera, reflectionCameraPos);

    ubo.model    = glm::mat4(1.0f);
    ubo.view     = reflectionView;
    ubo.proj     = camera.proj();
    ubo.lightDir = glm::vec4(m_sunDir, m_dayFactor);
    ubo.lightMVP = m_lightMVPCascade[0]; // legacy field kept for layout; holds cascade 0
    for (uint32_t c = 0; c < CSM_CASCADES; c++) ubo.lightMVPCascade[c] = m_lightMVPCascade[c];
    ubo.cascadeSplits = m_cascadeSplits;
    ubo.fogColor = glm::vec4(m_skyColor[0], m_skyColor[1], m_skyColor[2], 1.0f);
    ubo.clipPlane = glm::vec4(0.0f, 0.0f, 1.0f, -WATER_REFLECTION_PLANE_Z);
    ubo.animationParams = glm::vec4(gameTime, 0.0f, 0.0f, 0.0f);
    ubo.cameraPos       = glm::vec4(reflectionCameraPos, 1.0f);
    ubo.reflectionViewProj = camera.proj() * reflectionView;
    ubo.invViewProj = glm::inverse(ubo.proj * ubo.view);
    ubo.prevViewProj = ubo.proj * ubo.view;
    ubo.temporalParams = glm::vec4(0.0f);
    populatePortLighting(ubo, m_portInstanceCount, m_portInstances, gameTime);
    populateIslandData(ubo, m_islandCount, m_islandPosRadius, m_islandRotation);
    memcpy(m_reflectionUniformBuffers[currentFrame].mapped, &ubo, sizeof(ubo));
}

void VulkanContext::updateShipTransform(const glm::vec3& position, float heading, float gameTime) {
    // Float the hero ship on the actual FFT surface: the buoyancy compute pass sampled
    // wave heights around the ship on the GPU (~2 frames ago, so the tiny readback is
    // already complete), then tilt the hull so its deck aligns with the surface normal
    // and the bow points toward the heading.
    (void)gameTime; // wave phase now lives entirely in the GPU FFT
    constexpr float SEA_LEVEL = (float)SHARED_SEA_LEVEL;
    const float step = OCEAN_CASCADE_L[OCEAN_CASCADES - 1] / (float)OCEAN_FFT_N; // finest cascade texel

    // 5 heights: center, -x, +x, -y, +y (see ocean_buoyancy.comp).
    const float* h = m_oceanBuoyancyBuffers.empty()
        ? nullptr : (const float*)m_oceanBuoyancyBuffers[m_currentFrame].mapped;

    float     height = SEA_LEVEL;
    glm::vec3 up(0.0f, 0.0f, 1.0f);
    if (h) {
        height = SEA_LEVEL + h[0];
        up = glm::normalize(glm::vec3(h[1] - h[2], h[3] - h[4], 2.0f * step));
    }

    glm::vec3 pos  = glm::vec3(position.x, position.y, height - SHIP_VISUAL_DRAFT);
    glm::vec3 fwd0 = glm::vec3(std::cos(heading), std::sin(heading), 0.0f);
    glm::vec3 left = glm::normalize(glm::cross(up, fwd0));  // ship +Y (port)
    glm::vec3 fwd  = glm::normalize(glm::cross(left, up));  // ship +X (bow), perpendicular to up

    glm::mat4 m(1.0f);
    m[0] = glm::vec4(fwd  * SHIP_WORLD_SCALE, 0.0f); // X column = bow
    m[1] = glm::vec4(left * SHIP_WORLD_SCALE, 0.0f); // Y column = port
    m[2] = glm::vec4(up   * SHIP_WORLD_SCALE, 0.0f); // Z column = deck up
    m[3] = glm::vec4(pos,  1.0f); // translation
    m_shipModel = m;
}

// ============================================================
//  Hotbar / inventory UI geometry (rebuilt each frame)
// ============================================================
void VulkanContext::updateHotbar() {
    const float W = (float)m_swapchainExtent.width;
    const float H = (float)m_swapchainExtent.height;

    std::vector<UIVertex> verts;
    verts.reserve(256);

    auto pushQuad = [&](float x, float y, float w, float h, glm::vec4 color) {
        auto toNDC = [&](float px, float py) {
            return glm::vec2(px / W * 2.0f - 1.0f, py / H * 2.0f - 1.0f);
        };
        glm::vec2 p0 = toNDC(x,     y);
        glm::vec2 p1 = toNDC(x + w, y);
        glm::vec2 p2 = toNDC(x + w, y + h);
        glm::vec2 p3 = toNDC(x,     y + h);
        verts.push_back({p0, color});
        verts.push_back({p1, color});
        verts.push_back({p2, color});
        verts.push_back({p0, color});
        verts.push_back({p2, color});
        verts.push_back({p3, color});
    };

    // Tiny 3x5 UI glyphs: digits first, then A-Z, then '/'. Row bits 4=left, 2=mid, 1=right.
    static const uint8_t GLYPHS[37][5] = {
        {7,5,5,5,7}, {2,2,2,2,2}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
        {7,5,7,5,5}, {6,5,6,5,6}, {7,4,4,4,7}, {6,5,5,5,6}, {7,4,6,4,7},
        {7,4,6,4,4}, {7,4,5,5,7}, {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,7},
        {5,5,6,5,5}, {4,4,4,4,7}, {5,7,7,5,5}, {5,7,7,7,5}, {7,5,5,5,7},
        {7,5,7,4,4}, {7,5,5,7,1}, {7,5,7,6,5}, {7,4,7,1,7}, {7,2,2,2,2},
        {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5}, {5,5,2,2,2},
        {7,1,2,4,7},
        {1,1,2,4,4}, // '/'
    };
    auto glyphIndex = [](char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
        if (ch == '/') return 36;
        return -1;
    };
    auto pushGlyph = [&](char ch, float ox, float oy, float px, glm::vec4 col) {
        int idx = glyphIndex(ch);
        if (idx < 0) return;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (GLYPHS[idx][r] & (1 << (2 - c)))
                    pushQuad(ox + c * px, oy + r * px, px, px, col);
    };
    auto pushText = [&](const char* text, float ox, float oy, float px, glm::vec4 col) {
        float cx = ox;
        for (const char* p = text; *p; ++p) {
            pushGlyph(*p, cx, oy, px, col);
            cx += 4.0f * px;
        }
    };
    auto textWidth = [](const char* text, float px) {
        int n = 0;
        for (const char* p = text; *p; ++p) ++n;
        return n > 0 ? n * 4.0f * px - px : 0.0f;
    };
    auto pushCenteredText = [&](const char* text, float y, float px, glm::vec4 col) {
        pushText(text, W * 0.5f - textWidth(text, px) * 0.5f, y, px, col);
    };
    auto pushNumber = [&](int value, float ox, float oy, float px, glm::vec4 col) {
        if (value < 0) value = 0;
        int digs[12]; int n = 0;
        if (value == 0) digs[n++] = 0;
        else for (int v = value; v > 0 && n < 12; v /= 10) digs[n++] = v % 10;
        float cx = ox;
        for (int i = n - 1; i >= 0; i--) {        // most significant first
            pushGlyph(char('0' + digs[i]), cx, oy, px, col);
            cx += 4.0f * px;
        }
    };

    if (m_mainMenuHud) {
        pushQuad(0.0f, 0.0f, W, H, {0.06f, 0.08f, 0.07f, 0.72f});
        pushCenteredText("OCEAN VOYAGE", H * 0.5f - 76.0f, 10.0f, {0.95f, 0.92f, 0.82f, 1.0f});
        const char* rows[] = { "START", "SETTINGS" };
        for (int i = 0; i < 2; ++i) {
            float rx, ry, rw, rh;
            mainMenuRowRect(i, W, H, rx, ry, rw, rh);
            pushQuad(rx, ry, rw, rh, {0.12f, 0.14f, 0.13f, 0.85f});
            pushCenteredText(rows[i], ry + 9.0f, 5.0f, {0.95f, 0.92f, 0.82f, 0.92f});
        }

        if (verts.size() > UI_MAX_VERTS) verts.resize(UI_MAX_VERTS); // guard against buffer overflow
        m_uiVertexCount = (uint32_t)verts.size();
        memcpy(m_uiBuffer[m_currentFrame].mapped, verts.data(), sizeof(UIVertex) * verts.size());
        return;
    }

    if (m_settingsHud) {
        pushQuad(0.0f, 0.0f, W, H, {0.06f, 0.08f, 0.07f, 0.72f});
        const char* aaText = "AA OFF";
        if (m_aaModeHud == 1) aaText = "AA FXAA";
        else if (m_aaModeHud == 2) aaText = "AA SMAA";
        else if (m_aaModeHud == 3) aaText = "AA TAA";

        const char* reflText = "REFL FULL";
        if (m_reflectionModeHud == 0) reflText = "REFL SKY";
        else if (m_reflectionModeHud == 1) reflText = "REFL SSR";
        else if (m_reflectionModeHud == 2) reflText = "REFL PLANAR";

        pushCenteredText("SETTINGS", H * 0.5f - 72.0f, 8.0f, {0.95f, 0.92f, 0.82f, 1.0f});
        const char* rows[] = { m_vsyncHud ? "VSYNC ON" : "VSYNC OFF", aaText, reflText, "BACK" };
        for (int i = 0; i < 4; ++i) {
            float rx, ry, rw, rh;
            settingsRowRect(i, W, H, rx, ry, rw, rh);
            pushQuad(rx, ry, rw, rh, {0.12f, 0.14f, 0.13f, 0.85f});
            pushCenteredText(rows[i], ry + 9.0f, 5.0f, {0.95f, 0.92f, 0.82f, 0.92f});
        }

        if (verts.size() > UI_MAX_VERTS) verts.resize(UI_MAX_VERTS); // guard against buffer overflow
        m_uiVertexCount = (uint32_t)verts.size();
        memcpy(m_uiBuffer[m_currentFrame].mapped, verts.data(), sizeof(UIVertex) * verts.size());
        return;
    }

    if (m_loadingHud) {
        pushQuad(0.0f, 0.0f, W, H, {0.06f, 0.08f, 0.07f, 0.72f});
        pushCenteredText("LOADING", H * 0.5f - 18.0f, 8.0f, {0.95f, 0.92f, 0.82f, 1.0f});

        if (verts.size() > UI_MAX_VERTS) verts.resize(UI_MAX_VERTS); // guard against buffer overflow
        m_uiVertexCount = (uint32_t)verts.size();
        memcpy(m_uiBuffer[m_currentFrame].mapped, verts.data(), sizeof(UIVertex) * verts.size());
        return;
    }

    // --- Ship HUD (speed / heading / throttle / rudder), top-left ---
    {
        const glm::vec4 col = {0.95f, 0.96f, 0.92f, 0.95f};
        const float gpx = 4.0f;                       // glyph scale
        const float lh  = 5.0f * gpx + 6.0f;          // line height
        const float lx  = 16.0f;                      // label x
        const float vx  = 16.0f + 4.0f * gpx * 4.0f;  // value x (after a 3-char label + gap)
        float hy = 16.0f;

        // Speed (rounded world units/s)
        pushText("SPD", lx, hy, gpx, col);
        pushNumber((int)(m_shipSpeedHud + 0.5f), vx, hy, gpx, col);
        hy += lh;

        // Heading (degrees, wrapped to 0..359)
        int hdg = (int)(m_shipHeadingHud * 57.2957795f + 0.5f); // rad -> deg
        hdg %= 360; if (hdg < 0) hdg += 360;
        pushText("HDG", lx, hy, gpx, col);
        pushNumber(hdg, vx, hy, gpx, col);
        hy += lh;

        // Throttle %. The glyph set has no minus, so the sign is shown as a
        // direction letter: F = forward, R = reverse.
        int thr = (int)(m_shipThrottleHud * 100.0f + (m_shipThrottleHud >= 0.0f ? 0.5f : -0.5f));
        pushText("THR", lx, hy, gpx, col);
        pushText(thr > 0 ? "F" : (thr < 0 ? "R" : ""), vx, hy, gpx, col);
        pushNumber(thr < 0 ? -thr : thr, vx + 4.0f * gpx, hy, gpx, col);
        hy += lh;

        // Rudder %. S = starboard (right), P = port (left).
        int rud = (int)(m_shipRudderHud * 100.0f + (m_shipRudderHud >= 0.0f ? 0.5f : -0.5f));
        pushText("RUD", lx, hy, gpx, col);
        pushText(rud > 0 ? "S" : (rud < 0 ? "P" : ""), vx, hy, gpx, col);
        pushNumber(rud < 0 ? -rud : rud, vx + 4.0f * gpx, hy, gpx, col);
        hy += lh;

        static const char* kCompass8[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        auto compassOctant = [](glm::vec2 dir) {
            const float bearing = std::atan2(dir.x, dir.y); // 0 = +Y (N), clockwise
            int octant = (int)std::floor(bearing * (4.0f / 3.14159265f) + 0.5f);
            return ((octant % 8) + 8) % 8;
        };

        // Nearest port: name + 8-way compass letters (+Y = north) + distance (m).
        if (m_portDistanceHud >= 0.0f) {
            char portLine[32];
            std::snprintf(portLine, sizeof(portLine), "%s %s %d",
                          m_nearestPortNameHud ? m_nearestPortNameHud : "",
                          kCompass8[compassOctant(m_portDirHud)], (int)(m_portDistanceHud + 0.5f));
            pushText("PRT", lx, hy, gpx, col);
            pushText(portLine, vx, hy, gpx, col);
            hy += lh;
        }

        // Wind: nautical convention — the compass point it blows FROM + m/s.
        if (m_windSpeedHud > 0.0f) {
            char windLine[16];
            std::snprintf(windLine, sizeof(windLine), "%s %d",
                          kCompass8[compassOctant(-m_windDirHud)],
                          (int)(m_windSpeedHud + 0.5f));
            pushText("WND", lx, hy, gpx, col);
            pushText(windLine, vx, hy, gpx, col);
            hy += lh;
        }

        // Route destination (T to cycle): name + bearing + distance, or HERE
        // once the ship is inside the destination radius.
        if (m_routeDistanceHud >= 0.0f) {
            const glm::vec4 routeCol = {0.65f, 0.88f, 0.95f, 0.95f};
            char routeLine[32];
            if (m_routeArrivedHud)
                std::snprintf(routeLine, sizeof(routeLine), "%s HERE",
                              m_routePortNameHud ? m_routePortNameHud : "");
            else
                std::snprintf(routeLine, sizeof(routeLine), "%s %s %d",
                              m_routePortNameHud ? m_routePortNameHud : "",
                              kCompass8[compassOctant(m_routeDirHud)],
                              (int)(m_routeDistanceHud + 0.5f));
            pushText("DST", lx, hy, gpx, routeCol);
            pushText(routeLine, vx, hy, gpx, routeCol);
            hy += lh;
        }

        // Cargo hold usage and money.
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d/%d", m_cargoUsedHud, m_cargoCapHud);
        pushText("CRG", lx, hy, gpx, col);
        pushText(buf, vx, hy, gpx, col);
        hy += lh;

        pushText("GLD", lx, hy, gpx, col);
        pushNumber(m_moneyHud, vx, hy, gpx, col);
        hy += lh;

        // Docking hint: actionable beats informational.
        if (!m_dockedHud && m_canDockHud)
            pushText("PRESS ENTER TO DOCK", lx, hy, gpx, {0.95f, 0.85f, 0.45f, 0.95f});
        else if (!m_dockedHud && m_nearPortHud)
            pushText("NEAR PORT", lx, hy, gpx, {0.95f, 0.85f, 0.45f, 0.95f});
    }

    // Port menu (docked): port name + actions. Drawn under the pause overlay so
    // pausing while docked still dims the menu. Hidden while the market is open.
    if (m_dockedHud && !m_marketOpenHud && !m_pausedHud) {
        // Title: "CARDIFF  COAL PORT" — name plus the port-type label.
        char title[48];
        std::snprintf(title, sizeof(title), "%s  %s",
                      m_portNameHud ? m_portNameHud : "PORT",
                      m_portTypeNameHud ? m_portTypeNameHud : "");
        pushCenteredText(title, H * 0.5f - 72.0f, 8.0f, {0.95f, 0.92f, 0.82f, 0.95f});
        const char* rows[] = { "SET SAIL", "TRADE" };
        for (int i = 0; i < 2; ++i) {
            float rx, ry, rw, rh;
            portMenuRowRect(i, W, H, rx, ry, rw, rh);
            pushQuad(rx, ry, rw, rh, {0.12f, 0.14f, 0.13f, 0.88f});
            pushCenteredText(rows[i], ry + 9.0f, 5.0f, {0.95f, 0.92f, 0.82f, 0.92f});
        }
    }

    // Market table (docked trade screen): GOOD | BUY | SELL | STK | HELD rows
    // with the selected row highlighted. Keys: Up/Down select, B buy, S sell.
    if (m_dockedHud && m_marketOpenHud && !m_pausedHud) {
        const float gpx   = 4.0f;
        const float rowH  = 5.0f * gpx + 12.0f;
        const float pad   = 18.0f;
        const float colGood = 0.0f, colBuy = 190.0f, colSell = 290.0f,
                    colStk = 390.0f, colHeld = 490.0f;
        const float panelW = colHeld + 90.0f + 2.0f * pad;
        const float panelH = 56.0f + rowH * (float)(m_marketRowsHudCount + 1) + 44.0f;
        const float px0 = (W - panelW) * 0.5f;
        const float py0 = (H - panelH) * 0.5f;
        const float tx0 = px0 + pad; // table left edge

        pushQuad(px0, py0, panelW, panelH, {0.07f, 0.09f, 0.08f, 0.90f});

        char title[40];
        std::snprintf(title, sizeof(title), "%s MARKET", m_portNameHud ? m_portNameHud : "PORT");
        pushCenteredText(title, py0 + 14.0f, 6.0f, {0.95f, 0.92f, 0.82f, 0.95f});

        float ry = py0 + 56.0f;
        const glm::vec4 headCol = {0.75f, 0.78f, 0.72f, 0.9f};
        pushText("GOOD", tx0 + colGood, ry, gpx, headCol);
        pushText("BUY",  tx0 + colBuy,  ry, gpx, headCol);
        pushText("SELL", tx0 + colSell, ry, gpx, headCol);
        pushText("STK",  tx0 + colStk,  ry, gpx, headCol);
        pushText("HELD", tx0 + colHeld, ry, gpx, headCol);
        ry += rowH;

        for (int i = 0; i < m_marketRowsHudCount; i++) {
            const MarketRowHud& row = m_marketRowsHud[(size_t)i];
            const bool selected = (i == m_marketSelHud);
            if (selected)
                pushQuad(tx0 - 6.0f, ry - 5.0f, panelW - 2.0f * pad + 12.0f, rowH - 2.0f,
                         {0.25f, 0.30f, 0.26f, 0.85f});
            const glm::vec4 rowCol = selected ? glm::vec4{0.98f, 0.96f, 0.85f, 1.0f}
                                              : glm::vec4{0.88f, 0.88f, 0.82f, 0.85f};
            pushText(row.name, tx0 + colGood, ry, gpx, rowCol);
            pushNumber(row.buy,   tx0 + colBuy,  ry, gpx, rowCol);
            pushNumber(row.sell,  tx0 + colSell, ry, gpx, rowCol);
            pushNumber(row.stock, tx0 + colStk,  ry, gpx, rowCol);
            pushNumber(row.held,  tx0 + colHeld, ry, gpx, rowCol);
            ry += rowH;
        }

        pushText("B BUY   S SELL   ESC BACK", tx0, py0 + panelH - 30.0f, gpx,
                 {0.75f, 0.78f, 0.72f, 0.9f});
    }

    if (m_pausedHud) {
        pushQuad(0.0f, 0.0f, W, H, {0.0f, 0.0f, 0.0f, 0.42f});
        pushCenteredText("PAUSED", H * 0.5f - 72.0f, 8.0f, {0.95f, 0.92f, 0.82f, 0.95f});
        const char* rows[] = { "RESUME", "SETTINGS", "QUIT" };
        for (int i = 0; i < 3; ++i) {
            float rx, ry, rw, rh;
            pauseMenuRowRect(i, W, H, rx, ry, rw, rh);
            pushQuad(rx, ry, rw, rh, {0.12f, 0.14f, 0.13f, 0.88f});
            pushCenteredText(rows[i], ry + 9.0f, 5.0f, {0.95f, 0.92f, 0.82f, 0.92f});
        }
    }

    if (verts.size() > UI_MAX_VERTS) verts.resize(UI_MAX_VERTS); // guard against buffer overflow
    m_uiVertexCount = (uint32_t)verts.size();
    memcpy(m_uiBuffer[m_currentFrame].mapped, verts.data(), sizeof(UIVertex) * verts.size());
}
