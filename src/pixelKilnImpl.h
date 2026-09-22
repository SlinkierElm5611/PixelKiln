//
// Created by Stefan Balta on 2026-07-12.
//

#ifndef PIXELKILN_PIXELKILNIMPL_H
#define PIXELKILN_PIXELKILNIMPL_H
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "computeProgram.h"
#include "config.h"
#include "imageDesc.h"
#include "nativeWindow.h"
#include "programCall.h"
#include "rasterDrawProgram.h"
#include "swapchainDesc.h"

// Two timeline semaphores drive everything:
//  - the transfer timeline is signalled by every submission to the transfer queue (uploads, uniform uploads, downloads)
//  - the all timeline is signalled by every submission to the all queue (program calls), its values are the tickets
// Every resource remembers the last value on each timeline that touched it. Work on one queue that touches a resource
// waits for the other queue's last use of it, so the next transfer overlaps the current draw/compute unless it
// actually conflicts with it. When the device only has one queue both queue references point to it.
class PixelKilnImpl
{
private:
    struct Buffer {
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        uint64_t size = 0;
        uint64_t lastAllUse = 0;
        uint64_t lastTransferUse = 0;
    };
    struct Image {
        vk::Image image;
        vk::ImageView view;
        vk::DeviceMemory memory;
        ImageDesc desc;
        vk::ImageAspectFlags aspect;
        vk::ImageLayout layout = vk::ImageLayout::eUndefined; // layout after all recorded work
        uint64_t lastAllUse = 0;
        uint64_t lastTransferUse = 0;
        uint64_t swapchain = 0; // owning swapchain for swapchain images (image and memory not owned), 0 otherwise
    };
    struct Program {
        ProgramType type = PROGRAM_TYPE_COMPUTE;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout setLayout;
        UniformBindings bindings;
        std::vector<ImageFormat> colorFormats;
        ImageFormat depthFormat = IMAGE_FORMAT_UNDEFINED;
        uint32_t vertexBufferCount = 0;
        uint64_t lastAllUse = 0;
    };
    struct StagingBuffer {
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        void* mapped = nullptr;
        bool coherent = true;
    };
    struct CommandBuffer {
        vk::CommandBuffer commandBuffer;
        uint64_t value = 0; // timeline value of its last submission, UINT64_MAX while being recorded
    };
    struct DescriptorPool {
        vk::DescriptorPool pool;
        uint64_t lastAllUse = 0;
    };
    struct UniformRegion {
        uint64_t offset;
        uint64_t size;
        uint64_t allValue;
    };
    struct AcquireSemaphore {
        vk::Semaphore semaphore;
        uint64_t allValue = 0; // all value of the submission that waited on it, reusable once completed
    };
    struct Swapchain {
        vk::SurfaceKHR surface;
        vk::SwapchainKHR swapchain;
        SwapchainDesc desc;
        SwapchainInfo info;
        vk::SurfaceFormatKHR surfaceFormat;
        vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
        vk::ImageUsageFlags usage;
        std::vector<uint64_t> images; // PixelKiln image handles, one per swapchain image
        std::vector<vk::Semaphore> renderedSemaphores; // one per image index, waited by vkQueuePresentKHR
        std::vector<AcquireSemaphore> acquireSemaphores;
        int64_t acquiredIndex = -1; // image index between acquire and present
        int64_t pendingAcquire = -1; // acquireSemaphores index signalled by acquire and not yet waited on
        bool needsRecreate = true;
    };
    struct PendingDestroy {
        uint64_t allValue;
        uint64_t transferValue;
        std::function<void()> destroy;
    };
    enum QueueKind { QUEUE_ALL, QUEUE_TRANSFER };

    static constexpr uint64_t UNIFORM_RING_SIZE = 4 * 1024 * 1024;
    static constexpr uint32_t DESCRIPTOR_POOL_SETS = 64;
    static constexpr uint32_t DESCRIPTORS_PER_TYPE = 256; // per pool, also the most a single program may declare

    static uint64_t alignUp(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

    Config m_config;
    vk::Instance m_instance;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    vk::PhysicalDevice m_physicalDevice;
    vk::PhysicalDeviceProperties m_physicalDeviceProperties;
    vk::PhysicalDeviceMemoryProperties m_memoryProperties;
    vk::Device m_device;

    uint32_t m_allFamily = 0;
    uint32_t m_transferFamily = 0;
    vk::Queue m_allQueue;
    vk::Queue m_transferQueue; // may be the same queue as m_allQueue
    vk::CommandPool m_allCommandPool;
    vk::CommandPool m_transferCommandPool;
    std::vector<CommandBuffer> m_allCommandBuffers;
    std::vector<CommandBuffer> m_transferCommandBuffers;
    vk::Semaphore m_allTimeline;
    vk::Semaphore m_transferTimeline;
    uint64_t m_allValue = 0; // last value submitted
    uint64_t m_transferValue = 0;

    StagingBuffer m_uniformStaging;
    Buffer m_uniformBuffer;
    uint64_t m_uniformHead = 0;
    std::deque<UniformRegion> m_uniformRegions; // live regions, oldest first
    uint64_t m_uniformReuseValue = 0; // newest all value whose ring region has been retired and may be overwritten

    DescriptorPool m_descriptorPool;
    std::vector<DescriptorPool> m_retiredDescriptorPools;
    std::unordered_map<uint32_t, vk::Sampler> m_samplers;

    uint64_t m_nextHandle = 1;
    std::unordered_map<uint64_t, Program> m_programs;
    std::unordered_map<uint64_t, Buffer> m_buffers;
    std::unordered_map<uint64_t, Image> m_images;
    std::unordered_map<uint64_t, Swapchain> m_swapchains;
    bool m_surfaceSupport = false; // VK_KHR_surface enabled on the instance
    bool m_swapchainSupport = false; // VK_KHR_swapchain enabled on the device
    std::vector<PendingDestroy> m_pendingDestroys;

    // pixelKilnImpl.cpp
    void createInstance();
    void selectPhysicalDevice();
    void createDevice();
    void createSyncObjects();
    void destroyAll();
    uint64_t completedValue(QueueKind queue);
    void waitValue(QueueKind queue, uint64_t value);
    vk::CommandBuffer beginCommands(QueueKind queue);
    // Ends and submits the command buffer. It waits on the other queue's timeline for waitValue (0 = no wait) and
    // signals and returns the next value of its own timeline. Binary semaphores (swapchain acquire / present) can be
    // waited and signalled in the same submission, all queue only.
    uint64_t submitCommands(QueueKind queue, vk::CommandBuffer commandBuffer, uint64_t waitValue,
                            const std::vector<vk::Semaphore> &binaryWaits = {}, vk::Semaphore binarySignal = {});
    void deferDestroy(uint64_t allValue, uint64_t transferValue, std::function<void()> destroy);
    void collectGarbage();

    // pixelKilnImplResources.cpp
    uint32_t findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags required, vk::MemoryPropertyFlags preferred);
    vk::DeviceMemory allocateMemory(vk::MemoryRequirements requirements, vk::MemoryPropertyFlags required,
                                    vk::MemoryPropertyFlags preferred, bool* coherent = nullptr);
    void applySharingMode(vk::BufferCreateInfo &info, uint32_t* families);
    void applySharingMode(vk::ImageCreateInfo &info, uint32_t* families);
    // Orders this transfer submission after earlier transfer submissions (they may run concurrently on the same
    // queue), e.g. two uploads into the same buffer, or an upload after a download of it. Transfer stages only, so
    // it is legal on transfer-only queue families.
    void transferBarrier(vk::CommandBuffer commandBuffer);
    Buffer createDeviceBuffer(uint64_t size, vk::BufferUsageFlags usage);
    StagingBuffer createStagingBuffer(uint64_t size, bool readback);
    void destroyStagingBuffer(const StagingBuffer &staging);
    void readStagingBuffer(const StagingBuffer &staging, void* data, uint64_t size);
    Buffer &getBuffer(uint64_t buffer);
    Image &getImage(uint64_t image);
    vk::FormatFeatureFlags formatFeatures(ImageFormat format);
    void createUniformRing();
    void retireUniformRegions();
    // Returns the offset of `size` free bytes (size must already be aligned) in both uniform ring buffers.
    uint64_t allocateUniforms(uint64_t size);
    vk::Sampler getSampler(const SamplerDesc &desc);
    vk::DescriptorSet allocateDescriptorSet(vk::DescriptorSetLayout layout);
    DescriptorPool acquireDescriptorPool();

    // pixelKilnImplSwapchain.cpp
    Swapchain &getSwapchain(uint64_t swapchain);
    // Checks that a swapchain image may be used right now (acquired and not yet presented).
    void checkSwapchainImage(const Image &image);
    // Returns false when the window currently has no area, the swapchain is then left for a later acquire.
    bool recreateSwapchain(uint64_t handle, Swapchain &swapchain);
    void destroySwapchainImages(Swapchain &swapchain);
    void teardownSwapchain(Swapchain &swapchain);

    // pixelKilnImplPrograms.cpp
    Program &getProgram(uint64_t program);
    vk::DescriptorSetLayout createSetLayout(const UniformBindings &bindings, vk::ShaderStageFlags stages);
    vk::ShaderModule createShaderModule(const Shader &shader);

public:
    uint64_t loadComputeProgram(const ComputeProgram &program);
    uint64_t loadRasterDrawProgram(const RasterDrawProgram &program);
    void unloadProgram(uint64_t program);

    uint64_t createBuffer(uint64_t size);
    void destroyBuffer(uint64_t buffer);
    void uploadBuffer(uint64_t buffer, const void* data, uint64_t size, uint64_t offset);
    void downloadBuffer(uint64_t buffer, void* data, uint64_t size, uint64_t offset);

    uint64_t createImage(const ImageDesc &desc);
    void destroyImage(uint64_t image);
    void uploadImage(uint64_t image, const void* data, uint64_t size);
    void downloadImage(uint64_t image, void* data, uint64_t size);

    uint64_t call(const ProgramCall &call);
    bool isComplete(uint64_t ticket);
    void wait(uint64_t ticket);
    void waitIdle();

    uint64_t createSwapchain(const NativeWindow &window, const SwapchainDesc &desc);
    void destroySwapchain(uint64_t swapchain);
    void resizeSwapchain(uint64_t swapchain, uint32_t width, uint32_t height);
    SwapchainInfo getSwapchainInfo(uint64_t swapchain);
    uint64_t acquireSwapchainImage(uint64_t swapchain);
    void present(uint64_t swapchain);

    PixelKilnImpl(Config config);
    ~PixelKilnImpl();
};

#endif //PIXELKILN_PIXELKILNIMPL_H
