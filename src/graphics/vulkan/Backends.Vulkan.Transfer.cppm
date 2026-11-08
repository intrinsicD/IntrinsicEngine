// Vulkan staging, timeline submission and readback ownership behind the RHI transfer queue.
module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

#include "Vulkan.hpp"
#include "Backends.Vulkan.StagingRing.hpp"

export module Extrinsic.Backends.Vulkan:Transfer;

export import Extrinsic.Core.ResourcePool;
export import Extrinsic.Core.Telemetry;
export import Extrinsic.RHI.Descriptors;
export import Extrinsic.RHI.Handles;
export import Extrinsic.RHI.QueueAffinity;
export import Extrinsic.RHI.Transfer;
export import Extrinsic.RHI.TransferQueue;
export import :Memory;
export import :Pipelines;
export import :Sync;
export import :TransferLanes;

namespace Extrinsic::Backends::Vulkan
{
    export constexpr uint64_t kStagingBeltCapacity = 64ull * 1024 * 1024;

    export class VulkanDevice;

    export class StagingBelt
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
        void CancelPending();
        void GarbageCollect(uint64_t completedValue);

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] size_t Capacity() const noexcept { return m_Capacity; }

    private:
        VkDevice      m_Device  = VK_NULL_HANDLE;
        VmaAllocator  m_Vma     = VK_NULL_HANDLE;
        VkBuffer      m_Buffer  = VK_NULL_HANDLE;
        VmaAllocation m_Alloc   = VK_NULL_HANDLE;
        void*         m_Mapped  = nullptr;
        size_t        m_Capacity;
        Detail::StagingRing m_Ring;
        std::mutex    m_Mutex;
    };

    export class VulkanTransferQueue final : public RHI::ITransferQueue
    {
    public:
        struct Config
        {
            VkDevice     Device      = VK_NULL_HANDLE;
            VmaAllocator Vma         = VK_NULL_HANDLE;
            VkQueue      Queue       = VK_NULL_HANDLE;
            uint32_t     QueueFamily = 0;
            // The method queue, or null; preferably distinct from frame compute.
            VkQueue ComputeQueue = VK_NULL_HANDLE;
            uint32_t ComputeQueueFamily = 0;
            VkQueue FrameComputeQueue = VK_NULL_HANDLE;
            bool ForceGraphicsCompute = false;
            size_t       StagingCapacity = kStagingBeltCapacity;
            // Optional external-synchronization mutex of the frame queues (graphics, present, frame compute).
            std::mutex*  QueueSubmitMutex = nullptr;
            // The method queue's own mutex when it is a distinct VkQueue; QueueSubmitMutex (or null) when it is one
            // of the frame queues, so the same VkQueue always shares one lock.
            std::mutex* ComputeQueueSubmitMutex = nullptr;
        };

        explicit VulkanTransferQueue(const Config& cfg);
        ~VulkanTransferQueue() override;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] uint64_t QueryCompletedValue() const;

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
        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle dst,
                                                                std::span<const std::byte> src) override;
        [[nodiscard]] RHI::ReadbackToken DownloadBuffer(RHI::BufferHandle src,
                                                        uint64_t size,
                                                        uint64_t offset,
                                                        RHI::ReadbackSink sink) override;
        [[nodiscard]] RHI::ReadbackToken DownloadTexture(RHI::TextureHandle src,
                                                         RHI::TextureLayout srcLayout,
                                                         uint32_t mipLevel,
                                                         uint32_t arrayLayer,
                                                         RHI::ReadbackSink sink) override;
        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override;
        [[nodiscard]] bool IsComplete(RHI::ReadbackToken token) const override;
        void CollectCompleted() override;
        [[nodiscard]] RHI::TransferQueueDiagnostics GetDiagnostics() const noexcept override;
        [[nodiscard]] RHI::ReadbackToken ComputeFence() const noexcept override;
        // vkWaitSemaphores on the token's lane timeline; a lost device answers Failed (and stays lost).
        [[nodiscard]] RHI::ComputeWaitStatus WaitCompute(
            RHI::ReadbackToken token, uint64_t timeoutNanoseconds) override;
        [[nodiscard]] bool TakeCompletedReadback(RHI::ReadbackToken token, std::span<std::byte> destination) override;
        void ReleaseReadback(RHI::ReadbackToken token) override;

        // The lane SubmitComputeReadback records and submits on.
        [[nodiscard]] TransferLane ComputeLane() const noexcept;
        // Worker-safe liveness of method compute: a valid service whose device was not lost. Lost once any thread
        // observed VK_ERROR_DEVICE_LOST here, or the device reported it (MarkLost, on its own thread).
        [[nodiscard]] bool ComputeLive() const noexcept;
        [[nodiscard]] bool Lost() const noexcept
        {
            return m_Lost.load(std::memory_order_acquire);
        }
        void MarkLost() noexcept;
        // A device one-shot graphics submission (scene upload) that also signals the graphics lane at a fresh
        // ticket, so a later compute submission's wait on the last issued ticket orders after it. `submit` must
        // carry no signal of its own. The submission's VkResult.
        [[nodiscard]] VkResult SubmitGraphicsTicketed(const VkSubmitInfo2& submit, VkFence fence);
        // Compute -> graphics visibility: a wait on the compute timeline at the value CollectCompleted last observed
        // complete (published results only, never pending method work; already signaled, so it never stalls). Null
        // semaphore when nothing completed on a separate compute lane. Every graphics-queue submission carries it.
        [[nodiscard]] VkSemaphoreSubmitInfo PublishedComputeWait() const noexcept;
        void NoteComputeVisibilityWait() noexcept;
        friend class VulkanDevice;

    private:
        struct RetiredCommandBuffer
        {
            VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
            uint64_t RetireValue = 0;
        };
        struct Lane
        {
            VkQueue Queue = VK_NULL_HANDLE;
            uint32_t Family = 0;
            VkSemaphore Timeline = VK_NULL_HANDLE;
            VkCommandPool Pool = VK_NULL_HANDLE;
            // The VkQueue's host-synchronization lock (null: none); shared by every lane on the same VkQueue.
            std::mutex* QueueMutex = nullptr;
            // Held from allocation through recording and submission of a lane command buffer, and by retirement:
            // the pool is externally synchronized, and tickets are drawn and pushed to InFlight in its order. Lock
            // order: Record -> QueueMutex, and Record -> m_Mutex; m_Mutex is never held while taking a queue lock.
            std::mutex Record{};
            // Under Record: the compute-lane command buffer being recorded skips the graphics-ticket wait
            // (RHI::ComputeInputOrder::LaneOrderOnly); set by SubmitComputeReadback, consumed by its submission.
            bool LaneOrderOnly = false;
            std::atomic<uint64_t> NextTicket{1};
            // The highest ticket actually submitted (a refused submission hands its ticket back first).
            std::atomic<uint64_t> SubmittedTicket{0};
            std::deque<RetiredCommandBuffer> InFlight{}; // guarded by m_Mutex
            // IsComplete's bound for readback tokens: every pending entry of the lane at or below it was delivered,
            // taken or released, and the device finished it. Set by CollectCompleted after its sinks ran; never
            // passes an undelivered or untaken earlier entry.
            std::atomic<uint64_t> CompletedReadbackTicket{0};
        };
        [[nodiscard]] Lane& LaneFor(TransferLane lane) noexcept
        {
            return m_Lanes[static_cast<size_t>(lane)];
        }
        [[nodiscard]] const Lane& LaneFor(TransferLane lane) const noexcept
        {
            return m_Lanes[static_cast<size_t>(lane)];
        }
        [[nodiscard]] uint64_t QueryLaneCompletedValue(TransferLane lane) const;
        // A primary command buffer from the lane's pool, not begun. The caller holds the lane's Record mutex
        // until the buffer is submitted or freed (FreeCommandBuffer too).
        [[nodiscard]] VkCommandBuffer Allocate(TransferLane lane = TransferLane::Graphics);
        void FreeCommandBuffer(TransferLane lane, VkCommandBuffer cmd);
        [[nodiscard]] std::mutex& RecordMutex(TransferLane lane) noexcept
        {
            return LaneFor(lane).Record;
        }
        // SubmitComputeReadback's recorder runs under its lane's Record mutex (a non-recursive lock): while it runs,
        // this thread's nested SubmitComputeReadback on that lane, and on the graphics lane its uploads, downloads,
        // ticketed submissions and retirement, are refused (logged) instead of self-deadlocking. Recorders only
        // record commands (the RHI contract). Other threads and the other lane's lock are unaffected.
        class RecorderScope
        {
        public:
            RecorderScope(const VulkanTransferQueue& recording, TransferLane lane) noexcept;
            ~RecorderScope();
            RecorderScope(const RecorderScope&) = delete;
            RecorderScope& operator=(const RecorderScope&) = delete;

        private:
            const VulkanTransferQueue* m_PreviousQueue;
            TransferLane m_PreviousLane;
        };
        // True (logged with `operation`) when this thread is inside this queue's recorder on `lane`.
        [[nodiscard]] bool RefuseInsideRecorder(TransferLane lane, std::string_view operation) const;
        [[nodiscard]] bool RefuseInsideGraphicsRecorder(std::string_view operation) const
        {
            return RefuseInsideRecorder(TransferLane::Graphics, operation);
        }
        // Allocate(Graphics) and begin; the caller holds the graphics lane's Record mutex.
        [[nodiscard]] VkCommandBuffer Begin();
        struct DownloadSource
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            uint64_t Offset = 0, Size = 0;
            size_t SlotIndex = 0;
        };
        // Validates a buffer readback and reserves its staging slot; nullopt (logged and counted
        // as dropped) on refusal.
        [[nodiscard]] std::optional<DownloadSource> PrepareDownload(RHI::BufferHandle src,
            uint64_t size,
            uint64_t offset,
            const RHI::ReadbackSink& sink,
            TransferLane lane = TransferLane::Graphics);
        // Records the source barrier (after every earlier write on this lane) and the copy
        // into the staging slot, then submits; frees `cmd` and the slot on failure.
        [[nodiscard]] RHI::ReadbackToken RecordAndSubmitDownload(VkCommandBuffer cmd,
            const DownloadSource& source,
            RHI::ReadbackSink sink,
            TransferLane lane = TransferLane::Graphics);
        // GRAPHICS-150: `cmd` (begun, from Allocate(lane)) already holds the producer's commands.
        [[nodiscard]] RHI::ReadbackToken SubmitRecordedDownload(VkCommandBuffer cmd,
            RHI::BufferHandle src,
            uint64_t size,
            RHI::ReadbackSink sink,
            TransferLane lane = TransferLane::Graphics);
        [[nodiscard]] bool FinishCommandBuffer(
            VkCommandBuffer cmd, std::string_view operation, TransferLane lane = TransferLane::Graphics);
        // Caller holds the lane's Record mutex (not m_Mutex). Returns the lane's ticket (0 when refused, `cmd`
        // freed). A compute-lane submission waits on the graphics lane's last submitted ticket (every upload,
        // download and device one-shot upload issued before it, and by queue order every earlier graphics batch,
        // frames included) unless its lane's LaneOrderOnly is set, and submits nothing to the graphics queue; a
        // graphics-lane one carries the published compute wait.
        [[nodiscard]] uint64_t SubmitTimelineLocked(
            VkCommandBuffer cmd, std::string_view operation, TransferLane lane = TransferLane::Graphics);
        [[nodiscard]] RHI::TransferToken Submit(VkCommandBuffer cmd);
        [[nodiscard]] RHI::ReadbackToken SubmitReadback(VkCommandBuffer cmd,
            size_t slotIndex,
            uint64_t sizeBytes,
            RHI::ReadbackSink sink,
            TransferLane lane = TransferLane::Graphics);
        void RetireCompletedCommandBuffers(TransferLane lane, uint64_t completedValue);
        // Slots are lane-private (created EXCLUSIVE; never handed to another queue family).
        [[nodiscard]] size_t AcquireReadbackSlot(uint64_t sizeBytes, TransferLane lane = TransferLane::Graphics);
        void ReleaseReadbackSlot(size_t slotIndex, uint64_t retireValue);
        void DrainCompletedReadbacks(const std::array<uint64_t, 2>& completed);
        void DestroyReadbackSlots();

        struct ReadbackSlot
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VmaAllocation Allocation = VK_NULL_HANDLE;
            void* MappedPtr = nullptr;
            uint64_t SizeBytes = 0;
            uint64_t RetireValue = 0;
            TransferLane Lane = TransferLane::Graphics;
            bool InUse = false;
        };

        struct PendingReadback
        {
            RHI::ReadbackToken Token{};
            size_t SlotIndex = 0;
            uint64_t SizeBytes = 0;
            RHI::ReadbackSink Sink{};
            bool Released = false; // a pulled readback given up: dropped, uncounted, once the device finished it
        };
        // Caller holds m_Mutex: frees an erased entry's staging slot (its token retires it).
        void FreeSlotLocked(const PendingReadback& entry);

        VkDevice      m_Device        = VK_NULL_HANDLE;
        VmaAllocator m_Vma = VK_NULL_HANDLE;
        // Indexed by TransferLane; the compute lane is empty (null queue) when it resolves to graphics.
        std::array<Lane, 2> m_Lanes{};
        RHI::QueueAffinityResolution m_ComputeResolution{};
        bool m_ComputeSharesFrameComputeQueue = false;
        bool m_ComputeSharesFrameQueueLock = false;
        std::atomic<bool> m_Lost{false};
        // The graphics lane's Record mutex also holds one pending staging transaction (uploads stage under it).
        std::unique_ptr<StagingBelt> m_Belt;
        // Slots, pending readbacks and the lanes' InFlight lists; never held while taking a queue lock.
        mutable std::mutex m_Mutex;
        std::vector<ReadbackSlot> m_ReadbackSlots;
        std::deque<PendingReadback> m_PendingReadbacks;
        uint64_t m_ReadbackRingBytes = 0;
        // Compute-lane value CollectCompleted observed complete (results published to the host).
        std::atomic<uint64_t> m_PublishedComputeValue{0};
        std::atomic<uint64_t> m_ComputeSubmissions{0};
        std::atomic<uint64_t> m_ComputeGraphicsWaits{0};
        std::atomic<uint64_t> m_ComputeHostBarriers{0};
        std::atomic<uint64_t> m_ComputeVisibilityWaits{0};
        std::atomic<uint64_t> m_DownloadsQueued{0};
        std::atomic<uint64_t> m_DownloadsCompleted{0};
        std::atomic<uint64_t> m_DownloadsDropped{0};
        std::atomic<uint64_t> m_ReadbackBytesStaged{0};
        std::atomic<uint64_t> m_ReadbackRingHighWaterBytes{0};
        std::atomic<uint64_t> m_ReadbackSlotsUncached{0};
        std::atomic<uint64_t> m_ReadbackHostBarriers{0};

        Core::ResourcePool<VulkanBuffer, RHI::BufferHandle,   kMaxFramesInFlight>* m_Buffers = nullptr;
        Core::ResourcePool<VulkanImage,  RHI::TextureHandle,  kMaxFramesInFlight>* m_Images  = nullptr;
    };
}
