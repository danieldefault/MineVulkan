#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "core/mesher.h"
#include "core/ui.h"

struct CameraParams {
    Mat4 viewProj;  // sem pré-rotação (o renderer aplica)
    Vec3 position;
    float fogEnd = 100.0f;
    float fogColor[4] = {0.62f, 0.78f, 0.98f, 1.0f};
};

// Renderer Vulkan 1.0 voltado a GPUs mobile (Mali/Adreno):
//  - profundidade transitória (LAZILY_ALLOCATED, fica na memória do tile)
//  - pré-rotação do swapchain (evita rotação pelo compositor no Android)
//  - um index buffer compartilhado para todos os quads
//  - frustum culling por chunk
class Renderer {
public:
    static constexpr int kFramesInFlight = 2;

    bool createInstance(const std::vector<const char*>& platformExtensions, bool enableValidation);
    VkInstance instance() const { return instance_; }

    // Recebe uma surface já criada pela plataforma. Cria o device na primeira vez.
    // fallbackWidth/Height: usado se o driver não informar currentExtent.
    bool attachSurface(VkSurfaceKHR surface, uint32_t fallbackWidth, uint32_t fallbackHeight);
    void detachSurface();  // Android: janela destruída (app em segundo plano)
    bool hasSurface() const { return surface_ != VK_NULL_HANDLE; }
    void notifyResize(uint32_t fallbackWidth, uint32_t fallbackHeight);

    void uploadBlockTextures(const std::vector<std::vector<uint32_t>>& mipLevels, uint32_t layers);

    // Ciclo do frame: beginFrame() -> (uploads de malha) -> endFrame().
    bool beginFrame();
    void endFrame(const CameraParams& cam, const std::vector<UiVertex>& ui);

    void uploadChunkMesh(int64_t key, int cx, int cz, const std::vector<ChunkVertex>& verts);
    void freeChunkMesh(int64_t key);

    // Tamanho lógico (orientação atual da tela) — usar para aspect ratio e UI.
    float logicalWidth() const { return static_cast<float>(logicalExtent_.width); }
    float logicalHeight() const { return static_cast<float>(logicalExtent_.height); }

    uint32_t lastDrawnChunks() const { return lastDrawnChunks_; }
    uint32_t validationErrors() const { return validationErrors_; }

    void shutdown();

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct GpuMesh {
        Buffer vb;
        uint32_t quadCount = 0;
        int cx = 0, cz = 0;
    };
    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        Buffer uiBuffer;
        std::vector<Buffer> garbage;  // liberado quando este slot voltar a ser usado
    };

    bool createDevice();
    bool createSwapchain();
    void destroySwapchain(bool keepHandleForReuse);
    void createRenderPass();
    void createPipelines();
    void createDepth();
    uint32_t findMemoryType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred = 0) const;
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(Buffer& b);
    VkShaderModule createShader(const uint32_t* code, size_t bytes);
    void recordFrame(Frame& f, uint32_t imageIndex, const CameraParams& cam, const std::vector<UiVertex>& ui);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT type,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                        void* user);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};         // dimensões do swapchain (orientação nativa)
    VkExtent2D logicalExtent_{};  // orientação atual (após rotação)
    VkExtent2D fallbackExtent_{};
    Rot2 preRotation_;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderFinished_;  // um por imagem do swapchain
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    bool swapchainDirty_ = false;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet textureSet_ = VK_NULL_HANDLE;
    VkPipelineLayout voxelLayout_ = VK_NULL_HANDLE, uiLayout_ = VK_NULL_HANDLE;
    VkPipeline voxelPipeline_ = VK_NULL_HANDLE, uiPipeline_ = VK_NULL_HANDLE;

    VkImage texImage_ = VK_NULL_HANDLE;
    VkDeviceMemory texMemory_ = VK_NULL_HANDLE;
    VkImageView texView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    Buffer indexBuffer_;
    uint32_t maxQuads_ = 0;
    std::unordered_map<int64_t, GpuMesh> meshes_;

    Frame frames_[kFramesInFlight];
    uint32_t frameIndex_ = 0;
    uint32_t currentImage_ = 0;
    bool frameActive_ = false;
    uint32_t lastDrawnChunks_ = 0;
    uint32_t validationErrors_ = 0;
};
