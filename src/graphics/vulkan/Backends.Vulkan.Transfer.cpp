module;

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <mutex>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Transfer;
import Extrinsic.Core.Logging;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.TextureUpload;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §8  VulkanTransferQueue
// =============================================================================

VulkanTransferQueue::VulkanTransferQueue(const Config& cfg)
    : m_Device(cfg.Device), m_Vma(cfg.Vma),
      m_ComputeResolution(ResolveMethodComputeLane(cfg.ComputeQueue != VK_NULL_HANDLE, cfg.ForceGraphicsCompute))
{
    LaneFor(TransferLane::Graphics).Queue = cfg.Queue;
    LaneFor(TransferLane::Graphics).Family = cfg.QueueFamily;
    LaneFor(TransferLane::Graphics).QueueMutex = cfg.QueueSubmitMutex;
    LaneFor(TransferLane::Compute).QueueMutex = cfg.ComputeQueueSubmitMutex;
    if (m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE || cfg.Queue == VK_NULL_HANDLE)
    {
        Core::Log::Error("[VulkanTransferQueue] Cannot create transfer queue without valid device/VMA/queue handles");
        return;
    }

    const auto createLane = [this](Lane& lane, const VkQueue queue, const uint32_t family) -> bool
    {
        lane.Queue = queue;
        lane.Family = family;
        VkSemaphoreTypeCreateInfo typeCI{};
        typeCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeCI.initialValue = 0;
        VkSemaphoreCreateInfo semCI{};
        semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semCI.pNext = &typeCI;
        if (vkCreateSemaphore(m_Device, &semCI, nullptr, &lane.Timeline) != VK_SUCCESS)
            lane.Timeline = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo poolCI{};
        poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCI.queueFamilyIndex = family;
        poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (lane.Timeline != VK_NULL_HANDLE &&
            vkCreateCommandPool(m_Device, &poolCI, nullptr, &lane.Pool) != VK_SUCCESS)
            lane.Pool = VK_NULL_HANDLE;
        return lane.Timeline != VK_NULL_HANDLE && lane.Pool != VK_NULL_HANDLE;
    };
    const auto destroyLane = [this](Lane& lane)
    {
        if (lane.Pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_Device, lane.Pool, nullptr);
        if (lane.Timeline != VK_NULL_HANDLE)
            vkDestroySemaphore(m_Device, lane.Timeline, nullptr);
        lane.Queue = VK_NULL_HANDLE;
        lane.Pool = VK_NULL_HANDLE;
        lane.Timeline = VK_NULL_HANDLE;
    };
    const auto destroyLanes = [&]
    {
        for (Lane& lane : m_Lanes)
            destroyLane(lane);
    };

    if (!createLane(LaneFor(TransferLane::Graphics), cfg.Queue, cfg.QueueFamily))
    {
        Core::Log::Error("[VulkanTransferQueue] timeline/command pool creation failed; transfer service unavailable");
        destroyLanes();
        return;
    }
    // A compute lane that cannot be created demotes method compute to graphics, reported as such.
    if (m_ComputeResolution.Resolved == RHI::QueueAffinity::AsyncCompute &&
        !createLane(LaneFor(TransferLane::Compute), cfg.ComputeQueue, cfg.ComputeQueueFamily))
    {
        Core::Log::Warn(
            "[VulkanTransferQueue] compute lane timeline/command pool creation failed; method compute runs on graphics");
        destroyLane(LaneFor(TransferLane::Compute));
        m_ComputeResolution.Resolved = RHI::QueueAffinity::Graphics;
        m_ComputeResolution.Demoted = true;
    }

    m_ComputeSharesFrameComputeQueue =
        (m_ComputeResolution.Resolved == RHI::QueueAffinity::AsyncCompute ? cfg.ComputeQueue : cfg.Queue) ==
        cfg.FrameComputeQueue;
    m_ComputeSharesFrameQueueLock = m_ComputeResolution.Resolved != RHI::QueueAffinity::AsyncCompute ||
        (cfg.ComputeQueueSubmitMutex != nullptr && cfg.ComputeQueueSubmitMutex == cfg.QueueSubmitMutex);
    Core::Log::Info(
        "[VulkanTransferQueue] Method compute: {}, shares frame-compute queue: {}, shares frame queue lock: {}",
        m_ComputeResolution.Resolved == RHI::QueueAffinity::AsyncCompute ? "compute lane" : "graphics lane",
        m_ComputeSharesFrameComputeQueue,
        m_ComputeSharesFrameQueueLock);

    m_Belt = std::make_unique<StagingBelt>(m_Device, m_Vma, cfg.StagingCapacity);
    if (!m_Belt || !m_Belt->IsValid())
    {
        Core::Log::Error("[VulkanTransferQueue] Staging belt creation failed; transfer service unavailable");
        m_Belt.reset();
        destroyLanes();
    }
}

VulkanTransferQueue::~VulkanTransferQueue()
{
    // Both lanes go idle before their command buffers, slots and timelines are released (each under its own
    // VkQueue's lock).
    for (const Lane& lane : m_Lanes)
    {
        if (lane.Queue == VK_NULL_HANDLE || lane.Timeline == VK_NULL_HANDLE)
            continue;
        std::unique_lock<std::mutex> queueLock;
        if (lane.QueueMutex != nullptr)
            queueLock = std::unique_lock{*lane.QueueMutex};
        (void)vkQueueWaitIdle(lane.Queue);
    }
    RetireCompletedCommandBuffers(TransferLane::Graphics, std::numeric_limits<std::uint64_t>::max());
    RetireCompletedCommandBuffers(TransferLane::Compute, std::numeric_limits<std::uint64_t>::max());
    DestroyReadbackSlots();
    for (Lane& lane : m_Lanes)
    {
        if (lane.Pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_Device, lane.Pool, nullptr);
        if (lane.Timeline != VK_NULL_HANDLE)
            vkDestroySemaphore(m_Device, lane.Timeline, nullptr);
    }
}

bool VulkanTransferQueue::IsValid() const noexcept
{
    const Lane& graphics = LaneFor(TransferLane::Graphics);
    const Lane& compute = LaneFor(TransferLane::Compute);
    const bool computeReady = m_ComputeResolution.Resolved != RHI::QueueAffinity::AsyncCompute ||
        (compute.Timeline != VK_NULL_HANDLE && compute.Pool != VK_NULL_HANDLE);
    return m_Device != VK_NULL_HANDLE && m_Vma != VK_NULL_HANDLE && graphics.Queue != VK_NULL_HANDLE &&
        graphics.Timeline != VK_NULL_HANDLE && graphics.Pool != VK_NULL_HANDLE && computeReady && m_Belt &&
        m_Belt->IsValid();
}

TransferLane VulkanTransferQueue::ComputeLane() const noexcept
{
    return m_ComputeResolution.Resolved == RHI::QueueAffinity::AsyncCompute ? TransferLane::Compute
                                                                            : TransferLane::Graphics;
}

uint64_t VulkanTransferQueue::QueryCompletedValue() const
{
    return QueryLaneCompletedValue(TransferLane::Graphics);
}

uint64_t VulkanTransferQueue::QueryLaneCompletedValue(const TransferLane laneId) const
{
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] QueryCompletedValue on invalid transfer service; returning zero");
        return 0;
    }
    const Lane& lane = LaneFor(laneId);
    if (lane.Timeline == VK_NULL_HANDLE)
        return 0;

    uint64_t val = 0;
    const VkResult result = vkGetSemaphoreCounterValue(m_Device, lane.Timeline, &val);
    if (result != VK_SUCCESS)
    {
        Core::Log::Warn("[VulkanTransferQueue] vkGetSemaphoreCounterValue failed; returning zero");
        return 0;
    }
    return val;
}

VkCommandBuffer VulkanTransferQueue::Allocate(const TransferLane laneId)
{
    if (!IsValid() || LaneFor(laneId).Pool == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] Cannot begin transfer command buffer; service is invalid");
        return VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo allocCI{};
    allocCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCI.commandPool = LaneFor(laneId).Pool;
    allocCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCI.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    // The caller holds the lane's Record mutex: retirement frees into this pool under it.
    const VkResult result = vkAllocateCommandBuffers(m_Device, &allocCI, &cmd);
    if (result != VK_SUCCESS || cmd == VK_NULL_HANDLE)
    {
        Core::Log::Error("[VulkanTransferQueue] vkAllocateCommandBuffers failed; upload skipped");
        return VK_NULL_HANDLE;
    }
    return cmd;
}

void VulkanTransferQueue::FreeCommandBuffer(const TransferLane laneId, VkCommandBuffer cmd)
{
    if (cmd == VK_NULL_HANDLE || m_Device == VK_NULL_HANDLE || LaneFor(laneId).Pool == VK_NULL_HANDLE)
        return;
    vkFreeCommandBuffers(m_Device, LaneFor(laneId).Pool, 1u, &cmd); // under the lane's Record mutex
}

VkCommandBuffer VulkanTransferQueue::Begin()
{
    VkCommandBuffer cmd = Allocate();
    if (cmd == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo beginCI{};
    beginCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginCI) != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanTransferQueue] vkBeginCommandBuffer failed; upload skipped");
        FreeCommandBuffer(TransferLane::Graphics, cmd);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

bool VulkanTransferQueue::FinishCommandBuffer(
    VkCommandBuffer cmd, const std::string_view operation, const TransferLane lane)
{
    if (!IsValid() || cmd == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] Cannot submit {} command buffer; service or command buffer is invalid",
                       operation == "upload" ? std::string_view{"transfer"} : operation);
        FreeCommandBuffer(lane, cmd);
        return false;
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanTransferQueue] vkEndCommandBuffer failed; {} skipped", operation);
        FreeCommandBuffer(lane, cmd);
        return false;
    }
    return true;
}

VkSemaphoreSubmitInfo VulkanTransferQueue::PublishedComputeWait() const noexcept
{
    VkSemaphoreSubmitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    const uint64_t published = m_PublishedComputeValue.load(std::memory_order_acquire);
    const Lane& compute = LaneFor(TransferLane::Compute);
    if (published == 0u || compute.Timeline == VK_NULL_HANDLE)
        return wait;
    wait.semaphore = compute.Timeline;
    wait.value = published;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return wait;
}

void VulkanTransferQueue::NoteComputeVisibilityWait() noexcept
{
    m_ComputeVisibilityWaits.fetch_add(1u, std::memory_order_relaxed);
}

RHI::ReadbackToken VulkanTransferQueue::ComputeFence() const noexcept
{
    // The last accepted ticket: a reserved one may still be refused and handed back, and would then never signal.
    const uint64_t submitted = LaneFor(TransferLane::Compute).SubmittedTicket.load(std::memory_order_acquire);
    return submitted == 0u ? RHI::ReadbackToken{}
                           : RHI::ReadbackToken{EncodeLaneTicket(TransferLane::Compute, submitted)};
}

namespace
{
    // The queue and lane whose SubmitComputeReadback recorder this thread runs (RecorderScope), else null.
    thread_local const VulkanTransferQueue* t_Recorder = nullptr;
    thread_local TransferLane t_RecorderLane = TransferLane::Graphics;
}

VulkanTransferQueue::RecorderScope::RecorderScope(
    const VulkanTransferQueue& recording, const TransferLane lane) noexcept
    : m_PreviousQueue(t_Recorder), m_PreviousLane(t_RecorderLane)
{
    t_Recorder = &recording;
    t_RecorderLane = lane;
}

VulkanTransferQueue::RecorderScope::~RecorderScope()
{
    t_Recorder = m_PreviousQueue;
    t_RecorderLane = m_PreviousLane;
}

bool VulkanTransferQueue::RefuseInsideRecorder(const TransferLane lane, const std::string_view operation) const
{
    if (t_Recorder != this || t_RecorderLane != lane)
        return false;
    Core::Log::Error("[VulkanTransferQueue] {} refused inside a {}-lane compute recorder (it holds the lane's lock); "
                     "prepare host work before IDevice::SubmitComputeReadback",
        operation,
        lane == TransferLane::Graphics ? "graphics" : "compute");
    return true;
}

bool VulkanTransferQueue::ComputeLive() const noexcept
{
    return !m_Lost.load(std::memory_order_acquire) && IsValid();
}

void VulkanTransferQueue::MarkLost() noexcept
{
    m_Lost.store(true, std::memory_order_release);
}

RHI::ComputeWaitStatus VulkanTransferQueue::WaitCompute(
    const RHI::ReadbackToken token, const uint64_t timeoutNanoseconds)
{
    if (!token.IsValid())
        return RHI::ComputeWaitStatus::Complete;
    const Lane& lane = LaneFor(LaneOfTicket(token.Value));
    if (!ComputeLive() || lane.Timeline == VK_NULL_HANDLE)
        return RHI::ComputeWaitStatus::Failed;
    const uint64_t value = LaneTicketValue(token.Value);
    VkSemaphoreWaitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait.semaphoreCount = 1u;
    wait.pSemaphores = &lane.Timeline;
    wait.pValues = &value;
    // Semaphores need no host synchronization for waits; the caller's own submission is the only work awaited.
    const VkResult result = vkWaitSemaphores(m_Device, &wait, timeoutNanoseconds);
    if (result == VK_SUCCESS)
        return RHI::ComputeWaitStatus::Complete;
    if (result == VK_TIMEOUT)
        return RHI::ComputeWaitStatus::Pending;
    // Lost (or out of memory): the value may never be signaled; the device notes its loss on its own thread.
    MarkLost();
    return RHI::ComputeWaitStatus::Failed;
}

void VulkanTransferQueue::FreeSlotLocked(const PendingReadback& entry)
{
    if (entry.SlotIndex >= m_ReadbackSlots.size())
        return;
    ReadbackSlot& slot = m_ReadbackSlots[entry.SlotIndex];
    slot.InUse = false;
    slot.RetireValue = entry.Token.Value;
}

bool VulkanTransferQueue::TakeCompletedReadback(const RHI::ReadbackToken token, const std::span<std::byte> destination)
{
    if (!token.IsValid() || !IsValid())
        return false;
    const TransferLane lane = LaneOfTicket(token.Value);
    const uint64_t done = QueryLaneCompletedValue(lane);
    {
        std::scoped_lock lock{m_Mutex};
        const auto it = std::ranges::find(m_PendingReadbacks, token, &PendingReadback::Token);
        // Only a pulled readback: CollectCompleted never delivers or drops it, so the bytes are still here.
        if (it == m_PendingReadbacks.end() || !it->Sink.Pulled || it->Released ||
            it->SizeBytes != destination.size_bytes() || LaneTicketValue(token.Value) > done)
            return false;
        if (it->SizeBytes != 0u)
        {
            if (it->SlotIndex >= m_ReadbackSlots.size() || m_ReadbackSlots[it->SlotIndex].MappedPtr == nullptr)
                return false;
            ReadbackSlot& slot = m_ReadbackSlots[it->SlotIndex];
            if (slot.Allocation != VK_NULL_HANDLE)
                (void)vmaInvalidateAllocation(m_Vma, slot.Allocation, 0, it->SizeBytes);
            std::memcpy(destination.data(), slot.MappedPtr, destination.size_bytes());
            m_DownloadsCompleted.fetch_add(1u, std::memory_order_relaxed);
        }
        FreeSlotLocked(*it);
        m_PendingReadbacks.erase(it); // its record goes now; CollectCompleted's watermark no longer waits for it
    }
    // The taker's own retirement: the lane's completed command buffers return to its pool without a frame.
    RetireCompletedCommandBuffers(lane, done);
    return true;
}

void VulkanTransferQueue::ReleaseReadback(const RHI::ReadbackToken token)
{
    if (!token.IsValid() || !IsValid())
        return;
    const TransferLane lane = LaneOfTicket(token.Value);
    const uint64_t done = QueryLaneCompletedValue(lane);
    {
        std::scoped_lock lock{m_Mutex};
        const auto it = std::ranges::find(m_PendingReadbacks, token, &PendingReadback::Token);
        if (it == m_PendingReadbacks.end() || !it->Sink.Pulled)
            return;
        it->Released = true;
        if (LaneTicketValue(token.Value) > done)
            return; // still on the device: freed by a later Take, Release or CollectCompleted
        FreeSlotLocked(*it);
        m_PendingReadbacks.erase(it);
    }
    RetireCompletedCommandBuffers(lane, done);
}

uint64_t VulkanTransferQueue::SubmitTimelineLocked(
    VkCommandBuffer cmd, const std::string_view operation, const TransferLane laneId)
{
    Lane& lane = LaneFor(laneId);
    const Lane& graphics = LaneFor(TransferLane::Graphics);
    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSemaphoreSubmitInfo sigInfo{};
    sigInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    sigInfo.semaphore = lane.Timeline;
    // All commands: a completion-only compute submission records no transfer, so a
    // transfer-stage signal would not cover its dispatches.
    sigInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &sigInfo;

    // Graphics lane: acquire the completed compute results (already signaled; ordering only). Compute lane: wait
    // on the graphics lane's last submitted ticket. Every graphics-queue producer of method input draws one: the
    // residency's uploads, a method's private uploads (seeds, slot maps), CPU-uploaded ring fronts, downloads and the
    // device's one-shot uploads; frames do not produce method input (see the RHI contract). A producer whose token
    // the caller holds was submitted before this call, so its ticket is covered; an already signaled value costs
    // nothing, and no graphics-queue submission or lock is needed here. A LaneOrderOnly submission (a prepared
    // worker page, RHI::ComputeInputOrder) waits for nothing: its inputs precede an earlier compute-lane submission
    // that carried this wait, and a semaphore wait also orders every later submission of its queue.
    VkSemaphoreSubmitInfo waitInfo =
        laneId == TransferLane::Graphics ? PublishedComputeWait() : VkSemaphoreSubmitInfo{};
    if (laneId == TransferLane::Compute && !std::exchange(lane.LaneOrderOnly, false))
    {
        const uint64_t issued = graphics.SubmittedTicket.load(std::memory_order_acquire);
        if (issued != 0u && graphics.Timeline != VK_NULL_HANDLE)
        {
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfo.semaphore = graphics.Timeline;
            waitInfo.value = issued;
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
    }
    if (waitInfo.semaphore != VK_NULL_HANDLE)
    {
        submit.waitSemaphoreInfoCount = 1u;
        submit.pWaitSemaphoreInfos = &waitInfo;
    }

    uint64_t ticket = 0u;
    VkResult result;
    {
        // Only this lane's VkQueue lock: a separate method queue never waits for a present or frame submission.
        std::unique_lock<std::mutex> queueLock;
        if (lane.QueueMutex != nullptr)
            queueLock = std::unique_lock{*lane.QueueMutex};
        // Tickets are drawn under the lane's Record mutex, so each timeline is signaled in increasing order; a
        // refused submission hands its ticket back, so fences never name a value that is never signaled.
        ticket = lane.NextTicket.fetch_add(1, std::memory_order_acq_rel);
        sigInfo.value = ticket;
        result = vkQueueSubmit2(lane.Queue, 1, &submit, VK_NULL_HANDLE);
        if (result != VK_SUCCESS)
            lane.NextTicket.store(ticket, std::memory_order_release);
        else
            lane.SubmittedTicket.store(ticket, std::memory_order_release);
    }
    if (result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
            MarkLost();
        Core::Log::Error("[VulkanTransferQueue] vkQueueSubmit2 failed; {} skipped", operation);
        vkFreeCommandBuffers(m_Device, lane.Pool, 1u, &cmd);
        return 0u;
    }
    // Handoff counters are read back from the batch actually submitted, so a dropped wait drops its count.
    const bool submittedWait = submit.waitSemaphoreInfoCount == 1u && submit.pWaitSemaphoreInfos != nullptr;
    if (laneId == TransferLane::Compute)
    {
        m_ComputeSubmissions.fetch_add(1u, std::memory_order_relaxed);
        if (submittedWait && submit.pWaitSemaphoreInfos->semaphore == graphics.Timeline)
            m_ComputeGraphicsWaits.fetch_add(1u, std::memory_order_relaxed);
    }
    else if (submittedWait && submit.pWaitSemaphoreInfos->semaphore == LaneFor(TransferLane::Compute).Timeline)
    {
        NoteComputeVisibilityWait();
    }
    std::scoped_lock lock{m_Mutex};
    lane.InFlight.push_back(RetiredCommandBuffer{.CommandBuffer = cmd, .RetireValue = ticket});
    return ticket;
}

VkResult VulkanTransferQueue::SubmitGraphicsTicketed(const VkSubmitInfo2& submit, const VkFence fence)
{
    Lane& graphics = LaneFor(TransferLane::Graphics);
    if (!IsValid())
        return VK_ERROR_INITIALIZATION_FAILED;
    VkSemaphoreSubmitInfo signal{};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = graphics.Timeline;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 ticketed = submit;
    ticketed.signalSemaphoreInfoCount = 1u;
    ticketed.pSignalSemaphoreInfos = &signal;
    if (RefuseInsideGraphicsRecorder("A device one-shot submission"))
        return VK_ERROR_INITIALIZATION_FAILED;
    std::scoped_lock record{graphics.Record};
    std::unique_lock<std::mutex> queueLock;
    if (graphics.QueueMutex != nullptr)
        queueLock = std::unique_lock{*graphics.QueueMutex};
    signal.value = graphics.NextTicket.fetch_add(1, std::memory_order_acq_rel);
    const VkResult result = vkQueueSubmit2(graphics.Queue, 1u, &ticketed, fence);
    if (result != VK_SUCCESS)
        graphics.NextTicket.store(signal.value, std::memory_order_release);
    else
        graphics.SubmittedTicket.store(signal.value, std::memory_order_release);
    return result;
}

RHI::TransferToken VulkanTransferQueue::Submit(VkCommandBuffer cmd)
{
    // The caller staged under the graphics lane's Record mutex and still holds it.
    if (!FinishCommandBuffer(cmd, "upload"))
    {
        m_Belt->CancelPending();
        return {};
    }

    const uint64_t ticket = SubmitTimelineLocked(cmd, "upload");
    if (ticket == 0u)
    {
        m_Belt->CancelPending();
        return {};
    }
    m_Belt->Retire(ticket);
    return RHI::TransferToken{ticket};
}

RHI::ReadbackToken VulkanTransferQueue::SubmitReadback(
    VkCommandBuffer cmd, size_t slotIndex, uint64_t sizeBytes, RHI::ReadbackSink sink, const TransferLane lane)
{
    // The copy into the slot is made visible to the host before the CPU reads it at collection: the timeline wait
    // orders the device's work, a host-read barrier the memory (both buffer and texture downloads end here).
    bool hostBarrier = false;
    {
        std::scoped_lock lock{m_Mutex};
        if (slotIndex < m_ReadbackSlots.size() && m_ReadbackSlots[slotIndex].Buffer != VK_NULL_HANDLE && sizeBytes > 0u)
        {
            VkBufferMemoryBarrier2 toHost{};
            toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            toHost.buffer = m_ReadbackSlots[slotIndex].Buffer;
            toHost.offset = 0u;
            toHost.size = sizeBytes;
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.bufferMemoryBarrierCount = 1u;
            dependency.pBufferMemoryBarriers = &toHost;
            vkCmdPipelineBarrier2(cmd, &dependency);
            hostBarrier = true;
        }
    }
    if (!FinishCommandBuffer(cmd, "readback", lane))
        return {};

    // The caller holds the lane's Record mutex, so this lane's pending readbacks are pushed in ticket order.
    const uint64_t ticket = SubmitTimelineLocked(cmd, "readback", lane);
    if (ticket == 0u)
        return {};
    std::scoped_lock lock{m_Mutex};
    if (hostBarrier)
    {
        m_ReadbackHostBarriers.fetch_add(1u, std::memory_order_relaxed);
        if (lane == TransferLane::Compute)
            m_ComputeHostBarriers.fetch_add(1u, std::memory_order_relaxed);
    }
    const RHI::ReadbackToken token{EncodeLaneTicket(lane, ticket)};
    if (slotIndex < m_ReadbackSlots.size())
        m_ReadbackSlots[slotIndex].RetireValue = token.Value;
    m_PendingReadbacks.push_back(PendingReadback{
        .Token = token,
        .SlotIndex = slotIndex,
        .SizeBytes = sizeBytes,
        .Sink = std::move(sink),
    });
    return token;
}

void VulkanTransferQueue::RetireCompletedCommandBuffers(const TransferLane laneId, const uint64_t completedValue)
{
    Lane& lane = LaneFor(laneId);
    if (m_Device == VK_NULL_HANDLE || lane.Pool == VK_NULL_HANDLE ||
        (laneId == TransferLane::Graphics && RefuseInsideGraphicsRecorder("Command buffer retirement")))
        return; // a later retirement frees them

    std::scoped_lock lock{lane.Record, m_Mutex}; // the pool, then the list
    while (!lane.InFlight.empty() && lane.InFlight.front().RetireValue <= completedValue)
    {
        const VkCommandBuffer cmd = lane.InFlight.front().CommandBuffer;
        if (cmd != VK_NULL_HANDLE)
            vkFreeCommandBuffers(m_Device, lane.Pool, 1u, &cmd);
        lane.InFlight.pop_front();
    }
}

size_t VulkanTransferQueue::AcquireReadbackSlot(uint64_t sizeBytes, const TransferLane lane)
{
    if (sizeBytes == 0u || sizeBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return std::numeric_limits<size_t>::max();

    std::scoped_lock lock{m_Mutex};
    for (size_t index = 0; index < m_ReadbackSlots.size(); ++index)
    {
        ReadbackSlot& slot = m_ReadbackSlots[index];
        if (!slot.InUse && slot.Lane == lane && slot.Buffer != VK_NULL_HANDLE && slot.SizeBytes >= sizeBytes)
        {
            slot.InUse = true;
            slot.RetireValue = 0u;
            return index;
        }
    }

    if (sizeBytes > std::numeric_limits<uint64_t>::max() - m_ReadbackRingBytes)
    {
        Core::Log::Warn("[VulkanTransferQueue] Readback staging allocation rejected; ring size would overflow");
        return std::numeric_limits<size_t>::max();
    }

    VkBufferCreateInfo bufferCI{};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = sizeBytes;
    bufferCI.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    const VmaAllocationCreateInfo allocationCI = HostReadbackAllocationInfo();

    ReadbackSlot slot{};
    VmaAllocationInfo info{};
    const VkResult result = vmaCreateBuffer(m_Vma,
                                            &bufferCI,
                                            &allocationCI,
                                            &slot.Buffer,
                                            &slot.Allocation,
                                            &info);
    if (result != VK_SUCCESS || slot.Buffer == VK_NULL_HANDLE ||
        slot.Allocation == VK_NULL_HANDLE || info.pMappedData == nullptr)
    {
        Core::Log::Error("[VulkanTransferQueue] Failed to allocate readback staging buffer");
        if (slot.Buffer != VK_NULL_HANDLE && slot.Allocation != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_Vma, slot.Buffer, slot.Allocation);
        return std::numeric_limits<size_t>::max();
    }

    slot.MappedPtr = info.pMappedData;
    slot.SizeBytes = sizeBytes;
    slot.Lane = lane;
    slot.InUse = true;
    if (IsUncachedHostReadback(m_Vma, slot.Allocation))
        m_ReadbackSlotsUncached.fetch_add(1u, std::memory_order_relaxed);
    m_ReadbackSlots.push_back(slot);
    m_ReadbackRingBytes += sizeBytes;

    uint64_t highWater = m_ReadbackRingHighWaterBytes.load(std::memory_order_relaxed);
    while (highWater < m_ReadbackRingBytes &&
           !m_ReadbackRingHighWaterBytes.compare_exchange_weak(highWater,
                                                               m_ReadbackRingBytes,
                                                               std::memory_order_relaxed,
                                                               std::memory_order_relaxed))
    {
    }

    return m_ReadbackSlots.size() - 1u;
}

void VulkanTransferQueue::ReleaseReadbackSlot(size_t slotIndex, uint64_t retireValue)
{
    std::scoped_lock lock{m_Mutex};
    if (slotIndex >= m_ReadbackSlots.size())
        return;
    ReadbackSlot& slot = m_ReadbackSlots[slotIndex];
    slot.InUse = false;
    slot.RetireValue = retireValue;
}

void VulkanTransferQueue::DrainCompletedReadbacks(const std::array<uint64_t, 2>& completedValues)
{
    struct ReadyReadback
    {
        RHI::ReadbackToken Token{};
        RHI::ReadbackSink Sink{};
        std::vector<std::byte> Bytes{};
    };

    std::vector<ReadyReadback> ready;
    std::array<uint64_t, 2> watermark = completedValues;
    {
        std::scoped_lock lock{m_Mutex};
        auto it = m_PendingReadbacks.begin();
        while (it != m_PendingReadbacks.end())
        {
            const bool complete = it->Token.IsValid() && IsLaneTicketComplete(it->Token.Value, completedValues);
            // A pulled readback belongs to its submitter until it takes or releases it: never delivered or dropped
            // here; a released one is freed once complete, without a callback.
            if (complete && it->Sink.Pulled && it->Released)
            {
                FreeSlotLocked(*it);
                it = m_PendingReadbacks.erase(it);
                continue;
            }
            if (!complete || it->Sink.Pulled)
            {
                // Lane FIFO: no later token of this lane reports complete before this one is resolved.
                if (it->Token.IsValid())
                {
                    auto& bound = watermark[static_cast<size_t>(LaneOfTicket(it->Token.Value))];
                    bound = std::min(bound, LaneTicketValue(it->Token.Value) - 1u);
                }
                ++it;
                continue;
            }

            std::vector<std::byte> bytes(static_cast<size_t>(it->SizeBytes));
            if (it->SlotIndex < m_ReadbackSlots.size())
            {
                ReadbackSlot& slot = m_ReadbackSlots[it->SlotIndex];
                if (slot.Allocation != VK_NULL_HANDLE)
                {
                    const VkResult invalidateResult =
                        vmaInvalidateAllocation(m_Vma, slot.Allocation, 0, it->SizeBytes);
                    if (invalidateResult != VK_SUCCESS)
                    {
                        Core::Log::Warn("[VulkanTransferQueue] readback vmaInvalidateAllocation reported VkResult={}; bytes may reflect stale CPU cache on non-coherent memory.",
                                        static_cast<int>(invalidateResult));
                    }
                }
                if (slot.MappedPtr != nullptr && !bytes.empty())
                    std::memcpy(bytes.data(), slot.MappedPtr, bytes.size());
            }
            FreeSlotLocked(*it);

            ready.push_back(ReadyReadback{
                .Token = it->Token,
                .Sink = std::move(it->Sink),
                .Bytes = std::move(bytes),
            });
            it = m_PendingReadbacks.erase(it);
        }
    }

    for (ReadyReadback& readback : ready)
    {
        readback.Sink.Deliver(std::span<const std::byte>{readback.Bytes});
        if (!readback.Bytes.empty())
            m_DownloadsCompleted.fetch_add(1u, std::memory_order_relaxed);
    }
    // After the sinks ran: each lane's readback tokens up to its first unresolved entry (and the device's value).
    for (const TransferLane lane : {TransferLane::Graphics, TransferLane::Compute})
    {
        std::atomic<uint64_t>& delivered = LaneFor(lane).CompletedReadbackTicket;
        const uint64_t value = watermark[static_cast<size_t>(lane)];
        uint64_t completed = delivered.load(std::memory_order_acquire);
        while (completed < value &&
            !delivered.compare_exchange_weak(completed, value, std::memory_order_release, std::memory_order_acquire))
        {
        }
    }
}

void VulkanTransferQueue::DestroyReadbackSlots()
{
    std::scoped_lock lock{m_Mutex};
    m_PendingReadbacks.clear();
    for (ReadbackSlot& slot : m_ReadbackSlots)
    {
        if (slot.Buffer != VK_NULL_HANDLE && slot.Allocation != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_Vma, slot.Buffer, slot.Allocation);
    }
    m_ReadbackSlots.clear();
    m_ReadbackRingBytes = 0u;
}

RHI::TransferToken VulkanTransferQueue::UploadBuffer(RHI::BufferHandle dst,
                                                      const void* data,
                                                      uint64_t size,
                                                      uint64_t offset)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadBufferRaw", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadBufferRaw")};
    if (RefuseInsideGraphicsRecorder("UploadBuffer"))
        return {};
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; transfer service is invalid");
        return {};
    }
    if (!m_Buffers)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; buffer pool is unavailable");
        return {};
    }
    if (data == nullptr || size == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; source data is empty");
        return {};
    }

    auto* buf = m_Buffers->GetIfValid(dst);
    if (!buf || buf->Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; destination buffer handle is invalid");
        return {};
    }
    if (offset > buf->SizeBytes || size > buf->SizeBytes - offset)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; upload range exceeds destination buffer");
        return {};
    }

    std::scoped_lock uploadLock{LaneFor(TransferLane::Graphics).Record};
    auto staging = m_Belt->Allocate(static_cast<size_t>(size), 4);
    if (!staging.MappedPtr || staging.Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; staging allocation failed");
        return {};
    }
    std::memcpy(staging.MappedPtr, data, size);

    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
    {
        m_Belt->CancelPending();
        return {};
    }

    // Uploads share the graphics queue with frame submissions. Queue order
    // alone is an execution order, not a memory dependency: a later staged
    // overwrite still needs to wait for earlier shader reads or transfer
    // writes of the same destination range. Keep that producer-side ordering
    // here; callers continue to own the TransferWrite -> consumer barrier.
    VkBufferMemoryBarrier2 beforeCopy{};
    beforeCopy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    beforeCopy.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    beforeCopy.srcAccessMask =
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    beforeCopy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    beforeCopy.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    beforeCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCopy.buffer = buf->Buffer;
    beforeCopy.offset = offset;
    beforeCopy.size = size;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &beforeCopy;
    vkCmdPipelineBarrier2(cmd, &dependency);

    VkBufferCopy region{staging.Offset, offset, size};
    vkCmdCopyBuffer(cmd, staging.Buffer, buf->Buffer, 1, &region);
    return Submit(cmd);
}

RHI::TransferToken VulkanTransferQueue::UploadBuffer(RHI::BufferHandle dst,
                                                      std::span<const std::byte> src,
                                                      uint64_t offset)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadBufferSpan", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadBufferSpan")};
    return UploadBuffer(dst, src.data(), static_cast<uint64_t>(src.size_bytes()), offset);
}

RHI::TransferToken VulkanTransferQueue::UploadTexture(RHI::TextureHandle dst,
                                                       const void* data,
                                                       uint64_t dataSizeBytes,
                                                       uint32_t mipLevel,
                                                       uint32_t arrayLayer)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadTexture", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadTexture")};
    if (RefuseInsideGraphicsRecorder("UploadTexture"))
        return {};
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; transfer service is invalid");
        return {};
    }
    if (!m_Images)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; image pool is unavailable");
        return {};
    }
    if (data == nullptr || dataSizeBytes == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; source data is empty");
        return {};
    }

    auto* img = m_Images->GetIfValid(dst);
    if (!img || img->Image == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; destination texture handle is invalid");
        return {};
    }
    if (mipLevel >= img->MipLevels || arrayLayer >= img->ArrayLayers)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; mip level or array layer is out of range");
        return {};
    }
    if ((img->Usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; destination image lacks transfer-dst usage");
        return {};
    }

    const RHI::TextureDesc desc{
        .Width = img->Width, .Height = img->Height,
        .MipLevels = img->MipLevels, .Fmt = img->RhiFormat,
    };
    const std::uint64_t expectedSize = RHI::ComputeSubresourceUploadSize(desc, mipLevel);
    if (expectedSize == 0u || dataSizeBytes != expectedSize)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; upload size mismatch: expected={} actual={}",
                       expectedSize, dataSizeBytes);
        return {};
    }
    std::scoped_lock uploadLock{LaneFor(TransferLane::Graphics).Record};
    auto staging = m_Belt->Allocate(static_cast<size_t>(dataSizeBytes),
                                   RHI::RequiredBufferOffsetAlignment(img->RhiFormat));
    if (!staging.MappedPtr || staging.Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; staging allocation failed");
        return {};
    }
    std::memcpy(staging.MappedPtr, data, dataSizeBytes);

    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
    {
        m_Belt->CancelPending();
        return {};
    }

    // Transition: Undefined → TransferDst
    VkImageMemoryBarrier2 toXfer{};
    toXfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toXfer.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
    toXfer.srcAccessMask       = 0;
    toXfer.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toXfer.dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toXfer.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toXfer.image               = img->Image;
    toXfer.subresourceRange    = {AspectFromFormat(img->Format), mipLevel, 1, arrayLayer, 1};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
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
    toRead.dstStageMask   = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    toRead.dstAccessMask  = 0;
    toRead.oldLayout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dep.pImageMemoryBarriers = &toRead;
    vkCmdPipelineBarrier2(cmd, &dep);

    return Submit(cmd);
}

RHI::TransferToken VulkanTransferQueue::UploadTextureFullChain(RHI::TextureHandle dst,
                                                               std::span<const std::byte> src)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadTextureFullChain", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadTextureFullChain")};
    if (RefuseInsideGraphicsRecorder("UploadTextureFullChain"))
        return {};
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; transfer service is invalid");
        return {};
    }
    if (!m_Images)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; image pool is unavailable");
        return {};
    }
    if (src.empty())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; source data is empty");
        return {};
    }

    auto* img = m_Images->GetIfValid(dst);
    if (!img || img->Image == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; destination texture handle is invalid");
        return {};
    }
    if ((img->Usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; destination image lacks transfer-dst usage");
        return {};
    }
    if ((img->Usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; destination image lacks sampled usage");
        return {};
    }
    const bool supported2DArray = img->Dimension == RHI::TextureDimension::Tex2D && img->Depth == 1u;
    const bool supportedCube = img->Dimension == RHI::TextureDimension::TexCube &&
                               img->Depth == 1u &&
                               img->ArrayLayers == 6u;
    if (!supported2DArray && !supportedCube)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; only 2D color texture arrays and six-face cubemaps are supported by this slice");
        return {};
    }

    RHI::TextureDesc desc{};
    desc.Width = img->Width;
    desc.Height = img->Height;
    desc.DepthOrArrayLayers = img->ArrayLayers;
    desc.MipLevels = img->MipLevels;
    desc.Fmt = img->RhiFormat;
    desc.Dimension = img->Dimension;
    desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;

    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    if (!layoutOr.has_value())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; upload layout computation failed with error={}",
                        static_cast<int>(layoutOr.error()));
        return {};
    }
    const RHI::TextureUploadLayout& layout = *layoutOr;
    if (layout.TotalBytes != static_cast<std::uint64_t>(src.size_bytes()))
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; upload size mismatch: expected={} actual={}",
                        layout.TotalBytes,
                        static_cast<std::uint64_t>(src.size_bytes()));
        return {};
    }
    if (layout.TotalBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        layout.Subresources.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; upload layout is too large for this host/backend");
        return {};
    }

    const std::uint32_t offsetAlignment = RHI::RequiredBufferOffsetAlignment(desc.Fmt);
    std::scoped_lock uploadLock{LaneFor(TransferLane::Graphics).Record};
    auto staging = m_Belt->Allocate(static_cast<std::size_t>(layout.TotalBytes), offsetAlignment);
    if (!staging.MappedPtr || staging.Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; staging allocation failed");
        return {};
    }

    auto* stagingBytes = static_cast<std::byte*>(staging.MappedPtr);
    for (const RHI::TextureUploadSubresource& sub : layout.Subresources)
    {
        std::memcpy(stagingBytes + static_cast<std::size_t>(sub.OffsetBytes),
                    src.data() + static_cast<std::size_t>(sub.OffsetBytes),
                    static_cast<std::size_t>(sub.SizeBytes));
    }

    std::vector<VkBufferImageCopy> regions;
    regions.reserve(layout.Subresources.size());
    const VkImageAspectFlags aspectMask = AspectFromFormat(img->Format);
    for (const RHI::TextureUploadSubresource& sub : layout.Subresources)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = staging.Offset + sub.OffsetBytes;
        region.imageSubresource = VkImageSubresourceLayers{.aspectMask = aspectMask,
                                                           .mipLevel = sub.MipLevel,
                                                           .baseArrayLayer = sub.ArrayLayer,
                                                           .layerCount = 1u};
        region.imageExtent = VkExtent3D{.width = sub.Width,
                                        .height = sub.Height,
                                        .depth = sub.Depth};
        regions.push_back(region);
    }

    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
    {
        m_Belt->CancelPending();
        return {};
    }

    VkImageMemoryBarrier2 toXfer{};
    toXfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toXfer.srcStageMask = img->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED
                              ? VK_PIPELINE_STAGE_2_NONE
                              : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    toXfer.srcAccessMask = img->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED
                               ? 0
                               : VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    toXfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toXfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toXfer.oldLayout = img->CurrentLayout;
    toXfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toXfer.image = img->Image;
    toXfer.subresourceRange = VkImageSubresourceRange{.aspectMask = aspectMask,
                                                      .baseMipLevel = 0u,
                                                      .levelCount = img->MipLevels,
                                                      .baseArrayLayer = 0u,
                                                      .layerCount = img->ArrayLayers};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1u;
    dep.pImageMemoryBarriers = &toXfer;
    vkCmdPipelineBarrier2(cmd, &dep);

    vkCmdCopyBufferToImage(cmd,
                           staging.Buffer,
                           img->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(regions.size()),
                           regions.data());

    VkImageMemoryBarrier2 toRead = toXfer;
    toRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toRead.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    toRead.dstAccessMask = 0;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dep.pImageMemoryBarriers = &toRead;
    vkCmdPipelineBarrier2(cmd, &dep);

    const RHI::TransferToken token = Submit(cmd);
    if (token.IsValid())
        img->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return token;
}

std::optional<VulkanTransferQueue::DownloadSource> VulkanTransferQueue::PrepareDownload(
    RHI::BufferHandle src, uint64_t size, uint64_t offset, const RHI::ReadbackSink& sink, const TransferLane lane)
{
    const auto refuse = [&](std::string_view reason) -> std::optional<DownloadSource> {
        Core::Log::Warn("[VulkanTransferQueue] DownloadBuffer rejected; {}", reason);
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return std::nullopt;
    };
    if (!IsValid())
        return refuse("transfer service is invalid");
    if (!m_Buffers)
        return refuse("buffer pool is unavailable");
    if (!sink.IsValidForSize(size))
        return refuse("readback sink is invalid for the requested size");

    VulkanBuffer* buf = m_Buffers->GetIfValid(src);
    if (!buf || buf->Buffer == VK_NULL_HANDLE)
        return refuse("source buffer handle is invalid");
    if ((buf->Usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0)
        return refuse("source buffer lacks transfer-src usage");

    const RHI::BufferDesc sourceDesc{
        .SizeBytes = buf->SizeBytes,
        .Usage = RHI::BufferUsage::TransferSrc,
        .HostVisible = buf->HostVisible,
        .DebugName = "VulkanTransferQueue.DownloadBuffer.Source",
    };
    const auto range = RHI::ValidateBufferRange(sourceDesc,
                                                RHI::BufferRange{
                                                    .OffsetBytes = offset,
                                                    .SizeBytes = size,
                                                });
    if (!range.has_value())
        return refuse("readback range is invalid");
    if (range->SizeBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return refuse("requested readback is too large for this host");

    const size_t slotIndex = AcquireReadbackSlot(range->SizeBytes, lane);
    if (slotIndex == std::numeric_limits<size_t>::max())
        return refuse("readback staging allocation failed");
    return DownloadSource{.Buffer = buf->Buffer, .Offset = range->OffsetBytes, .Size = range->SizeBytes,
                          .SlotIndex = slotIndex};
}

RHI::ReadbackToken VulkanTransferQueue::RecordAndSubmitDownload(
    VkCommandBuffer cmd, const DownloadSource& source, RHI::ReadbackSink sink, const TransferLane lane)
{
    VkBufferMemoryBarrier2 beforeCopy{};
    beforeCopy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    beforeCopy.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    beforeCopy.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    beforeCopy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    beforeCopy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    beforeCopy.buffer = source.Buffer;
    beforeCopy.offset = source.Offset;
    beforeCopy.size = source.Size;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &beforeCopy;
    vkCmdPipelineBarrier2(cmd, &dependency);

    VkBufferCopy copy{};
    copy.srcOffset = source.Offset;
    copy.dstOffset = 0u;
    copy.size = source.Size;
    {
        std::scoped_lock lock{m_Mutex};
        if (source.SlotIndex >= m_ReadbackSlots.size() || m_ReadbackSlots[source.SlotIndex].Buffer == VK_NULL_HANDLE)
        {
            Core::Log::Warn("[VulkanTransferQueue] DownloadBuffer rejected; readback staging slot became invalid");
            if (source.SlotIndex < m_ReadbackSlots.size())
                m_ReadbackSlots[source.SlotIndex].InUse = false;
            vkFreeCommandBuffers(m_Device, LaneFor(lane).Pool, 1u, &cmd);
            m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
            return {};
        }
        vkCmdCopyBuffer(cmd, source.Buffer, m_ReadbackSlots[source.SlotIndex].Buffer, 1u, &copy);
    }

    const RHI::ReadbackToken token = SubmitReadback(cmd, source.SlotIndex, source.Size, std::move(sink), lane);
    if (!token.IsValid())
    {
        ReleaseReadbackSlot(source.SlotIndex, 0u);
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return {};
    }

    m_DownloadsQueued.fetch_add(1u, std::memory_order_relaxed);
    m_ReadbackBytesStaged.fetch_add(source.Size, std::memory_order_relaxed);
    return token;
}

RHI::ReadbackToken VulkanTransferQueue::DownloadBuffer(RHI::BufferHandle src,
                                                       uint64_t size,
                                                       uint64_t offset,
                                                       RHI::ReadbackSink sink)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::DownloadBuffer", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::DownloadBuffer")};
    if (RefuseInsideGraphicsRecorder("DownloadBuffer"))
    {
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return {};
    }
    const auto source = PrepareDownload(src, size, offset, sink);
    if (!source)
        return {};
    std::scoped_lock record{LaneFor(TransferLane::Graphics).Record};
    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
    {
        ReleaseReadbackSlot(source->SlotIndex, 0u);
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return {};
    }
    return RecordAndSubmitDownload(cmd, *source, std::move(sink));
}

RHI::ReadbackToken VulkanTransferQueue::SubmitRecordedDownload(
    VkCommandBuffer cmd, RHI::BufferHandle src, uint64_t size, RHI::ReadbackSink sink, const TransferLane lane)
{
    // Completion-only compute shares the timeline/sink lifecycle, without a staging slot.
    if (src.IsValid() && size == 0u && sink.IsValidForSize(0u))
        return SubmitReadback(cmd, std::numeric_limits<size_t>::max(), 0u, std::move(sink), lane);
    const auto source = src.IsValid() ? PrepareDownload(src, size, 0u, sink, lane) : std::nullopt;
    if (!source)
    {
        FreeCommandBuffer(lane, cmd);
        return {};
    }
    return RecordAndSubmitDownload(cmd, *source, std::move(sink), lane);
}

RHI::ReadbackToken VulkanTransferQueue::DownloadTexture(RHI::TextureHandle src,
                                                        RHI::TextureLayout srcLayout,
                                                        uint32_t mipLevel,
                                                        uint32_t arrayLayer,
                                                        RHI::ReadbackSink sink)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::DownloadTexture", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::DownloadTexture")};
    if (RefuseInsideGraphicsRecorder("DownloadTexture"))
    {
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return {};
    }
    const auto drop = [this](const char* message) -> RHI::ReadbackToken
    {
        Core::Log::Warn("{}", message);
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return {};
    };

    if (!IsValid())
        return drop("[VulkanTransferQueue] DownloadTexture rejected; transfer service is invalid");
    if (!m_Images)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; image pool is unavailable");
    if (srcLayout != RHI::TextureLayout::TransferSrc)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; source layout must be TransferSrc");

    VulkanImage* img = m_Images->GetIfValid(src);
    if (!img || img->Image == VK_NULL_HANDLE)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; source texture handle is invalid");
    if ((img->Usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; source texture lacks transfer-src usage");
    const VkImageLayout vkSrcLayout = ToVkImageLayout(srcLayout);
    if (img->CurrentLayout != vkSrcLayout)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; tracked source layout is not TransferSrc");
    if (mipLevel >= img->MipLevels || arrayLayer >= img->ArrayLayers)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; mip level or array layer is out of range");
    if (RHI::IsDepthStencilFormat(img->RhiFormat) || !RHI::IsUploadableFormat(img->RhiFormat))
        return drop("[VulkanTransferQueue] DownloadTexture rejected; source format is not a supported color readback format");

    const bool supported2DArray = img->Dimension == RHI::TextureDimension::Tex2D && img->Depth == 1u;
    const bool supportedCube = img->Dimension == RHI::TextureDimension::TexCube &&
                               img->Depth == 1u &&
                               img->ArrayLayers == 6u;
    if (!supported2DArray && !supportedCube)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; only 2D color texture arrays and six-face cubemaps are supported by this slice");

    RHI::TextureDesc desc{};
    desc.Width = img->Width;
    desc.Height = img->Height;
    desc.DepthOrArrayLayers = img->ArrayLayers;
    desc.MipLevels = img->MipLevels;
    desc.Fmt = img->RhiFormat;
    desc.Dimension = img->Dimension;
    desc.Usage = RHI::TextureUsage::TransferSrc;

    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    if (!layoutOr.has_value())
        return drop("[VulkanTransferQueue] DownloadTexture rejected; texture layout computation failed");

    const RHI::TextureUploadSubresource* selected = nullptr;
    for (const RHI::TextureUploadSubresource& sub : layoutOr->Subresources)
    {
        if (sub.MipLevel == mipLevel && sub.ArrayLayer == arrayLayer)
        {
            selected = &sub;
            break;
        }
    }
    if (selected == nullptr || selected->SizeBytes == 0u)
        return drop("[VulkanTransferQueue] DownloadTexture rejected; requested subresource is not present in the layout");
    if (selected->SizeBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return drop("[VulkanTransferQueue] DownloadTexture rejected; requested subresource is too large for this host");
    if (!sink.IsValidForSize(selected->SizeBytes))
        return drop("[VulkanTransferQueue] DownloadTexture rejected; readback sink is invalid for the requested subresource size");

    const size_t slotIndex = AcquireReadbackSlot(selected->SizeBytes);
    if (slotIndex == std::numeric_limits<size_t>::max())
        return drop("[VulkanTransferQueue] DownloadTexture rejected; readback staging allocation failed");

    std::scoped_lock record{LaneFor(TransferLane::Graphics).Record};
    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
    {
        ReleaseReadbackSlot(slotIndex, 0u);
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return {};
    }

    const VkImageAspectFlags aspectMask = AspectFromFormat(img->Format);

    VkImageMemoryBarrier2 beforeCopy{};
    beforeCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    beforeCopy.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    beforeCopy.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    beforeCopy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    beforeCopy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    beforeCopy.oldLayout = vkSrcLayout;
    beforeCopy.newLayout = vkSrcLayout;
    beforeCopy.image = img->Image;
    beforeCopy.subresourceRange = VkImageSubresourceRange{.aspectMask = aspectMask,
                                                          .baseMipLevel = mipLevel,
                                                          .levelCount = 1u,
                                                          .baseArrayLayer = arrayLayer,
                                                          .layerCount = 1u};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = &beforeCopy;
    vkCmdPipelineBarrier2(cmd, &dependency);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0u;
    copy.imageSubresource = VkImageSubresourceLayers{.aspectMask = aspectMask,
                                                     .mipLevel = mipLevel,
                                                     .baseArrayLayer = arrayLayer,
                                                     .layerCount = 1u};
    copy.imageExtent = VkExtent3D{.width = selected->Width,
                                  .height = selected->Height,
                                  .depth = selected->Depth};
    {
        std::scoped_lock lock{m_Mutex};
        if (slotIndex >= m_ReadbackSlots.size() || m_ReadbackSlots[slotIndex].Buffer == VK_NULL_HANDLE)
        {
            Core::Log::Warn("[VulkanTransferQueue] DownloadTexture rejected; readback staging slot became invalid");
            if (slotIndex < m_ReadbackSlots.size())
                m_ReadbackSlots[slotIndex].InUse = false;
            vkFreeCommandBuffers(m_Device, LaneFor(TransferLane::Graphics).Pool, 1u, &cmd);
            m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
            return {};
        }
        vkCmdCopyImageToBuffer(cmd,
                               img->Image,
                               vkSrcLayout,
                               m_ReadbackSlots[slotIndex].Buffer,
                               1u,
                               &copy);
    }

    const RHI::ReadbackToken token = SubmitReadback(cmd,
                                                    slotIndex,
                                                    selected->SizeBytes,
                                                    std::move(sink));
    if (!token.IsValid())
    {
        ReleaseReadbackSlot(slotIndex, 0u);
        m_DownloadsDropped.fetch_add(1u, std::memory_order_relaxed);
        return {};
    }

    m_DownloadsQueued.fetch_add(1u, std::memory_order_relaxed);
    m_ReadbackBytesStaged.fetch_add(selected->SizeBytes, std::memory_order_relaxed);
    return token;
}

bool VulkanTransferQueue::IsComplete(RHI::TransferToken token) const
{
    if (!token.IsValid())
        return true;
    if (!IsValid())
        return false;
    return token.Value <= QueryCompletedValue();
}

bool VulkanTransferQueue::IsComplete(RHI::ReadbackToken token) const
{
    // A readback token completes when its sink ran (CollectCompleted); judged against its own lane.
    return IsLaneTicketComplete(token.Value,
        {LaneFor(TransferLane::Graphics).CompletedReadbackTicket.load(std::memory_order_acquire),
            LaneFor(TransferLane::Compute).CompletedReadbackTicket.load(std::memory_order_acquire)});
}

void VulkanTransferQueue::CollectCompleted()
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::CollectCompleted", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::CollectCompleted")};
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] CollectCompleted skipped; transfer service is invalid");
        return;
    }

    const uint64_t done = QueryCompletedValue();
    const uint64_t computeDone = QueryLaneCompletedValue(TransferLane::Compute);
    RetireCompletedCommandBuffers(TransferLane::Graphics, done);
    RetireCompletedCommandBuffers(TransferLane::Compute, computeDone);
    // Publish before delivery: a sink may immediately submit a graphics consumer of its completed result.
    uint64_t published = m_PublishedComputeValue.load(std::memory_order_relaxed);
    while (published < computeDone &&
        !m_PublishedComputeValue.compare_exchange_weak(
            published, computeDone, std::memory_order_release, std::memory_order_relaxed))
    {
    }
    DrainCompletedReadbacks({done, computeDone});
    m_Belt->GarbageCollect(done);
}

RHI::TransferQueueDiagnostics VulkanTransferQueue::GetDiagnostics() const noexcept
{
    return RHI::TransferQueueDiagnostics{
        .DownloadsQueued = m_DownloadsQueued.load(std::memory_order_relaxed),
        .DownloadsCompleted = m_DownloadsCompleted.load(std::memory_order_relaxed),
        .DownloadsDropped = m_DownloadsDropped.load(std::memory_order_relaxed),
        .ReadbackBytesStaged = m_ReadbackBytesStaged.load(std::memory_order_relaxed),
        .ReadbackRingHighWaterBytes = m_ReadbackRingHighWaterBytes.load(std::memory_order_relaxed),
        .ReadbackSlotsUncached = m_ReadbackSlotsUncached.load(std::memory_order_relaxed),
        .ReadbackHostBarriers = m_ReadbackHostBarriers.load(std::memory_order_relaxed),
        .ComputeLane = m_ComputeResolution,
        .ComputeSharesFrameComputeQueue = m_ComputeSharesFrameComputeQueue,
        .ComputeSharesFrameQueueLock = m_ComputeSharesFrameQueueLock,
        .ComputeSubmissions = m_ComputeSubmissions.load(std::memory_order_relaxed),
        .ComputeGraphicsWaits = m_ComputeGraphicsWaits.load(std::memory_order_relaxed),
        .ComputeHostBarriers = m_ComputeHostBarriers.load(std::memory_order_relaxed),
        .ComputeVisibilityWaits = m_ComputeVisibilityWaits.load(std::memory_order_relaxed),
    };
}

} // namespace Extrinsic::Backends::Vulkan
