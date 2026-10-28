// Vulkan 1.3 backend: dynamic rendering, bindless textures, 4x MSAA, and optional hard sun
// shadows from ray queries (VK_KHR_ray_query + acceleration structures). The game's immediate
// draw calls are recorded into a list and replayed at end_window, which lets the frame's TLAS
// be built from the same instances before the render pass starts.
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

#include "render/fsr.hpp"
#include "render/renderer.hpp"

namespace tto {
namespace {

const std::uint32_t kMeshVert[] = {
#include "mesh.vert.inc"
};
const std::uint32_t kMeshFrag[] = {
#include "mesh.frag.inc"
};
const std::uint32_t kMeshRtFrag[] = {
#include "mesh_rt.frag.inc"
};
const std::uint32_t kMeshMFrag[] = {
#include "mesh_m.frag.inc"
};
const std::uint32_t kMeshRtMFrag[] = {
#include "mesh_rt_m.frag.inc"
};
const std::uint32_t kQuadVert[] = {
#include "quad.vert.inc"
};
const std::uint32_t kQuadFrag[] = {
#include "quad.frag.inc"
};

constexpr std::uint32_t kFrames = 2;
constexpr std::uint32_t kMaxTextures = 4096;
constexpr VkFormat kColorFmt = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFmt = VK_FORMAT_D32_SFLOAT;

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::format("{} failed ({})", what, static_cast<int>(r)));
}

struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* map = nullptr;
    VkDeviceAddress addr = 0;
};

struct Img {
    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    int w = 0, h = 0;
    std::uint32_t mips = 1;
};

struct Blas {
    VkAccelerationStructureKHR as = VK_NULL_HANDLE;
    Buffer buf;
    VkDeviceAddress addr = 0;
};

struct MeshRes {
    Buffer vb;
    Blas blas;
    std::uint32_t frameVerts = 0;
};

struct alignas(16) Push {
    glm::mat4 model{1};
    glm::vec4 color{1};
    glm::vec4 uvx{0, 0, 1, 1};
    glm::vec4 rect{0};
    glm::uvec4 params{0};
    glm::mat4 prevModel{1};
};
static_assert(sizeof(Push) == 192);
constexpr VkFormat kMotionFmt = VK_FORMAT_R16G16_SFLOAT;

struct alignas(16) CamBlock {
    glm::mat4 view{1}, proj{1};
    glm::vec4 lightDir{0, -1, 0, 0}, amb{0.45f}, dif{0.75f}, fogColor{0}, fogRange{100, 600, 0, 0};
    glm::mat4 vp{1}, prevVp{1};  // unjittered, for motion vectors
};

// *M: the FSR scene pass (single sample, colour + motion attachments).
enum Pipe : std::uint8_t { kOpaque, kBlend, kSky, kQuad, kOpaqueM, kBlendM, kSkyM, kPipeCount };

struct Cmd {
    enum Kind : std::uint8_t { Viewport, Clear, Draw } kind = Draw;
    Pipe pipe = kOpaque;
    int x = 0, y = 0, w = 0, h = 0;
    glm::vec3 clear{0};
    VkBuffer vb = VK_NULL_HANDLE;
    std::uint32_t first = 0, count = 0;
    Push push;
};

struct Instance {
    glm::mat4 model;
    VkDeviceAddress blas;
};

struct Retired {
    std::vector<Buffer> buffers;
    std::vector<Img> images;
    std::vector<VkAccelerationStructureKHR> as;
    std::vector<std::uint32_t> slots;
};

struct FrameSlot {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE, acquired2 = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    Buffer cams, instances, tlasBuf, scratch;
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    std::uint32_t tlasCapacity = 0;
    Retired retired;
};

class VkRenderer final : public Renderer {
public:
    ~VkRenderer() override { shutdown(); }
    bool init(SDL_Window* win, bool vsync, std::string* err);
    const char* name() const override { return "Vulkan 1.3"; }
    std::string device() const override { return deviceName_; }
    void shutdown() override;
    void update_texture(const Texture& t, const Image& img) override;
    void destroy_texture(Texture& t) override;
    void destroy(GpuMesh& m) override;
    void begin_window(int w, int h) override;
    void end_window() override;
    void begin_frame(int w, int h, const glm::vec3& clear) override;
    void draw_sky(const std::array<const Texture*, 6>& faces) override;
    void draw(const GpuMesh& m, const glm::mat4& model, const DrawOpts& o) override;
    using Renderer::draw;
    void begin_2d(int w, int h) override;
    void quad(float x, float y, float w, float h, const Texture* t, glm::vec4 color, glm::vec4 uv) override;
    Image read_pixels(int w, int h) override;
    bool ray_tracing_supported() const override { return rtSupported_; }
    void set_ray_tracing(bool on) override { rtOn_ = on && rtSupported_; }
    bool ray_tracing() const override { return rtOn_; }
    bool upscaler_supported() const override { return fsrSupported_; }
    void set_upscaler(int q) override { fsrMode_ = fsrSupported_ ? std::clamp(q, -1, 4) : -1; }
    int upscaler() const override { return fsrMode_; }
    bool frame_generation_supported() const override { return fgSupported_; }
    void set_frame_generation(bool on) override {
        const bool want = on && fgSupported_;
        if (want != fgOn_) needRecreate_ = true;  // swapchain image count, FSR context with interpolation
        fgOn_ = want;
    }
    bool frame_generation() const override { return fgOn_ && fsrMode_ >= 0; }

protected:
    Texture backend_texture(const Image& img, bool mipRepeat) override;
    std::uint32_t backend_vertices(const std::vector<float>& v, const GpuMesh& g) override;

private:
    // ---- setup
    void create_device();
    void create_swapchain();
    void destroy_swapchain();
    void create_targets();
    void destroy_targets();
    void create_pipelines();
    VkShaderModule module(const std::uint32_t* code, std::size_t bytes);
    // ---- resources
    Buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host);
    void free_buffer(Buffer& b);
    Img make_image(int w, int h, VkFormat fmt, VkImageUsageFlags usage, std::uint32_t mips, VkSampleCountFlagBits samples,
                   VkImageAspectFlags aspect);
    void free_image(Img& i);
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags props) const;
    void immediate(const std::function<void(VkCommandBuffer)>& f);
    void write_texture_slot(std::uint32_t slot, VkImageView view, bool repeat);
    std::uint32_t alloc_slot();
    void retire(Retired&& r);
    void build_blas(MeshRes& m, const GpuMesh& g);
    // ---- frame
    std::uint32_t camera_index();
    void ensure_frame_buffers(FrameSlot& f, std::size_t cams, std::size_t instances);
    // genSwap: frame generation target for the interpolated frame (real frame goes to swap); both may be null.
    void record(VkCommandBuffer cmd, FrameSlot& f, VkImage genSwap, VkImage swap, Buffer* readback);
    void process_retired(FrameSlot& f);
    // ---- FSR 3.1.4
    struct Scene {
        int begin = -1, end = -1;  // [viewport cmd, next viewport cmd)
        int w = 0, h = 0;
        glm::vec3 clear{0};
    };
    Scene find_scene() const;
    void ensure_fsr(VkExtent2D display);
    void destroy_fsr();
    void record_scene_pass(VkCommandBuffer c, const Scene& sc, bool interpolate);
    void record_main(VkCommandBuffer c, FrameSlot& f, const Scene* sc, std::uint32_t sceneSlot);
    void finish_to(VkCommandBuffer c, VkImage swap, Buffer* readback);

    SDL_Window* win_ = nullptr;
    bool vsync_ = true;
    std::string deviceName_;
    VkInstance inst_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice dev_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t qfam_ = 0;
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer imm_ = VK_NULL_HANDLE;
    VkFence immFence_ = VK_NULL_HANDLE;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_4_BIT;

    VkSwapchainKHR swap_ = VK_NULL_HANDLE;
    VkFormat swapFmt_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> swapImages_;
    std::vector<VkSemaphore> rendered_;  // one per swapchain image
    bool needRecreate_ = false;
    Img msaa_, depth_, resolve_;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, kPipeCount> pipes_{};
    VkSampler samplerRepeat_ = VK_NULL_HANDLE, samplerClamp_ = VK_NULL_HANDLE;

    std::array<FrameSlot, kFrames> frames_;
    std::uint32_t frame_ = 0;
    bool inFrame_ = false;

    std::vector<Img> textures_;     // by slot
    std::vector<bool> slotRepeat_;
    std::vector<std::uint32_t> freeSlots_;
    std::unordered_map<std::uint32_t, MeshRes> meshes_;
    std::uint32_t nextMesh_ = 1;

    std::vector<Cmd> cmds_;
    std::vector<CamBlock> camBlocks_;
    std::vector<Instance> instances_;
    glm::mat4 ortho_{1};
    Buffer skyVb_;

    // FSR 3.1.4 upscaling / frame generation
    bool fsrSupported_ = false, fgSupported_ = false, fgOn_ = false;
    int fsrMode_ = -1;
    std::unique_ptr<Fsr3Upscaler> fsr_;
    VkExtent2D fsrDisplay_{0, 0}, fsrRender_{0, 0};
    int fsrModeBuilt_ = -2;
    bool fsrGen_ = false, fsrReset_ = true, sceneLast_ = false;
    Img sColor_, sMotion_, sDepth_, sReactive_, fsrOut_, fsrGenOut_;
    std::uint32_t fsrOutSlot_ = 0, fsrGenSlot_ = 0;
    bool genFirst_ = true;
    std::uint64_t jitterFrame_ = 0, genFrame_ = 0;
    glm::vec2 jitter_{0};
    bool sceneFramed_ = false;  // a begin_frame of this frame set the render extent / jitter
    glm::mat4 prevVp_{1}, curVp_{1}, curProj_{1};
    bool curVpSet_ = false;
    std::unordered_map<std::uint64_t, glm::mat4> prevModels_, curModels_;
    std::chrono::steady_clock::time_point lastFrame_ = std::chrono::steady_clock::now();
    float frameMs_ = 16.7f;

    // ray tracing
    bool rtSupported_ = false, rtOn_ = false;
    PFN_vkCreateAccelerationStructureKHR pCreateAS_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR pDestroyAS_ = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR pSizes_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR pBuild_ = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR pAsAddr_ = nullptr;
};

// ------------------------------------------------------------------ setup

bool VkRenderer::init(SDL_Window* win, bool vsync, std::string* err) {
    win_ = win;
    vsync_ = vsync;
    try {
        create_device();
        create_swapchain();
        create_targets();
        create_pipelines();
        Image w;
        w.w = w.h = 1;
        w.rgba = {255, 255, 255, 255};
        white_ = backend_texture(w, true);
        // Sky cube (same layout as the GL backend): 6 faces x 2 triangles, pos/normal/uv.
        const float S = 1.0f;
        struct F {
            glm::vec3 o, u, v;
        };
        const F faces[6] = {
            {{S, -S, S}, {0, 0, -2 * S}, {0, 2 * S, 0}},  {{-S, -S, -S}, {0, 0, 2 * S}, {0, 2 * S, 0}},
            {{-S, S, S}, {2 * S, 0, 0}, {0, 0, -2 * S}},  {{-S, -S, -S}, {2 * S, 0, 0}, {0, 0, 2 * S}},
            {{-S, -S, S}, {2 * S, 0, 0}, {0, 2 * S, 0}},  {{S, -S, -S}, {-2 * S, 0, 0}, {0, 2 * S, 0}},
        };
        std::vector<float> sv;
        for (const auto& f : faces) {
            const glm::vec3 c[4] = {f.o, f.o + f.u, f.o + f.u + f.v, f.o + f.v};
            const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for (int i : {0, 1, 2, 0, 2, 3}) sv.insert(sv.end(), {c[i].x, c[i].y, c[i].z, 0, 0, 0, uv[i][0], uv[i][1]});
        }
        skyVb_ = make_buffer(sv.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        std::memcpy(skyVb_.map, sv.data(), sv.size() * 4);
        rtOn_ = rtSupported_;
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        shutdown();
        return false;
    }
}

void VkRenderer::create_device() {
    unsigned n = 0;
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&n);
    if (!exts) throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError());
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "toyota-trafik";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = n;
    ici.ppEnabledExtensionNames = exts;
    check(vkCreateInstance(&ici, nullptr, &inst_), "vkCreateInstance");
    if (!SDL_Vulkan_CreateSurface(win_, inst_, nullptr, &surface_)) throw std::runtime_error(std::string("surface: ") + SDL_GetError());

    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(inst_, &count, nullptr);
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(inst_, &count, devs.data());
    int bestScore = -1;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        std::uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
        for (std::uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
            if (score > bestScore) {
                bestScore = score;
                phys_ = d;
                qfam_ = i;
                deviceName_ = p.deviceName;
            }
            break;
        }
    }
    if (!phys_) throw std::runtime_error("no Vulkan 1.3 GPU with a presentable graphics queue");
    vkGetPhysicalDeviceMemoryProperties(phys_, &memProps_);

    std::uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> avail(en);
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, avail.data());
    auto has = [&](const char* e) { return std::any_of(avail.begin(), avail.end(), [&](const auto& x) { return std::strcmp(x.extensionName, e) == 0; }); };
    std::vector<const char*> dext = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const bool rtExt = has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) && has(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                       has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    VkPhysicalDeviceRayQueryFeaturesKHR rqF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    asF.pNext = &rqF;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;
    f13.pNext = rtExt ? &asF : nullptr;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(phys_, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2 || !f12.runtimeDescriptorArray || !f12.descriptorBindingPartiallyBound ||
        !f12.descriptorBindingSampledImageUpdateAfterBind || !f12.shaderSampledImageArrayNonUniformIndexing || !f13.shaderDemoteToHelperInvocation)
        throw std::runtime_error("GPU lacks dynamic rendering / descriptor indexing");
    rtSupported_ = rtExt && asF.accelerationStructure && rqF.rayQuery && f12.bufferDeviceAddress;
    if (rtSupported_)
        dext.insert(dext.end(), {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                 VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME});

    VkPhysicalDeviceRayQueryFeaturesKHR rqE{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    rqE.rayQuery = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asE{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    asE.accelerationStructure = VK_TRUE;
    asE.pNext = &rqE;
    VkPhysicalDeviceVulkan13Features e13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    e13.dynamicRendering = VK_TRUE;
    e13.synchronization2 = VK_TRUE;
    e13.shaderDemoteToHelperInvocation = f13.shaderDemoteToHelperInvocation;  // glslc maps `discard` to demote for vulkan1.3
    e13.pNext = rtSupported_ ? &asE : nullptr;
    VkPhysicalDeviceVulkan12Features e12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    e12.runtimeDescriptorArray = VK_TRUE;
    e12.descriptorBindingPartiallyBound = VK_TRUE;
    e12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    e12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    e12.bufferDeviceAddress = rtSupported_ ? VK_TRUE : VK_FALSE;
    e12.pNext = &e13;
    VkPhysicalDeviceFeatures2 e2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    e2.features.samplerAnisotropy = f2.features.samplerAnisotropy;
    // FSR 3.1.4 needs storage-image formats without qualifiers; the scene pass needs independent blend (motion).
    const char* fsrWhy = nullptr;
    VkPhysicalDeviceProperties lim;
    vkGetPhysicalDeviceProperties(phys_, &lim);
    if (lim.limits.maxPushConstantsSize < sizeof(Push)) throw std::runtime_error("maxPushConstantsSize < 192");
    fsrSupported_ = f2.features.independentBlend && Fsr3Upscaler::supported(phys_, &fsrWhy);
    fgSupported_ = fsrSupported_ && Fsr3Upscaler::frame_generation_supported(phys_, kColorFmt, &fsrWhy);
    if (fsrSupported_) {
        e2.features.independentBlend = VK_TRUE;
        Fsr3Upscaler::enable_features(phys_, e2.features);
    }
    e2.pNext = &e12;

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &e2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<std::uint32_t>(dext.size());
    dci.ppEnabledExtensionNames = dext.data();
    check(vkCreateDevice(phys_, &dci, nullptr, &dev_), "vkCreateDevice");
    vkGetDeviceQueue(dev_, qfam_, 0, &queue_);
    if (rtSupported_) {
        pCreateAS_ = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(dev_, "vkCreateAccelerationStructureKHR"));
        pDestroyAS_ = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(dev_, "vkDestroyAccelerationStructureKHR"));
        pSizes_ = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(dev_, "vkGetAccelerationStructureBuildSizesKHR"));
        pBuild_ = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(dev_, "vkCmdBuildAccelerationStructuresKHR"));
        pAsAddr_ = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(dev_, "vkGetAccelerationStructureDeviceAddressKHR"));
        rtSupported_ = pCreateAS_ && pDestroyAS_ && pSizes_ && pBuild_ && pAsAddr_;
    }

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qfam_;
    check(vkCreateCommandPool(dev_, &pci, nullptr, &pool_), "command pool");
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(dev_, &cai, &imm_), "command buffer");
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(dev_, &fci, nullptr, &immFence_), "fence");
    for (auto& f : frames_) {
        check(vkAllocateCommandBuffers(dev_, &cai, &f.cmd), "command buffer");
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(dev_, &fci, nullptr, &f.fence), "fence");
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(dev_, &sci, nullptr, &f.acquired), "semaphore");
        check(vkCreateSemaphore(dev_, &sci, nullptr, &f.acquired2), "semaphore");
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys_, &props);
    const VkSampleCountFlags ok = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    samples_ = ok & VK_SAMPLE_COUNT_4_BIT ? VK_SAMPLE_COUNT_4_BIT : ok & VK_SAMPLE_COUNT_2_BIT ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;

    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    s.maxLod = VK_LOD_CLAMP_NONE;
    s.anisotropyEnable = f2.features.samplerAnisotropy;
    s.maxAnisotropy = f2.features.samplerAnisotropy ? std::min(8.0f, props.limits.maxSamplerAnisotropy) : 1.0f;
    check(vkCreateSampler(dev_, &s, nullptr, &samplerRepeat_), "sampler");
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.anisotropyEnable = VK_FALSE;
    s.maxAnisotropy = 1;
    s.maxLod = 0;
    check(vkCreateSampler(dev_, &s, nullptr, &samplerClamp_), "sampler");

    // Descriptors: 0 bindless textures, 1 camera blocks, 2 TLAS (ray tracing only). One set per frame slot.
    std::vector<VkDescriptorSetLayoutBinding> b = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    if (rtSupported_) b.push_back({2, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    std::vector<VkDescriptorBindingFlags> bf(b.size(), 0);
    bf[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bfi{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bfi.bindingCount = static_cast<std::uint32_t>(bf.size());
    bfi.pBindingFlags = bf.data();
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.pNext = &bfi;
    lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    lci.bindingCount = static_cast<std::uint32_t>(b.size());
    lci.pBindings = b.data();
    check(vkCreateDescriptorSetLayout(dev_, &lci, nullptr, &setLayout_), "set layout");
    std::vector<VkDescriptorPoolSize> ps = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures * kFrames},
                                            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFrames}};
    if (rtSupported_) ps.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, kFrames});
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    dpi.maxSets = kFrames;
    dpi.poolSizeCount = static_cast<std::uint32_t>(ps.size());
    dpi.pPoolSizes = ps.data();
    check(vkCreateDescriptorPool(dev_, &dpi, nullptr, &descPool_), "descriptor pool");
    for (auto& f : frames_) {
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &setLayout_;
        check(vkAllocateDescriptorSets(dev_, &dai, &f.set), "descriptor set");
    }
    VkPushConstantRange pr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo plc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plc.setLayoutCount = 1;
    plc.pSetLayouts = &setLayout_;
    plc.pushConstantRangeCount = 1;
    plc.pPushConstantRanges = &pr;
    check(vkCreatePipelineLayout(dev_, &plc, nullptr, &layout_), "pipeline layout");
}

void VkRenderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps);
    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, fmts.data());
    VkSurfaceFormatKHR fmt = fmts.at(0);
    for (const auto& f : fmts)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) fmt = f;
    swapFmt_ = fmt.format;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &n, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_)
        for (auto m : modes)
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR || (m == VK_PRESENT_MODE_MAILBOX_KHR && mode == VK_PRESENT_MODE_FIFO_KHR)) mode = m;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(win_, &w, &h);
    extent_ = caps.currentExtent.width != 0xFFFFFFFFu ? caps.currentExtent : VkExtent2D{static_cast<std::uint32_t>(std::max(w, 1)), static_cast<std::uint32_t>(std::max(h, 1))};
    if (extent_.width == 0 || extent_.height == 0) return;  // minimised
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = std::max(caps.minImageCount + (fgOn_ ? 2u : 1u), 3u);  // frame generation holds two images
    if (caps.maxImageCount) sci.minImageCount = std::min(sci.minImageCount, caps.maxImageCount);
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = mode;
    sci.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(dev_, &sci, nullptr, &swap_), "swapchain");
    vkGetSwapchainImagesKHR(dev_, swap_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(dev_, swap_, &n, swapImages_.data());
    rendered_.resize(n);
    for (auto& s : rendered_) {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(dev_, &si, nullptr, &s), "semaphore");
    }
}

void VkRenderer::destroy_swapchain() {
    for (auto s : rendered_) vkDestroySemaphore(dev_, s, nullptr);
    rendered_.clear();
    swapImages_.clear();
    if (swap_) vkDestroySwapchainKHR(dev_, swap_, nullptr);
    swap_ = VK_NULL_HANDLE;
}

void VkRenderer::create_targets() {
    if (!extent_.width || !extent_.height) return;
    const int w = static_cast<int>(extent_.width), h = static_cast<int>(extent_.height);
    msaa_ = make_image(w, h, kColorFmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, 1, samples_, VK_IMAGE_ASPECT_COLOR_BIT);
    depth_ = make_image(w, h, kDepthFmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, 1, samples_, VK_IMAGE_ASPECT_DEPTH_BIT);
    resolve_ = make_image(w, h, kColorFmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VkRenderer::destroy_targets() {
    free_image(msaa_);
    free_image(depth_);
    free_image(resolve_);
}

VkShaderModule VkRenderer::module(const std::uint32_t* code, std::size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m;
    check(vkCreateShaderModule(dev_, &ci, nullptr, &m), "shader module");
    return m;
}

void VkRenderer::create_pipelines() {
    const VkShaderModule mv = module(kMeshVert, sizeof kMeshVert);
    const VkShaderModule mf = rtSupported_ ? module(kMeshRtFrag, sizeof kMeshRtFrag) : module(kMeshFrag, sizeof kMeshFrag);
    const VkShaderModule mm = rtSupported_ ? module(kMeshRtMFrag, sizeof kMeshRtMFrag) : module(kMeshMFrag, sizeof kMeshMFrag);
    const VkShaderModule qv = module(kQuadVert, sizeof kQuadVert), qf = module(kQuadFrag, sizeof kQuadFrag);
    for (int p = 0; p < kPipeCount; ++p) {
        const bool q = p == kQuad;
        const bool m = p >= kOpaqueM;  // FSR scene pass
        if (m && !fsrSupported_) continue;
        VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        st[0].module = q ? qv : mv;
        st[0].pName = "main";
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        st[1].module = q ? qf : m ? mm : mf;
        st[1].pName = "main";
        VkVertexInputBindingDescription vb{0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription va[3] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (!q) {
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &vb;
            vi.vertexAttributeDescriptionCount = 3;
            vi.pVertexAttributeDescriptions = va;
        }
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = q ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = m ? VK_SAMPLE_COUNT_1_BIT : samples_;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = p == kOpaque || p == kBlend || p == kOpaqueM || p == kBlendM;
        ds.depthWriteEnable = p == kOpaque || p == kOpaqueM;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        if (p == kBlend || p == kQuad || p == kBlendM) {
            ba.blendEnable = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        // Motion: opaque draws and the sky write it, translucent draws keep the motion of what they cover.
        VkPipelineColorBlendAttachmentState bm{};
        bm.colorWriteMask = p == kBlendM ? 0 : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT);
        const VkPipelineColorBlendAttachmentState bas[2] = {ba, bm};
        cb.attachmentCount = m ? 2 : 1;
        cb.pAttachments = bas;
        const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dyn;
        VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        const VkFormat fmts[2] = {kColorFmt, kMotionFmt};
        ri.colorAttachmentCount = m ? 2 : 1;
        ri.pColorAttachmentFormats = fmts;
        ri.depthAttachmentFormat = kDepthFmt;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.pNext = &ri;
        gp.stageCount = 2;
        gp.pStages = st;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dy;
        gp.layout = layout_;
        check(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipes_[static_cast<std::size_t>(p)]), "pipeline");
    }
    for (auto m : {mv, mf, mm, qv, qf}) vkDestroyShaderModule(dev_, m, nullptr);
}

void VkRenderer::shutdown() {
    if (!dev_) {
        if (surface_) SDL_Vulkan_DestroySurface(inst_, surface_, nullptr);
        if (inst_) vkDestroyInstance(inst_, nullptr);
        surface_ = VK_NULL_HANDLE;
        inst_ = VK_NULL_HANDLE;
        return;
    }
    vkDeviceWaitIdle(dev_);
    destroy_fsr();
    for (auto& f : frames_) {
        process_retired(f);
        free_buffer(f.cams);
        free_buffer(f.instances);
        free_buffer(f.scratch);
        if (f.tlas) pDestroyAS_(dev_, f.tlas, nullptr);
        free_buffer(f.tlasBuf);
        vkDestroyFence(dev_, f.fence, nullptr);
        vkDestroySemaphore(dev_, f.acquired, nullptr);
        vkDestroySemaphore(dev_, f.acquired2, nullptr);
    }
    for (auto& [h, m] : meshes_) {
        free_buffer(m.vb);
        if (m.blas.as) pDestroyAS_(dev_, m.blas.as, nullptr);
        free_buffer(m.blas.buf);
    }
    meshes_.clear();
    for (auto& t : textures_) free_image(t);
    textures_.clear();
    cache_.clear();
    free_buffer(skyVb_);
    destroy_targets();
    destroy_swapchain();
    for (auto p : pipes_)
        if (p) vkDestroyPipeline(dev_, p, nullptr);
    vkDestroyPipelineLayout(dev_, layout_, nullptr);
    vkDestroyDescriptorPool(dev_, descPool_, nullptr);
    vkDestroyDescriptorSetLayout(dev_, setLayout_, nullptr);
    vkDestroySampler(dev_, samplerRepeat_, nullptr);
    vkDestroySampler(dev_, samplerClamp_, nullptr);
    vkDestroyFence(dev_, immFence_, nullptr);
    vkDestroyCommandPool(dev_, pool_, nullptr);
    vkDestroyDevice(dev_, nullptr);
    dev_ = VK_NULL_HANDLE;
    SDL_Vulkan_DestroySurface(inst_, surface_, nullptr);
    vkDestroyInstance(inst_, nullptr);
    surface_ = VK_NULL_HANDLE;
    inst_ = VK_NULL_HANDLE;
}

// ------------------------------------------------------------------ resources

std::uint32_t VkRenderer::memory_type(std::uint32_t bits, VkMemoryPropertyFlags props) const {
    for (std::uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & props) == props) return i;
    throw std::runtime_error("no suitable Vulkan memory type");
}

Buffer VkRenderer::make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
    Buffer b;
    b.size = std::max<VkDeviceSize>(size, 16);
    if (rtSupported_) usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = b.size;
    ci.usage = usage;
    check(vkCreateBuffer(dev_, &ci, nullptr, &b.buf), "buffer");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev_, b.buf, &req);
    VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = rtSupported_ ? &fi : nullptr;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                              : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(dev_, &ai, nullptr, &b.mem), "buffer memory");
    vkBindBufferMemory(dev_, b.buf, b.mem, 0);
    if (host) vkMapMemory(dev_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map);
    if (rtSupported_) {
        VkBufferDeviceAddressInfo bi{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        bi.buffer = b.buf;
        b.addr = vkGetBufferDeviceAddress(dev_, &bi);
    }
    return b;
}

void VkRenderer::free_buffer(Buffer& b) {
    if (b.buf) vkDestroyBuffer(dev_, b.buf, nullptr);
    if (b.mem) vkFreeMemory(dev_, b.mem, nullptr);
    b = {};
}

Img VkRenderer::make_image(int w, int h, VkFormat fmt, VkImageUsageFlags usage, std::uint32_t mips, VkSampleCountFlagBits samples,
                           VkImageAspectFlags aspect) {
    Img i;
    i.w = w;
    i.h = h;
    i.mips = mips;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
    ci.mipLevels = mips;
    ci.arrayLayers = 1;
    ci.samples = samples;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    check(vkCreateImage(dev_, &ci, nullptr, &i.img), "image");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev_, i.img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(dev_, &ai, nullptr, &i.mem), "image memory");
    vkBindImageMemory(dev_, i.img, i.mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = i.img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {aspect, 0, mips, 0, 1};
    check(vkCreateImageView(dev_, &vi, nullptr, &i.view), "image view");
    return i;
}

void VkRenderer::free_image(Img& i) {
    if (i.view) vkDestroyImageView(dev_, i.view, nullptr);
    if (i.img) vkDestroyImage(dev_, i.img, nullptr);
    if (i.mem) vkFreeMemory(dev_, i.mem, nullptr);
    i = {};
}

void VkRenderer::immediate(const std::function<void(VkCommandBuffer)>& f) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(imm_, 0);
    vkBeginCommandBuffer(imm_, &bi);
    f(imm_);
    vkEndCommandBuffer(imm_);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &imm_;
    vkResetFences(dev_, 1, &immFence_);
    check(vkQueueSubmit(queue_, 1, &si, immFence_), "queue submit");
    vkWaitForFences(dev_, 1, &immFence_, VK_TRUE, UINT64_MAX);
}

void barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
             std::uint32_t mip = 0, std::uint32_t mips = VK_REMAINING_MIP_LEVELS) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {aspect, mip, mips, 0, 1};
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1;
    d.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(c, &d);
}

std::uint32_t VkRenderer::alloc_slot() {
    if (!freeSlots_.empty()) {
        const auto s = freeSlots_.back();
        freeSlots_.pop_back();
        return s;
    }
    if (textures_.size() >= kMaxTextures) throw std::runtime_error("texture slots exhausted");
    textures_.emplace_back();
    slotRepeat_.push_back(true);
    return static_cast<std::uint32_t>(textures_.size() - 1);
}

void VkRenderer::write_texture_slot(std::uint32_t slot, VkImageView view, bool repeat) {
    VkDescriptorImageInfo ii{repeat ? samplerRepeat_ : samplerClamp_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (auto& f : frames_) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = f.set;
        w.dstBinding = 0;
        w.dstArrayElement = slot;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
    }
}

Texture VkRenderer::backend_texture(const Image& img, bool mipRepeat) {
    Texture t;
    t.w = img.w;
    t.h = img.h;
    for (std::size_t i = 3; i < img.rgba.size(); i += 4)
        if (img.rgba[i] < 255) {
            t.hasAlpha = true;
            break;
        }
    const std::uint32_t mips = mipRepeat ? static_cast<std::uint32_t>(std::floor(std::log2(std::max(img.w, img.h)))) + 1 : 1;
    Img im = make_image(img.w, img.h, kColorFmt, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mips,
                        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    const auto px = flip_rows(img);
    Buffer st = make_buffer(px.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(st.map, px.data(), px.size());
    immediate([&](VkCommandBuffer c) {
        barrier(c, im.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy r{};
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageExtent = {static_cast<std::uint32_t>(img.w), static_cast<std::uint32_t>(img.h), 1};
        vkCmdCopyBufferToImage(c, st.buf, im.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        int w = img.w, h = img.h;
        for (std::uint32_t m = 1; m < mips; ++m) {
            barrier(c, im.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1);
            VkImageBlit b{};
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1};
            b.srcOffsets[1] = {w, h, 1};
            w = std::max(w / 2, 1);
            h = std::max(h / 2, 1);
            b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
            b.dstOffsets[1] = {w, h, 1};
            vkCmdBlitImage(c, im.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, im.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
            barrier(c, im.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1);
        }
        barrier(c, im.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    free_buffer(st);
    const std::uint32_t slot = alloc_slot();
    textures_[slot] = im;
    slotRepeat_[slot] = mipRepeat;
    write_texture_slot(slot, im.view, mipRepeat);
    t.id = slot;
    return t;
}

void VkRenderer::update_texture(const Texture& t, const Image& img) {
    if (t.id >= textures_.size() || !textures_[t.id].img || img.w != textures_[t.id].w || img.h != textures_[t.id].h) return;
    vkQueueWaitIdle(queue_);  // the image may still be sampled by a frame in flight (video only, 25 fps)
    const auto px = flip_rows(img);
    Buffer st = make_buffer(px.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(st.map, px.data(), px.size());
    const VkImage im = textures_[t.id].img;
    immediate([&](VkCommandBuffer c) {
        barrier(c, im, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy r{};
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageExtent = {static_cast<std::uint32_t>(img.w), static_cast<std::uint32_t>(img.h), 1};
        vkCmdCopyBufferToImage(c, st.buf, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        barrier(c, im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    free_buffer(st);
}

void VkRenderer::retire(Retired&& r) {
    // Freed only after the current frame slot's fence signals (resources may be referenced by
    // commands already recorded for this frame or by frames in flight).
    auto& dst = frames_[frame_].retired;
    for (auto& b : r.buffers) dst.buffers.push_back(b);
    for (auto& i : r.images) dst.images.push_back(i);
    for (auto a : r.as) dst.as.push_back(a);
    for (auto s : r.slots) dst.slots.push_back(s);
}

void VkRenderer::process_retired(FrameSlot& f) {
    for (auto& b : f.retired.buffers) free_buffer(b);
    for (auto& i : f.retired.images) free_image(i);
    for (auto a : f.retired.as) pDestroyAS_(dev_, a, nullptr);
    for (auto s : f.retired.slots) freeSlots_.push_back(s);
    f.retired = {};
}

void VkRenderer::destroy_texture(Texture& t) {
    if (t.id && t.id < textures_.size() && t.id != white_.id && textures_[t.id].img) {
        Retired r;
        r.images.push_back(textures_[t.id]);
        r.slots.push_back(t.id);
        textures_[t.id] = {};
        retire(std::move(r));
    }
    t = {};
}

std::uint32_t VkRenderer::backend_vertices(const std::vector<float>& v, const GpuMesh& g) {
    MeshRes m;
    const VkDeviceSize bytes = v.size() * sizeof(float);
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (rtSupported_) usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    m.vb = make_buffer(bytes, usage, false);
    Buffer st = make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(st.map, v.data(), bytes);
    immediate([&](VkCommandBuffer c) {
        VkBufferCopy r{0, 0, bytes};
        vkCmdCopyBuffer(c, st.buf, m.vb.buf, 1, &r);
    });
    free_buffer(st);
    m.frameVerts = g.frameVerts;
    if (rtSupported_) build_blas(m, g);
    const std::uint32_t h = nextMesh_++;
    meshes_[h] = m;
    return h;
}

void VkRenderer::build_blas(MeshRes& m, const GpuMesh& g) {
    // One geometry per opaque, non-alpha-tested draw group of frame 0.
    std::vector<VkAccelerationStructureGeometryKHR> geos;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    std::vector<std::uint32_t> prims;
    for (const auto& d : g.draws) {
        if (d.alpha < 0.999f || (d.tex && d.tex->hasAlpha) || d.count < 3) continue;
        VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto& tr = geo.geometry.triangles;
        tr.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tr.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tr.vertexData.deviceAddress = m.vb.addr + static_cast<VkDeviceAddress>(d.first) * 32;
        tr.vertexStride = 32;
        tr.maxVertex = d.count - 1;
        tr.indexType = VK_INDEX_TYPE_NONE_KHR;
        geos.push_back(geo);
        ranges.push_back({d.count / 3, 0, 0, 0});
        prims.push_back(d.count / 3);
    }
    if (geos.empty()) return;
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.geometryCount = static_cast<std::uint32_t>(geos.size());
    bi.pGeometries = geos.data();
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    pSizes_(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, prims.data(), &sz);
    m.blas.buf = make_buffer(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = m.blas.buf.buf;
    ci.size = sz.accelerationStructureSize;
    ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    check(pCreateAS_(dev_, &ci, nullptr, &m.blas.as), "BLAS");
    Buffer scratch = make_buffer(sz.buildScratchSize + 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    bi.dstAccelerationStructure = m.blas.as;
    bi.scratchData.deviceAddress = (scratch.addr + 255) & ~VkDeviceAddress(255);
    const VkAccelerationStructureBuildRangeInfoKHR* pr = ranges.data();
    immediate([&](VkCommandBuffer c) { pBuild_(c, 1, &bi, &pr); });
    free_buffer(scratch);
    VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    ai.accelerationStructure = m.blas.as;
    m.blas.addr = pAsAddr_(dev_, &ai);
}

void VkRenderer::destroy(GpuMesh& g) {
    if (auto it = meshes_.find(g.handle); it != meshes_.end()) {
        Retired r;
        r.buffers.push_back(it->second.vb);
        if (it->second.blas.as) {
            r.as.push_back(it->second.blas.as);
            r.buffers.push_back(it->second.blas.buf);
        }
        meshes_.erase(it);
        retire(std::move(r));
    }
    g = {};
}

// ------------------------------------------------------------------ FSR 3.1.4

VkRenderer::Scene VkRenderer::find_scene() const {
    // The first begin_frame segment that holds 3D draws (the map view); menus only draw quads.
    Scene sc;
    for (std::size_t i = 0; i + 1 < cmds_.size(); ++i) {
        if (cmds_[i].kind != Cmd::Viewport || cmds_[i + 1].kind != Cmd::Clear) continue;
        std::size_t e = i + 1;
        bool has3d = false;
        while (++e < cmds_.size() && cmds_[e].kind != Cmd::Viewport) has3d |= cmds_[e].kind == Cmd::Draw && cmds_[e].pipe != kQuad;
        if (!has3d) continue;
        sc.begin = static_cast<int>(i);
        sc.end = static_cast<int>(e);
        sc.w = cmds_[i].w;
        sc.h = cmds_[i].h;
        sc.clear = cmds_[i + 1].clear;
        return sc;
    }
    return sc;
}

void VkRenderer::destroy_fsr() {
    if (!fsr_ && !fsrOut_.img) return;
    vkDeviceWaitIdle(dev_);
    fsr_.reset();
    for (Img* i : {&sColor_, &sMotion_, &sDepth_, &sReactive_, &fsrOut_, &fsrGenOut_}) free_image(*i);
    for (std::uint32_t* sl : {&fsrOutSlot_, &fsrGenSlot_})
        if (*sl) {
            freeSlots_.push_back(*sl);
            *sl = 0;
        }
    fsrDisplay_ = {0, 0};
}

void VkRenderer::ensure_fsr(VkExtent2D display) {
    if (fsrMode_ < 0 || !display.width || !display.height) return;
    if (fsr_ && fsrDisplay_.width == display.width && fsrDisplay_.height == display.height && fsrModeBuilt_ == fsrMode_ && fsrGen_ == fgOn_) return;
    destroy_fsr();
    const VkExtent2D render = Fsr3Upscaler::render_extent(display, static_cast<FsrQuality>(fsrMode_));
    const int rw = static_cast<int>(render.width), rh = static_cast<int>(render.height);
    const int dw = static_cast<int>(display.width), dh = static_cast<int>(display.height);
    constexpr VkImageUsageFlags att = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr VkImageUsageFlags out = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sColor_ = make_image(rw, rh, kColorFmt, att, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    sMotion_ = make_image(rw, rh, kMotionFmt, att, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    sDepth_ = make_image(rw, rh, kDepthFmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT);
    sReactive_ = make_image(rw, rh, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    fsrOut_ = make_image(dw, dh, kColorFmt, out, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    const bool gen = fgOn_ && fgSupported_;
    if (gen) fsrGenOut_ = make_image(dw, dh, kColorFmt, out, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    immediate([&](VkCommandBuffer c) {
        // The reactive mask stays 0 (no per-draw reactivity); outputs start black, all in SHADER_READ_ONLY.
        for (const Img* i : {&sReactive_, &fsrOut_, &fsrGenOut_}) {
            if (!i->img) continue;
            barrier(c, i->img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
            const VkClearColorValue zero{{0, 0, 0, 1}};
            const VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(c, i->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &r);
            barrier(c, i->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        }
    });
    try {
        fsr_ = std::make_unique<Fsr3Upscaler>(phys_, dev_, render, display, false, gen ? kColorFmt : VK_FORMAT_UNDEFINED);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FSR 3.1.4 unavailable: %s\n", e.what());
        fsrSupported_ = fgSupported_ = false;
        fsrMode_ = -1;
        destroy_fsr();
        return;
    }
    fsrOutSlot_ = alloc_slot();
    write_texture_slot(fsrOutSlot_, fsrOut_.view, false);
    if (gen) {
        fsrGenSlot_ = alloc_slot();
        write_texture_slot(fsrGenSlot_, fsrGenOut_.view, false);
    }
    fsrDisplay_ = display;
    fsrRender_ = render;
    fsrModeBuilt_ = fsrMode_;
    fsrGen_ = gen;
    fsrReset_ = genFirst_ = true;
}

void VkRenderer::record_scene_pass(VkCommandBuffer c, const Scene& sc, bool interpolate) {
    // 1. The map view at render resolution (jittered): colour, UV-space motion and depth, no MSAA.
    for (const Img* i : {&sColor_, &sMotion_})
        barrier(c, i->img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    barrier(c, sDepth_.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);
    VkRenderingAttachmentInfo ca[2]{{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}};
    ca[0].imageView = sColor_.view;
    ca[1].imageView = sMotion_.view;
    for (auto& a : ca) {
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    ca[0].clearValue.color = {{sc.clear.r, sc.clear.g, sc.clear.b, 1}};
    ca[1].clearValue.color = {{0, 0, 0, 0}};
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    da.imageView = sDepth_.view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    da.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, fsrRender_};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 2;
    ri.pColorAttachments = ca;
    ri.pDepthAttachment = &da;
    vkCmdBeginRendering(c, &ri);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &frames_[frame_].set, 0, nullptr);
    const VkViewport v{0, static_cast<float>(fsrRender_.height), static_cast<float>(fsrRender_.width), -static_cast<float>(fsrRender_.height), 0, 1};
    const VkRect2D full{{0, 0}, fsrRender_};
    vkCmdSetViewport(c, 0, 1, &v);
    vkCmdSetScissor(c, 0, 1, &full);
    int bound = -1;
    VkBuffer boundVb = VK_NULL_HANDLE;
    for (int i = sc.begin; i < sc.end; ++i) {
        const Cmd& cmd = cmds_[static_cast<std::size_t>(i)];
        if (cmd.kind != Cmd::Draw || cmd.pipe == kQuad) continue;
        const int pipe = cmd.pipe == kOpaque ? kOpaqueM : cmd.pipe == kBlend ? kBlendM : kSkyM;
        if (bound != pipe) {
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_[static_cast<std::size_t>(pipe)]);
            bound = pipe;
        }
        if (cmd.vb != boundVb) {
            const VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(c, 0, 1, &cmd.vb, &off);
            boundVb = cmd.vb;
        }
        vkCmdPushConstants(c, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &cmd.push);
        vkCmdDraw(c, cmd.count, 1, cmd.first, 0);
    }
    vkCmdEndRendering(c);
    for (const Img* i : {&sColor_, &sMotion_})
        barrier(c, i->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    barrier(c, sDepth_.img, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    // The previous frame's composite pass sampled fsrOut_ / fsrGenOut_: finish those reads before FSR writes.
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(c, &dep);
    // 2. FSR 3.1.4 upscale into fsrOut_ (the letterboxed scene viewport size).
    constexpr VkImageUsageFlags att = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr VkImageUsageFlags out = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const float fovY = 2.0f * std::atan(1.0f / curProj_[1][1]);
    const bool reset = fsrReset_ || !sceneLast_;
    Fsr3Upscaler::Dispatch d{c,
                             {sColor_.img, kColorFmt, fsrRender_, att},
                             {sDepth_.img, kDepthFmt, fsrRender_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT},
                             {sMotion_.img, kMotionFmt, fsrRender_, att},
                             {sReactive_.img, VK_FORMAT_R8_UNORM, fsrRender_, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT},
                             {fsrOut_.img, kColorFmt, fsrDisplay_, out},
                             {jitter_.x, jitter_.y},
                             frameMs_, 0.3f, 0.1f, 3000.0f, fovY, reset};
    fsr_->dispatch(d);
    // 3. Frame generation: the frame halfway between the previous and this upscaled (HUD-less) image.
    if (interpolate) {
        barrier(c, fsrGenOut_.img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT);
        fsr_->interpolate({c, {fsrOut_.img, kColorFmt, fsrDisplay_, out}, {fsrGenOut_.img, kColorFmt, fsrDisplay_, out}, frameMs_, 0.1f, 3000.0f, fovY,
                           reset || genFirst_, ++genFrame_});
        genFirst_ = false;
        barrier(c, fsrGenOut_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    vkCmdPipelineBarrier2(c, &dep);
}

// ------------------------------------------------------------------ frame recording

std::uint32_t VkRenderer::camera_index() {
    CamBlock b;
    b.view = view_;
    b.proj = proj_;
    b.vp = proj_ * view_;
    if (!curVpSet_) {  // the first camera of the frame is the scene camera for reprojection
        curVp_ = b.vp;
        curProj_ = proj_;
        curVpSet_ = true;
    }
    b.prevVp = sceneLast_ ? prevVp_ : b.vp;
    if (fsrMode_ >= 0 && sceneFramed_) {
        // FSR sub-pixel jitter (render pixels) as a clip-space offset; NDC y is up as in D3D.
        const glm::vec2 j = jitter_;
        b.proj = glm::translate(glm::mat4(1), {2.0f * j.x / static_cast<float>(fsrRender_.width), -2.0f * j.y / static_cast<float>(fsrRender_.height), 0}) * proj_;
    }
    b.lightDir = glm::vec4(glm::normalize(light_.dir), 0);
    b.amb = glm::vec4(light_.ambient, 1);
    b.dif = glm::vec4(light_.diffuse, 1);
    b.fogColor = glm::vec4(fog_.color, fog_.enabled ? 1.0f : 0.0f);
    b.fogRange = glm::vec4(fog_.start, fog_.end, time_, 0);  // z: water animation clock
    if (camBlocks_.empty() || std::memcmp(&camBlocks_.back(), &b, sizeof b) != 0) camBlocks_.push_back(b);
    return static_cast<std::uint32_t>(camBlocks_.size() - 1);
}

void VkRenderer::begin_window(int, int) {
    FrameSlot& f = frames_[frame_];
    vkWaitForFences(dev_, 1, &f.fence, VK_TRUE, UINT64_MAX);
    process_retired(f);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(win_, &w, &h);
    if (needRecreate_ || static_cast<std::uint32_t>(w) != extent_.width || static_cast<std::uint32_t>(h) != extent_.height) {
        vkDeviceWaitIdle(dev_);
        destroy_targets();
        destroy_swapchain();
        create_swapchain();
        create_targets();
        needRecreate_ = false;
    }
    cmds_.clear();
    camBlocks_.clear();
    instances_.clear();
    curModels_.clear();
    curVpSet_ = false;
    sceneFramed_ = false;
    inFrame_ = true;
    Cmd v;
    v.kind = Cmd::Viewport;
    v.w = static_cast<int>(extent_.width);
    v.h = static_cast<int>(extent_.height);
    cmds_.push_back(v);
}

void VkRenderer::begin_frame(int w, int h, const glm::vec3& clear) {
    if (fsrMode_ >= 0 && !sceneFramed_ && w > 0 && h > 0) {
        sceneFramed_ = true;
        fsrRender_ = Fsr3Upscaler::render_extent({static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)}, static_cast<FsrQuality>(fsrMode_));
        const auto j = fsr_ && fsrDisplay_.width == static_cast<std::uint32_t>(w) && fsrDisplay_.height == static_cast<std::uint32_t>(h) && fsrModeBuilt_ == fsrMode_
                           ? fsr_->jitter(jitterFrame_) : std::array<float, 2>{0, 0};
        jitter_ = {j[0], j[1]};
    }
    Cmd v;
    v.kind = Cmd::Viewport;
    v.x = vpx_;
    v.y = vpy_;
    v.w = w;
    v.h = h;
    cmds_.push_back(v);
    v.kind = Cmd::Clear;
    v.clear = clear;
    cmds_.push_back(v);
}

void VkRenderer::draw_sky(const std::array<const Texture*, 6>& faces) {
    // Same as GL: unlit cube around the camera, no depth test/write.
    const glm::mat4 vrot = glm::mat4(glm::mat3(view_));
    const glm::mat4 model = glm::inverse(view_) * vrot * glm::scale(glm::mat4(1), glm::vec3(10.0f));
    const std::uint32_t cam = camera_index();
    for (int i = 0; i < 6; ++i) {
        Cmd c;
        c.pipe = kSky;
        c.vb = skyVb_.buf;
        c.first = static_cast<std::uint32_t>(i * 6);
        c.count = 6;
        c.push.model = model;
        c.push.prevModel = model;
        c.push.params = {faces[static_cast<std::size_t>(i)] ? faces[static_cast<std::size_t>(i)]->id : white_.id, 1u | 2u, cam, 0};
        cmds_.push_back(c);
    }
}

void VkRenderer::draw(const GpuMesh& m, const glm::mat4& model, const DrawOpts& o) {
    const auto it = meshes_.find(m.handle);
    if (it == meshes_.end()) return;
    const std::uint32_t cam = camera_index();
    const std::uint32_t base = m.frameVerts * static_cast<std::uint32_t>(std::clamp(o.frame, 0, m.frames - 1));
    glm::mat4 prev = model;
    if (o.id) {
        curModels_[o.id] = model;
        if (const auto pit = prevModels_.find(o.id); pit != prevModels_.end()) prev = pit->second;
    }
    for (const auto& d : m.draws) {
        Cmd c;
        const float a = d.alpha * o.alpha;
        c.pipe = a < 0.999f ? kBlend : kOpaque;
        c.vb = it->second.vb.buf;
        c.first = base + d.first;
        c.count = d.count;
        c.push.model = model;
        c.push.prevModel = prev;
        const glm::vec3 col = (d.tintable || o.forceTint) && o.tint ? *o.tint : d.color;
        c.push.color = glm::vec4(col, a);
        c.push.uvx = o.uv;
        c.push.params = {d.tex ? d.tex->id : white_.id, (o.unlit ? 1u : 0u) | (rtOn_ ? 4u : 0u) | (d.water && !o.unlit ? 8u : 0u), cam, 0};
        cmds_.push_back(c);
    }
    if (rtOn_ && o.castShadow && !o.unlit && it->second.blas.addr) instances_.push_back({model, it->second.blas.addr});
}

void VkRenderer::begin_2d(int w, int h) {
    ortho_ = glm::ortho(0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f);
    Cmd v;
    v.kind = Cmd::Viewport;
    v.x = vpx_;
    v.y = vpy_;
    v.w = vpw_;
    v.h = vph_;
    cmds_.push_back(v);
}

void VkRenderer::quad(float x, float y, float w, float h, const Texture* t, glm::vec4 color, glm::vec4 uv) {
    Cmd c;
    c.pipe = kQuad;
    c.count = 4;
    c.push.model = ortho_;
    c.push.color = color;
    c.push.uvx = {uv.x, 1.0f - uv.y, uv.z, 1.0f - uv.w};  // textures are stored bottom-up
    c.push.rect = {x, y, w, h};
    c.push.params = {t ? t->id : white_.id, 0, 0, 0};
    cmds_.push_back(c);
}

void VkRenderer::ensure_frame_buffers(FrameSlot& f, std::size_t cams, std::size_t inst) {
    const VkDeviceSize camBytes = std::max<std::size_t>(cams, 1) * sizeof(CamBlock);
    if (f.cams.size < camBytes) {
        free_buffer(f.cams);
        f.cams = make_buffer(camBytes * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        VkDescriptorBufferInfo bi{f.cams.buf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = f.set;
        w.dstBinding = 1;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
    }
    if (!rtSupported_) return;
    const std::uint32_t need = static_cast<std::uint32_t>(std::max<std::size_t>(inst, 1));
    if (f.tlasCapacity >= need && f.tlas) return;
    const std::uint32_t cap = std::max(need * 2, 256u);
    if (f.tlas) pDestroyAS_(dev_, f.tlas, nullptr);
    free_buffer(f.tlasBuf);
    free_buffer(f.instances);
    free_buffer(f.scratch);
    f.instances = make_buffer(cap * sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
    VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geo.geometry.instances.data.deviceAddress = f.instances.addr;
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    bi.geometryCount = 1;
    bi.pGeometries = &geo;
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    pSizes_(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &cap, &sz);
    f.tlasBuf = make_buffer(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    f.scratch = make_buffer(sz.buildScratchSize + 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = f.tlasBuf.buf;
    ci.size = sz.accelerationStructureSize;
    ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    check(pCreateAS_(dev_, &ci, nullptr, &f.tlas), "TLAS");
    f.tlasCapacity = cap;
    VkWriteDescriptorSetAccelerationStructureKHR asw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asw.accelerationStructureCount = 1;
    asw.pAccelerationStructures = &f.tlas;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.pNext = &asw;
    w.dstSet = f.set;
    w.dstBinding = 2;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
}

void VkRenderer::record(VkCommandBuffer c, FrameSlot& f, VkImage genSwap, VkImage swap, Buffer* readback) {
    ensure_frame_buffers(f, camBlocks_.size(), instances_.size());
    if (!camBlocks_.empty()) std::memcpy(f.cams.map, camBlocks_.data(), camBlocks_.size() * sizeof(CamBlock));
    // TLAS from this frame's shadow casters (built every frame: cars, people and the train move).
    if (rtSupported_) {
        auto* inst = static_cast<VkAccelerationStructureInstanceKHR*>(f.instances.map);
        for (std::size_t i = 0; i < instances_.size(); ++i) {
            VkAccelerationStructureInstanceKHR& d = inst[i];
            const glm::mat4& m = instances_[i].model;
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 4; ++k) d.transform.matrix[r][k] = m[k][r];
            d.instanceCustomIndex = 0;
            d.mask = 0xFF;
            d.instanceShaderBindingTableRecordOffset = 0;
            d.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
            d.accelerationStructureReference = instances_[i].blas;
        }
        VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geo.geometry.instances.data.deviceAddress = f.instances.addr;
        VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        bi.dstAccelerationStructure = f.tlas;
        bi.geometryCount = 1;
        bi.pGeometries = &geo;
        bi.scratchData.deviceAddress = (f.scratch.addr + 255) & ~VkDeviceAddress(255);
        VkAccelerationStructureBuildRangeInfoKHR range{static_cast<std::uint32_t>(instances_.size()), 0, 0, 0};
        const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
        pBuild_(c, 1, &bi, &pr);
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        mb.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(c, &d);
    }
    const Scene sc = fsrMode_ >= 0 && fsr_ ? find_scene() : Scene{};
    const bool up = sc.begin >= 0 && fsrDisplay_.width == static_cast<std::uint32_t>(sc.w) && fsrDisplay_.height == static_cast<std::uint32_t>(sc.h);
    const bool gen = up && genSwap && fsrGen_;
    if (up) record_scene_pass(c, sc, gen);
    if (gen) {  // interpolated frame first, with this frame's HUD (the HUD is never interpolated)
        record_main(c, f, &sc, fsrGenSlot_);
        finish_to(c, genSwap, nullptr);
    }
    record_main(c, f, up ? &sc : nullptr, fsrOutSlot_);
    finish_to(c, swap, readback);
}

void VkRenderer::record_main(VkCommandBuffer c, FrameSlot& f, const Scene* sc, std::uint32_t sceneSlot) {
    barrier(c, msaa_.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    barrier(c, resolve_.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    barrier(c, depth_.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, 0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);
    const bool msaa = samples_ != VK_SAMPLE_COUNT_1_BIT;
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = msaa ? msaa_.view : resolve_.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color = {{0, 0, 0, 1}};
    if (msaa) {
        ca.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        ca.resolveImageView = resolve_.view;
        ca.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    da.imageView = depth_.view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    da.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, extent_};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    ri.pDepthAttachment = &da;
    vkCmdBeginRendering(c, &ri);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &f.set, 0, nullptr);
    VkRect2D vpRect{{0, 0}, extent_};
    int bound = -1;
    VkBuffer boundVb = VK_NULL_HANDLE;
    auto clampRect = [&](int x, int y, int w, int h) {
        const int x0 = std::clamp(x, 0, static_cast<int>(extent_.width)), y0 = std::clamp(y, 0, static_cast<int>(extent_.height));
        const int x1 = std::clamp(x + w, 0, static_cast<int>(extent_.width)), y1 = std::clamp(y + h, 0, static_cast<int>(extent_.height));
        return VkRect2D{{x0, y0}, {static_cast<std::uint32_t>(std::max(x1 - x0, 0)), static_cast<std::uint32_t>(std::max(y1 - y0, 0))}};
    };
    for (std::size_t ci = 0; ci < cmds_.size(); ++ci) {
        const Cmd& cmd = cmds_[ci];
        const bool inScene = sc && static_cast<int>(ci) >= sc->begin && static_cast<int>(ci) < sc->end;
        if (inScene && cmd.kind == Cmd::Draw && cmd.pipe != kQuad) continue;  // replaced by the upscaled image
        if (cmd.kind == Cmd::Viewport) {
            // Negative height: GL clip-space Y up, same pixel mapping as the GL backend.
            VkViewport v{static_cast<float>(cmd.x), static_cast<float>(cmd.y + cmd.h), static_cast<float>(cmd.w), -static_cast<float>(cmd.h), 0, 1};
            vkCmdSetViewport(c, 0, 1, &v);
            vpRect = clampRect(cmd.x, cmd.y, cmd.w, cmd.h);
            vkCmdSetScissor(c, 0, 1, &vpRect);
            continue;
        }
        if (cmd.kind == Cmd::Clear) {
            if (!vpRect.extent.width || !vpRect.extent.height) continue;
            VkClearAttachment at[2]{};
            at[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            at[0].clearValue.color = {{cmd.clear.r, cmd.clear.g, cmd.clear.b, 1}};
            at[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            at[1].clearValue.depthStencil = {1.0f, 0};
            VkClearRect cr{vpRect, 0, 1};
            vkCmdClearAttachments(c, 2, at, 1, &cr);
            if (inScene) {  // FSR output (top row first, no flip) over the scene viewport
                Push q;
                q.model = glm::ortho(0.0f, static_cast<float>(sc->w), static_cast<float>(sc->h), 0.0f);
                q.rect = {0, 0, static_cast<float>(sc->w), static_cast<float>(sc->h)};
                q.uvx = {0, 0, 1, 1};
                q.params = {sceneSlot, 0, 0, 0};
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_[kQuad]);
                bound = kQuad;
                vkCmdPushConstants(c, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &q);
                vkCmdDraw(c, 4, 1, 0, 0);
            }
            continue;
        }
        if (!vpRect.extent.width || !vpRect.extent.height) continue;
        if (bound != cmd.pipe) {
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_[cmd.pipe]);
            bound = cmd.pipe;
        }
        if (cmd.pipe != kQuad && cmd.vb != boundVb) {
            const VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(c, 0, 1, &cmd.vb, &off);
            boundVb = cmd.vb;
        }
        vkCmdPushConstants(c, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &cmd.push);
        vkCmdDraw(c, cmd.count, 1, cmd.first, 0);
    }
    vkCmdEndRendering(c);
}

void VkRenderer::finish_to(VkCommandBuffer c, VkImage sw, Buffer* readback) {
    barrier(c, resolve_.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    if (readback) {
        VkBufferImageCopy r{};
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(c, resolve_.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->buf, 1, &r);
    }
    if (sw) {
        barrier(c, sw, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageBlit b{};
        b.srcSubresource = b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b.srcOffsets[1] = b.dstOffsets[1] = {static_cast<int>(extent_.width), static_cast<int>(extent_.height), 1};
        vkCmdBlitImage(c, resolve_.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sw, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
        barrier(c, sw, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE, 0);
    }
}

Image VkRenderer::read_pixels(int, int) {
    Image img;
    if (!inFrame_ || !swap_) return img;
    FrameSlot& f = frames_[frame_];  // already waited in begin_window and not yet submitted
    if (fsrMode_ >= 0)
        if (const Scene sc = find_scene(); sc.begin >= 0) ensure_fsr({static_cast<std::uint32_t>(sc.w), static_cast<std::uint32_t>(sc.h)});
    Buffer rb = make_buffer(static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    immediate([&](VkCommandBuffer c) { record(c, f, VK_NULL_HANDLE, VK_NULL_HANDLE, &rb); });
    img.w = static_cast<int>(extent_.width);
    img.h = static_cast<int>(extent_.height);
    const auto* px = static_cast<const std::uint8_t*>(rb.map);
    img.rgba.assign(px, px + static_cast<std::size_t>(img.w) * img.h * 4);
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    free_buffer(rb);
    return img;
}

void VkRenderer::end_window() {
    if (!inFrame_) return;
    inFrame_ = false;
    FrameSlot& f = frames_[frame_];
    const auto now = std::chrono::steady_clock::now();
    frameMs_ = std::clamp(std::chrono::duration<float, std::milli>(now - lastFrame_).count(), 1.0f, 100.0f);
    lastFrame_ = now;
    const Scene sc = fsrMode_ >= 0 ? find_scene() : Scene{};
    if (sc.begin >= 0) ensure_fsr({static_cast<std::uint32_t>(sc.w), static_cast<std::uint32_t>(sc.h)});
    const bool gen = sc.begin >= 0 && fsrGen_ && fsr_;
    auto bookkeeping = [&] {
        prevModels_.swap(curModels_);
        if (curVpSet_) prevVp_ = curVp_;
        sceneLast_ = sc.begin >= 0 && fsr_;
        if (sceneLast_) fsrReset_ = false;
        ++jitterFrame_;
    };
    if (!swap_) {  // minimised
        bookkeeping();
        return;
    }
    // Frame generation presents two images per rendered frame: the interpolated one, then the real one.
    std::uint32_t idx[2] = {0, 0};
    const int n = gen ? 2 : 1;
    for (int k = 0; k < n; ++k) {
        const VkResult r = vkAcquireNextImageKHR(dev_, swap_, UINT64_MAX, k ? f.acquired2 : f.acquired, VK_NULL_HANDLE, &idx[k]);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            // Out of date: recreate next frame. A first image acquired already is released by the swapchain rebuild.
            needRecreate_ = true;
            if (k == 1) vkDeviceWaitIdle(dev_);
            bookkeeping();
            return;
        }
        if (r == VK_SUBOPTIMAL_KHR) needRecreate_ = true;
    }
    vkResetFences(dev_, 1, &f.fence);
    vkResetCommandBuffer(f.cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    record(f.cmd, f, gen ? swapImages_[idx[0]] : VK_NULL_HANDLE, swapImages_[idx[gen ? 1 : 0]], nullptr);
    vkEndCommandBuffer(f.cmd);
    VkSemaphoreSubmitInfo wait[2]{{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
    VkSemaphoreSubmitInfo sig[2]{{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
    for (int k = 0; k < n; ++k) {
        wait[k].semaphore = k ? f.acquired2 : f.acquired;
        wait[k].stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        sig[k].semaphore = rendered_[idx[k]];
        sig[k].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = f.cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = static_cast<std::uint32_t>(n);
    si.pWaitSemaphoreInfos = wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = static_cast<std::uint32_t>(n);
    si.pSignalSemaphoreInfos = sig;
    check(vkQueueSubmit2(queue_, 1, &si, f.fence), "queue submit");
    // In order; FIFO shows each image for at least one refresh, so the display rate is twice the render rate.
    for (int k = 0; k < n; ++k) {
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &rendered_[idx[k]];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap_;
        pi.pImageIndices = &idx[k];
        const VkResult r = vkQueuePresentKHR(queue_, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) needRecreate_ = true;
    }
    bookkeeping();
    frame_ = (frame_ + 1) % kFrames;
}

}  // namespace

std::unique_ptr<Renderer> make_vk_renderer(SDL_Window* win, bool vsync, std::string* error) {
    auto r = std::make_unique<VkRenderer>();
    if (!r->init(win, vsync, error)) return nullptr;
    return r;
}

}  // namespace tto
