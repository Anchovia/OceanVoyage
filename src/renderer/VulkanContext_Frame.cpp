#include "VulkanContext.h"
#include "VulkanContext_Private.h"
#include "renderer/Types.h"
#include "platform/Window.h"
#include "world/World.h"
#include "game/Camera.h"

#include <stdexcept>
#include <cstring>
#include <cmath>

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
        if (ImGui::Begin("Pastel Farm Dev", &m_devUiVisible)) {
            ImGui::TextUnformatted("F3 toggles this panel");
            ImGui::Separator();
            ImGui::Text("Day: %d", frame.day);
            ImGui::Text("Time of day: %.3f", frame.timeOfDay);
            ImGui::Text("Player: %.2f, %.2f, %.2f",
                frame.playerPosition.x, frame.playerPosition.y, frame.playerPosition.z);
            ImGui::Text("Chunks loaded: %d", (int)m_world.chunks().size());
            ImGui::Text("Drops: %d", (int)frame.drops.size());
            ImGui::Text("Selected slot: %d", frame.hotbarSelected + 1);
            ImGui::Text("Near workbench: %s", frame.nearWorkbench ? "yes" : "no");
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
void VulkanContext::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin);

#ifdef PASTEL_DEV_BUILD
    if (m_devTimingSupported) {
        const uint32_t base = m_currentFrame * DEV_TIMESTAMP_COUNT;
        vkCmdResetQueryPool(cmd, m_devQueryPool, base, DEV_TIMESTAMP_COUNT);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_devQueryPool, base);
    }
#endif

    // Shadow pass — render chunk depth from sun's perspective
    {
        VkClearValue shadowClear{};
        shadowClear.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo shadowRp{};
        shadowRp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowRp.renderPass      = m_shadowRenderPass;
        shadowRp.framebuffer     = m_shadowFramebuffer;
        shadowRp.renderArea      = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
        shadowRp.clearValueCount = 1;
        shadowRp.pClearValues    = &shadowClear;

        // Always begin/end so the shadow image is cleared (depth=1.0 → fully lit) and
        // transitioned to READ_ONLY_OPTIMAL. At night skip the chunk geometry: the
        // fragment shaders gate shadow sampling on dayFactor > 0.01 anyway.
        vkCmdBeginRenderPass(cmd, &shadowRp, VK_SUBPASS_CONTENTS_INLINE);
        if (m_dayFactor > 0.01f) {
            // Cull chunks outside the light's ortho box — they aren't captured anyway
            Frustum lightFrustum = Frustum::extractFrom(m_lightMVP);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
            vkCmdPushConstants(cmd, m_shadowPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &m_lightMVP);

            for (auto& [coord, data] : m_chunkBuffers) {
                if (data.vertexBuffer == VK_NULL_HANDLE || data.indexCount == 0) continue;

                glm::vec3 chunkMin = { coord.x * CHUNK_SIZE,       coord.y * CHUNK_SIZE,       0.0f };
                glm::vec3 chunkMax = { (coord.x + 1) * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE, (float)CHUNK_DEPTH };
                if (!lightFrustum.containsAABB(chunkMin, chunkMax)) continue;

                VkBuffer     vBuf[] = {data.vertexBuffer};
                VkDeviceSize offs[] = {0};
                vkCmdBindVertexBuffers(cmd, 0, 1, vBuf, offs);
                vkCmdBindIndexBuffer(cmd, data.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, data.indexCount, 1, 0, 0, 0);
            }

            // Objects cast shadows too — per-type mesh, instanced, reuse the light frustum cull
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowObjectPipeline);
                vkCmdPushConstants(cmd, m_shadowPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &m_lightMVP);
                for (auto& [coord, data] : m_chunkBuffers) {
                    if (data.objGroups.empty()) continue;

                    glm::vec3 chunkMin = { coord.x * CHUNK_SIZE,       coord.y * CHUNK_SIZE,       0.0f };
                    glm::vec3 chunkMax = { (coord.x + 1) * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE, (float)CHUNK_DEPTH };
                    if (!lightFrustum.containsAABB(chunkMin, chunkMax)) continue;

                    for (auto& g : data.objGroups) {
                        if (!objectDef(g.type).castShadow) continue;
                        const ObjectMesh& mesh = m_objectMeshes[(size_t)g.type];
                        if (mesh.count == 0 || g.count == 0) continue;
                        VkBuffer     bufs[] = { mesh.vbuf, g.buffer };
                        VkDeviceSize offs[] = { 0, 0 };
                        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
                        vkCmdDraw(cmd, mesh.count, g.count, 0, 0);
                    }
                }
            }

            // Grass shadow casting disabled: thin alpha-card blades are ~1 texel wide in
            // the shadow map, so they alias/flicker badly as the sun sweeps (DEVLOG
            // 2026-06-03, confirmed via capture). Grass still receives shadow + uses root
            // darkening for grounding; only the noisy casting is removed. Flip to re-enable.
            constexpr bool kGrassCastsShadow = false;
            if (kGrassCastsShadow && !m_shadowGrassDescriptorSets.empty()) {
                static constexpr float GRASS_SHADOW_RADIUS = 56.0f;
                static constexpr float GRASS_SHADOW_RADIUS_SQ = GRASS_SHADOW_RADIUS * GRASS_SHADOW_RADIUS;

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowGrassPipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_shadowGrassPipelineLayout, 0, 1, &m_shadowGrassDescriptorSets[m_currentFrame], 0, nullptr);
                vkCmdPushConstants(cmd, m_shadowGrassPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &m_lightMVP);

                for (auto& [coord, data] : m_chunkBuffers) {
                    if (m_grassCardMesh.count == 0 || data.grassCount == 0) continue;

                    glm::vec3 chunkMin = { coord.x * CHUNK_SIZE,       coord.y * CHUNK_SIZE,       0.0f };
                    glm::vec3 chunkMax = { (coord.x + 1) * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE, (float)CHUNK_DEPTH };
                    if (!lightFrustum.containsAABB(chunkMin, chunkMax)) continue;

                    const glm::vec2 chunkCenter = {
                        (coord.x + 0.5f) * (float)CHUNK_SIZE,
                        (coord.y + 0.5f) * (float)CHUNK_SIZE
                    };
                    const glm::vec2 d = chunkCenter - glm::vec2(m_shadowCenter);
                    if (glm::dot(d, d) > GRASS_SHADOW_RADIUS_SQ) continue;

                    VkBuffer     bufs[] = { m_grassCardMesh.vbuf, data.grassBuffer };
                    VkDeviceSize offs[] = { 0, 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
                    vkCmdDraw(cmd, m_grassCardMesh.count, data.grassCount, 0, 0);
                }
            }

            // Ship (placeholder) casts a shadow too — instanced object shadow caster,
            // always inside the light box (no cull).
            if (m_shipMesh.count > 0) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowObjectPipeline);
                vkCmdPushConstants(cmd, m_shadowPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &m_lightMVP);
                VkBuffer     sBufs[] = { m_shipMesh.vbuf, m_shipInstBuffer[m_currentFrame] };
                VkDeviceSize sOffs[] = { 0, 0 };
                vkCmdBindVertexBuffers(cmd, 0, 2, sBufs, sOffs);
                vkCmdDraw(cmd, m_shipMesh.count, 1, 0, 0);
            }
        }
        vkCmdEndRenderPass(cmd);
    }
#ifdef PASTEL_DEV_BUILD
    writeDevTimestamp(cmd, 1);
#endif

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

    // Chunk mesh (hidden face culling, dedicated pipeline)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_chunkPipeline);
    for (auto& [coord, data] : m_chunkBuffers) {
        if (data.vertexBuffer == VK_NULL_HANDLE || data.indexCount == 0) continue;

        glm::vec3 chunkMin = { coord.x * CHUNK_SIZE,       coord.y * CHUNK_SIZE,       0.0f };
        glm::vec3 chunkMax = { (coord.x + 1) * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE, (float)CHUNK_DEPTH };
        if (!m_frustum.containsAABB(chunkMin, chunkMax)) continue;

        VkBuffer     vBuf[] = { data.vertexBuffer };
        VkDeviceSize offs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vBuf, offs);
        vkCmdBindIndexBuffer(cmd, data.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, data.indexCount, 1, 0, 0, 0);
    }

    // Ground dressing - visual-only flat patches and tiny pebbles, not shadow casters
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_objectPipeline);
        for (auto& [coord, data] : m_chunkBuffers) {
            if (data.groundPatchCount == 0 && data.pebbleCount == 0) continue;

            glm::vec3 chunkMin = { coord.x * CHUNK_SIZE,       coord.y * CHUNK_SIZE,       0.0f };
            glm::vec3 chunkMax = { (coord.x + 1) * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE, (float)CHUNK_DEPTH };
            if (!m_frustum.containsAABB(chunkMin, chunkMax)) continue;

            if (m_groundPatchMesh.count > 0 && data.groundPatchCount > 0) {
                VkBuffer     bufs[] = { m_groundPatchMesh.vbuf, data.groundPatchBuffer };
                VkDeviceSize offs[] = { 0, 0 };
                vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
                vkCmdDraw(cmd, m_groundPatchMesh.count, data.groundPatchCount, 0, 0);
            }

            if (m_pebbleMesh.count > 0 && data.pebbleCount > 0) {
                VkBuffer     bufs[] = { m_pebbleMesh.vbuf, data.pebbleBuffer };
                VkDeviceSize offs[] = { 0, 0 };
                vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
                vkCmdDraw(cmd, m_pebbleMesh.count, data.pebbleCount, 0, 0);
            }
        }
    }

    // Grass alpha cards — visual-only dressing, not a shadow caster
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_grassPipeline);
        for (auto& [coord, data] : m_chunkBuffers) {
            if (m_grassCardMesh.count == 0 || data.grassCount == 0) continue;

            glm::vec3 chunkMin = { coord.x * CHUNK_SIZE,       coord.y * CHUNK_SIZE,       0.0f };
            glm::vec3 chunkMax = { (coord.x + 1) * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE, (float)CHUNK_DEPTH };
            if (!m_frustum.containsAABB(chunkMin, chunkMax)) continue;

            VkBuffer     bufs[] = { m_grassCardMesh.vbuf, data.grassBuffer };
            VkDeviceSize offs[] = { 0, 0 };
            vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
            vkCmdDraw(cmd, m_grassCardMesh.count, data.grassCount, 0, 0);
        }
    }

    // Objects — per-type mesh, instanced per chunk
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_objectPipeline);
        for (auto& [coord, data] : m_chunkBuffers) {
            if (data.objGroups.empty()) continue;

            glm::vec3 chunkMin = { coord.x * CHUNK_SIZE,       coord.y * CHUNK_SIZE,       0.0f };
            glm::vec3 chunkMax = { (coord.x + 1) * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE, (float)CHUNK_DEPTH };
            if (!m_frustum.containsAABB(chunkMin, chunkMax)) continue;

            for (auto& g : data.objGroups) {
                const ObjectMesh& mesh = m_objectMeshes[(size_t)g.type];
                if (mesh.count == 0 || g.count == 0) continue;
                VkBuffer     bufs[] = { mesh.vbuf, g.buffer };
                VkDeviceSize offs[] = { 0, 0 };
                vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
                vkCmdDraw(cmd, mesh.count, g.count, 0, 0);
            }
        }
    }

    // Player / selector / drops — dynamic world entities, hidden in menu states
    // (MainMenu/Settings/Loading) so no orphan player cube shows behind the menu.
    const bool worldVisible = !(m_mainMenuHud || m_settingsHud || m_loadingHud);

    // Ocean surface — Gerstner-wave grid that follows the camera. Opaque + depth-tested
    // so the ship and (future) islands occlude it correctly.
    if (worldVisible && m_oceanIndexCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_oceanPipeline);
        VkBuffer     oBufs[] = { m_oceanVertexBuffer };
        VkDeviceSize oOffs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, oBufs, oOffs);
        vkCmdBindIndexBuffer(cmd, m_oceanIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_oceanIndexCount, 1, 0, 0, 0);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    if (worldVisible && m_showSelector) {
        VkBuffer     sBufs[] = {m_selectorVertexBuffer, m_selectorInstBuffer[m_currentFrame]};
        VkDeviceSize sOffs[] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, sBufs, sOffs);
        vkCmdBindIndexBuffer(cmd, m_selectorIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, (uint32_t)kSelectorIndices.size(), 1, 0, 0, 0);
    }

    // Dropped items (small cubes, same instanced pipeline as the selector)
    if (worldVisible && m_dropCount > 0) {
        VkBuffer     dBufs[] = {m_itemVertexBuffer, m_dropInstBuffer[m_currentFrame]};
        VkDeviceSize dOffs[] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, dBufs, dOffs);
        vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, (uint32_t)kIndices.size(), m_dropCount, 0, 0, 0);
    }

    // Ship (placeholder) — replaces the player cube. Drawn with the rotation-capable
    // object pipeline so the bow faces the player heading; the per-frame instance seats
    // it on the sea surface. Drawn last in the scene pass; switches the bound pipeline.
    if (worldVisible && m_shipMesh.count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_objectPipeline);
        VkBuffer     sBufs[] = {m_shipMesh.vbuf, m_shipInstBuffer[m_currentFrame]};
        VkDeviceSize sOffs[] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, sBufs, sOffs);
        vkCmdDraw(cmd, m_shipMesh.count, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd); // end scene pass (offscreen color now SHADER_READ_ONLY)
#ifdef PASTEL_DEV_BUILD
    writeDevTimestamp(cmd, 2);
#endif

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

    vkEndCommandBuffer(cmd);
}

// ============================================================
//  drawFrame
// ============================================================
void VulkanContext::drawFrame(const FrameRenderData& frame) {
    m_hotbarSelected   = frame.hotbarSelected;
    m_invHud           = frame.inventory;
    m_inventoryOpen    = frame.inventoryOpen;
    m_mainMenuHud      = frame.mainMenu;
    m_settingsHud      = frame.settings;
    m_loadingHud       = frame.loading;
    m_pausedHud        = frame.paused;
    m_vsyncHud         = frame.vsyncEnabled;
    m_aaModeHud        = frame.aaMode;
    m_nearWorkbenchHud = frame.nearWorkbench;
    m_dayHud           = frame.day;

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
    vkWaitForFences(m_device, 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
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
        vkWaitForFences(m_device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    m_imagesInFlight[imageIndex] = m_inFlight[m_currentFrame];

    vkResetFences(m_device, 1, &m_inFlight[m_currentFrame]);

    // Sun direction + light space matrix — orthographic from sun, centered on player
    {
        const float kSunAzimuth = glm::radians(225.0f); // rotate light direction in world (tune to taste)
        float elevation = sinf(frame.timeOfDay * 3.14159265f);
        float azimuth   = (frame.timeOfDay - 0.5f) * 3.14159265f + kSunAzimuth; // π → 180° sweep (sunrise→noon→sunset)
        m_sunDir    = glm::normalize(glm::vec3(cosf(azimuth), sinf(azimuth), elevation));
        m_dayFactor = elevation; // 0 at midnight, 1 at noon

        // Light frustum half-extent. Kept tight to the visible (un-fogged) range so
        // the 2048 shadow map spends its resolution where it shows: fog fully hides
        // geometry past ~57 units, so a smaller box ~doubles effective texel density
        // and cuts the blocky-edge shimmer that crawls as the sun rotates.
        const float range = 45.0f;
        glm::mat4 lightView = glm::lookAt(
            frame.playerPosition + m_sunDir * 150.0f,
            frame.playerPosition,
            glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 lightProj = glm::ortho(-range, range, -range, range, 1.0f, 300.0f);
        lightProj[1][1] *= -1.0f;

        // Texel snapping: anchor the shadow texel grid to world space so the projected
        // scene shifts in whole-texel steps. Removes per-frame shadow edge shimmering.
        // World origin is the fixed reference; ortho keeps w == 1 so no perspective divide.
        glm::mat4 unsnapped = lightProj * lightView;
        glm::vec4 originLS  = unsnapped * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        float texelScale    = (float)SHADOW_MAP_SIZE * 0.5f;
        glm::vec2 inTexels  = glm::vec2(originLS) * texelScale;
        glm::vec2 offset    = (glm::round(inTexels) - inTexels) / texelScale;
        lightProj[3][0]    += offset.x;
        lightProj[3][1]    += offset.y;

        m_lightMVP = lightProj * lightView;
        m_shadowCenter = frame.playerPosition;
    }

    updateUniformBuffer(m_currentFrame, frame.camera, frame.gameTime);
    updateShipInstanceBuffer(frame.playerPosition, frame.playerHeading, frame.gameTime);
    updateDropInstanceBuffer(frame.drops);
    updateSelectorInstanceBuffer(frame.targetTile);
    updateHotbar();
    rebuildDirtyChunks();
    m_frustum = Frustum::extractFrom(frame.camera.viewProj());
#ifdef PASTEL_DEV_BUILD
    buildDevUi(frame);
#endif
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
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
    vkQueueSubmit(m_graphicsQueue, 1, &submit, m_inFlight[m_currentFrame]);

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &m_renderFinished[imageIndex];
    present.swapchainCount     = 1;
    present.pSwapchains        = &m_swapchain;
    present.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(m_presentQueue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window.wasResized()) {
        m_window.resetResized();
        recreateSwapchain();
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================
//  Per-frame update functions
// ============================================================
void VulkanContext::updateUniformBuffer(uint32_t currentFrame, const Camera& camera, float gameTime) {
    UniformBufferObject ubo{};
    ubo.model    = glm::mat4(1.0f);
    ubo.view     = camera.view();
    ubo.proj     = camera.proj();
    ubo.lightDir = glm::vec4(m_sunDir, m_dayFactor); // w = dayFactor (0=night, 1=noon)
    ubo.lightMVP = m_lightMVP;
    ubo.fogColor = glm::vec4(m_skyColor[0], m_skyColor[1], m_skyColor[2], 1.0f);
    ubo.animationParams = glm::vec4(gameTime, 0.0f, 0.0f, 0.0f);
    ubo.cameraPos       = glm::vec4(camera.position(), 1.0f);
    memcpy(m_uniformBuffers[currentFrame].mapped, &ubo, sizeof(ubo));
}

void VulkanContext::updatePlayerInstanceBuffer(const glm::vec3& playerPosition) {
    static const glm::vec3 kPlayerColor = {1.0f, 0.45f, 0.1f};
    InstanceData inst{playerPosition, kPlayerColor, kPlayerColor};
    memcpy(m_playerInstBuffer[m_currentFrame].mapped, &inst, sizeof(inst));
}

namespace {
// Gerstner sea-surface height at a world XY. MUST match the wave set in shaders/ocean.vert.
// Horizontal displacement is ignored (good enough to float the ship vertically).
float oceanSurfaceHeight(float wx, float wy, float t) {
    struct Wave { float dx, dy, L, A; };
    static const Wave waves[4] = {
        { 1.0f,  0.0f,  9.0f,  0.115f },
        { 0.6f,  0.8f,  5.0f,  0.060f },
        {-0.8f,  0.4f,  13.0f, 0.085f },
        { 0.2f, -1.0f,  3.3f,  0.030f },
    };
    float h = 0.5f; // SEA_LEVEL (matches ocean.vert)
    for (const Wave& w : waves) {
        float inv   = 1.0f / std::sqrt(w.dx * w.dx + w.dy * w.dy);
        float k     = 6.2831853f / w.L;
        float omega = std::sqrt(9.8f * k);
        float phase = k * (w.dx * inv * wx + w.dy * inv * wy) - omega * t;
        h += w.A * std::sin(phase);
    }
    return h;
}
} // namespace

void VulkanContext::updateShipInstanceBuffer(const glm::vec3& position, float heading, float gameTime) {
    // Float the placeholder ship on the Gerstner ocean surface: ride the local wave height
    // with a small draft so the hull sits partly in the water. The canonical wave model
    // lives in shaders/ocean.vert; oceanSurfaceHeight() must stay in sync with it.
    constexpr float DRAFT = 0.08f;
    float z = oceanSurfaceHeight(position.x, position.y, gameTime) - DRAFT;
    ObjectInstance inst{ glm::vec3(position.x, position.y, z), 1.0f, heading };
    memcpy(m_shipInstBuffer[m_currentFrame].mapped, &inst, sizeof(inst));
}

void VulkanContext::updateDropInstanceBuffer(const std::vector<DroppedItem>& drops) {
    m_dropCount = std::min((uint32_t)drops.size(), MAX_DROPS);
    InstanceData* dst = reinterpret_cast<InstanceData*>(m_dropInstBuffer[m_currentFrame].mapped);
    for (uint32_t i = 0; i < m_dropCount; i++) {
        const glm::vec3 c = itemColor(drops[i].type);
        dst[i] = InstanceData{ drops[i].pos, c, c };
    }
}

void VulkanContext::updateSelectorInstanceBuffer(const std::optional<glm::ivec3>& targetTile) {
    m_showSelector = targetTile.has_value();
    if (!m_showSelector) return;

    static const glm::vec3 kSelectorColor = {1.0f, 0.9f, 0.1f};
    const glm::ivec3 tile = *targetTile;
    InstanceData inst{m_world.tileCenter(tile.x, tile.y, tile.z), kSelectorColor, kSelectorColor};
    memcpy(m_selectorInstBuffer[m_currentFrame].mapped, &inst, sizeof(inst));
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

    // Tiny 3x5 UI glyphs: digits first, then A-Z. Row bits 4=left, 2=mid, 1=right.
    static const uint8_t GLYPHS[36][5] = {
        {7,5,5,5,7}, {2,2,2,2,2}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
        {7,5,7,5,5}, {6,5,6,5,6}, {7,4,4,4,7}, {6,5,5,5,6}, {7,4,6,4,7},
        {7,4,6,4,4}, {7,4,5,5,7}, {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,7},
        {5,5,6,5,5}, {4,4,4,4,7}, {5,7,7,5,5}, {5,7,7,7,5}, {7,5,5,5,7},
        {7,5,7,4,4}, {7,5,5,7,1}, {7,5,7,6,5}, {7,4,7,1,7}, {7,2,2,2,2},
        {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5}, {5,5,2,2,2},
        {7,1,2,4,7},
    };
    auto glyphIndex = [](char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
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
        pushCenteredText("PASTEL FARM", H * 0.5f - 76.0f, 10.0f, {0.95f, 0.92f, 0.82f, 1.0f});
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

        pushCenteredText("SETTINGS", H * 0.5f - 72.0f, 8.0f, {0.95f, 0.92f, 0.82f, 1.0f});
        const char* rows[] = { m_vsyncHud ? "VSYNC ON" : "VSYNC OFF", aaText, "BACK" };
        for (int i = 0; i < 3; ++i) {
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

    // --- Hotbar ---
    const float slot = 56.0f, gap = 6.0f, pad = 6.0f;
    const float barW = HOTBAR_SLOTS * slot + (HOTBAR_SLOTS - 1) * gap;
    const float startX = (W - barW) * 0.5f;
    const float startY = H - slot - 20.0f;

    pushQuad(startX - pad, startY - pad, barW + 2 * pad, slot + 2 * pad, {0.10f, 0.10f, 0.12f, 0.7f});

    float selX = startX + m_hotbarSelected * (slot + gap);
    pushQuad(selX - 3.0f, startY - 3.0f, slot + 6.0f, slot + 6.0f, {1.0f, 0.85f, 0.2f, 1.0f});

    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        float x = startX + i * (slot + gap);
        glm::vec4 bg = (i == m_hotbarSelected)
            ? glm::vec4(0.35f, 0.35f, 0.40f, 1.0f)
            : glm::vec4(0.20f, 0.20f, 0.24f, 0.9f);
        pushQuad(x, startY, slot, slot, bg);

        const ItemStack& st = m_invHud[i];
        if (st.type != ItemType::NONE) {
            float inset = 10.0f;
            pushQuad(x + inset, startY + inset, slot - 2 * inset, slot - 2 * inset,
                     glm::vec4(itemColor(st.type), 1.0f));
            if (st.count > 1)
                pushNumber(st.count, x + 5.0f, startY + slot - 5 * 3.0f - 5.0f, 3.0f, {1.0f, 1.0f, 1.0f, 0.95f});
        }
    }

    // --- Inventory overlay ---
    if (m_inventoryOpen) {
        const float gridW = INV_COLS * INV_SLOT_SIZE + (INV_COLS - 1) * INV_GAP;
        const float gridH = INV_ROWS * INV_SLOT_SIZE + (INV_ROWS - 1) * INV_GAP;
        const float ox = (W - gridW) * 0.5f - INV_PAD;
        const float oy = (H - gridH) * 0.5f - INV_PAD;
        const float panelW = gridW + 2 * INV_PAD;
        const float panelH = gridH + 2 * INV_PAD;

        // Dimmed background over entire screen
        pushQuad(0.0f, 0.0f, W, H, {0.0f, 0.0f, 0.0f, 0.45f});

        // Panel background
        pushQuad(ox, oy, panelW, panelH, {0.12f, 0.12f, 0.15f, 0.92f});

        for (int idx = 0; idx < INV_SLOTS; ++idx) {
            int c = idx % INV_COLS;
            int r = idx / INV_COLS;
            float sx = ox + INV_PAD + c * (INV_SLOT_SIZE + INV_GAP);
            float sy = oy + INV_PAD + r * (INV_SLOT_SIZE + INV_GAP);

            glm::vec4 slotBg = (idx == m_hotbarSelected)
                ? glm::vec4(0.35f, 0.35f, 0.40f, 1.0f)
                : glm::vec4(0.22f, 0.22f, 0.27f, 1.0f);
            pushQuad(sx, sy, INV_SLOT_SIZE, INV_SLOT_SIZE, slotBg);

            const ItemStack& st = m_invHud[idx];
            if (st.type != ItemType::NONE) {
                float inset = 10.0f;
                pushQuad(sx + inset, sy + inset, INV_SLOT_SIZE - 2 * inset, INV_SLOT_SIZE - 2 * inset,
                         glm::vec4(itemColor(st.type), 1.0f));
                if (st.count > 1)
                    pushNumber(st.count, sx + 4.0f, sy + INV_SLOT_SIZE - 5 * 2.0f - 4.0f, 2.0f, {1.0f, 1.0f, 1.0f, 0.95f});
            }
        }

        // --- Crafting panel (basic recipes; row = result + inputs, dim if unaffordable) ---
        auto invCount = [&](ItemType t) {
            int c = 0;
            for (const ItemStack& s : m_invHud) if (s.type == t) c += s.count;
            return c;
        };
        int rn = 0;
        const Recipe* rtable = craftingRecipes(rn);
        for (int i = 0; i < rn; i++) {
            if (rtable[i].requiresWorkbench && !m_nearWorkbenchHud) continue;
            const Recipe& rc = rtable[i];

            bool ok = true;
            for (const RecipeInput& in : rc.inputs) {
                if (in.type == ItemType::NONE) continue;
                if (invCount(in.type) < in.count) { ok = false; break; }
            }
            const float a = ok ? 1.0f : 0.4f;

            float rx, ry, rw, rh;
            craftRowRect(i, W, H, rx, ry, rw, rh);
            pushQuad(rx, ry, rw, rh, {0.18f, 0.18f, 0.22f, ok ? 0.95f : 0.8f});

            const float sw = rh - 12.0f;
            pushQuad(rx + 6.0f, ry + 6.0f, sw, sw, glm::vec4(itemColor(rc.result), a));
            if (rc.resultCount > 1)
                pushNumber(rc.resultCount, rx + 8.0f, ry + rh - 5 * 2.0f - 6.0f, 2.0f, {1.0f, 1.0f, 1.0f, 0.95f});

            float ix = rx + sw + 20.0f;
            for (const RecipeInput& in : rc.inputs) {
                if (in.type == ItemType::NONE) continue;
                pushQuad(ix, ry + 6.0f, sw, sw, glm::vec4(itemColor(in.type), a));
                pushNumber(in.count, ix + 2.0f, ry + rh - 5 * 2.0f - 6.0f, 2.0f, {1.0f, 1.0f, 1.0f, 0.95f});
                ix += sw + 14.0f;
            }
        }
    }

    // Day counter HUD (top-left)
    pushNumber(m_dayHud, 16.0f, 16.0f, 4.0f, {1.0f, 1.0f, 1.0f, 0.9f});

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
