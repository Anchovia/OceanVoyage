#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <string>
#include <unordered_map>
#include <array>
#include <stdexcept>
#include "renderer/Types.h"
#include "renderer/Frustum.h"

class Window;
class Camera;

inline void vkCheck(VkResult result, const char* message) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(message);
}

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
    glm::vec3                                shipPosition;
    glm::vec3                                shipVelocity;
    float                                    shipHeading;    // radians; ship bow orientation
    float                                    shipThrottle;   // -1..1 (HUD)
    float                                    shipRudder;     // -1..1 (HUD)
    float                                    timeOfDay;
    float                                    gameTime;
    bool                                     mainMenu;
    bool                                     settings;
    bool                                     loading;
    bool                                     paused;
    bool                                     vsyncEnabled;
    int                                      aaMode;
};

class VulkanContext {
public:
    VulkanContext(Window& window);
    ~VulkanContext();

    void drawFrame(const FrameRenderData& frame);
    void waitIdle();

#ifdef PASTEL_DEV_BUILD
    void beginDevFrame();
    bool devWantsMouse() const;
    bool devWantsKeyboard() const;
    void toggleDevUi();
    float devMoveSpeedMultiplier() const { return m_devMoveSpeedMultiplier; }
#endif

private:
    static constexpr float    SHIP_WORLD_SCALE = 6.0f; // LSV018 source length ~5.6 -> ~34 world units
    static constexpr float    SHIP_VISUAL_DRAFT = 0.02f;
    static constexpr float    SHIP_WAKE_POWER = 1.45f;
    static constexpr uint32_t SHIP_HULL_PROFILE_SAMPLES = 16;

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
        bool             depthTest;          // depthTestEnable
        bool             depthWrite = true;  // transparent surfaces can test depth without writing it
        bool             alphaBlend;         // semi-transparent (UI/water)
        VkPipelineLayout layout;
        VkRenderPass     renderPass = VK_NULL_HANDLE;
    };
    VkPipeline createPipeline(const PipelineConfig& cfg);
    void createScenePipelineLayout();
    void createSkyPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createDescriptorSetLayout();
    void createUIPipeline();
    void createUIBuffer();
    void updateHotbar();
    void createOceanDescriptorSetLayout();
    void createOceanPipeline();
    void createOceanMesh();
    void createShipPipeline();
    void createPostRenderPass();
    void createOffscreenResources();
    void createPlanarReflectionResources();
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
    TextureResource createDDSBC1Texture(const std::string& path, bool srgb);
    // Layered variant for a sampler2DArray (bytes laid out layer-major, all same size).
    TextureResource createTextureArray(uint32_t width, uint32_t height, uint32_t layerCount,
        VkFormat format, const void* bytes, VkDeviceSize size, bool withSampler, bool mipmapped = false);
    void loadImportedShipMesh();
    void createOceanNormalTextures();
    void createUniformBuffers();
    void createReflectionUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createReflectionDescriptorSets();
    void createOceanDescriptors();
    void updateOceanDescriptors();
    void updateOceanHistoryDescriptor(uint32_t currentFrame);
    void copySceneColorForWater(VkCommandBuffer cmd);
    void copySceneDepthForWater(VkCommandBuffer cmd);
    void createOceanFFT();    // Tessendorf FFT ocean: compute resources + spectrum (VulkanContext_Ocean.cpp)
    void createOceanFFTSim();  // per-frame spectrum animation resources
    void createOceanFFTTransform(); // butterfly texture + IFFT pipeline + ping-pong
    void createOceanFFTAssemble();   // displacement map + assembly pipeline
    void createOceanWake();          // ship-driven wake mask simulation resources
    void recordOceanFFT(VkCommandBuffer cmd); // per-frame compute dispatch (records into the frame cmd buffer)
    void recordOceanWake(VkCommandBuffer cmd);
    void destroyOceanFFT();
    void updateUniformBuffer(uint32_t currentFrame, const Camera& camera, float gameTime);
    void updateReflectionUniformBuffer(uint32_t currentFrame, const Camera& camera, float gameTime);
    void updateShipTransform(const glm::vec3& position, float heading, float gameTime);
    void createDepthResources();
    void createShadowResources();
    void createShadowPipeline();
    void createShadowSampler();
    void createImage(uint32_t width, uint32_t height, VkFormat format,
        VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory,
        uint32_t mipLevels = 1, uint32_t arrayLayers = 1);
    VkFormat findSceneColorFormat();
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
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isSwapchainAdequate(VkPhysicalDevice device);

    // ---- Vulkan handles ----
    Window& m_window;

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
    VkFormat                 m_sceneColorFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_swapchainExtent  = {};
    std::vector<VkImageView> m_swapchainImageViews;
    bool                     m_vsyncEnabled     = true;
    std::vector<VkFramebuffer> m_sceneFramebuffers;  // offscreen color + depth (per frame in flight)
    std::vector<VkFramebuffer> m_postFramebuffers;   // swapchain (per image)

    VkRenderPass             m_renderPass        = VK_NULL_HANDLE;
    VkPipelineLayout         m_pipelineLayout    = VK_NULL_HANDLE;  // shared scene layout (sky/chunk/object/grass/ship)
    VkPipeline               m_skyPipeline       = VK_NULL_HANDLE;  // Procedural analytic sky background
    VkPipeline               m_uiPipeline        = VK_NULL_HANDLE;  // 2D UI overlay
    VkPipelineLayout         m_uiPipelineLayout  = VK_NULL_HANDLE;
    VkPipeline               m_oceanPipeline     = VK_NULL_HANDLE;  // Gerstner-wave ocean surface
    VkPipelineLayout         m_oceanPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_oceanDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool         m_oceanDescriptorPool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_oceanDescriptorSets;

    // FFT ocean (Tessendorf) compute resources. Phase 0: initial spectrum h0(k) only.
    static constexpr uint32_t OCEAN_FFT_N = 512;  // FFT resolution per axis (power of two)
    static constexpr uint32_t OCEAN_CASCADES = 3;  // multi-scale FFT cascades (array layers)
    // World size (m) of each cascade tile, largest → smallest. Must match the ocean shaders.
    static constexpr float    OCEAN_CASCADE_L[OCEAN_CASCADES] = { 2048.0f, 512.0f, 128.0f };
    VkImage               m_oceanH0Image  = VK_NULL_HANDLE; // rg = h0(k), ba = conj(h0(-k))
    VkDeviceMemory        m_oceanH0Memory = VK_NULL_HANDLE;
    VkImageView           m_oceanH0View   = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_oceanSpectrumDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_oceanFFTDescriptorPool           = VK_NULL_HANDLE;
    VkDescriptorSet       m_oceanSpectrumDescriptorSet       = VK_NULL_HANDLE;
    VkPipelineLayout      m_oceanSpectrumPipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_oceanSpectrumPipeline            = VK_NULL_HANDLE;
    // Per-frame animated spectrum H(k,t): rg = Dy+i·Dx, ba = Dz.
    VkImage               m_oceanSpectrumImage  = VK_NULL_HANDLE;
    VkDeviceMemory        m_oceanSpectrumMemory = VK_NULL_HANDLE;
    VkImageView           m_oceanSpectrumView   = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_oceanUpdateDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_oceanUpdateDescriptorSet       = VK_NULL_HANDLE;
    VkPipelineLayout      m_oceanUpdatePipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_oceanUpdatePipeline            = VK_NULL_HANDLE;
    float                 m_oceanTime = 0.0f; // game time fed to the FFT spectrum each frame

    // Butterfly IFFT: the spectrum image doubles as one ping-pong buffer; m_oceanFFTPong
    // is the other. The butterfly texture drives the radix-2 passes.
    static constexpr uint32_t OCEAN_FFT_LOG2N = 9; // log2(OCEAN_FFT_N); 1<<9 == 512
    VkImage               m_oceanFFTPongImage  = VK_NULL_HANDLE;
    VkDeviceMemory        m_oceanFFTPongMemory = VK_NULL_HANDLE;
    VkImageView           m_oceanFFTPongView   = VK_NULL_HANDLE;
    TextureResource       m_oceanButterflyTex; // (tw.re, tw.im, topIdx, botIdx) per stage/index
    VkDescriptorSetLayout m_oceanFFTDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_oceanFFTSetPingToPong       = VK_NULL_HANDLE; // src=spectrum(ping), dst=pong
    VkDescriptorSet       m_oceanFFTSetPongToPing       = VK_NULL_HANDLE; // src=pong, dst=spectrum(ping)
    VkPipelineLayout      m_oceanFFTPipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_oceanFFTPipeline            = VK_NULL_HANDLE;

    // Assembled world-space displacement map (R16F, GENERAL): sampled by the ocean vertex
    // shader. xyz = (choppy x, choppy z, height), w = Jacobian whitecap seed.
    // Double-buffered (one per frame in flight): the assemble pass writes frame i's map while
    // the previous frame's graphics still reads frame (i-1)'s map, so the ocean compute can
    // overlap the previous frame's rendering instead of serializing behind it.
    std::vector<VkImage>        m_oceanDisplacementImage;
    std::vector<VkDeviceMemory> m_oceanDisplacementMemory;
    std::vector<VkImageView>    m_oceanDisplacementView;
    VkSampler             m_oceanDisplacementSampler = VK_NULL_HANDLE; // linear, repeat (shared)
    // Displacement copied back to host memory each frame so the CPU can float the ship on the
    // actual FFT surface (one buffer per frame in flight; read with a 2-frame latency).
    std::vector<GpuBuffer> m_oceanReadbackBuffers;
    VkDescriptorSetLayout m_oceanAssembleDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_oceanAssembleDescriptorSets; // one per frame (write target differs)
    VkPipelineLayout      m_oceanAssemblePipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_oceanAssemblePipeline            = VK_NULL_HANDLE;

    // Per-cascade surface-slope map (RGBA16F, GENERAL): .rg = world height gradient (dH/dx, dH/dy).
    // The assemble pass writes it from exact texel neighbours so the water shader can build smooth
    // normals by bilinearly sampling the slope (no texel-grid artefacts from differencing the
    // bilinear displacement). Shares the displacement sampler (linear, repeat).
    std::vector<VkImage>        m_oceanSlopeImage;  // double-buffered, mirrors the displacement map
    std::vector<VkDeviceMemory> m_oceanSlopeMemory;
    std::vector<VkImageView>    m_oceanSlopeView;

    // Ship wake mask, ping-ponged per frame so wake foam/turbulence persists through
    // decay/diffusion/advection instead of being painted directly in the water shader.
    static constexpr uint32_t OCEAN_WAKE_N = 1024;
    static constexpr float    OCEAN_WAKE_WORLD_SIZE = 1024.0f;
    std::vector<VkImage>        m_oceanWakeImage;
    std::vector<VkDeviceMemory> m_oceanWakeMemory;
    std::vector<VkImageView>    m_oceanWakeView;
    VkDescriptorSetLayout       m_oceanWakeDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_oceanWakeDescriptorSets;
    VkPipelineLayout            m_oceanWakePipelineLayout = VK_NULL_HANDLE;
    VkPipeline                  m_oceanWakePipeline       = VK_NULL_HANDLE;
    glm::vec2                   m_oceanWakeShipPosition{15.0f, 15.0f};
    glm::vec2                   m_oceanWakeShipVelocity{0.0f, 0.0f};
    float                       m_oceanWakeShipHeading = 0.0f;
    float                       m_oceanWakeDeltaTime   = 0.0f;
    float                       m_oceanWakePrevTime    = 0.0f;
    bool                        m_oceanWakeHasPrevTime = false;

    VkPipeline               m_shipPipeline       = VK_NULL_HANDLE; // Hero ship (push-constant model matrix)
    VkPipelineLayout         m_shipPipelineLayout = VK_NULL_HANDLE;

    // Post-process: scene → offscreen color, then fullscreen pass → swapchain
    VkRenderPass             m_sceneLoadRenderPass      = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_sceneLoadFramebuffers;
    VkRenderPass             m_postRenderPass          = VK_NULL_HANDLE;
    VkPipeline               m_postPipeline            = VK_NULL_HANDLE;
    VkPipelineLayout         m_postPipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_postDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool         m_postDescriptorPool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_postDescriptorSets;
    VkSampler                m_postSampler             = VK_NULL_HANDLE;
    VkSampler                m_sceneDepthSampler       = VK_NULL_HANDLE;
    std::vector<VkImage>        m_offscreenImage;   // per frame in flight
    std::vector<VkDeviceMemory> m_offscreenMemory;
    std::vector<VkImageView>    m_offscreenView;
    glm::mat4                    m_prevViewProj = glm::mat4(1.0f);
    uint32_t                     m_temporalHistoryFrames = 0;
    std::vector<VkImage>        m_sceneColorCopyImage;
    std::vector<VkDeviceMemory> m_sceneColorCopyMemory;
    std::vector<VkImageView>    m_sceneColorCopyView;
    std::vector<bool>           m_sceneColorCopyReady;

    // Planar water reflection: mirrored scene color sampled by the ocean shader.
    std::vector<VkFramebuffer>   m_reflectionFramebuffers;
    std::vector<VkImage>         m_reflectionImage;
    std::vector<VkDeviceMemory>  m_reflectionMemory;
    std::vector<VkImageView>     m_reflectionView;

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

    GpuBuffer                m_oceanVertexBuffer; // flat grid displaced by the ocean vertex shader
    GpuBuffer                m_oceanIndexBuffer;
    uint32_t                 m_oceanIndexCount = 0;
    Frustum                  m_frustum;
    Frustum                  m_reflectionFrustum;

    // Shared low-poly object meshes, indexed by ObjectType (instanced per Object)
    struct ObjectMesh {
        GpuBuffer      vbuf;
        uint32_t       count = 0;
    };
    ObjectMesh m_shipMesh;   // Imported hero ship hull (drawn via the ship pipeline)
    glm::mat4  m_shipModel = glm::mat4(1.0f); // ship world transform (bob + wave tilt + heading)
    struct ShipHullProfile {
        glm::vec3 localBoundsMin{0.0f};
        glm::vec3 localBoundsMax{0.0f};
        float sternOffset = 16.8f;      // world metres from model origin toward stern
        float bowOffset = 18.2f;        // world metres from model origin toward bow
        float centerlineOffset = 0.0f;  // world metres on ship local Y
        float halfBeam = 3.0f;          // maximum half width in world metres
        float waterlineCutZ = 0.0f;     // local Z cut used for footprint extraction
        std::array<float, SHIP_HULL_PROFILE_SAMPLES> halfWidthSamples{};
    };
    ShipHullProfile m_shipHullProfile;
    TextureResource m_shipAlbedoTex;
    TextureResource m_shipNormalTex;
    TextureResource m_shipSpecularTex;

    // Multi-scale ocean normal maps (UNORM, mipmapped, anisotropic).
    TextureResource m_oceanNormalA;
    TextureResource m_oceanNormalB;


    // UI / hotbar — one buffer per frame in flight (avoids overwrite while GPU still reads)
    static constexpr uint32_t   UI_MAX_VERTS = 2048;
    std::vector<GpuBuffer>      m_uiBuffer;
    uint32_t                 m_uiVertexCount   = 0;
    bool                     m_mainMenuHud      = false;
    bool                     m_settingsHud      = false;
    bool                     m_loadingHud       = false;
    bool                     m_pausedHud       = false;
    bool                     m_vsyncHud        = true;
    int                      m_aaModeHud       = 0;
    float                    m_shipSpeedHud    = 0.0f;
    float                    m_shipHeadingHud  = 0.0f; // radians
    float                    m_shipThrottleHud = 0.0f; // -1..1
    float                    m_shipRudderHud   = 0.0f; // -1..1
    std::array<float, 4>     m_skyColor        = {0.08f, 0.08f, 0.12f, 1.0f};

    VkImage                      m_depthImage           = VK_NULL_HANDLE;
    VkDeviceMemory               m_depthImageMemory     = VK_NULL_HANDLE;
    VkImageView                  m_depthImageView       = VK_NULL_HANDLE;
    std::vector<VkImage>         m_sceneDepthCopyImage;
    std::vector<VkDeviceMemory>  m_sceneDepthCopyMemory;
    std::vector<VkImageView>     m_sceneDepthCopyView;
    std::vector<bool>            m_sceneDepthCopyReady;

    static constexpr uint32_t    SHADOW_MAP_SIZE        = 2048;
    static constexpr uint32_t    CSM_CASCADES           = 3; // cascaded shadow map slices
    VkImage                      m_shadowImage          = VK_NULL_HANDLE; // depth array, one layer per cascade
    VkDeviceMemory               m_shadowImageMemory    = VK_NULL_HANDLE;
    VkImageView                  m_shadowImageView      = VK_NULL_HANDLE; // 2D_ARRAY view, sampled by receivers
    std::array<VkImageView, CSM_CASCADES>   m_shadowLayerView{};  // per-cascade 2D views for the framebuffers
    VkRenderPass                 m_shadowRenderPass     = VK_NULL_HANDLE;
    std::array<VkFramebuffer, CSM_CASCADES> m_shadowFramebuffers{}; // one framebuffer per cascade layer
    VkPipelineLayout             m_shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline                   m_shadowPipeline       = VK_NULL_HANDLE;
    VkSampler                    m_shadowSampler        = VK_NULL_HANDLE;
    std::array<glm::mat4, CSM_CASCADES> m_lightMVPCascade{}; // per-cascade light-space transforms
    glm::vec4                    m_cascadeSplits        = glm::vec4(0.0f); // xyz = cascade far view-depths
    glm::vec3                    m_shadowCenter         = glm::vec3(0.0f);
    glm::vec3                    m_sunDir               = glm::vec3(0.0f, 0.0f, 1.0f);
    float                        m_dayFactor            = 0.0f;

    VkDescriptorSetLayout        m_descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<GpuBuffer>       m_uniformBuffers;
    VkDescriptorPool             m_descriptorPool   = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<GpuBuffer>       m_reflectionUniformBuffers;
    std::vector<VkDescriptorSet> m_reflectionDescriptorSets;

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
    float            m_devMoveSpeedMultiplier = 1.0f;
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
