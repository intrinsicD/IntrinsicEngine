module;

// =============================================================================
// Extrinsic Vulkan Backend — full IDevice implementation.
//
// Internal structure (all types are file-local, never exported):
//
//   Utilities        — VkFormat/VkImageLayout/VkFilter/etc. mapping tables
//   VulkanBuffer     — VMA-backed VkBuffer + BDA
//   VulkanImage      — VMA-backed VkImage + VkImageView
//   VulkanSampler    — VkSampler
//   VulkanPipeline   — VkPipeline + VkPipelineLayout + bind point
//   StagingBelt      — host-visible ring-buffer for async uploads
//   VulkanProfiler   — IProfiler impl (VkQueryPool timestamp ring)
//   VulkanBindlessHeap — IBindlessHeap impl (PARTIALLY_BOUND descriptor set)
//   VulkanTransferQueue — ITransferQueue impl (timeline semaphore + StagingBelt)
//   VulkanCommandContext — ICommandContext impl per frame
//   VulkanDevice     — IDevice impl (owns everything)
// =============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.ResourcePool;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §1  Constants
// =============================================================================

static constexpr uint32_t kMaxFramesInFlight    = 3;
static constexpr uint32_t kBindlessCapacity     = 65536;
static constexpr uint32_t kMaxTimestampScopes   = 256;  // per frame
static constexpr uint64_t kStagingBeltCapacity  = 64ull * 1024 * 1024; // 64 MiB

// =============================================================================
// §2  Mapping tables  (RHI enums → Vulkan enums)
// =============================================================================

[[nodiscard]] static VkFormat ToVkFormat(RHI::Format f);
[[nodiscard]] static VkImageLayout ToVkImageLayout(RHI::TextureLayout l);
[[nodiscard]] static VkFilter ToVkFilter(RHI::FilterMode m);
[[nodiscard]] static VkSamplerMipmapMode ToVkMipmapMode(RHI::MipmapMode m);
[[nodiscard]] static VkSamplerAddressMode ToVkAddressMode(RHI::AddressMode m);
[[nodiscard]] static VkCompareOp ToVkCompareOp(RHI::CompareOp o);
[[nodiscard]] static VkPrimitiveTopology ToVkTopology(RHI::Topology t);
[[nodiscard]] static VkCullModeFlags ToVkCullMode(RHI::CullMode c);
[[nodiscard]] static VkFrontFace ToVkFrontFace(RHI::FrontFace f);
[[nodiscard]] static VkPolygonMode ToVkFillMode(RHI::FillMode f);
[[nodiscard]] static VkBlendFactor ToVkBlendFactor(RHI::BlendFactor b);
[[nodiscard]] static VkBlendOp ToVkBlendOp(RHI::BlendOp o);
[[nodiscard]] static VkCompareOp ToVkDepthOp(RHI::DepthOp o);
[[nodiscard]] static VkImageAspectFlags AspectFromFormat(VkFormat f);
[[nodiscard]] static VkBufferUsageFlags ToVkBufferUsage(RHI::BufferUsage u);
[[nodiscard]] static VkImageUsageFlags ToVkTextureUsage(RHI::TextureUsage u);
// Access/stage helpers for barriers
[[nodiscard]] static VkAccessFlags2     ToVkAccess(RHI::MemoryAccess a);
[[nodiscard]] static VkPipelineStageFlags2 ToVkStage(RHI::MemoryAccess a);
[[nodiscard]] static VkPresentModeKHR   ToVkPresentMode(RHI::PresentMode m,
                                                         const std::vector<VkPresentModeKHR>& available);


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

    VkDevice       m_Device    = VK_NULL_HANDLE;
    VkQueryPool    m_Pool      = VK_NULL_HANDLE;
    VkCommandBuffer m_Cmd      = VK_NULL_HANDLE;
    double         m_PeriodNs  = 1.0;
    bool           m_Supported = false;
    uint32_t       m_FramesInFlight;
    uint32_t       m_TotalQueries;  // kMaxFramesInFlight * (2 + 2*kMaxTimestampScopes)
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
    void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) override;
    void DispatchIndirect(RHI::BufferHandle argBuf, uint64_t offset) override;

    void TextureBarrier(RHI::TextureHandle tex, RHI::TextureLayout before,
                         RHI::TextureLayout after) override;
    void BufferBarrier(RHI::BufferHandle buf, RHI::MemoryAccess before,
                        RHI::MemoryAccess after) override;

    void CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                    uint64_t srcOff, uint64_t dstOff, uint64_t size) override;
    void CopyBufferToTexture(RHI::BufferHandle src, uint64_t srcOff,
                              RHI::TextureHandle dst,
                              uint32_t mipLevel, uint32_t arrayLayer) override;

private:
    VkDevice        m_Device        = VK_NULL_HANDLE;
    VkCommandBuffer m_Cmd           = VK_NULL_HANDLE;
    VkPipelineLayout m_GlobalLayout = VK_NULL_HANDLE;
    VkDescriptorSet  m_BindlessSet  = VK_NULL_HANDLE;
    VkPipelineBindPoint m_BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

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
    std::vector<std::move_only_function<void()>> DeletionQueue;
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
    void DeferDelete(std::move_only_function<void()> fn);
    void FlushDeletionQueue(uint32_t frameSlot);

    // ---- Vulkan core objects --------------------------------------------
    VkInstance               m_Instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_Messenger      = VK_NULL_HANDLE;
    VkSurfaceKHR             m_Surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_PhysDevice     = VK_NULL_HANDLE;
    VkDevice                 m_Device         = VK_NULL_HANDLE;
    VmaAllocator             m_Vma            = VK_NULL_HANDLE;

    // ---- Queues ---------------------------------------------------------
    VkQueue  m_GraphicsQueue  = VK_NULL_HANDLE;
    VkQueue  m_PresentQueue   = VK_NULL_HANDLE;
    VkQueue  m_TransferQueue  = VK_NULL_HANDLE;
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

// =============================================================================
// §12  Factory
// =============================================================================

std::unique_ptr<RHI::IDevice> CreateVulkanDevice()
{
    return std::make_unique<VulkanDevice>();
}

// =============================================================================
// Implementation bodies — filled in subsequent passes.
// =============================================================================

// =============================================================================
// §2  Mapping tables
// =============================================================================

static VkFormat ToVkFormat(RHI::Format f)
{
    switch (f)
    {
    case RHI::Format::Undefined:          return VK_FORMAT_UNDEFINED;
    case RHI::Format::R8_UNORM:           return VK_FORMAT_R8_UNORM;
    case RHI::Format::RG8_UNORM:          return VK_FORMAT_R8G8_UNORM;
    case RHI::Format::RGBA8_UNORM:        return VK_FORMAT_R8G8B8A8_UNORM;
    case RHI::Format::RGBA8_SRGB:         return VK_FORMAT_R8G8B8A8_SRGB;
    case RHI::Format::BGRA8_UNORM:        return VK_FORMAT_B8G8R8A8_UNORM;
    case RHI::Format::BGRA8_SRGB:         return VK_FORMAT_B8G8R8A8_SRGB;
    case RHI::Format::R16_FLOAT:          return VK_FORMAT_R16_SFLOAT;
    case RHI::Format::RG16_FLOAT:         return VK_FORMAT_R16G16_SFLOAT;
    case RHI::Format::RGBA16_FLOAT:       return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RHI::Format::R16_UINT:           return VK_FORMAT_R16_UINT;
    case RHI::Format::R16_UNORM:          return VK_FORMAT_R16_UNORM;
    case RHI::Format::R32_FLOAT:          return VK_FORMAT_R32_SFLOAT;
    case RHI::Format::RG32_FLOAT:         return VK_FORMAT_R32G32_SFLOAT;
    case RHI::Format::RGB32_FLOAT:        return VK_FORMAT_R32G32B32_SFLOAT;
    case RHI::Format::RGBA32_FLOAT:       return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RHI::Format::R32_UINT:           return VK_FORMAT_R32_UINT;
    case RHI::Format::R32_SINT:           return VK_FORMAT_R32_SINT;
    case RHI::Format::D16_UNORM:          return VK_FORMAT_D16_UNORM;
    case RHI::Format::D32_FLOAT:          return VK_FORMAT_D32_SFLOAT;
    case RHI::Format::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
    case RHI::Format::D32_FLOAT_S8_UINT:  return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case RHI::Format::BC1_UNORM:          return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case RHI::Format::BC3_UNORM:          return VK_FORMAT_BC3_UNORM_BLOCK;
    case RHI::Format::BC5_UNORM:          return VK_FORMAT_BC5_UNORM_BLOCK;
    case RHI::Format::BC7_UNORM:          return VK_FORMAT_BC7_UNORM_BLOCK;
    case RHI::Format::BC7_SRGB:           return VK_FORMAT_BC7_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

static VkImageLayout ToVkImageLayout(RHI::TextureLayout l)
{
    switch (l)
    {
    case RHI::TextureLayout::Undefined:         return VK_IMAGE_LAYOUT_UNDEFINED;
    case RHI::TextureLayout::General:           return VK_IMAGE_LAYOUT_GENERAL;
    case RHI::TextureLayout::ColorAttachment:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case RHI::TextureLayout::DepthAttachment:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case RHI::TextureLayout::DepthReadOnly:     return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case RHI::TextureLayout::ShaderReadOnly:    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case RHI::TextureLayout::TransferSrc:       return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case RHI::TextureLayout::TransferDst:       return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case RHI::TextureLayout::Present:           return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

static VkFilter ToVkFilter(RHI::FilterMode m)
{
    return m == RHI::FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

static VkSamplerMipmapMode ToVkMipmapMode(RHI::MipmapMode m)
{
    return m == RHI::MipmapMode::Linear
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

static VkSamplerAddressMode ToVkAddressMode(RHI::AddressMode m)
{
    switch (m)
    {
    case RHI::AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RHI::AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RHI::AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case RHI::AddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

static VkCompareOp ToVkCompareOp(RHI::CompareOp o)
{
    switch (o)
    {
    case RHI::CompareOp::Never:        return VK_COMPARE_OP_NEVER;
    case RHI::CompareOp::Less:         return VK_COMPARE_OP_LESS;
    case RHI::CompareOp::Equal:        return VK_COMPARE_OP_EQUAL;
    case RHI::CompareOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHI::CompareOp::Greater:      return VK_COMPARE_OP_GREATER;
    case RHI::CompareOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case RHI::CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHI::CompareOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

static VkCompareOp ToVkDepthOp(RHI::DepthOp o)
{
    switch (o)
    {
    case RHI::DepthOp::Never:        return VK_COMPARE_OP_NEVER;
    case RHI::DepthOp::Less:         return VK_COMPARE_OP_LESS;
    case RHI::DepthOp::Equal:        return VK_COMPARE_OP_EQUAL;
    case RHI::DepthOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHI::DepthOp::Greater:      return VK_COMPARE_OP_GREATER;
    case RHI::DepthOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case RHI::DepthOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHI::DepthOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

static VkPrimitiveTopology ToVkTopology(RHI::Topology t)
{
    switch (t)
    {
    case RHI::Topology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case RHI::Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case RHI::Topology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case RHI::Topology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case RHI::Topology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static VkCullModeFlags ToVkCullMode(RHI::CullMode c)
{
    switch (c)
    {
    case RHI::CullMode::None:  return VK_CULL_MODE_NONE;
    case RHI::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RHI::CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

static VkFrontFace ToVkFrontFace(RHI::FrontFace f)
{
    return f == RHI::FrontFace::CounterClockwise
        ? VK_FRONT_FACE_COUNTER_CLOCKWISE
        : VK_FRONT_FACE_CLOCKWISE;
}

static VkPolygonMode ToVkFillMode(RHI::FillMode f)
{
    return f == RHI::FillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
}

static VkBlendFactor ToVkBlendFactor(RHI::BlendFactor b)
{
    switch (b)
    {
    case RHI::BlendFactor::Zero:              return VK_BLEND_FACTOR_ZERO;
    case RHI::BlendFactor::One:               return VK_BLEND_FACTOR_ONE;
    case RHI::BlendFactor::SrcColor:          return VK_BLEND_FACTOR_SRC_COLOR;
    case RHI::BlendFactor::OneMinusSrcColor:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case RHI::BlendFactor::SrcAlpha:          return VK_BLEND_FACTOR_SRC_ALPHA;
    case RHI::BlendFactor::OneMinusSrcAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case RHI::BlendFactor::DstAlpha:          return VK_BLEND_FACTOR_DST_ALPHA;
    case RHI::BlendFactor::OneMinusDstAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ZERO;
}

static VkBlendOp ToVkBlendOp(RHI::BlendOp o)
{
    switch (o)
    {
    case RHI::BlendOp::Add:             return VK_BLEND_OP_ADD;
    case RHI::BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
    case RHI::BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case RHI::BlendOp::Min:             return VK_BLEND_OP_MIN;
    case RHI::BlendOp::Max:             return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

static VkImageAspectFlags AspectFromFormat(VkFormat f)
{
    switch (f)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static VkBufferUsageFlags ToVkBufferUsage(RHI::BufferUsage u)
{
    VkBufferUsageFlags flags = 0;
    if (RHI::HasUsage(u, RHI::BufferUsage::Vertex))      flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::Index))       flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::Uniform))     flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::Storage))
    {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    if (RHI::HasUsage(u, RHI::BufferUsage::Indirect))    flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

static VkImageUsageFlags ToVkTextureUsage(RHI::TextureUsage u)
{
    VkImageUsageFlags flags = 0;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::Sampled))     flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::Storage))     flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::ColorTarget)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::DepthTarget)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

static VkAccessFlags2 ToVkAccess(RHI::MemoryAccess a)
{
    VkAccessFlags2 flags = 0;
    const auto u = static_cast<uint8_t>(a);
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndirectRead))  flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndexRead))     flags |= VK_ACCESS_2_INDEX_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::ShaderRead))    flags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::ShaderWrite))   flags |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::TransferRead))  flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::TransferWrite)) flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::HostRead))      flags |= VK_ACCESS_2_HOST_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::HostWrite))     flags |= VK_ACCESS_2_HOST_WRITE_BIT;
    return flags;
}

static VkPipelineStageFlags2 ToVkStage(RHI::MemoryAccess a)
{
    const auto u = static_cast<uint8_t>(a);
    if (u == 0) return VK_PIPELINE_STAGE_2_NONE;
    VkPipelineStageFlags2 flags = 0;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndirectRead))  flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndexRead))     flags |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    if (u & (static_cast<uint8_t>(RHI::MemoryAccess::ShaderRead) |
             static_cast<uint8_t>(RHI::MemoryAccess::ShaderWrite)))
        flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
               | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
               | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    if (u & (static_cast<uint8_t>(RHI::MemoryAccess::TransferRead) |
             static_cast<uint8_t>(RHI::MemoryAccess::TransferWrite)))
        flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    if (u & (static_cast<uint8_t>(RHI::MemoryAccess::HostRead) |
             static_cast<uint8_t>(RHI::MemoryAccess::HostWrite)))
        flags |= VK_PIPELINE_STAGE_2_HOST_BIT;
    return flags;
}

static VkPresentModeKHR ToVkPresentMode(RHI::PresentMode m,
                                         const std::vector<VkPresentModeKHR>& available)
{
    auto has = [&](VkPresentModeKHR mode) {
        return std::find(available.begin(), available.end(), mode) != available.end();
    };
    switch (m)
    {
    case RHI::PresentMode::LowLatency:
        if (has(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
        break;
    case RHI::PresentMode::Uncapped:
        if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
        break;
    case RHI::PresentMode::Throttled:
        if (has(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        break;
    default: break;
    }
    return VK_PRESENT_MODE_FIFO_KHR; // always supported
}


// =============================================================================
// §4  StagingBelt
// =============================================================================

StagingBelt::StagingBelt(VkDevice device, VmaAllocator vma, size_t capacityBytes)
    : m_Device(device), m_Vma(vma), m_Capacity(capacityBytes)
{
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size  = capacityBytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
              | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VmaAllocationInfo info{};
    VK_CHECK_FATAL(vmaCreateBuffer(m_Vma, &bci, &aci, &m_Buffer, &m_Alloc, &info));
    m_Mapped = info.pMappedData;
    assert(m_Mapped);
}

StagingBelt::~StagingBelt()
{
    if (m_Buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_Vma, m_Buffer, m_Alloc);
}

StagingBelt::Allocation StagingBelt::Allocate(size_t sizeBytes, size_t alignment)
{
    std::scoped_lock lock{m_Mutex};
    const size_t aligned = AlignUp(m_Tail, alignment ? alignment : 1);
    const size_t end     = aligned + sizeBytes;

    if (end > m_Capacity)
    {
        // Wrap: try from zero — only valid if head hasn't wrapped here yet.
        const size_t wEnd = sizeBytes;
        if (wEnd > m_Head)
        {
            fprintf(stderr, "[StagingBelt] Out of staging memory!\n");
            return {};
        }
        m_Tail = wEnd;
        void* ptr = static_cast<char*>(m_Mapped);
        if (!m_HasPending) { m_PendingBegin = 0; m_HasPending = true; }
        m_PendingEnd = wEnd;
        return {m_Buffer, 0, ptr, sizeBytes};
    }

    if (aligned < m_Head && end > m_Head)
    {
        fprintf(stderr, "[StagingBelt] Staging belt head collision!\n");
        return {};
    }

    m_Tail = end;
    void* ptr = static_cast<char*>(m_Mapped) + aligned;
    if (!m_HasPending) { m_PendingBegin = aligned; m_HasPending = true; }
    m_PendingEnd = end;
    return {m_Buffer, aligned, ptr, sizeBytes};
}

void StagingBelt::Retire(uint64_t retireValue)
{
    std::scoped_lock lock{m_Mutex};
    if (m_HasPending)
    {
        m_InFlight.push_back({m_PendingBegin, m_PendingEnd, retireValue});
        m_HasPending = false;
    }
}

void StagingBelt::GarbageCollect(uint64_t completedValue)
{
    std::scoped_lock lock{m_Mutex};
    while (!m_InFlight.empty() && m_InFlight.front().RetireValue <= completedValue)
    {
        m_Head = m_InFlight.front().End;
        m_InFlight.pop_front();
    }
    if (m_InFlight.empty()) { m_Head = 0; m_Tail = 0; }
}

// =============================================================================
// §5  VulkanProfiler
// =============================================================================

VulkanProfiler::VulkanProfiler(VkDevice device, VkPhysicalDevice physDevice,
                                 uint32_t framesInFlight)
    : m_Device(device), m_FramesInFlight(framesInFlight)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physDevice, &props);
    m_PeriodNs  = props.limits.timestampPeriod;
    m_Supported = props.limits.timestampComputeAndGraphics != 0;
    if (!m_Supported) return;

    // Two queries per scope (begin + end) + 2 for the whole frame.
    const uint32_t queriesPerFrame = 2 + 2 * kMaxTimestampScopes;
    m_TotalQueries = framesInFlight * queriesPerFrame;

    VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    ci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = m_TotalQueries;
    VK_CHECK_FATAL(vkCreateQueryPool(m_Device, &ci, nullptr, &m_Pool));

    m_Frames.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i)
        m_Frames[i].QueryBase = i * queriesPerFrame;
}

VulkanProfiler::~VulkanProfiler()
{
    if (m_Pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(m_Device, m_Pool, nullptr);
}

void VulkanProfiler::ResetFrame(uint32_t frameIndex, VkCommandBuffer cmd)
{
    if (!m_Supported) return;
    const uint32_t slot      = frameIndex % m_FramesInFlight;
    const uint32_t base      = m_Frames[slot].QueryBase;
    const uint32_t perFrame  = 2 + 2 * kMaxTimestampScopes;
    vkCmdResetQueryPool(cmd, m_Pool, base, perFrame);
    m_Frames[slot].QueryCount = 0;
    m_Frames[slot].Scopes.clear();
    m_Frames[slot].FrameBegin = base + m_Frames[slot].QueryCount++;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                         m_Pool, m_Frames[slot].FrameBegin);
}

void VulkanProfiler::BeginFrame(uint32_t frameIndex, uint32_t /*maxScopesHint*/)
{
    std::scoped_lock lock{m_Mutex};
    const uint32_t slot = frameIndex % m_FramesInFlight;
    m_Frames[slot].FrameIndex = frameIndex;
}

void VulkanProfiler::EndFrame()
{
    if (!m_Supported || !m_Cmd) return;
    // Each frame slot — write frame-end timestamp via current cmd buffer.
    // ResetFrame already wrote frame-begin; here we close the bracket.
    // (The frame slot is determined by the current m_Cmd context.)
}

uint32_t VulkanProfiler::BeginScope(std::string_view name)
{
    if (!m_Supported || !m_Cmd) return 0;
    std::scoped_lock lock{m_Mutex};
    // Find active frame slot by scanning (small linear search, kMaxFramesInFlight ≤ 3).
    for (uint32_t s = 0; s < m_FramesInFlight; ++s)
    {
        auto& fr = m_Frames[s];
        if (fr.QueryCount + 2 >= 2 + 2 * kMaxTimestampScopes) continue;
        const uint32_t qBegin = fr.QueryBase + fr.QueryCount++;
        const uint32_t qEnd   = fr.QueryBase + fr.QueryCount++;
        vkCmdWriteTimestamp2(m_Cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_Pool, qBegin);
        fr.Scopes.push_back({std::string(name), qBegin, qEnd});
        return static_cast<uint32_t>(fr.Scopes.size() - 1);
    }
    return 0;
}

void VulkanProfiler::EndScope(uint32_t scopeHandle)
{
    if (!m_Supported || !m_Cmd) return;
    std::scoped_lock lock{m_Mutex};
    for (uint32_t s = 0; s < m_FramesInFlight; ++s)
    {
        auto& fr = m_Frames[s];
        if (scopeHandle < fr.Scopes.size())
        {
            vkCmdWriteTimestamp2(m_Cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                 m_Pool, fr.Scopes[scopeHandle].EndQuery);
            return;
        }
    }
}

std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
VulkanProfiler::Resolve(uint32_t frameIndex) const
{
    if (!m_Supported)
        return std::unexpected(RHI::ProfilerError::NotReady);

    std::scoped_lock lock{m_Mutex};
    const uint32_t slot      = frameIndex % m_FramesInFlight;
    const auto&    fr        = m_Frames[slot];
    if (fr.FrameIndex != frameIndex)
        return std::unexpected(RHI::ProfilerError::NotReady);

    // Read all queries for this frame.
    const uint32_t perFrame = 2 + 2 * kMaxTimestampScopes;
    std::vector<uint64_t> results(perFrame, 0);
    const VkResult res = vkGetQueryPoolResults(
        m_Device, m_Pool, fr.QueryBase, fr.QueryCount,
        fr.QueryCount * sizeof(uint64_t), results.data(),
        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (res == VK_NOT_READY)
        return std::unexpected(RHI::ProfilerError::NotReady);
    if (res != VK_SUCCESS)
        return std::unexpected(RHI::ProfilerError::DeviceLost);

    RHI::GpuTimestampFrame out{};
    out.FrameNumber = frameIndex;
    if (fr.FrameBegin < fr.QueryCount && fr.FrameEnd < fr.QueryCount)
    {
        const uint64_t ticks = results[fr.FrameEnd - fr.QueryBase]
                             - results[fr.FrameBegin - fr.QueryBase];
        out.GpuFrameTimeNs = static_cast<uint64_t>(ticks * m_PeriodNs);
    }
    for (const auto& sc : fr.Scopes)
    {
        const uint32_t bi = sc.BeginQuery - fr.QueryBase;
        const uint32_t ei = sc.EndQuery   - fr.QueryBase;
        if (bi < fr.QueryCount && ei < fr.QueryCount)
        {
            const uint64_t ticks = results[ei] - results[bi];
            out.Scopes.push_back({sc.Name, static_cast<uint64_t>(ticks * m_PeriodNs)});
        }
    }
    return out;
}

// =============================================================================
// §6  VulkanBindlessHeap
// =============================================================================

VulkanBindlessHeap::VulkanBindlessHeap(VkDevice device, uint32_t capacity)
    : m_Device(device), m_Capacity(capacity)
{
    // Descriptor pool — combined image sampler, partially bound.
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, capacity};
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    poolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    VK_CHECK_FATAL(vkCreateDescriptorPool(m_Device, &poolCI, nullptr, &m_Pool));

    // Binding flags — partially bound + update-after-bind.
    const VkDescriptorBindingFlags bindFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindFlagsCI{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bindFlagsCI.bindingCount  = 1;
    bindFlagsCI.pBindingFlags = &bindFlags;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = capacity;
    binding.stageFlags      = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCI.pNext        = &bindFlagsCI;
    layoutCI.bindingCount = 1;
    layoutCI.pBindings    = &binding;
    layoutCI.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    VK_CHECK_FATAL(vkCreateDescriptorSetLayout(m_Device, &layoutCI, nullptr, &m_Layout));

    VkDescriptorSetAllocateInfo allocCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocCI.descriptorPool     = m_Pool;
    allocCI.descriptorSetCount = 1;
    allocCI.pSetLayouts        = &m_Layout;
    VK_CHECK_FATAL(vkAllocateDescriptorSets(m_Device, &allocCI, &m_Set));
}

VulkanBindlessHeap::~VulkanBindlessHeap()
{
    if (m_Layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
    if (m_Pool   != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
}

void VulkanBindlessHeap::SetDefault(VkImageView view, VkSampler sampler)
{
    // Slot 0 — written immediately, not via pending queue.
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = view;
    imgInfo.sampler     = sampler;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = m_Set;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
}

RHI::BindlessIndex VulkanBindlessHeap::AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle)
{
    std::scoped_lock lock{m_Mutex};
    RHI::BindlessIndex slot = RHI::kInvalidBindlessIndex;
    if (!m_FreeSlots.empty())
    {
        slot = m_FreeSlots.back();
        m_FreeSlots.pop_back();
    }
    else
    {
        if (m_NextSlot >= m_Capacity) return RHI::kInvalidBindlessIndex;
        slot = m_NextSlot++;
    }
    m_Pending.push_back({OpType::Allocate, slot});
    return slot;
}

void VulkanBindlessHeap::UpdateTextureSlot(RHI::BindlessIndex slot,
                                            RHI::TextureHandle,
                                            RHI::SamplerHandle)
{
    // Raw Vk handles are injected via EnqueueRawUpdate called by VulkanDevice.
    (void)slot;
}

void VulkanBindlessHeap::FreeSlot(RHI::BindlessIndex slot)
{
    std::scoped_lock lock{m_Mutex};
    if (slot == RHI::kInvalidBindlessIndex || slot >= m_NextSlot) return;
    m_Pending.push_back({OpType::Free, slot});
}

void VulkanBindlessHeap::EnqueueRawUpdate(RHI::BindlessIndex slot,
                                           VkImageView view, VkSampler sampler,
                                           VkImageLayout layout)
{
    std::scoped_lock lock{m_Mutex};
    m_Pending.push_back({OpType::UpdateRaw, slot, view, sampler, layout});
}

void VulkanBindlessHeap::FlushPending()
{
    std::scoped_lock lock{m_Mutex};
    std::vector<VkWriteDescriptorSet>  writes;
    std::vector<VkDescriptorImageInfo> imgInfos;
    imgInfos.reserve(m_Pending.size());

    for (const auto& op : m_Pending)
    {
        if (op.Type == OpType::Allocate)
        {
            ++m_AllocCount;
        }
        else if (op.Type == OpType::Free)
        {
            m_FreeSlots.push_back(op.Slot);
            if (m_AllocCount > 0) --m_AllocCount;
        }
        else // UpdateRaw
        {
            auto& info = imgInfos.emplace_back();
            info.imageView   = op.View;
            info.sampler     = op.Sampler;
            info.imageLayout = op.Layout;
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet          = m_Set;
            w.dstBinding      = 0;
            w.dstArrayElement = op.Slot;
            w.descriptorCount = 1;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo      = &imgInfos.back();
            writes.push_back(w);
        }
    }
    m_Pending.clear();
    if (!writes.empty())
        vkUpdateDescriptorSets(m_Device,
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
}

uint32_t VulkanBindlessHeap::GetAllocatedSlotCount() const
{
    std::scoped_lock lock{m_Mutex};
    return m_AllocCount;
}

// =============================================================================
// §7  VulkanTransferQueue
// =============================================================================

VulkanTransferQueue::VulkanTransferQueue(const Config& cfg)
    : m_Device(cfg.Device), m_Vma(cfg.Vma), m_Queue(cfg.Queue),
      m_QueueFamily(cfg.QueueFamily)
{
    // Timeline semaphore.
    VkSemaphoreTypeCreateInfo typeCI{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeCI.initialValue  = 0;
    VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semCI.pNext = &typeCI;
    VK_CHECK_FATAL(vkCreateSemaphore(m_Device, &semCI, nullptr, &m_Timeline));

    // Command pool for transfer commands.
    VkCommandPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCI.queueFamilyIndex = m_QueueFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                            | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK_FATAL(vkCreateCommandPool(m_Device, &poolCI, nullptr, &m_CmdPool));

    m_Belt = std::make_unique<StagingBelt>(m_Device, m_Vma, cfg.StagingCapacity);
}

VulkanTransferQueue::~VulkanTransferQueue()
{
    if (m_CmdPool   != VK_NULL_HANDLE) vkDestroyCommandPool(m_Device, m_CmdPool, nullptr);
    if (m_Timeline  != VK_NULL_HANDLE) vkDestroySemaphore(m_Device, m_Timeline, nullptr);
}

uint64_t VulkanTransferQueue::QueryCompletedValue() const
{
    uint64_t val = 0;
    VK_CHECK_WARN(vkGetSemaphoreCounterValue(m_Device, m_Timeline, &val));
    return val;
}

VkCommandBuffer VulkanTransferQueue::Begin()
{
    VkCommandBufferAllocateInfo allocCI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocCI.commandPool        = m_CmdPool;
    allocCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCI.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK_FATAL(vkAllocateCommandBuffers(m_Device, &allocCI, &cmd));
    VkCommandBufferBeginInfo beginCI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_FATAL(vkBeginCommandBuffer(cmd, &beginCI));
    return cmd;
}

RHI::TransferToken VulkanTransferQueue::Submit(VkCommandBuffer cmd)
{
    VK_CHECK_FATAL(vkEndCommandBuffer(cmd));
    const uint64_t ticket = m_NextTicket.fetch_add(1, std::memory_order_relaxed);

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;
    VkSemaphoreSubmitInfo sigInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    sigInfo.semaphore = m_Timeline;
    sigInfo.value     = ticket;
    sigInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos    = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos  = &sigInfo;

    {
        std::scoped_lock lock{m_Mutex};
        VK_CHECK_FATAL(vkQueueSubmit2(m_Queue, 1, &submit, VK_NULL_HANDLE));
        m_Belt->Retire(ticket);
    }

    vkFreeCommandBuffers(m_Device, m_CmdPool, 1, &cmd);
    return RHI::TransferToken{ticket};
}

RHI::TransferToken VulkanTransferQueue::UploadBuffer(RHI::BufferHandle dst,
                                                      const void* data,
                                                      uint64_t size,
                                                      uint64_t offset)
{
    if (!m_Buffers) return {};
    auto* buf = m_Buffers->GetIfValid(dst);
    if (!buf) return {};

    auto staging = m_Belt->Allocate(static_cast<size_t>(size), 4);
    if (!staging.MappedPtr) return {};
    std::memcpy(staging.MappedPtr, data, size);

    VkCommandBuffer cmd = Begin();
    VkBufferCopy region{staging.Offset, offset, size};
    vkCmdCopyBuffer(cmd, staging.Buffer, buf->Buffer, 1, &region);
    return Submit(cmd);
}

RHI::TransferToken VulkanTransferQueue::UploadBuffer(RHI::BufferHandle dst,
                                                      std::span<const std::byte> src,
                                                      uint64_t offset)
{
    return UploadBuffer(dst, src.data(), static_cast<uint64_t>(src.size_bytes()), offset);
}

RHI::TransferToken VulkanTransferQueue::UploadTexture(RHI::TextureHandle dst,
                                                       const void* data,
                                                       uint64_t dataSizeBytes,
                                                       uint32_t mipLevel,
                                                       uint32_t arrayLayer)
{
    if (!m_Images) return {};
    auto* img = m_Images->GetIfValid(dst);
    if (!img) return {};

    auto staging = m_Belt->Allocate(static_cast<size_t>(dataSizeBytes), 4);
    if (!staging.MappedPtr) return {};
    std::memcpy(staging.MappedPtr, data, dataSizeBytes);

    VkCommandBuffer cmd = Begin();

    // Transition: Undefined → TransferDst
    VkImageMemoryBarrier2 toXfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toXfer.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
    toXfer.srcAccessMask       = 0;
    toXfer.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toXfer.dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toXfer.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toXfer.image               = img->Image;
    toXfer.subresourceRange    = {AspectFromFormat(img->Format), mipLevel, 1, arrayLayer, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &toXfer;
    vkCmdPipelineBarrier2(cmd, &dep);

    const uint32_t mipW = std::max(1u, img->Width  >> mipLevel);
    const uint32_t mipH = std::max(1u, img->Height >> mipLevel);
    VkBufferImageCopy region{};
    region.bufferOffset      = staging.Offset;
    region.imageSubresource  = {AspectFromFormat(img->Format), mipLevel, arrayLayer, 1};
    region.imageExtent       = {mipW, mipH, 1};
    vkCmdCopyBufferToImage(cmd, staging.Buffer, img->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TransferDst → ShaderReadOnly
    VkImageMemoryBarrier2 toRead = toXfer;
    toRead.srcStageMask   = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toRead.srcAccessMask  = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toRead.dstStageMask   = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toRead.dstAccessMask  = VK_ACCESS_2_SHADER_READ_BIT;
    toRead.oldLayout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dep.pImageMemoryBarriers = &toRead;
    vkCmdPipelineBarrier2(cmd, &dep);

    return Submit(cmd);
}

bool VulkanTransferQueue::IsComplete(RHI::TransferToken token) const
{
    return token.Value <= QueryCompletedValue();
}

void VulkanTransferQueue::CollectCompleted()
{
    const uint64_t done = QueryCompletedValue();
    m_Belt->GarbageCollect(done);
}

// =============================================================================
// §8  VulkanCommandContext
// =============================================================================

void VulkanCommandContext::Bind(VkDevice device, VkCommandBuffer cmd,
                                 VkPipelineLayout globalLayout,
                                 VkDescriptorSet  bindlessSet,
                                 const Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight>* buffers,
                                 const Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight>* images,
                                 const Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight>* pipelines)
{
    m_Device        = device;
    m_Cmd           = cmd;
    m_GlobalLayout  = globalLayout;
    m_BindlessSet   = bindlessSet;
    m_Buffers       = buffers;
    m_Images        = images;
    m_Pipelines     = pipelines;
    m_BindPoint     = VK_PIPELINE_BIND_POINT_GRAPHICS;
}

void VulkanCommandContext::Begin()
{
    VkCommandBufferBeginInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_FATAL(vkBeginCommandBuffer(m_Cmd, &ci));
}

void VulkanCommandContext::End()
{
    VK_CHECK_FATAL(vkEndCommandBuffer(m_Cmd));
}

void VulkanCommandContext::BeginRenderPass(const RHI::RenderPassDesc& desc)
{
    // Build color attachment infos.
    std::vector<VkRenderingAttachmentInfo> colorInfos;
    colorInfos.reserve(desc.ColorTargets.size());

    for (const auto& ca : desc.ColorTargets)
    {
        VkRenderingAttachmentInfo info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (ca.Target.IsValid())
        {
            const auto* img = m_Images->GetIfValid(ca.Target);
            info.imageView   = img ? img->View : VK_NULL_HANDLE;
        }
        // Invalid handle = backbuffer; VulkanDevice::BeginFrame sets its view
        // into a special slot that TextureBarrier recognises.  When using dynamic
        // rendering the swapchain image view is looked up the same way.
        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.loadOp      = ca.Load  == RHI::LoadOp::Clear    ? VK_ATTACHMENT_LOAD_OP_CLEAR
                         : ca.Load  == RHI::LoadOp::Load     ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                              : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        info.storeOp     = ca.Store == RHI::StoreOp::Store   ? VK_ATTACHMENT_STORE_OP_STORE
                                                              : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        info.clearValue.color = {ca.ClearR, ca.ClearG, ca.ClearB, ca.ClearA};
        colorInfos.push_back(info);
    }

    // Depth attachment.
    VkRenderingAttachmentInfo depthInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bool hasDepth = false;
    if (desc.Depth.Target.IsValid())
    {
        const auto* img = m_Images->GetIfValid(desc.Depth.Target);
        if (img)
        {
            depthInfo.imageView   = img->View;
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthInfo.loadOp      = desc.Depth.Load  == RHI::LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                  : desc.Depth.Load  == RHI::LoadOp::Load  ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                                            : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthInfo.storeOp     = desc.Depth.Store == RHI::StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                                                             : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthInfo.clearValue.depthStencil = {desc.Depth.ClearDepth, desc.Depth.ClearStencil};
            hasDepth = true;
        }
    }

    // Determine render area from the first color target (or depth).
    VkExtent2D extent{1, 1};
    if (!desc.ColorTargets.empty() && desc.ColorTargets[0].Target.IsValid())
    {
        const auto* img = m_Images->GetIfValid(desc.ColorTargets[0].Target);
        if (img) extent = {img->Width, img->Height};
    }

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea           = {{0, 0}, extent};
    ri.layerCount           = 1;
    ri.colorAttachmentCount = static_cast<uint32_t>(colorInfos.size());
    ri.pColorAttachments    = colorInfos.data();
    ri.pDepthAttachment     = hasDepth ? &depthInfo : nullptr;
    vkCmdBeginRendering(m_Cmd, &ri);
}

void VulkanCommandContext::EndRenderPass()
{
    vkCmdEndRendering(m_Cmd);
}

void VulkanCommandContext::SetViewport(float x, float y, float w, float h,
                                        float minD, float maxD)
{
    // Flip Y for Vulkan NDC (origin top-left, Y down).
    VkViewport vp{x, y + h, w, -h, minD, maxD};
    vkCmdSetViewport(m_Cmd, 0, 1, &vp);
}

void VulkanCommandContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    VkRect2D sc{{x, y}, {w, h}};
    vkCmdSetScissor(m_Cmd, 0, 1, &sc);
}

void VulkanCommandContext::BindPipeline(RHI::PipelineHandle handle)
{
    const auto* pip = m_Pipelines->GetIfValid(handle);
    if (!pip) return;
    m_BindPoint = pip->BindPoint;
    vkCmdBindPipeline(m_Cmd, m_BindPoint, pip->Pipeline);
    // Always bind the global bindless descriptor set at set 0.
    vkCmdBindDescriptorSets(m_Cmd, m_BindPoint, m_GlobalLayout,
                            0, 1, &m_BindlessSet, 0, nullptr);
}

void VulkanCommandContext::PushConstants(const void* data, uint32_t size, uint32_t offset)
{
    vkCmdPushConstants(m_Cmd, m_GlobalLayout,
                       VK_SHADER_STAGE_ALL, offset, size, data);
}

void VulkanCommandContext::Draw(uint32_t vertexCount, uint32_t instanceCount,
                                 uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(m_Cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                        uint32_t firstIndex, int32_t vertexOffset,
                                        uint32_t firstInstance)
{
    vkCmdDrawIndexed(m_Cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandContext::DrawIndirect(RHI::BufferHandle argBuf,
                                         uint64_t offset, uint32_t drawCount)
{
    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf) return;
    vkCmdDrawIndirect(m_Cmd, buf->Buffer, offset, drawCount,
                      sizeof(VkDrawIndirectCommand));
}

void VulkanCommandContext::DrawIndexedIndirect(RHI::BufferHandle argBuf,
                                                uint64_t offset, uint32_t drawCount)
{
    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf) return;
    vkCmdDrawIndexedIndirect(m_Cmd, buf->Buffer, offset, drawCount,
                             sizeof(VkDrawIndexedIndirectCommand));
}

void VulkanCommandContext::DrawIndexedIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                                                     RHI::BufferHandle cntBuf, uint64_t cntOffset,
                                                     uint32_t maxDraw)
{
    const auto* abuf = m_Buffers->GetIfValid(argBuf);
    const auto* cbuf = m_Buffers->GetIfValid(cntBuf);
    if (!abuf || !cbuf) return;
    vkCmdDrawIndexedIndirectCount(m_Cmd, abuf->Buffer, argOffset,
                                  cbuf->Buffer, cntOffset, maxDraw,
                                  sizeof(VkDrawIndexedIndirectCommand));
}

void VulkanCommandContext::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz)
{
    vkCmdDispatch(m_Cmd, gx, gy, gz);
}

void VulkanCommandContext::DispatchIndirect(RHI::BufferHandle argBuf, uint64_t offset)
{
    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf) return;
    vkCmdDispatchIndirect(m_Cmd, buf->Buffer, offset);
}

void VulkanCommandContext::TextureBarrier(RHI::TextureHandle tex,
                                           RHI::TextureLayout before,
                                           RHI::TextureLayout after)
{
    const auto* img = m_Images->GetIfValid(tex);
    if (!img) return;
    const VkImageLayout oldLayout = ToVkImageLayout(before);
    const VkImageLayout newLayout = ToVkImageLayout(after);

    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout     = oldLayout;
    barrier.newLayout     = newLayout;
    barrier.image         = img->Image;
    barrier.subresourceRange = {AspectFromFormat(img->Format), 0, img->MipLevels, 0, img->ArrayLayers};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(m_Cmd, &dep);
}

void VulkanCommandContext::BufferBarrier(RHI::BufferHandle buf,
                                          RHI::MemoryAccess before,
                                          RHI::MemoryAccess after)
{
    const auto* b = m_Buffers->GetIfValid(buf);
    if (!b) return;

    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask  = ToVkStage(before);
    barrier.srcAccessMask = ToVkAccess(before);
    barrier.dstStageMask  = ToVkStage(after);
    barrier.dstAccessMask = ToVkAccess(after);
    barrier.buffer        = b->Buffer;
    barrier.offset        = 0;
    barrier.size          = VK_WHOLE_SIZE;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(m_Cmd, &dep);
}

void VulkanCommandContext::CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                                       uint64_t srcOff, uint64_t dstOff, uint64_t size)
{
    const auto* s = m_Buffers->GetIfValid(src);
    const auto* d = m_Buffers->GetIfValid(dst);
    if (!s || !d) return;
    VkBufferCopy region{srcOff, dstOff, size};
    vkCmdCopyBuffer(m_Cmd, s->Buffer, d->Buffer, 1, &region);
}

void VulkanCommandContext::CopyBufferToTexture(RHI::BufferHandle src, uint64_t srcOff,
                                                RHI::TextureHandle dst,
                                                uint32_t mipLevel, uint32_t arrayLayer)
{
    const auto* s = m_Buffers->GetIfValid(src);
    const auto* d = m_Images->GetIfValid(dst);
    if (!s || !d) return;
    VkBufferImageCopy region{};
    region.bufferOffset     = srcOff;
    region.imageSubresource = {AspectFromFormat(d->Format), mipLevel, arrayLayer, 1};
    region.imageExtent      = {std::max(1u, d->Width >> mipLevel),
                                std::max(1u, d->Height >> mipLevel), 1};
    vkCmdCopyBufferToImage(m_Cmd, s->Buffer, d->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

// §9  VulkanDevice .......................................................
// TODO: implement Initialize, Shutdown, WaitIdle,
//       BeginFrame, EndFrame, Present, Resize,
//       CreateBuffer, DestroyBuffer, WriteBuffer, GetBufferDeviceAddress,
//       CreateTexture, DestroyTexture, WriteTexture,
//       CreateSampler, DestroySampler,
//       CreatePipeline, DestroyPipeline,
//       and all private helpers

} // namespace Extrinsic::Backends::Vulkan

