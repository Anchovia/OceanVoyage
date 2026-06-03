#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <string>
#include <unordered_map>
#include <array>
#include "world/Chunk.h"
#include "renderer/Frustum.h"

class Window;
class World;
class Camera;

// Owning RAII pair for a VkBuffer + its VkDeviceMemory (and optional persistent map).
// Move-only; frees on destruction. Implicitly converts to VkBuffer for bind/draw calls,
// so existing handle reads keep compiling unchanged.
struct GpuBuffer {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void*          mapped = nullptr;        // non-null while persistently mapped
    VkDevice       device = VK_NULL_HANDLE; // owner device, for self-destruction

    GpuBuffer() = default;
    GpuBuffer(const GpuBuffer&)            = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& o) noexcept
        : buffer(o.buffer), memory(o.memory), mapped(o.mapped), device(o.device) {
        o.buffer = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE; o.mapped = nullptr; o.device = VK_NULL_HANDLE;
    }
    GpuBuffer& operator=(GpuBuffer&& o) noexcept {
        if (this != &o) {
            destroy();
            buffer = o.buffer; memory = o.memory; mapped = o.mapped; device = o.device;
            o.buffer = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE; o.mapped = nullptr; o.device = VK_NULL_HANDLE;
        }
        return *this;
    }
    ~GpuBuffer() { destroy(); }

    operator VkBuffer() const { return buffer; }

    void destroy() {
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; mapped = nullptr; device = VK_NULL_HANDLE;
    }
};

// RAII wrapper for an uploaded sampled texture: image + memory + view (+ optional
// sampler). Mirrors GpuBuffer's move-only ownership so it self-frees and can be
// stored as a member or returned by value. The sampler stays VK_NULL_HANDLE when
// the texture is sampled through a shared sampler owned elsewhere (e.g. SMAA LUTs).
struct TextureResource {
    VkImage        image   = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkImageView    view    = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
    VkDevice       device  = VK_NULL_HANDLE; // owner device, for self-destruction

    TextureResource() = default;
    TextureResource(const TextureResource&)            = delete;
    TextureResource& operator=(const TextureResource&) = delete;
    TextureResource(TextureResource&& o) noexcept
        : image(o.image), memory(o.memory), view(o.view), sampler(o.sampler), device(o.device) {
        o.image = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE; o.view = VK_NULL_HANDLE;
        o.sampler = VK_NULL_HANDLE; o.device = VK_NULL_HANDLE;
    }
    TextureResource& operator=(TextureResource&& o) noexcept {
        if (this != &o) {
            destroy();
            image = o.image; memory = o.memory; view = o.view; sampler = o.sampler; device = o.device;
            o.image = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE; o.view = VK_NULL_HANDLE;
            o.sampler = VK_NULL_HANDLE; o.device = VK_NULL_HANDLE;
        }
        return *this;
    }
    ~TextureResource() { destroy(); }

    void destroy() {
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        if (view)    vkDestroyImageView(device, view, nullptr);
        if (image)   vkDestroyImage(device, image, nullptr);
        if (memory)  vkFreeMemory(device, memory, nullptr);
        image = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; view = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE; device = VK_NULL_HANDLE;
    }
};

// Per-frame snapshot the renderer consumes. Mirrors the previous drawFrame
// argument list (by-ref for heavy data, by-value for scalars).
struct FrameRenderData {
    const Camera&                            camera;
    glm::vec3                                playerPosition;
    std::optional<glm::ivec3>                targetTile;
    int                                      hotbarSelected;
    const std::array<ItemStack, INV_SLOTS>&  inventory;
    float                                    timeOfDay;
    float                                    gameTime;
    bool                                     inventoryOpen;
    int                                      day;
    const std::vector<DroppedItem>&          drops;
    bool                                     nearWorkbench;
    bool                                     mainMenu;
    bool                                     settings;
    bool                                     loading;
    bool                                     paused;
    bool                                     vsyncEnabled;
    int                                      aaMode;
};

class VulkanContext {
public:
    VulkanContext(Window& window, World& world);
    ~VulkanContext();

    void drawFrame(const FrameRenderData& frame);
    void waitIdle();

#ifdef PASTEL_DEV_BUILD
    void beginDevFrame();
    bool devWantsMouse() const;
    bool devWantsKeyboard() const;
    void toggleDevUi();
#endif

private:
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    // Builds the full mip chain from level 0 via vkCmdBlitImage; leaves all levels SHADER_READ_ONLY.
    void generateMipmaps(VkImage image, VkFormat format, int32_t width, int32_t height,
        uint32_t mipLevels, uint32_t layerCount);
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    struct PipelineConfig {
        const char*                                    vertPath;
        const char*                                    fragPath;
        std::vector<VkVertexInputBindingDescription>   bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
        VkCullModeFlags  cullMode;
        bool             depthTest;   // depthTestEnable + depthWriteEnable
        bool             alphaBlend;  // semi-transparent (UI)
        VkPipelineLayout layout;
        VkRenderPass     renderPass = VK_NULL_HANDLE;
    };
    VkPipeline createPipeline(const PipelineConfig& cfg);
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createDescriptorSetLayout();
    void createVertexBuffer();
    void createIndexBuffer();
    void createSelectorBuffers();
    void createChunkPipeline();
    void buildChunkBuffer(const glm::ivec2& coord, Chunk& chunk);
    void buildChunkObjectBuffer(const glm::ivec2& coord, Chunk& chunk);
    void buildGrassDressingBuffer(const glm::ivec2& coord, Chunk& chunk);
    void buildGroundDressingBuffer(const glm::ivec2& coord, Chunk& chunk);
    void rebuildDirtyChunks();
    void createUIPipeline();
    void createUIBuffer();
    void updateHotbar();
    void createObjectPipeline();
    void createGrassPipeline();
    void createPostRenderPass();
    void createOffscreenResources();
    void createPostPipeline();
    void createPostSampler();
    void createPostDescriptors();
    void updatePostDescriptors();
    void createSmaaRenderPass();
    void createSmaaResources();
    void createSmaaPipelines();
    void createSmaaLookupTextures();
    void createSmaaDescriptors();
    void updateSmaaDescriptors();
    // Generic uploaded-texture helper: staging upload + image + view (+ optional sampler).
    TextureResource createTexture(uint32_t width, uint32_t height, VkFormat format,
        const void* bytes, VkDeviceSize size, bool withSampler, bool mipmapped = false);
    // Layered variant for a sampler2DArray (bytes laid out layer-major, all same size).
    TextureResource createTextureArray(uint32_t width, uint32_t height, uint32_t layerCount,
        VkFormat format, const void* bytes, VkDeviceSize size, bool withSampler, bool mipmapped = false);
    void createObjectMeshes();
    void createGrassTexture();
    void createTerrainTextureArray();
    void createItemMesh();
    void createDropInstanceBuffer();
    void updateDropInstanceBuffer(const std::vector<DroppedItem>& drops);
    void createPlayerInstanceBuffer(const glm::vec3& playerPosition);
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void updateUniformBuffer(uint32_t currentFrame, const Camera& camera, float gameTime);
    void updatePlayerInstanceBuffer(const glm::vec3& playerPosition);
    void updateSelectorInstanceBuffer(const std::optional<glm::ivec3>& targetTile);
    void createDepthResources();
    void createShadowResources();
    void createShadowPipeline();
    void createShadowObjectPipeline();
    void createShadowPlayerPipeline();
    void createShadowGrassPipeline();
    void createShadowSampler();
    void createShadowGrassDescriptors();
    void createImage(uint32_t width, uint32_t height, VkFormat format,
        VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory,
        uint32_t mipLevels = 1);
    VkFormat findDepthFormat();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
        VkImageTiling tiling, VkFormatFeatureFlags features);

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    void cleanupSwapchain();
    void recreateSwapchain();

    void deferDestroy(GpuBuffer&& buf);

#ifdef PASTEL_DEV_BUILD
    void createDevTools();
    void destroyDevTools();
    void buildDevUi(const FrameRenderData& frame);
    void readDevGpuTimings(uint32_t frameIndex);
    void writeDevTimestamp(VkCommandBuffer cmd, uint32_t index);
#endif

    VkShaderModule          createShaderModule(const std::vector<char>& code);
    std::vector<char>       readFile(const std::string& path);
    uint32_t                findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    GpuBuffer               createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties);

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphics;
        std::optional<uint32_t> present;
        bool complete() const { return graphics && present; }
    };
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    // ---- Vulkan handles ----
    Window& m_window;
    World&  m_world;

    VkInstance               m_instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger   = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface          = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice   = VK_NULL_HANDLE;
    bool                     m_anisotropyEnabled = false; // samplerAnisotropy device feature available
    float                    m_maxAnisotropy     = 1.0f;  // clamped to device limit (set at device creation)
    VkDevice                 m_device           = VK_NULL_HANDLE;
    VkQueue                  m_graphicsQueue    = VK_NULL_HANDLE;
    VkQueue                  m_presentQueue     = VK_NULL_HANDLE;

    VkSwapchainKHR           m_swapchain        = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapchainImages;
    VkFormat                 m_swapchainFormat  = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_swapchainExtent  = {};
    std::vector<VkImageView> m_swapchainImageViews;
    bool                     m_vsyncEnabled     = true;
    std::vector<VkFramebuffer> m_sceneFramebuffers;  // offscreen color + depth (per frame in flight)
    std::vector<VkFramebuffer> m_postFramebuffers;   // swapchain (per image)

    VkRenderPass             m_renderPass        = VK_NULL_HANDLE;
    VkPipelineLayout         m_pipelineLayout    = VK_NULL_HANDLE;
    VkPipeline               m_pipeline          = VK_NULL_HANDLE;  // Player / selector (instancing)
    VkPipeline               m_chunkPipeline     = VK_NULL_HANDLE;  // Chunk mesh
    VkPipeline               m_uiPipeline        = VK_NULL_HANDLE;  // 2D UI overlay
    VkPipelineLayout         m_uiPipelineLayout  = VK_NULL_HANDLE;
    VkPipeline               m_objectPipeline    = VK_NULL_HANDLE;  // Instanced low-poly props and dressing
    VkPipeline               m_grassPipeline     = VK_NULL_HANDLE;  // Instanced alpha-card grass

    // Post-process: scene → offscreen color, then fullscreen pass → swapchain
    VkRenderPass             m_postRenderPass          = VK_NULL_HANDLE;
    VkPipeline               m_postPipeline            = VK_NULL_HANDLE;
    VkPipelineLayout         m_postPipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_postDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool         m_postDescriptorPool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_postDescriptorSets;
    VkSampler                m_postSampler             = VK_NULL_HANDLE;
    std::vector<VkImage>        m_offscreenImage;   // per frame in flight
    std::vector<VkDeviceMemory> m_offscreenMemory;
    std::vector<VkImageView>    m_offscreenView;

    // SMAA 1x: scene color -> edge weights -> blend weights -> swapchain
    VkRenderPass             m_smaaRenderPass                 = VK_NULL_HANDLE;
    VkPipeline               m_smaaEdgePipeline               = VK_NULL_HANDLE;
    VkPipelineLayout         m_smaaEdgePipelineLayout         = VK_NULL_HANDLE;
    VkPipeline               m_smaaBlendPipeline              = VK_NULL_HANDLE;
    VkPipelineLayout         m_smaaBlendPipelineLayout        = VK_NULL_HANDLE;
    VkPipeline               m_smaaNeighborhoodPipeline       = VK_NULL_HANDLE;
    VkPipelineLayout         m_smaaNeighborhoodPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_smaaEdgeDescriptorSetLayout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_smaaBlendDescriptorSetLayout   = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_smaaNeighborhoodDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool         m_smaaDescriptorPool             = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_smaaEdgeDescriptorSets;
    std::vector<VkDescriptorSet> m_smaaBlendDescriptorSets;
    std::vector<VkDescriptorSet> m_smaaNeighborhoodDescriptorSets;
    std::vector<VkFramebuffer>   m_smaaEdgeFramebuffers;
    std::vector<VkFramebuffer>   m_smaaBlendFramebuffers;
    std::vector<VkImage>        m_smaaEdgeImage;
    std::vector<VkDeviceMemory> m_smaaEdgeMemory;
    std::vector<VkImageView>    m_smaaEdgeView;
    std::vector<VkImage>        m_smaaBlendImage;
    std::vector<VkDeviceMemory> m_smaaBlendMemory;
    std::vector<VkImageView>    m_smaaBlendView;
    // SMAA precomputed LUTs (sampled through the shared m_postSampler, no own sampler).
    TextureResource m_smaaAreaTex;
    TextureResource m_smaaSearchTex;

    GpuBuffer                m_vertexBuffer;
    GpuBuffer                m_indexBuffer;
    struct ChunkRenderData {
        GpuBuffer      vertexBuffer;
        GpuBuffer      indexBuffer;
        uint32_t       indexCount   = 0;
        // Per-chunk object instances, one buffer group per ObjectType present
        struct ObjGroup {
            ObjectType     type   = ObjectType::TREE;
            GpuBuffer      buffer;
            uint32_t       count  = 0;
        };
        std::vector<ObjGroup> objGroups;
        GpuBuffer      grassBuffer;
        uint32_t       grassCount = 0;
        GpuBuffer      groundPatchBuffer;
        uint32_t       groundPatchCount = 0;
        GpuBuffer      pebbleBuffer;
        uint32_t       pebbleCount = 0;
    };
    std::unordered_map<glm::ivec2, ChunkRenderData, IVec2Hash> m_chunkBuffers;
    Frustum                  m_frustum;

    // Shared low-poly object meshes, indexed by ObjectType (instanced per Object)
    struct ObjectMesh {
        GpuBuffer      vbuf;
        uint32_t       count = 0;
    };
    std::array<ObjectMesh, (size_t)ObjectType::COUNT> m_objectMeshes;
    ObjectMesh m_grassClumpMesh;
    ObjectMesh m_grassCardMesh;
    ObjectMesh m_groundPatchMesh;
    ObjectMesh m_pebbleMesh;

    // Grass blade textures (sampled by the grass card pipeline). The opacity mask is
    // split out so foliage material maps can expand without repacking the color image.
    TextureResource m_grassTex;
    TextureResource m_grassOpacityTex;

    // Terrain material texture array (sampler2DArray, one layer per tile material).
    TextureResource m_terrainTex;

    // Dropped items — shared small cube mesh + per-frame instance buffer (reuses m_indexBuffer + m_pipeline)
    static constexpr uint32_t   MAX_DROPS = 256;
    GpuBuffer                   m_itemVertexBuffer;
    std::vector<GpuBuffer>      m_dropInstBuffer;
    uint32_t                    m_dropCount = 0;

    // UI / hotbar — one buffer per frame in flight (avoids overwrite while GPU still reads)
    static constexpr uint32_t   UI_MAX_VERTS = 2048;
    std::vector<GpuBuffer>      m_uiBuffer;
    uint32_t                 m_uiVertexCount   = 0;
    int                      m_hotbarSelected  = 0;
    int                      m_dayHud          = 0;
    std::array<ItemStack, INV_SLOTS> m_invHud{};
    bool                     m_inventoryOpen   = false;
    bool                     m_mainMenuHud      = false;
    bool                     m_settingsHud      = false;
    bool                     m_loadingHud       = false;
    bool                     m_pausedHud       = false;
    bool                     m_vsyncHud        = true;
    int                      m_aaModeHud       = 0;
    bool                     m_nearWorkbenchHud = false;
    std::array<float, 4>     m_skyColor        = {0.08f, 0.08f, 0.12f, 1.0f};
    std::vector<GpuBuffer>      m_playerInstBuffer;
    GpuBuffer                m_selectorVertexBuffer;
    GpuBuffer                m_selectorIndexBuffer;
    std::vector<GpuBuffer>      m_selectorInstBuffer;
    bool                     m_showSelector          = false;

    VkImage                      m_depthImage           = VK_NULL_HANDLE;
    VkDeviceMemory               m_depthImageMemory     = VK_NULL_HANDLE;
    VkImageView                  m_depthImageView       = VK_NULL_HANDLE;

    static constexpr uint32_t    SHADOW_MAP_SIZE        = 2048;
    VkImage                      m_shadowImage          = VK_NULL_HANDLE;
    VkDeviceMemory               m_shadowImageMemory    = VK_NULL_HANDLE;
    VkImageView                  m_shadowImageView      = VK_NULL_HANDLE;
    VkRenderPass                 m_shadowRenderPass     = VK_NULL_HANDLE;
    VkFramebuffer                m_shadowFramebuffer    = VK_NULL_HANDLE;
    VkPipelineLayout             m_shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline                   m_shadowPipeline       = VK_NULL_HANDLE;
    VkPipeline                   m_shadowObjectPipeline = VK_NULL_HANDLE;  // instanced tree shadow caster
    VkPipeline                   m_shadowPlayerPipeline = VK_NULL_HANDLE;  // player cube shadow caster
    VkPipelineLayout             m_shadowGrassPipelineLayout = VK_NULL_HANDLE;
    VkPipeline                   m_shadowGrassPipeline       = VK_NULL_HANDLE;  // alpha-tested grass caster
    VkDescriptorSetLayout        m_shadowGrassDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool             m_shadowGrassDescriptorPool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_shadowGrassDescriptorSets;
    VkSampler                    m_shadowSampler        = VK_NULL_HANDLE;
    glm::mat4                    m_lightMVP             = glm::mat4(1.0f);
    glm::vec3                    m_shadowCenter         = glm::vec3(0.0f);
    glm::vec3                    m_sunDir               = glm::vec3(0.0f, 0.0f, 1.0f);
    float                        m_dayFactor            = 0.0f;

    VkDescriptorSetLayout        m_descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<GpuBuffer>       m_uniformBuffers;
    VkDescriptorPool             m_descriptorPool   = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    VkCommandPool            m_commandPool      = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> m_imageAvailable;   // per frame in flight
    std::vector<VkSemaphore> m_renderFinished;   // per swapchain image (present wait)
    std::vector<VkFence>     m_inFlight;          // per frame in flight
    std::vector<VkFence>     m_imagesInFlight;    // per swapchain image; non-owning fence refs
    uint32_t                 m_currentFrame     = 0;

#ifdef PASTEL_DEV_BUILD
    static constexpr uint32_t DEV_TIMESTAMP_COUNT = 5; // start, shadow, scene, post, imgui/end

    struct DevGpuTiming {
        bool  valid    = false;
        float totalMs  = 0.0f;
        float shadowMs = 0.0f;
        float sceneMs  = 0.0f;
        float postMs   = 0.0f;
        float imguiMs  = 0.0f;
    };

    VkDescriptorPool m_devDescriptorPool = VK_NULL_HANDLE;
    VkQueryPool      m_devQueryPool      = VK_NULL_HANDLE;
    float            m_devTimestampPeriod = 0.0f;
    bool             m_devTimingSupported = false;
    bool             m_devUiVisible       = true;
    bool             m_devFrameStarted    = false;
    DevGpuTiming     m_devGpuTiming;
    std::array<bool, MAX_FRAMES_IN_FLIGHT> m_devQueriesWritten{};
#endif

    struct DeferredDelete {
        GpuBuffer      buffer;
        uint64_t       frame  = 0;
    };
    std::vector<DeferredDelete> m_deletionQueue;
    uint64_t                    m_frameCount = 0;
};
