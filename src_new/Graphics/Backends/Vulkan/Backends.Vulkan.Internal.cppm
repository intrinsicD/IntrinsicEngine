module;

// =============================================================================
// Backends.Vulkan.Internal — non-exported partition.
//
// Declares all Vulkan-backend-internal types and helper function signatures.
// This partition is imported by every Vulkan backend implementation unit
// (Mappings, Staging, Profiler, Bindless, Transfer, CommandContext, Device)
// but is NEVER re-exported from the umbrella Extrinsic.Backends.Vulkan module.
// External consumers see only CreateVulkanDevice().
// =============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Internal;

// Re-export all Extrinsic imports so that implementation TUs only need
// `import :Internal;` — no need to repeat the same import list six times.
export import Extrinsic.Core.Config.Render;
export import Extrinsic.Core.ResourcePool;
export import Extrinsic.Core.Telemetry;
export import Extrinsic.RHI.Bindless;
export import Extrinsic.RHI.CommandContext;
export import Extrinsic.RHI.Descriptors;
export import Extrinsic.RHI.Types;
export import Extrinsic.RHI.Device;
export import Extrinsic.RHI.FrameHandle;
export import Extrinsic.RHI.Handles;
export import Extrinsic.RHI.Profiler;
export import Extrinsic.RHI.Transfer;
export import Extrinsic.RHI.TransferQueue;
export import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Vulkan
{
    #if __cpp_lib_move_only_function >= 202110L
    using VulkanDeferredDelete = std::move_only_function<void()>;
    #else
    using VulkanDeferredDelete = std::function<void()>;
    #endif


// =============================================================================
// §1  Constants  (module linkage — not visible outside Extrinsic.Backends.Vulkan)
// =============================================================================

constexpr uint32_t kMaxFramesInFlight   = 3;
constexpr uint32_t kBindlessCapacity    = 65536;
constexpr uint32_t kMaxTimestampScopes  = 256;  // per frame
constexpr uint64_t kStagingBeltCapacity = 64ull * 1024 * 1024; // 64 MiB

// =============================================================================
// §2  Mapping table declarations  (module linkage — defined in Mappings.cpp)
// =============================================================================

[[nodiscard]] VkFormat              ToVkFormat(RHI::Format f);
[[nodiscard]] VkImageLayout         ToVkImageLayout(RHI::TextureLayout l);
[[nodiscard]] VkFilter              ToVkFilter(RHI::FilterMode m);
[[nodiscard]] VkSamplerMipmapMode   ToVkMipmapMode(RHI::MipmapMode m);
[[nodiscard]] VkSamplerAddressMode  ToVkAddressMode(RHI::AddressMode m);
[[nodiscard]] VkCompareOp           ToVkCompareOp(RHI::CompareOp o);
[[nodiscard]] VkPrimitiveTopology   ToVkTopology(RHI::Topology t);
[[nodiscard]] VkCullModeFlags       ToVkCullMode(RHI::CullMode c);
[[nodiscard]] VkFrontFace           ToVkFrontFace(RHI::FrontFace f);
[[nodiscard]] VkPolygonMode         ToVkFillMode(RHI::FillMode f);
[[nodiscard]] VkBlendFactor         ToVkBlendFactor(RHI::BlendFactor b);
[[nodiscard]] VkBlendOp             ToVkBlendOp(RHI::BlendOp o);
[[nodiscard]] VkCompareOp           ToVkDepthOp(RHI::DepthOp o);
[[nodiscard]] VkImageAspectFlags    AspectFromFormat(VkFormat f);
[[nodiscard]] VkBufferUsageFlags    ToVkBufferUsage(RHI::BufferUsage u);
[[nodiscard]] VkImageUsageFlags     ToVkTextureUsage(RHI::TextureUsage u);
[[nodiscard]] VkAccessFlags2        ToVkAccess(RHI::MemoryAccess a);
[[nodiscard]] VkPipelineStageFlags2 ToVkStage(RHI::MemoryAccess a);
[[nodiscard]] VkPresentModeKHR      ToVkPresentMode(RHI::PresentMode m,
                                                     const std::vector<VkPresentModeKHR>& available);
[[nodiscard]] VkIndexType           ToVkIndexType(RHI::IndexType t);

// =============================================================================
// §4  Internal GPU resource types
// =============================================================================

struct VulkanBuffer
{
    VkBuffer      Buffer     = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    void*         MappedPtr  = nullptr;   // non-null only for host-visible
    uint64_t      SizeBytes  = 0;
    bool          HostVisible = false;
    bool          HasBDA      = false;    // created with SHADER_DEVICE_ADDRESS
};

struct VulkanImage
{
    VkImage       Image       = VK_NULL_HANDLE;
    VkImageView   View        = VK_NULL_HANDLE;
    VmaAllocation Allocation  = VK_NULL_HANDLE;  // VK_NULL_HANDLE for swapchain images
    VkFormat      Format      = VK_FORMAT_UNDEFINED;
    uint32_t      Width       = 0;
    uint32_t      Height      = 0;
    uint32_t      MipLevels   = 1;
    uint32_t      ArrayLayers = 1;
    bool          OwnsMemory  = true;     // false for swapchain images
};

struct VulkanSampler
{
    VkSampler Sampler = VK_NULL_HANDLE;
};

struct VulkanPipeline
{
    VkPipeline          Pipeline   = VK_NULL_HANDLE;
    VkPipelineLayout    Layout     = VK_NULL_HANDLE;
    VkPipelineBindPoint BindPoint  = VK_PIPELINE_BIND_POINT_GRAPHICS;
    bool                OwnsLayout = false; // false when using the global layout
};

// =============================================================================
// §5  StagingBelt — host-visible ring-buffer for async uploads
// =============================================================================

class StagingBelt
{
public:
    struct Allocation
    {
        VkBuffer Buffer     = VK_NULL_HANDLE;
        size_t   Offset     = 0;
        void*    MappedPtr  = nullptr;
        size_t   Size       = 0;
    };

    explicit StagingBelt(VkDevice device, VmaAllocator vma, size_t capacityBytes);
    ~StagingBelt();

    StagingBelt(const StagingBelt&)            = delete;
    StagingBelt& operator=(const StagingBelt&) = delete;

    [[nodiscard]] Allocation Allocate(size_t sizeBytes, size_t alignment);
    void Retire(uint64_t retireValue);
    void GarbageCollect(uint64_t completedValue);

    [[nodiscard]] size_t Capacity() const noexcept { return m_Capacity; }

private:
    static size_t AlignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

    struct Region { size_t Begin, End; uint64_t RetireValue; };

    VkDevice      m_Device  = VK_NULL_HANDLE;
    VmaAllocator  m_Vma     = VK_NULL_HANDLE;
    VkBuffer      m_Buffer  = VK_NULL_HANDLE;
    VmaAllocation m_Alloc   = VK_NULL_HANDLE;
    void*         m_Mapped  = nullptr;
    size_t        m_Capacity;
    size_t        m_Head    = 0;  // oldest live byte
    size_t        m_Tail    = 0;  // next free byte
    size_t        m_PendingBegin = 0;
    size_t        m_PendingEnd   = 0;
    bool          m_HasPending   = false;
    std::deque<Region> m_InFlight;
    std::mutex    m_Mutex;
};

// =============================================================================
// §6  VulkanProfiler — IProfiler backed by VkQueryPool timestamps
// =============================================================================

class VulkanProfiler final : public RHI::IProfiler
{
public:
    explicit VulkanProfiler(VkDevice device, VkPhysicalDevice physDevice,
                             uint32_t framesInFlight);
    ~VulkanProfiler() override;

    // Called by VulkanDevice::BeginFrame to inject the current command buffer.
    void SetCommandBuffer(VkCommandBuffer cmd) { m_Cmd = cmd; }
    // Called by VulkanDevice to reset queries for a frame.
    void ResetFrame(uint32_t frameIndex, VkCommandBuffer cmd);

    // IProfiler
    void BeginFrame(uint32_t frameIndex, uint32_t maxScopesHint) override;
    void EndFrame() override;
    [[nodiscard]] uint32_t BeginScope(std::string_view name) override;
    void EndScope(uint32_t scopeHandle) override;
    [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
        Resolve(uint32_t frameIndex) const override;
    [[nodiscard]] uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }

private:
    struct ScopeRecord { std::string Name; uint32_t BeginQuery, EndQuery; };
    struct FrameState
    {
        uint32_t FrameIndex  = 0;
        uint32_t QueryBase   = 0;
        uint32_t QueryCount  = 0;
        uint32_t FrameBegin  = ~0u;
        uint32_t FrameEnd    = ~0u;
        std::vector<ScopeRecord> Scopes;
    };

    VkDevice        m_Device    = VK_NULL_HANDLE;
    VkQueryPool     m_Pool      = VK_NULL_HANDLE;
    VkCommandBuffer m_Cmd       = VK_NULL_HANDLE;
    double          m_PeriodNs  = 1.0;
    bool            m_Supported = false;
    uint32_t        m_FramesInFlight;
    uint32_t        m_TotalQueries;  // kMaxFramesInFlight * (2 + 2*kMaxTimestampScopes)
    std::vector<FrameState> m_Frames;
    mutable std::mutex m_Mutex;
};

// =============================================================================
// §7  VulkanBindlessHeap — IBindlessHeap (PARTIALLY_BOUND descriptor array)
// =============================================================================

class VulkanBindlessHeap final : public RHI::IBindlessHeap
{
public:
    explicit VulkanBindlessHeap(VkDevice device, uint32_t capacity = kBindlessCapacity);
    ~VulkanBindlessHeap() override;

    // Called during VulkanDevice::Initialize to write the default descriptor.
    void SetDefault(VkImageView view, VkSampler sampler);

    [[nodiscard]] VkDescriptorSetLayout GetLayout() const { return m_Layout; }
    [[nodiscard]] VkDescriptorSet       GetSet()    const { return m_Set;    }

    // IBindlessHeap
    [[nodiscard]] RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle) override;
    void UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle, RHI::SamplerHandle) override;
    void FreeSlot(RHI::BindlessIndex) override;
    void FlushPending() override;
    [[nodiscard]] uint32_t GetCapacity()           const override { return m_Capacity; }
    [[nodiscard]] uint32_t GetAllocatedSlotCount() const override;

private:
    // Needs raw Vk handles to write descriptors.  Injected via Update helpers.
    // The heap owns NO image/sampler objects — it only stores descriptor entries.
    // VulkanDevice resolves TextureHandle/SamplerHandle → VkImageView/VkSampler
    // before calling EnqueueRawUpdate.
    friend class VulkanDevice;
    void EnqueueRawUpdate(RHI::BindlessIndex slot, VkImageView view, VkSampler sampler,
                          VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    enum class OpType : uint8_t { Allocate, UpdateRaw, Free };
    struct PendingOp
    {
        OpType          Type{};
        RHI::BindlessIndex Slot{};
        VkImageView     View    = VK_NULL_HANDLE;
        VkSampler       Sampler = VK_NULL_HANDLE;
        VkImageLayout   Layout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };

    VkDevice              m_Device   = VK_NULL_HANDLE;
    VkDescriptorPool      m_Pool     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_Layout   = VK_NULL_HANDLE;
    VkDescriptorSet       m_Set      = VK_NULL_HANDLE;
    uint32_t              m_Capacity = 0;
    uint32_t              m_NextSlot = 1;  // slot 0 reserved for default
    uint32_t              m_AllocCount = 0;
    std::vector<RHI::BindlessIndex> m_FreeSlots;
    std::vector<PendingOp>          m_Pending;
    mutable std::mutex              m_Mutex;
};

// =============================================================================
// §8  VulkanTransferQueue — ITransferQueue (timeline-semaphore + StagingBelt)
// =============================================================================

class VulkanTransferQueue final : public RHI::ITransferQueue
{
public:
    struct Config
    {
        VkDevice     Device;
        VmaAllocator Vma;
        VkQueue      Queue;          // transfer or graphics queue
        uint32_t     QueueFamily;
        size_t       StagingCapacity = kStagingBeltCapacity;
    };

    explicit VulkanTransferQueue(const Config& cfg);
    ~VulkanTransferQueue() override;

    // Called by VulkanDevice to find completed transfers.
    [[nodiscard]] uint64_t QueryCompletedValue() const;

    // ITransferQueue
    [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle dst,
                                                   const void* data,
                                                   uint64_t size,
                                                   uint64_t offset) override;
    [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle dst,
                                                   std::span<const std::byte> src,
                                                   uint64_t offset) override;
    [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle dst,
                                                    const void* data,
                                                    uint64_t dataSizeBytes,
                                                    uint32_t mipLevel,
                                                    uint32_t arrayLayer) override;
    [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override;
    void CollectCompleted() override;

    // Allow VulkanDevice to resolve handles → raw Vk objects before upload.
    friend class VulkanDevice;

private:
    [[nodiscard]] VkCommandBuffer Begin();
    [[nodiscard]] RHI::TransferToken Submit(VkCommandBuffer cmd);

    VkDevice      m_Device        = VK_NULL_HANDLE;
    VmaAllocator  m_Vma           = VK_NULL_HANDLE;
    VkQueue       m_Queue         = VK_NULL_HANDLE;
    uint32_t      m_QueueFamily   = 0;
    VkSemaphore   m_Timeline      = VK_NULL_HANDLE;
    VkCommandPool m_CmdPool       = VK_NULL_HANDLE;
    std::atomic<uint64_t> m_NextTicket{1};
    std::unique_ptr<StagingBelt> m_Belt;
    mutable std::mutex m_Mutex;

    // Pointer back to device resource pools (set by VulkanDevice after construction).
    Core::ResourcePool<VulkanBuffer, RHI::BufferHandle,   kMaxFramesInFlight>* m_Buffers = nullptr;
    Core::ResourcePool<VulkanImage,  RHI::TextureHandle,  kMaxFramesInFlight>* m_Images  = nullptr;
};

// =============================================================================
// §9  VulkanCommandContext — ICommandContext (one per frame-in-flight slot)
// =============================================================================

class VulkanCommandContext final : public RHI::ICommandContext
{
public:
    VulkanCommandContext() = default;

    // Set up each frame by VulkanDevice::BeginFrame.
    void Bind(VkDevice device,
              VkCommandBuffer cmd,
              VkPipelineLayout globalLayout,
              VkDescriptorSet  bindlessSet,
              const Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight>* buffers,
              const Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight>* images,
              const Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight>* pipelines);

    // ICommandContext
    void Begin() override;
    void End()   override;

    void BeginRenderPass(const RHI::RenderPassDesc& desc) override;
    void EndRenderPass() override;

    void SetViewport(float x, float y, float w, float h, float minD, float maxD) override;
    void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;

    void BindPipeline(RHI::PipelineHandle pipeline) override;
    void BindIndexBuffer(RHI::BufferHandle buffer, uint64_t offset,
                         RHI::IndexType indexType) override;
    void PushConstants(const void* data, uint32_t size, uint32_t offset) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void DrawIndirect(RHI::BufferHandle argBuf, uint64_t offset, uint32_t drawCount) override;
    void DrawIndexedIndirect(RHI::BufferHandle argBuf, uint64_t offset, uint32_t drawCount) override;
    void DrawIndexedIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                                   RHI::BufferHandle cntBuf, uint64_t cntOffset,
                                   uint32_t maxDraw) override;
    void DrawIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                           RHI::BufferHandle cntBuf, uint64_t cntOffset,
                           uint32_t maxDraw) override;
    void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) override;
    void DispatchIndirect(RHI::BufferHandle argBuf, uint64_t offset) override;

    void TextureBarrier(RHI::TextureHandle tex, RHI::TextureLayout before,
                         RHI::TextureLayout after) override;
    void BufferBarrier(RHI::BufferHandle buf, RHI::MemoryAccess before,
                        RHI::MemoryAccess after) override;
    void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override;

    void FillBuffer(RHI::BufferHandle buffer, uint64_t offset, uint64_t size,
                    uint32_t value) override;

    void CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                    uint64_t srcOff, uint64_t dstOff, uint64_t size) override;
    void CopyBufferToTexture(RHI::BufferHandle src, uint64_t srcOff,
                              RHI::TextureHandle dst,
                              uint32_t mipLevel, uint32_t arrayLayer) override;

private:
    VkDevice         m_Device        = VK_NULL_HANDLE;
    VkCommandBuffer  m_Cmd           = VK_NULL_HANDLE;
    VkPipelineLayout m_GlobalLayout  = VK_NULL_HANDLE;
    VkDescriptorSet  m_BindlessSet   = VK_NULL_HANDLE;
    VkPipelineBindPoint m_BindPoint  = VK_PIPELINE_BIND_POINT_GRAPHICS;

    const Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight>* m_Buffers   = nullptr;
    const Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight>* m_Images    = nullptr;
    const Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight>* m_Pipelines = nullptr;
};

// =============================================================================
// §10  Per-frame resources
// =============================================================================

struct PerFrame
{
    VkCommandPool   CmdPool       = VK_NULL_HANDLE;
    VkCommandBuffer CmdBuffer     = VK_NULL_HANDLE;
    VkFence         Fence         = VK_NULL_HANDLE;  // CPU-GPU sync
    VkSemaphore     ImageAcquired = VK_NULL_HANDLE;  // binary
    VkSemaphore     RenderDone    = VK_NULL_HANDLE;  // binary
    // Deferred-deletion lambdas executed when this slot is next reused.
    std::vector<VulkanDeferredDelete> DeletionQueue;
};

// =============================================================================
// §11  VulkanDevice — IDevice
// =============================================================================

class VulkanDevice final : public RHI::IDevice
{
public:
    VulkanDevice()  = default;
    ~VulkanDevice() override;

    // IDevice — lifecycle
    void Initialize(Platform::IWindow& window,
                    const Core::Config::RenderConfig& config) override;
    void Shutdown()  override;
    void WaitIdle()  override;

    [[nodiscard]] bool IsOperational() const noexcept override { return m_Operational; }

    // IDevice — frame
    bool BeginFrame(RHI::FrameHandle& outFrame) override;
    void EndFrame(const RHI::FrameHandle& frame) override;
    void Present(const RHI::FrameHandle& frame) override;

    void Resize(uint32_t width, uint32_t height) override;
    [[nodiscard]] Platform::Extent2D GetBackbufferExtent() const override;

    void SetPresentMode(RHI::PresentMode mode) override;
    [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return m_PresentMode; }

    [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle& frame) const override;

    // IDevice — commands
    [[nodiscard]] RHI::ICommandContext& GetGraphicsContext(uint32_t frameIndex) override;

    // IDevice — buffers
    [[nodiscard]] RHI::BufferHandle CreateBuffer(const RHI::BufferDesc& desc) override;
    void DestroyBuffer(RHI::BufferHandle handle) override;
    void WriteBuffer(RHI::BufferHandle handle, const void* data,
                     uint64_t size, uint64_t offset) override;
    [[nodiscard]] uint64_t GetBufferDeviceAddress(RHI::BufferHandle handle) const override;

    // IDevice — textures
    [[nodiscard]] RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override;
    void DestroyTexture(RHI::TextureHandle handle) override;
    void WriteTexture(RHI::TextureHandle handle, const void* data,
                      uint64_t dataSizeBytes, uint32_t mipLevel,
                      uint32_t arrayLayer) override;

    // IDevice — samplers
    [[nodiscard]] RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc& desc) override;
    void DestroySampler(RHI::SamplerHandle handle) override;

    // IDevice — pipelines
    [[nodiscard]] RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc& desc) override;
    void DestroyPipeline(RHI::PipelineHandle handle) override;

    // IDevice — subsystems
    [[nodiscard]] RHI::ITransferQueue& GetTransferQueue() override { return *m_TransferQueue; }
    [[nodiscard]] RHI::IBindlessHeap&  GetBindlessHeap()  override { return *m_BindlessHeap; }
    [[nodiscard]] RHI::IProfiler*      GetProfiler()      override { return m_Profiler.get(); }
    [[nodiscard]] uint32_t GetFramesInFlight()    const override { return kMaxFramesInFlight; }
    [[nodiscard]] uint64_t GetGlobalFrameNumber() const override { return m_GlobalFrameNumber; }

private:
    // ---- Init helpers ---------------------------------------------------
    void CreateInstance(const Core::Config::RenderConfig& cfg);
    void CreateSurface(Platform::IWindow& window);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateVma();
    void CreateSwapchain(uint32_t width, uint32_t height);
    void DestroySwapchain();
    void RegisterSwapchainImages();
    void CreatePerFrameResources();
    void DestroyPerFrameResources();
    void CreateGlobalPipelineLayout();
    void CreateDefaultSampler();

    // ---- Utility --------------------------------------------------------
    [[nodiscard]] uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    [[nodiscard]] VkFormat FindDepthFormat() const;
    [[nodiscard]] bool     SupportsFormat(VkFormat fmt, VkFormatFeatureFlags feats) const;

    // Submit a one-shot command buffer on the graphics queue (blocks until done).
    [[nodiscard]] VkCommandBuffer BeginOneShot();
    void EndOneShot(VkCommandBuffer cmd);

    // Deferred deletion — safe to call from any thread.
    void DeferDelete(VulkanDeferredDelete fn);
    void FlushDeletionQueue(uint32_t frameSlot);

    // ---- Vulkan core objects --------------------------------------------
    VkInstance               m_Instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_Messenger      = VK_NULL_HANDLE;
    VkSurfaceKHR             m_Surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_PhysDevice     = VK_NULL_HANDLE;
    VkDevice                 m_Device         = VK_NULL_HANDLE;
    VmaAllocator             m_Vma            = VK_NULL_HANDLE;

    // ---- Queues ---------------------------------------------------------
    VkQueue  m_GraphicsQueue    = VK_NULL_HANDLE;
    VkQueue  m_PresentQueue     = VK_NULL_HANDLE;
    VkQueue  m_TransferVkQueue  = VK_NULL_HANDLE;   // raw VkQueue (renamed — see m_TransferQueue below)
    uint32_t m_GraphicsFamily = 0;
    uint32_t m_PresentFamily  = 0;
    uint32_t m_TransferFamily = 0;
    std::mutex m_QueueMutex;

    // ---- Swapchain ------------------------------------------------------
    VkSwapchainKHR              m_Swapchain        = VK_NULL_HANDLE;
    VkFormat                    m_SwapchainFormat  = VK_FORMAT_UNDEFINED;
    VkExtent2D                  m_SwapchainExtent  = {};
    std::vector<VkImage>        m_SwapchainImages;
    std::vector<VkImageView>    m_SwapchainViews;
    std::vector<RHI::TextureHandle> m_SwapchainHandles; // registered in m_Images

    // ---- Per-frame resources --------------------------------------------
    std::array<PerFrame, kMaxFramesInFlight> m_Frames;
    uint32_t m_FrameSlot         = 0;  // cycles [0, kMaxFramesInFlight)
    uint64_t m_GlobalFrameNumber = 0;

    // ---- Command contexts (one per frame slot) --------------------------
    std::array<VulkanCommandContext, kMaxFramesInFlight> m_CmdContexts;

    // ---- Resource pools -------------------------------------------------
    Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight> m_Buffers;
    Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight> m_Images;
    Core::ResourcePool<VulkanSampler,  RHI::SamplerHandle,  kMaxFramesInFlight> m_Samplers;
    Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight> m_Pipelines;

    // ---- Subsystems -----------------------------------------------------
    std::unique_ptr<VulkanBindlessHeap>   m_BindlessHeap;
    std::unique_ptr<VulkanProfiler>       m_Profiler;
    std::unique_ptr<VulkanTransferQueue>  m_TransferQueue;

    // ---- Global pipeline layout (set 0 = bindless, push 0..128B) --------
    VkPipelineLayout m_GlobalPipelineLayout = VK_NULL_HANDLE;

    // ---- Default sampler (used for bindless slot 0) ---------------------
    RHI::SamplerHandle m_DefaultSamplerHandle{};

    // ---- State ----------------------------------------------------------
    RHI::PresentMode m_PresentMode  = RHI::PresentMode::VSync;
    bool             m_Operational  = false;
    bool             m_NeedsResize  = false;
    bool             m_ValidationEnabled = false;
};

} // namespace Extrinsic::Backends::Vulkan
