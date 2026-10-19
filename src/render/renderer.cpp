#include "renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "core/world.h"

namespace {

#define VK_CHECK(expr)                                                         \
    do {                                                                       \
        VkResult r_ = (expr);                                                  \
        if (r_ != VK_SUCCESS) {                                                \
            LOGE("Vulkan: %s falhou (%d) em %s:%d", #expr, r_, __FILE__, __LINE__); \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

const uint32_t kVoxelVert[] =
#include "voxel.vert.inc"
    ;
const uint32_t kVoxelFrag[] =
#include "voxel.frag.inc"
    ;
const uint32_t kUiVert[] =
#include "ui.vert.inc"
    ;
const uint32_t kUiFrag[] =
#include "ui.frag.inc"
    ;

// Troque para false se a imagem aparecer girada em algum aparelho: o compositor
// do Android passa a fazer a rotação (custa um pouco de desempenho).
constexpr bool kUsePreRotation = true;
constexpr uint32_t kMaxUiVertices = 16384;

struct VoxelPush {
    float viewProj[16];
    float chunkOrigin[4];
    float camPos[4];
    float fogColor[4];
};
static_assert(sizeof(VoxelPush) == 112, "push constant deve caber em 128 bytes");

struct UiPush {
    float scale[4];
    float rot[4];
};

bool hasStencil(VkFormat f) { return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT; }

}  // namespace

// ---------------------------------------------------------------- instance / device

VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                       VkDebugUtilsMessageTypeFlagsEXT,
                                                       const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                       void* user) {
    auto* self = static_cast<Renderer*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++self->validationErrors_;
        LOGE("VALIDATION: %s", data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOGW("VALIDATION: %s", data->pMessage);
    }
    return VK_FALSE;
}

bool Renderer::createInstance(const std::vector<const char*>& platformExtensions, bool enableValidation) {
    std::vector<const char*> exts = platformExtensions;
    std::vector<const char*> layers;
    if (enableValidation) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> props(n);
        vkEnumerateInstanceLayerProperties(&n, props.data());
        bool found = false;
        for (const auto& p : props) found |= std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        if (found) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            LOGW("Camada de validação não encontrada; seguindo sem ela");
            enableValidation = false;
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "VoxelCraft";
    app.applicationVersion = 1;
    app.pEngineName = "VoxelCraft";
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) {
        LOGE("vkCreateInstance falhou: %d", r);
        return false;
    }

    if (enableValidation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = debugCallback;
            mi.pUserData = this;
            create(instance_, &mi, nullptr, &messenger_);
        }
        LOGI("Validação Vulkan ativa");
    }
    return true;
}

bool Renderer::createDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());

    int bestScore = -1;
    for (VkPhysicalDevice pd : devs) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qprops.data());
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
            if (!(qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(pd, &p);
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                        : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                physical_ = pd;
                queueFamily_ = i;
            }
            break;
        }
    }
    if (!physical_) {
        LOGE("Nenhuma GPU com suporte a gráficos + apresentação");
        return false;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_, &props);
    LOGI("GPU: %s (Vulkan %u.%u.%u)", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
         VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
    vkGetPhysicalDeviceMemoryProperties(physical_, &memProps_);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    VK_CHECK(vkCreateDevice(physical_, &dci, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &cmdPool_));

    for (auto& f : frames_) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cmdPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &f.cmd));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(device_, &fci, nullptr, &f.fence));
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &f.imageAvailable));
        f.uiBuffer = createBuffer(sizeof(UiVertex) * kMaxUiVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    }

    // Índices compartilhados: pior caso de um chunk (tabuleiro de xadrez 3D).
    maxQuads_ = CHUNK_VOLUME * 3;
    indexBuffer_ = createBuffer(static_cast<VkDeviceSize>(maxQuads_) * 6 * sizeof(uint32_t),
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true);
    auto* idx = static_cast<uint32_t*>(indexBuffer_.mapped);
    for (uint32_t q = 0; q < maxQuads_; ++q) {
        uint32_t b = q * 4;
        idx[q * 6 + 0] = b; idx[q * 6 + 1] = b + 1; idx[q * 6 + 2] = b + 2;
        idx[q * 6 + 3] = b + 2; idx[q * 6 + 4] = b + 3; idx[q * 6 + 5] = b;
    }

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = VK_LOD_CLAMP_NONE;  // sem clamp de LOD (recomendação Arm)
    VK_CHECK(vkCreateSampler(device_, &si, nullptr, &sampler_));

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings = &b;
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &setLayout_));
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VK_CHECK(vkCreateDescriptorPool(device_, &dpi, nullptr, &descPool_));
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descPool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &setLayout_;
    VK_CHECK(vkAllocateDescriptorSets(device_, &dai, &textureSet_));

    VkPushConstantRange vr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VoxelPush)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &vr;
    VK_CHECK(vkCreatePipelineLayout(device_, &pli, nullptr, &voxelLayout_));
    VkPushConstantRange ur{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPush)};
    VkPipelineLayoutCreateInfo uli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    uli.pushConstantRangeCount = 1;
    uli.pPushConstantRanges = &ur;
    VK_CHECK(vkCreatePipelineLayout(device_, &uli, nullptr, &uiLayout_));

    // Formato de profundidade: 24 bits se possível (16 bits gera z-fighting ao longe).
    const VkFormat candidates[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT,
                                   VK_FORMAT_D16_UNORM};
    for (VkFormat f : candidates) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(physical_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat_ = f;
            break;
        }
    }
    return true;
}

uint32_t Renderer::findMemoryType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const {
    for (int pass = 0; pass < 2; ++pass) {
        VkMemoryPropertyFlags want = pass == 0 ? (required | preferred) : required;
        for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    LOGE("Tipo de memória não encontrado");
    std::abort();
}

Renderer::Buffer Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device_, &bi, nullptr, &b.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = hostVisible
        ? findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        : findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &b.memory));
    VK_CHECK(vkBindBufferMemory(device_, b.buffer, b.memory, 0));
    if (hostVisible) VK_CHECK(vkMapMemory(device_, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    return b;
}

void Renderer::destroyBuffer(Buffer& b) {
    if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device_, b.memory, nullptr);  // libera o mapeamento junto
    b = Buffer{};
}

VkShaderModule Renderer::createShader(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &m));
    return m;
}

// ---------------------------------------------------------------- surface / swapchain

bool Renderer::attachSurface(VkSurfaceKHR surface, uint32_t fallbackWidth, uint32_t fallbackHeight) {
    surface_ = surface;
    fallbackExtent_ = {fallbackWidth, fallbackHeight};
    if (!device_ && !createDevice()) return false;
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_, queueFamily_, surface_, &present);
    if (!present) {
        LOGE("Fila não suporta apresentação nesta surface");
        return false;
    }
    return createSwapchain();
}

void Renderer::notifyResize(uint32_t fallbackWidth, uint32_t fallbackHeight) {
    fallbackExtent_ = {fallbackWidth, fallbackHeight};
    swapchainDirty_ = true;
}

bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps));

    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fn, formats.data());
    // UNORM de propósito: texturas e cores são tratadas em espaço gamma (visual "clássico").
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                                : formats[0];
    if (chosen.format == VK_FORMAT_UNDEFINED) chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto& f : formats)
        if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }

    VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (kUsePreRotation && (caps.supportedTransforms & caps.currentTransform)) transform = caps.currentTransform;
    else if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) transform = caps.currentTransform;

    VkExtent2D current = caps.currentExtent;
    if (current.width == 0xFFFFFFFFu) {
        current.width = std::clamp(fallbackExtent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        current.height = std::clamp(fallbackExtent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (current.width == 0 || current.height == 0) return false;  // janela minimizada

    // No Android, currentExtent vem na orientação atual; com pré-rotação de 90/270
    // o swapchain é criado na orientação nativa (dimensões trocadas).
    bool swapped = transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR || transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    logicalExtent_ = current;
    extent_ = swapped ? VkExtent2D{current.height, current.width} : current;
    switch (transform) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: preRotation_ = {0, -1, 1, 0}; break;
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: preRotation_ = {-1, 0, 0, -1}; break;
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: preRotation_ = {0, 1, -1, 0}; break;
        default: preRotation_ = {1, 0, 0, 1}; break;
    }

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        // Muitos drivers Android só expõem INHERIT.
        const VkCompositeAlphaFlagBitsKHR options[] = {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                                       VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                       VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
        for (auto o : options)
            if (caps.supportedCompositeAlpha & o) { alpha = o; break; }
    }

    uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainKHR old = swapchain_;
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = imageCount;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = transform;
    sci.compositeAlpha = alpha;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // vsync, sempre disponível, economiza bateria
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = old;
    VK_CHECK(vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_));
    if (old) vkDestroySwapchainKHR(device_, old, nullptr);

    if (chosen.format != colorFormat_) {
        // Primeira vez (ou formato mudou): render pass e pipelines dependem do formato.
        if (voxelPipeline_) vkDestroyPipeline(device_, voxelPipeline_, nullptr);
        if (uiPipeline_) vkDestroyPipeline(device_, uiPipeline_, nullptr);
        if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
        colorFormat_ = chosen.format;
        createRenderPass();
        createPipelines();
    }

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, images_.data());
    createDepth();
    imageViews_.resize(n);
    framebuffers_.resize(n);
    renderFinished_.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = colorFormat_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &imageViews_[i]));
        VkImageView atts[] = {imageViews_[i], depthView_};
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = renderPass_;
        fi.attachmentCount = 2;
        fi.pAttachments = atts;
        fi.width = extent_.width;
        fi.height = extent_.height;
        fi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &fi, nullptr, &framebuffers_[i]));
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &renderFinished_[i]));
    }
    swapchainDirty_ = false;
    LOGI("Swapchain %ux%u (lógico %ux%u), %u imagens, transform=%d", extent_.width, extent_.height,
         logicalExtent_.width, logicalExtent_.height, n, static_cast<int>(transform));
    return true;
}

void Renderer::createDepth() {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = depthFormat_;
    ii.extent = {extent_.width, extent_.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device_, &ii, nullptr, &depthImage_));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, depthImage_, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    // Em GPUs tile-based a profundidade nunca sai da memória on-chip.
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                        VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
    VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &depthMemory_));
    VK_CHECK(vkBindImageMemory(device_, depthImage_, depthMemory_, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = depthImage_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = depthFormat_;
    vi.subresourceRange = {static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT |
                                                           (hasStencil(depthFormat_) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)),
                           0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &depthView_));
}

void Renderer::destroySwapchain(bool keepHandleForReuse) {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto v : imageViews_) vkDestroyImageView(device_, v, nullptr);
    for (auto s : renderFinished_) vkDestroySemaphore(device_, s, nullptr);
    framebuffers_.clear();
    imageViews_.clear();
    renderFinished_.clear();
    images_.clear();
    if (depthView_) vkDestroyImageView(device_, depthView_, nullptr);
    if (depthImage_) vkDestroyImage(device_, depthImage_, nullptr);
    if (depthMemory_) vkFreeMemory(device_, depthMemory_, nullptr);
    depthView_ = VK_NULL_HANDLE;
    depthImage_ = VK_NULL_HANDLE;
    depthMemory_ = VK_NULL_HANDLE;
    if (!keepHandleForReuse && swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void Renderer::detachSurface() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    destroySwapchain(false);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------- render pass / pipelines

void Renderer::createRenderPass() {
    VkAttachmentDescription atts[2]{};
    atts[0].format = colorFormat_;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    atts[1].format = depthFormat_;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // não escreve a profundidade de volta na RAM
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 2;
    rpi.pAttachments = atts;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(device_, &rpi, nullptr, &renderPass_));
}

void Renderer::createPipelines() {
    VkShaderModule vv = createShader(kVoxelVert, sizeof(kVoxelVert));
    VkShaderModule vf = createShader(kVoxelFrag, sizeof(kVoxelFrag));
    VkShaderModule uv = createShader(kUiVert, sizeof(kUiVert));
    VkShaderModule uf = createShader(kUiFrag, sizeof(kUiFrag));

    auto stages = [](VkShaderModule v, VkShaderModule f, VkPipelineShaderStageCreateInfo* out) {
        out[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        out[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        out[0].module = v;
        out[0].pName = "main";
        out[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        out[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        out[1].module = f;
        out[1].pName = "main";
    };

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    // --- voxels
    {
        VkPipelineShaderStageCreateInfo st[2];
        stages(vv, vf, st);
        VkVertexInputBindingDescription bind{0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attr[2] = {{0, 0, VK_FORMAT_R8G8B8A8_UINT, 0},
                                                     {1, 0, VK_FORMAT_R8G8B8A8_UINT, 4}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = attr;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        dss.depthTestEnable = VK_TRUE;
        dss.depthWriteEnable = VK_TRUE;
        dss.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &dss;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &ds;
        pi.layout = voxelLayout_;
        pi.renderPass = renderPass_;
        VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &voxelPipeline_));
    }
    // --- UI (2D, alpha blending, sem profundidade)
    {
        VkPipelineShaderStageCreateInfo st[2];
        stages(uv, uf, st);
        VkVertexInputBindingDescription bind{0, sizeof(UiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attr[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                                     {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 8}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = attr;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &dss;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &ds;
        pi.layout = uiLayout_;
        pi.renderPass = renderPass_;
        VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &uiPipeline_));
    }
    vkDestroyShaderModule(device_, vv, nullptr);
    vkDestroyShaderModule(device_, vf, nullptr);
    vkDestroyShaderModule(device_, uv, nullptr);
    vkDestroyShaderModule(device_, uf, nullptr);
}

// ---------------------------------------------------------------- texturas

void Renderer::uploadBlockTextures(const std::vector<std::vector<uint32_t>>& mipLevels, uint32_t layers) {
    const uint32_t levels = static_cast<uint32_t>(mipLevels.size());
    const uint32_t size0 = 1u << (levels - 1);
    VkDeviceSize total = 0;
    for (const auto& l : mipLevels) total += l.size() * sizeof(uint32_t);

    Buffer staging = createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize off = 0;
    for (uint32_t i = 0; i < levels; ++i) {
        std::memcpy(static_cast<uint8_t*>(staging.mapped) + off, mipLevels[i].data(), mipLevels[i].size() * 4);
        VkBufferImageCopy r{};
        r.bufferOffset = off;
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, layers};
        uint32_t s = std::max(1u, size0 >> i);
        r.imageExtent = {s, s, 1};
        regions.push_back(r);
        off += mipLevels[i].size() * 4;
    }

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {size0, size0, 1};
    ii.mipLevels = levels;
    ii.arrayLayers = layers;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device_, &ii, nullptr, &texImage_));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, texImage_, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &texMemory_));
    VK_CHECK(vkBindImageMemory(device_, texImage_, texMemory_, 0));

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = cmdPool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device_, &cai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = texImage_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layers};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &b);
    vkCmdCopyBufferToImage(cmd, staging.buffer, texImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue_));
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
    destroyBuffer(staging);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = texImage_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layers};
    VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &texView_));

    VkDescriptorImageInfo dii{sampler_, texView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = textureSet_;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

// ---------------------------------------------------------------- malhas

void Renderer::uploadChunkMesh(int64_t key, int cx, int cz, const std::vector<ChunkVertex>& verts) {
    auto it = meshes_.find(key);
    if (it != meshes_.end()) {
        // A malha antiga pode estar em uso por frames em voo: libera depois.
        frames_[frameIndex_].garbage.push_back(it->second.vb);
        meshes_.erase(it);
    }
    if (verts.empty()) return;
    uint32_t quads = static_cast<uint32_t>(verts.size() / 4);
    if (quads > maxQuads_) {
        LOGE("Chunk com %u quads excede o limite %u", quads, maxQuads_);
        return;
    }
    GpuMesh m;
    m.vb = createBuffer(verts.size() * sizeof(ChunkVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    std::memcpy(m.vb.mapped, verts.data(), verts.size() * sizeof(ChunkVertex));
    m.quadCount = quads;
    m.cx = cx;
    m.cz = cz;
    uint8_t minY = 255, maxY = 0;
    for (const auto& v : verts) {
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    m.minY = minY;
    m.maxY = maxY;
    meshes_[key] = m;
}

void Renderer::freeChunkMesh(int64_t key) {
    auto it = meshes_.find(key);
    if (it == meshes_.end()) return;
    frames_[frameIndex_].garbage.push_back(it->second.vb);
    meshes_.erase(it);
}

// ---------------------------------------------------------------- frame

bool Renderer::beginFrame() {
    if (!surface_ || !device_) return false;
    if (swapchainDirty_ || !swapchain_ || images_.empty()) {
        vkDeviceWaitIdle(device_);
        destroySwapchain(true);
        if (!createSwapchain()) return false;
    }
    Frame& f = frames_[frameIndex_];
    VK_CHECK(vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX));
    for (auto& b : f.garbage) destroyBuffer(b);
    f.garbage.clear();

    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &currentImage_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainDirty_ = true;
        return false;
    }
    if (r == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;  // apresenta este e recria depois
    else if (r != VK_SUCCESS) {
        LOGE("vkAcquireNextImageKHR: %d", r);
        return false;
    }
    VK_CHECK(vkResetFences(device_, 1, &f.fence));
    frameActive_ = true;
    return true;
}

void Renderer::recordFrame(Frame& f, uint32_t imageIndex, const CameraParams& cam, const std::vector<UiVertex>& ui) {
    VkCommandBuffer cmd = f.cmd;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkClearValue clears[2];
    clears[0].color = {{cam.fogColor[0], cam.fogColor[1], cam.fogColor[2], 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = renderPass_;
    rbi.framebuffer = framebuffers_[imageIndex];
    rbi.renderArea = {{0, 0}, extent_};
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0, 0, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // --- mundo
    Mat4 vp = rot2ToMat4(preRotation_) * cam.viewProj;
    Frustum frustum = Frustum::fromMatrix(cam.viewProj);
    VoxelPush push{};
    std::memcpy(push.viewProj, vp.m, sizeof(push.viewProj));
    push.camPos[0] = cam.position.x;
    push.camPos[1] = cam.position.y;
    push.camPos[2] = cam.position.z;
    push.camPos[3] = cam.fogEnd;
    std::memcpy(push.fogColor, cam.fogColor, sizeof(push.fogColor));
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelLayout_, 0, 1, &textureSet_, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, indexBuffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(cmd, voxelLayout_, stages, 0, sizeof(VoxelPush), &push);
    // Frente para trás: o early-Z descarta fragmentos escondidos antes do fragment shader
    // (menos overdraw, que pesa em GPUs mobile).
    drawList_.clear();
    for (const auto& kv : meshes_) {
        const GpuMesh& m = kv.second;
        Vec3 mn(static_cast<float>(m.cx * CHUNK_SIZE), m.minY, static_cast<float>(m.cz * CHUNK_SIZE));
        Vec3 mx(mn.x + CHUNK_SIZE, m.maxY, mn.z + CHUNK_SIZE);
        if (!frustum.aabbVisible(mn, mx)) continue;
        float dx = mn.x + CHUNK_SIZE * 0.5f - cam.position.x, dz = mn.z + CHUNK_SIZE * 0.5f - cam.position.z;
        drawList_.push_back({dx * dx + dz * dz, &m});
    }
    std::sort(drawList_.begin(), drawList_.end(), [](const DrawItem& a, const DrawItem& b) { return a.dist2 < b.dist2; });
    for (const DrawItem& d : drawList_) {
        const GpuMesh& m = *d.mesh;
        float origin[4] = {static_cast<float>(m.cx * CHUNK_SIZE), 0.0f, static_cast<float>(m.cz * CHUNK_SIZE), 0.0f};
        vkCmdPushConstants(cmd, voxelLayout_, stages, offsetof(VoxelPush, chunkOrigin), sizeof(origin), origin);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vb.buffer, &offset);
        vkCmdDrawIndexed(cmd, m.quadCount * 6, 1, 0, 0, 0);
    }
    lastDrawnChunks_ = static_cast<uint32_t>(drawList_.size());

    // --- interface
    uint32_t uiCount = static_cast<uint32_t>(std::min<size_t>(ui.size(), kMaxUiVertices));
    if (uiCount > 0) {
        std::memcpy(f.uiBuffer.mapped, ui.data(), uiCount * sizeof(UiVertex));
        UiPush up{};
        up.scale[0] = 2.0f / static_cast<float>(logicalExtent_.width);
        up.scale[1] = 2.0f / static_cast<float>(logicalExtent_.height);
        up.rot[0] = preRotation_.a;
        up.rot[1] = preRotation_.b;
        up.rot[2] = preRotation_.c;
        up.rot[3] = preRotation_.d;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
        vkCmdPushConstants(cmd, uiLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPush), &up);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &f.uiBuffer.buffer, &offset);
        vkCmdDraw(cmd, uiCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::endFrame(const CameraParams& cam, const std::vector<UiVertex>& ui) {
    if (!frameActive_) return;
    frameActive_ = false;
    Frame& f = frames_[frameIndex_];
    recordFrame(f, currentImage_, cam, ui);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.imageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished_[currentImage_];
    VK_CHECK(vkQueueSubmit(queue_, 1, &si, f.fence));

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished_[currentImage_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &currentImage_;
    VkResult r = vkQueuePresentKHR(queue_, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;
    else if (r != VK_SUCCESS) LOGE("vkQueuePresentKHR: %d", r);

    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

void Renderer::shutdown() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        for (auto& kv : meshes_) destroyBuffer(kv.second.vb);
        meshes_.clear();
        for (auto& f : frames_) {
            for (auto& b : f.garbage) destroyBuffer(b);
            f.garbage.clear();
            destroyBuffer(f.uiBuffer);
            vkDestroyFence(device_, f.fence, nullptr);
            vkDestroySemaphore(device_, f.imageAvailable, nullptr);
        }
        destroyBuffer(indexBuffer_);
        destroySwapchain(false);
        if (texView_) vkDestroyImageView(device_, texView_, nullptr);
        if (texImage_) vkDestroyImage(device_, texImage_, nullptr);
        if (texMemory_) vkFreeMemory(device_, texMemory_, nullptr);
        vkDestroySampler(device_, sampler_, nullptr);
        vkDestroyPipeline(device_, voxelPipeline_, nullptr);
        vkDestroyPipeline(device_, uiPipeline_, nullptr);
        vkDestroyPipelineLayout(device_, voxelLayout_, nullptr);
        vkDestroyPipelineLayout(device_, uiLayout_, nullptr);
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
        vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
        vkDestroyCommandPool(device_, cmdPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (messenger_) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance_, messenger_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}
