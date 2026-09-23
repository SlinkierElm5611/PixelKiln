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
#include <vk_mem_alloc.h>

#include "computeProgram.h"
#include "config.h"
#include "imageDesc.h"
#include "nativeWindow.h"
#include "programCall.h"
#include "rasterDrawProgram.h"
#include "swapchainDesc.h"

// Two timeline semaphores drive everything:
//  - the transfer timeline is signalled by every submission to the transfer queue (uploads and downloads)
//  - the all timeline is signalled by every submission to the all queue (program calls, present). Every call gets
//    the next value as its ticket; calls are recorded into an open batch that signals its last ticket when submitted.
// Every resource remembers the last value on each timeline that touched it. Work on one queue that touches a resource
// waits for the other queue's last use of it, so the next transfer overlaps the current draw/compute unless it
// actually conflicts with it. When the device only has one queue both queue references point to it.
class PixelKilnImpl
{
private:
    enum QueueKind { QUEUE_ALL, QUEUE_TRANSFER };

    // Memory comes from VMA, which sub-allocates from large blocks instead of one vkAllocateMemory per resource.
    struct Buffer {
        vk::Buffer buffer;
        VmaAllocation allocation = nullptr;
        uint64_t size = 0;
        uint64_t lastAllUse = 0;
        uint64_t lastTransferUse = 0;
    };
    struct Image {
        vk::Image image;
        vk::ImageView view;
        VmaAllocation allocation = nullptr; // null for swapchain images
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
        bool pushDescriptors = false; // descriptors are pushed into the command buffer instead of allocated
        UniformBindings bindings;
        uint32_t pushConstantSize = 0;
        vk::ShaderStageFlags pushConstantStages;
        std::vector<ImageFormat> colorFormats;
        ImageFormat depthFormat = IMAGE_FORMAT_UNDEFINED;
        uint32_t samples = 1;
        uint32_t vertexBufferCount = 0;
        uint64_t lastAllUse = 0;
    };
    struct StagingBuffer {
        vk::Buffer buffer;
        VmaAllocation allocation = nullptr;
        void* mapped = nullptr; // persistently mapped
        uint64_t size = 0;
    };
    struct RingRegion {
        uint64_t offset;
        uint64_t size;
        uint64_t value; // timeline value of the submission reading it
    };
    // A persistently mapped buffer sub-allocated in FIFO order. A region is free again once the submission reading it
    // has completed, so allocating only waits when the ring is full.
    struct Ring {
        StagingBuffer staging;
        QueueKind queue = QUEUE_ALL; // timeline of the region values
        std::deque<RingRegion> regions; // live regions, oldest first
    };
    struct CommandBuffer {
        vk::CommandBuffer commandBuffer;
        uint64_t value = 0; // timeline value of its last submission, UINT64_MAX while being recorded
    };
    // All queue submission being recorded. Calls are appended to it and it is submitted when something needs its
    // results, the all queue runs dry, or it holds MAX_BATCH_CALLS calls (see flushBatch).
    struct Batch {
        vk::CommandBuffer commandBuffer; // null while no batch is open
        uint32_t callCount = 0;
        uint64_t waitTransfer = 0; // transfer value to wait for, the newest upload into anything the batch uses
        std::vector<vk::Semaphore> acquireWaits; // swapchain acquire semaphores
    };
    struct DescriptorPool {
        vk::DescriptorPool pool;
        uint64_t lastAllUse = 0;
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
        std::deque<uint64_t> presentValues; // all values of the latest presents, oldest first
        bool needsRecreate = true;
    };
    struct PendingDestroy {
        uint64_t allValue;
        uint64_t transferValue;
        std::function<void()> destroy;
    };

    static constexpr uint64_t UNIFORM_RING_SIZE = 4 * 1024 * 1024;
    static constexpr uint64_t UPLOAD_RING_SIZE = 16 * 1024 * 1024;
    static constexpr uint64_t UPLOAD_CHUNK_SIZE = UPLOAD_RING_SIZE / 4; // larger buffer uploads are staged in pieces
    static constexpr uint64_t READBACK_MIN_SIZE = 1024 * 1024;
    static constexpr uint64_t READBACK_MAX_SIZE = 32 * 1024 * 1024; // larger buffer downloads are read back in pieces
    static constexpr uint32_t MAX_BATCH_CALLS = 128;
    static constexpr uint32_t DESCRIPTOR_POOL_SETS = 64;
    static constexpr uint32_t DESCRIPTORS_PER_TYPE = 256; // per pool, also the most a single program may declare

    static uint64_t alignUp(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

    Config m_config;
    vk::Instance m_instance;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    vk::PhysicalDevice m_physicalDevice;
    vk::PhysicalDeviceProperties m_physicalDeviceProperties;
    vk::FormatFeatureFlags m_formatFeatures[IMAGE_FORMAT_COUNT] = {}; // optimal tiling features of each ImageFormat
    vk::SampleCountFlags m_integerColorSampleCounts; // framebufferIntegerColorSampleCounts
    vk::Device m_device;
    VmaAllocator m_allocator = nullptr;

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
    uint64_t m_allValue = 0; // last ticket handed out, it may still be in the open batch
    uint64_t m_allSubmitted = 0; // last all value submitted
    std::deque<uint64_t> m_allSignals; // values signalled by submitted batches that haven't completed yet
    uint64_t m_allRetiredSignal = 0; // newest signalled value known to have completed
    uint64_t m_transferValue = 0; // last transfer value submitted
    Batch m_batch;

    Ring m_uniformRing{{}, QUEUE_ALL, {}}; // call uniform data, the GPU reads it straight from mapped memory
    Ring m_uploadRing{{}, QUEUE_TRANSFER, {}}; // upload staging, created on first use
    StagingBuffer m_readback; // download staging, grown on demand up to READBACK_MAX_SIZE

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
    uint32_t m_maxPushDescriptors = 0; // 0 without VK_KHR_push_descriptor
    PFN_vkCmdPushDescriptorSetKHR m_cmdPushDescriptorSet = nullptr;
    bool m_debugUtilsSupport = false; // VK_EXT_debug_utils enabled on the instance, independent of enableValidation
    PFN_vkSetDebugUtilsObjectNameEXT m_setDebugUtilsObjectName = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT m_cmdBeginDebugUtilsLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT m_cmdEndDebugUtilsLabel = nullptr;
    std::vector<PendingDestroy> m_pendingDestroys;

    // pixelKilnImpl.cpp
    void createInstance();
    void selectPhysicalDevice();
    void createDevice();
    void createAllocator();
    void createSyncObjects();
    void destroyAll();
    uint64_t completedValue(QueueKind queue);
    // A signalled all value that completes ticket `allValue` (0 = none needed), submitting the open batch if it holds
    // the ticket. Waits use it rather than the ticket itself: a batch only signals its last ticket, and waiting for a
    // value between two signals, although valid, trips the validation layer. For completed tickets it is a newer
    // completed signal, and still has to be waited on: the wait is what makes the work's writes visible.
    uint64_t submittedValue(uint64_t allValue);
    void waitValue(QueueKind queue, uint64_t value);
    vk::CommandBuffer beginCommands(QueueKind queue);
    // Ends and submits the command buffer. It waits on the other queue's timeline for waitValue (0 = no wait), which
    // must already be submitted. A transfer submission signals and returns the next transfer value, an all queue
    // submission signals m_allValue (the newest ticket). Binary semaphores (swapchain acquire / present) can be
    // waited and signalled in the same submission, all queue only.
    uint64_t submitCommands(QueueKind queue, vk::CommandBuffer commandBuffer, uint64_t waitValue,
                            const std::vector<vk::Semaphore> &binaryWaits = {}, vk::Semaphore binarySignal = {});
    // The open batch's command buffer, begun if needed.
    vk::CommandBuffer batchCommands();
    // Submits the open batch, if any. A present signal forces a submission even for an empty batch.
    void flushBatch(vk::Semaphore binarySignal = {});
    void deferDestroy(uint64_t allValue, uint64_t transferValue, std::function<void()> destroy);
    void collectGarbage();
    // Diagnostic only, both no-ops when VK_EXT_debug_utils isn't available or name/label is null. Visible in graphics
    // debuggers (RenderDoc, Nsight) attached to the process, no effect otherwise.
    void setDebugName(vk::ObjectType type, uint64_t handle, const char* name);
    void beginDebugLabel(vk::CommandBuffer commandBuffer, const char* label);
    void endDebugLabel(vk::CommandBuffer commandBuffer);

    // pixelKilnImplResources.cpp
    void applySharingMode(vk::BufferCreateInfo &info, uint32_t* families);
    void applySharingMode(vk::ImageCreateInfo &info, uint32_t* families);
    // Orders this transfer submission after earlier transfer submissions (they may run concurrently on the same
    // queue), e.g. two uploads into the same buffer, or an upload after a download of it. Transfer stages only, so
    // it is legal on transfer-only queue families.
    void transferBarrier(vk::CommandBuffer commandBuffer);
    Buffer createDeviceBuffer(uint64_t size, vk::BufferUsageFlags usage);
    // Host-visible buffer mapped for its whole life. Sequential write access gets coherent memory (no flushes needed),
    // random access (readback) prefers cached memory.
    StagingBuffer createMappedBuffer(uint64_t size, vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                                     VmaAllocationCreateFlags hostAccess);
    StagingBuffer createStagingBuffer(uint64_t size, bool readback);
    void createUniformRing();
    void destroyStagingBuffer(const StagingBuffer &staging);
    void readStagingBuffer(const StagingBuffer &staging, uint64_t offset, void* data, uint64_t size);
    // Makes m_readback at least `size` bytes (size <= READBACK_MAX_SIZE). Downloads wait for their copy, so it is
    // always idle between them.
    void reserveReadback(uint64_t size);
    // Offset of `size` free bytes in the ring, waiting for the oldest regions when it is full. The caller adds the
    // region once it knows the timeline value of the submission reading it.
    uint64_t allocateRing(Ring &ring, uint64_t size, uint64_t alignment);
    Ring &uploadRing(); // m_uploadRing, created on first use
    Buffer &getBuffer(uint64_t buffer);
    Image &getImage(uint64_t image);
    vk::FormatFeatureFlags formatFeatures(ImageFormat format);
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
    // Programs with at most maxPushDescriptors descriptors push them with each call: cheaper than allocating a set
    // per call, much cheaper on some drivers. Larger programs (or devices without push descriptors) use pools.
    vk::DescriptorSetLayout createSetLayout(const UniformBindings &bindings, vk::ShaderStageFlags stages,
                                            bool &pushDescriptors);
    vk::ShaderModule createShaderModule(const Shader &shader);

public:
    uint64_t loadComputeProgram(const ComputeProgram &program, const char* debugName = nullptr);
    uint64_t loadRasterDrawProgram(const RasterDrawProgram &program, const char* debugName = nullptr);
    void unloadProgram(uint64_t program);

    uint64_t createBuffer(uint64_t size, const char* debugName = nullptr);
    void destroyBuffer(uint64_t buffer);
    void uploadBuffer(uint64_t buffer, const void* data, uint64_t size, uint64_t offset);
    void downloadBuffer(uint64_t buffer, void* data, uint64_t size, uint64_t offset);

    uint64_t createImage(const ImageDesc &desc, const char* debugName = nullptr);
    void destroyImage(uint64_t image);
    uint32_t getSupportedSampleCounts(ImageFormat format);
    void uploadImage(uint64_t image, const void* data, uint64_t size);
    void downloadImage(uint64_t image, void* data, uint64_t size);

    uint64_t call(const ProgramCall &call);
    void flush();
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
