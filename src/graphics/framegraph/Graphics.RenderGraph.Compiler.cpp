module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <sstream>
#include <stack>
#include <ranges>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

module Extrinsic.Graphics.RenderGraph;

import :Barriers;
import :Compiler;

import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.Core.Error;

namespace Extrinsic::Graphics
{
    namespace
    {
        thread_local RenderGraphValidationResult g_LastCompileValidationResult{};

        [[nodiscard]] constexpr TextureBarrierState ToTextureBarrierState(const TextureUsage usage)
        {
            switch (usage)
            {
            case TextureUsage::ColorAttachmentRead:
            case TextureUsage::ColorAttachmentWrite: return TextureBarrierState::ColorAttachmentWrite;
            case TextureUsage::DepthRead: return TextureBarrierState::DepthRead;
            case TextureUsage::DepthWrite: return TextureBarrierState::DepthWrite;
            case TextureUsage::ShaderRead: return TextureBarrierState::ShaderRead;
            case TextureUsage::ShaderWrite: return TextureBarrierState::ShaderWrite;
            case TextureUsage::TransferSrc: return TextureBarrierState::TransferSrc;
            case TextureUsage::TransferDst: return TextureBarrierState::TransferDst;
            case TextureUsage::Present: return TextureBarrierState::Present;
            }
            return TextureBarrierState::Undefined;
        }

        [[nodiscard]] constexpr TextureBarrierState ToTextureBarrierState(const TextureState state)
        {
            switch (state)
            {
            case TextureState::Undefined: return TextureBarrierState::Undefined;
            case TextureState::ShaderRead: return TextureBarrierState::ShaderRead;
            case TextureState::ShaderWrite: return TextureBarrierState::ShaderWrite;
            case TextureState::ColorAttachmentWrite: return TextureBarrierState::ColorAttachmentWrite;
            case TextureState::DepthWrite: return TextureBarrierState::DepthWrite;
            case TextureState::TransferSrc: return TextureBarrierState::TransferSrc;
            case TextureState::TransferDst: return TextureBarrierState::TransferDst;
            case TextureState::Present: return TextureBarrierState::Present;
            }
            return TextureBarrierState::Undefined;
        }

        [[nodiscard]] constexpr BufferBarrierState ToBufferBarrierState(const BufferUsage usage)
        {
            switch (usage)
            {
            case BufferUsage::IndirectRead: return BufferBarrierState::IndirectRead;
            case BufferUsage::IndexRead: return BufferBarrierState::IndexRead;
            case BufferUsage::VertexRead: return BufferBarrierState::VertexRead;
            case BufferUsage::ShaderRead: return BufferBarrierState::ShaderRead;
            case BufferUsage::ShaderWrite: return BufferBarrierState::ShaderWrite;
            case BufferUsage::TransferSrc: return BufferBarrierState::TransferSrc;
            case BufferUsage::TransferDst: return BufferBarrierState::TransferDst;
            case BufferUsage::HostReadback: return BufferBarrierState::HostReadback;
            }
            return BufferBarrierState::Undefined;
        }

        [[nodiscard]] constexpr BufferBarrierState ToBufferBarrierState(const BufferState state)
        {
            switch (state)
            {
            case BufferState::Undefined: return BufferBarrierState::Undefined;
            case BufferState::ShaderRead: return BufferBarrierState::ShaderRead;
            case BufferState::ShaderWrite: return BufferBarrierState::ShaderWrite;
            case BufferState::VertexRead: return BufferBarrierState::VertexRead;
            case BufferState::IndexRead: return BufferBarrierState::IndexRead;
            case BufferState::IndirectRead: return BufferBarrierState::IndirectRead;
            case BufferState::TransferSrc: return BufferBarrierState::TransferSrc;
            case BufferState::TransferDst: return BufferBarrierState::TransferDst;
            case BufferState::HostReadback: return BufferBarrierState::HostReadback;
            }
            return BufferBarrierState::Undefined;
        }

        struct ResourceState
        {
            std::int32_t LastWriter = -1;
            std::vector<std::uint32_t> Readers{};
            std::int32_t LastAccessor = -1;
            RenderQueue LastAccessorQueue = RenderQueue::Graphics;
        };

        struct QueueHandoffDependency
        {
            std::uint32_t From = 0;
            std::uint32_t To = 0;
            RenderQueue FromQueue = RenderQueue::Graphics;
            RenderQueue ToQueue = RenderQueue::Graphics;
        };

        [[nodiscard]] bool ContainsSorted(const std::vector<std::uint32_t>& values, const std::uint32_t needle)
        {
            return std::binary_search(values.begin(), values.end(), needle);
        }

        bool AddEdge(const std::uint32_t from,
                     const std::uint32_t to,
                     std::vector<std::vector<std::uint32_t>>& adjacency,
                     std::vector<std::uint32_t>& indegree,
                     std::unordered_set<std::uint64_t>& dedup);

        [[nodiscard]] bool MaybeAddQueueHandoffEdge(const std::uint32_t passIndex,
                                                    const RenderQueue queue,
                                                    const ResourceState& state,
                                                    std::vector<std::vector<std::uint32_t>>& adjacency,
                                                    std::vector<std::uint32_t>& indegree,
                                                    std::unordered_set<std::uint64_t>& dedup,
                                                    std::vector<QueueHandoffDependency>& queueHandoffs)
        {
            if (state.LastAccessor < 0 || state.LastAccessorQueue == queue)
            {
                return false;
            }
            const std::uint32_t from = static_cast<std::uint32_t>(state.LastAccessor);
            const bool added = AddEdge(from, passIndex, adjacency, indegree, dedup);
            if (added)
            {
                queueHandoffs.push_back(QueueHandoffDependency{
                    .From = from,
                    .To = passIndex,
                    .FromQueue = state.LastAccessorQueue,
                    .ToQueue = queue,
                });
            }
            return added;
        }

        constexpr std::uint64_t EncodeEdge(const std::uint32_t from, const std::uint32_t to)
        {
            return (static_cast<std::uint64_t>(from) << 32ull) | static_cast<std::uint64_t>(to);
        }

        bool AddEdge(const std::uint32_t from,
                     const std::uint32_t to,
                     std::vector<std::vector<std::uint32_t>>& adjacency,
                     std::vector<std::uint32_t>& indegree,
                     std::unordered_set<std::uint64_t>& dedup)
        {
            if (from == to)
            {
                return false;
            }
            const std::uint64_t key = EncodeEdge(from, to);
            if (!dedup.insert(key).second)
            {
                return false;
            }
            adjacency[from].push_back(to);
            ++indegree[to];
            return true;
        }

        void ProcessRead(const std::uint32_t passIndex,
                         const RenderQueue queue,
                         std::uint32_t& queueHandoffEdgeCount,
                         ResourceState& state,
                         std::vector<std::vector<std::uint32_t>>& adjacency,
                         std::vector<std::uint32_t>& indegree,
                         std::unordered_set<std::uint64_t>& dedup,
                         std::vector<QueueHandoffDependency>& queueHandoffs)
        {
            if (MaybeAddQueueHandoffEdge(passIndex, queue, state, adjacency, indegree, dedup, queueHandoffs))
            {
                ++queueHandoffEdgeCount;
            }
            if (state.LastWriter >= 0)
            {
                AddEdge(static_cast<std::uint32_t>(state.LastWriter), passIndex, adjacency, indegree, dedup);
            }
            state.Readers.push_back(passIndex);
            state.LastAccessor = static_cast<std::int32_t>(passIndex);
            state.LastAccessorQueue = queue;
        }

        void ProcessWrite(const std::uint32_t passIndex,
                          const RenderQueue queue,
                          std::uint32_t& queueHandoffEdgeCount,
                          ResourceState& state,
                          std::vector<std::vector<std::uint32_t>>& adjacency,
                          std::vector<std::uint32_t>& indegree,
                          std::unordered_set<std::uint64_t>& dedup,
                          std::vector<QueueHandoffDependency>& queueHandoffs)
        {
            if (MaybeAddQueueHandoffEdge(passIndex, queue, state, adjacency, indegree, dedup, queueHandoffs))
            {
                ++queueHandoffEdgeCount;
            }
            if (state.LastWriter >= 0)
            {
                AddEdge(static_cast<std::uint32_t>(state.LastWriter), passIndex, adjacency, indegree, dedup);
            }
            for (const std::uint32_t reader : state.Readers)
            {
                AddEdge(reader, passIndex, adjacency, indegree, dedup);
            }
            state.Readers.clear();
            state.LastWriter = static_cast<std::int32_t>(passIndex);
            state.LastAccessor = static_cast<std::int32_t>(passIndex);
            state.LastAccessorQueue = queue;
        }

        void UpdateLifetime(ResourceLifetime& lifetime, const std::uint32_t passIndex)
        {
            if (!lifetime.HasUse)
            {
                lifetime.HasUse = true;
                lifetime.FirstUsePass = passIndex;
                lifetime.LastUsePass = passIndex;
                return;
            }

            lifetime.FirstUsePass = std::min(lifetime.FirstUsePass, passIndex);
            lifetime.LastUsePass = std::max(lifetime.LastUsePass, passIndex);
        }

        [[nodiscard]] constexpr std::uint32_t InvalidValidationIndex()
        {
            return std::numeric_limits<std::uint32_t>::max();
        }

        [[nodiscard]] bool BoolAt(const std::vector<bool>& values, const std::uint32_t index)
        {
            return index < values.size() && values[index];
        }

        template <typename T>
        [[nodiscard]] const T* ValueAt(const std::vector<T>& values, const std::uint32_t index)
        {
            return index < values.size() ? &values[index] : nullptr;
        }

        [[nodiscard]] std::string PassNameFor(const CompiledRenderGraph& compiled, const std::uint32_t passIndex)
        {
            if (const std::string* byIndex = ValueAt(compiled.PassNames, passIndex))
            {
                return *byIndex;
            }
            const auto orderIt = std::ranges::find(compiled.TopologicalOrder, passIndex);
            if (orderIt != compiled.TopologicalOrder.end())
            {
                const auto orderIndex = static_cast<std::uint32_t>(std::distance(compiled.TopologicalOrder.begin(), orderIt));
                if (const std::string* byOrder = ValueAt(compiled.PassNames, orderIndex))
                {
                    return *byOrder;
                }
            }
            return {};
        }

        [[nodiscard]] std::string ResourceNameFor(const CompiledRenderGraph& compiled,
                                                  const bool isTexture,
                                                  const std::uint32_t resourceIndex)
        {
            if (isTexture)
            {
                if (const std::string* name = ValueAt(compiled.TextureNames, resourceIndex))
                {
                    return *name;
                }
                return {};
            }
            if (const std::string* name = ValueAt(compiled.BufferNames, resourceIndex))
            {
                return *name;
            }
            return {};
        }

        void AddFinding(RenderGraphValidationResult& result,
                        const RenderGraphValidationSeverity severity,
                        const RenderGraphValidationCode code,
                        std::string message,
                        const CompiledRenderGraph& compiled,
                        const std::uint32_t passIndex = InvalidValidationIndex(),
                        const bool isTexture = true,
                        const std::uint32_t resourceIndex = InvalidValidationIndex())
        {
            result.Findings.push_back(RenderGraphValidationFinding{
                .Severity = severity,
                .Code = code,
                .Message = std::move(message),
                .PassIndex = passIndex,
                .PassName = passIndex == InvalidValidationIndex() ? std::string{} : PassNameFor(compiled, passIndex),
                .ResourceIndex = resourceIndex,
                .IsTextureResource = isTexture,
                .ResourceName = resourceIndex == InvalidValidationIndex() ? std::string{} : ResourceNameFor(compiled, isTexture, resourceIndex),
            });
        }

        void SortValidationFindings(RenderGraphValidationResult& result);

        void AddFinding(RenderGraphValidationResult& result,
                        const RenderGraphValidationSeverity severity,
                        const RenderGraphValidationCode code,
                        std::string message,
                        const std::uint32_t passIndex,
                        std::string passName,
                        const bool isTexture = true,
                        const std::uint32_t resourceIndex = InvalidValidationIndex(),
                        std::string resourceName = {})
        {
            result.Findings.push_back(RenderGraphValidationFinding{
                .Severity = severity,
                .Code = code,
                .Message = std::move(message),
                .PassIndex = passIndex,
                .PassName = std::move(passName),
                .ResourceIndex = resourceIndex,
                .IsTextureResource = isTexture,
                .ResourceName = std::move(resourceName),
            });
        }

        void SetLastCompileFinding(const RenderGraphValidationCode code,
                                   std::string message,
                                   const std::uint32_t passIndex,
                                   std::string passName,
                                   const bool isTexture = true,
                                   const std::uint32_t resourceIndex = InvalidValidationIndex(),
                                   std::string resourceName = {})
        {
            g_LastCompileValidationResult.Findings.clear();
            AddFinding(g_LastCompileValidationResult,
                       RenderGraphValidationSeverity::Error,
                       code,
                       std::move(message),
                       passIndex,
                       std::move(passName),
                       isTexture,
                       resourceIndex,
                       std::move(resourceName));
            SortValidationFindings(g_LastCompileValidationResult);
        }

        [[nodiscard]] const ImportedResourceAuthorization* FindAuthorization(
            const std::span<const ImportedResourceAuthorization> authorizations,
            const bool isTexture,
            const std::uint32_t resourceIndex)
        {
            const auto it = std::ranges::find_if(authorizations, [isTexture, resourceIndex](const ImportedResourceAuthorization& auth) {
                return auth.IsTexture == isTexture && auth.ResourceIndex == resourceIndex;
            });
            return it == authorizations.end() ? nullptr : &(*it);
        }

        [[nodiscard]] bool ContainsPassName(const std::vector<std::string>& names, const std::string& passName)
        {
            return std::ranges::find(names, passName) != names.end();
        }

        void SortValidationFindings(RenderGraphValidationResult& result)
        {
            std::ranges::stable_sort(result.Findings, [](const RenderGraphValidationFinding& lhs,
                                                         const RenderGraphValidationFinding& rhs) {
                return std::tie(lhs.Severity, lhs.Code, lhs.PassIndex, lhs.ResourceIndex, lhs.IsTextureResource, lhs.PassName,
                                lhs.ResourceName, lhs.Message) <
                       std::tie(rhs.Severity, rhs.Code, rhs.PassIndex, rhs.ResourceIndex, rhs.IsTextureResource, rhs.PassName,
                                rhs.ResourceName, rhs.Message);
            });
        }

        [[nodiscard]] constexpr const char* ToString(const RenderQueue queue)
        {
            switch (queue)
            {
            case RenderQueue::Graphics: return "graphics";
            case RenderQueue::AsyncCompute: return "async_compute";
            case RenderQueue::Transfer: return "transfer";
            }
            return "unknown";
        }

        [[nodiscard]] constexpr const char* ToString(const TextureState state)
        {
            switch (state)
            {
            case TextureState::Undefined: return "Undefined";
            case TextureState::ShaderRead: return "ShaderRead";
            case TextureState::ShaderWrite: return "ShaderWrite";
            case TextureState::ColorAttachmentWrite: return "ColorAttachmentWrite";
            case TextureState::DepthWrite: return "DepthWrite";
            case TextureState::TransferSrc: return "TransferSrc";
            case TextureState::TransferDst: return "TransferDst";
            case TextureState::Present: return "Present";
            }
            return "Unknown";
        }

        [[nodiscard]] constexpr const char* ToString(const BufferState state)
        {
            switch (state)
            {
            case BufferState::Undefined: return "Undefined";
            case BufferState::ShaderRead: return "ShaderRead";
            case BufferState::ShaderWrite: return "ShaderWrite";
            case BufferState::VertexRead: return "VertexRead";
            case BufferState::IndexRead: return "IndexRead";
            case BufferState::IndirectRead: return "IndirectRead";
            case BufferState::TransferSrc: return "TransferSrc";
            case BufferState::TransferDst: return "TransferDst";
            case BufferState::HostReadback: return "HostReadback";
            }
            return "Unknown";
        }

        [[nodiscard]] constexpr const char* ToString(const RHI::LoadOp op)
        {
            switch (op)
            {
            case RHI::LoadOp::Load: return "load";
            case RHI::LoadOp::Clear: return "clear";
            case RHI::LoadOp::DontCare: return "dont_care";
            }
            return "unknown";
        }

        [[nodiscard]] constexpr const char* ToString(const RHI::StoreOp op)
        {
            switch (op)
            {
            case RHI::StoreOp::Store: return "store";
            case RHI::StoreOp::DontCare: return "dont_care";
            }
            return "unknown";
        }

        [[nodiscard]] constexpr const char* ToString(const RHI::Format format)
        {
            switch (format)
            {
            case RHI::Format::Undefined: return "Undefined";
            case RHI::Format::R8_UNORM: return "R8_UNORM";
            case RHI::Format::RG8_UNORM: return "RG8_UNORM";
            case RHI::Format::RGBA8_UNORM: return "RGBA8_UNORM";
            case RHI::Format::RGBA8_SRGB: return "RGBA8_SRGB";
            case RHI::Format::BGRA8_UNORM: return "BGRA8_UNORM";
            case RHI::Format::BGRA8_SRGB: return "BGRA8_SRGB";
            case RHI::Format::R16_FLOAT: return "R16_FLOAT";
            case RHI::Format::RG16_FLOAT: return "RG16_FLOAT";
            case RHI::Format::RGBA16_FLOAT: return "RGBA16_FLOAT";
            case RHI::Format::R16_UINT: return "R16_UINT";
            case RHI::Format::R16_UNORM: return "R16_UNORM";
            case RHI::Format::R32_FLOAT: return "R32_FLOAT";
            case RHI::Format::RG32_FLOAT: return "RG32_FLOAT";
            case RHI::Format::RGB32_FLOAT: return "RGB32_FLOAT";
            case RHI::Format::RGBA32_FLOAT: return "RGBA32_FLOAT";
            case RHI::Format::R32_UINT: return "R32_UINT";
            case RHI::Format::R32_SINT: return "R32_SINT";
            case RHI::Format::D16_UNORM: return "D16_UNORM";
            case RHI::Format::D32_FLOAT: return "D32_FLOAT";
            case RHI::Format::D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
            case RHI::Format::D32_FLOAT_S8_UINT: return "D32_FLOAT_S8_UINT";
            case RHI::Format::BC1_UNORM: return "BC1_UNORM";
            case RHI::Format::BC3_UNORM: return "BC3_UNORM";
            case RHI::Format::BC5_UNORM: return "BC5_UNORM";
            case RHI::Format::BC7_UNORM: return "BC7_UNORM";
            case RHI::Format::BC7_SRGB: return "BC7_SRGB";
            }
            return "Unknown";
        }

        [[nodiscard]] constexpr std::size_t QueueIndex(const RenderQueue queue) noexcept
        {
            switch (queue)
            {
            case RenderQueue::Graphics:
                return 0u;
            case RenderQueue::AsyncCompute:
                return 1u;
            case RenderQueue::Transfer:
                return 2u;
            }
            return 0u;
        }

        [[nodiscard]] std::uint32_t RankForPass(const std::vector<std::uint32_t>& ranks,
                                                const std::uint32_t passIndex) noexcept
        {
            return passIndex < ranks.size() ? ranks[passIndex] : std::numeric_limits<std::uint32_t>::max();
        }

        [[nodiscard]] bool HasCrossQueueCycleEdge(const std::vector<QueueHandoffDependency>& queueHandoffs,
                                                  const std::vector<bool>& live,
                                                  const std::vector<std::uint32_t>& liveIndegree)
        {
            return std::ranges::any_of(queueHandoffs, [&live, &liveIndegree](const QueueHandoffDependency& edge) {
                return edge.From < live.size() && edge.To < live.size() &&
                       edge.From < liveIndegree.size() && edge.To < liveIndegree.size() &&
                       live[edge.From] && live[edge.To] &&
                       liveIndegree[edge.From] > 0u && liveIndegree[edge.To] > 0u;
            });
        }

        struct TimelineSignalAssignment
        {
            std::uint32_t PassIndex = 0;
            RenderQueue Queue = RenderQueue::Graphics;
            std::uint64_t Value = 0;
        };

        [[nodiscard]] std::uint64_t FindTimelineSignalValue(const std::vector<TimelineSignalAssignment>& assignments,
                                                            const std::uint32_t passIndex,
                                                            const RenderQueue queue)
        {
            const auto it = std::ranges::find_if(assignments, [passIndex, queue](const TimelineSignalAssignment& assignment) {
                return assignment.PassIndex == passIndex && assignment.Queue == queue;
            });
            return it == assignments.end() ? 0u : it->Value;
        }

        void SynthesizeCrossQueueTimeline(const std::vector<QueueHandoffDependency>& queueHandoffs,
                                          const std::vector<bool>& live,
                                          const std::vector<std::uint32_t>& topologicalRankByPass,
                                          std::vector<CrossQueueTimelineSignal>& signals,
                                          std::vector<CrossQueueTimelineWait>& waits,
                                          std::vector<CrossQueueTimelineEdge>& edges)
        {
            std::vector<QueueHandoffDependency> activeHandoffs{};
            activeHandoffs.reserve(queueHandoffs.size());
            for (const QueueHandoffDependency& handoff : queueHandoffs)
            {
                if (handoff.From < live.size() && handoff.To < live.size() && live[handoff.From] && live[handoff.To])
                {
                    activeHandoffs.push_back(handoff);
                }
            }

            std::ranges::stable_sort(activeHandoffs, [&topologicalRankByPass](const QueueHandoffDependency& lhs,
                                                                              const QueueHandoffDependency& rhs) {
                return std::tuple{RankForPass(topologicalRankByPass, lhs.From), lhs.From, lhs.To} <
                       std::tuple{RankForPass(topologicalRankByPass, rhs.From), rhs.From, rhs.To};
            });
            activeHandoffs.erase(std::unique(activeHandoffs.begin(),
                                             activeHandoffs.end(),
                                             [](const QueueHandoffDependency& lhs, const QueueHandoffDependency& rhs) {
                                                 return lhs.From == rhs.From && lhs.To == rhs.To;
                                             }),
                                 activeHandoffs.end());

            std::vector<TimelineSignalAssignment> assignments{};
            assignments.reserve(activeHandoffs.size());
            std::array<std::uint64_t, 3u> nextValueByQueue{};

            for (const QueueHandoffDependency& handoff : activeHandoffs)
            {
                const bool exists = std::ranges::any_of(assignments, [&handoff](const TimelineSignalAssignment& assignment) {
                    return assignment.PassIndex == handoff.From && assignment.Queue == handoff.FromQueue;
                });
                if (exists)
                {
                    continue;
                }

                const std::uint64_t value = ++nextValueByQueue[QueueIndex(handoff.FromQueue)];
                assignments.push_back(TimelineSignalAssignment{
                    .PassIndex = handoff.From,
                    .Queue = handoff.FromQueue,
                    .Value = value,
                });
                signals.push_back(CrossQueueTimelineSignal{
                    .PassIndex = handoff.From,
                    .Queue = handoff.FromQueue,
                    .Value = value,
                });
            }

            std::ranges::stable_sort(activeHandoffs, [&topologicalRankByPass](const QueueHandoffDependency& lhs,
                                                                              const QueueHandoffDependency& rhs) {
                return std::tuple{RankForPass(topologicalRankByPass, lhs.To), lhs.To, lhs.From} <
                       std::tuple{RankForPass(topologicalRankByPass, rhs.To), rhs.To, rhs.From};
            });

            waits.reserve(activeHandoffs.size());
            edges.reserve(activeHandoffs.size());
            for (const QueueHandoffDependency& handoff : activeHandoffs)
            {
                const std::uint64_t value = FindTimelineSignalValue(assignments, handoff.From, handoff.FromQueue);
                waits.push_back(CrossQueueTimelineWait{
                    .PassIndex = handoff.To,
                    .Queue = handoff.ToQueue,
                    .SignalPassIndex = handoff.From,
                    .SignalQueue = handoff.FromQueue,
                    .Value = value,
                });
                edges.push_back(CrossQueueTimelineEdge{
                    .SignalPassIndex = handoff.From,
                    .WaitPassIndex = handoff.To,
                    .SignalQueue = handoff.FromQueue,
                    .WaitQueue = handoff.ToQueue,
                    .Value = value,
                });
            }
        }
    }

    bool RenderGraphValidationResult::HasErrors() const
    {
        return CountBySeverity(RenderGraphValidationSeverity::Error) != 0u;
    }

    bool RenderGraphValidationResult::HasWarnings() const
    {
        return CountBySeverity(RenderGraphValidationSeverity::Warning) != 0u;
    }

    std::size_t RenderGraphValidationResult::CountBySeverity(const RenderGraphValidationSeverity severity) const
    {
        return static_cast<std::size_t>(std::ranges::count_if(Findings, [severity](const RenderGraphValidationFinding& finding) {
            return finding.Severity == severity;
        }));
    }

    QueuePartition PartitionPassesByQueue(const std::span<const RenderQueue> passQueues,
                                          const std::span<const std::uint32_t> livePasses,
                                          const std::span<const std::uint32_t> topologicalRankByPass,
                                          const RHI::QueueCapabilityProfile profile)
    {
        std::vector<std::uint32_t> orderedPasses{};
        orderedPasses.reserve(livePasses.size());
        for (const std::uint32_t passIndex : livePasses)
        {
            if (passIndex < passQueues.size())
            {
                orderedPasses.push_back(passIndex);
            }
        }

        const auto rankFor = [topologicalRankByPass](const std::uint32_t passIndex) {
            return passIndex < topologicalRankByPass.size()
                       ? topologicalRankByPass[passIndex]
                       : std::numeric_limits<std::uint32_t>::max();
        };

        std::ranges::stable_sort(orderedPasses, [rankFor](const std::uint32_t lhs, const std::uint32_t rhs) {
            return std::pair{rankFor(lhs), lhs} < std::pair{rankFor(rhs), rhs};
        });

        QueuePartition partition{};
        for (const std::uint32_t passIndex : orderedPasses)
        {
            const RenderQueue requested = passQueues[passIndex];
            const RHI::QueueAffinityResolution resolution = RHI::ResolveQueueAffinity(requested, profile);
            QueuePartitionedPass entry{
                .PassIndex = passIndex,
                .TopologicalRank = rankFor(passIndex),
                .Requested = resolution.Requested,
                .Resolved = resolution.Resolved,
                .Demoted = resolution.Demoted,
            };

            if (entry.Demoted)
            {
                ++partition.QueueAffinityDemotedCount;
            }

            switch (entry.Resolved)
            {
            case RenderQueue::Graphics:
                partition.Graphics.push_back(std::move(entry));
                break;
            case RenderQueue::AsyncCompute:
                partition.AsyncCompute.push_back(std::move(entry));
                break;
            case RenderQueue::Transfer:
                partition.Transfer.push_back(std::move(entry));
                break;
            }
        }

        return partition;
    }

    QueuePartition PartitionPassesByQueue(const CompiledRenderGraph& compiled,
                                          const RHI::QueueCapabilityProfile profile)
    {
        return PartitionPassesByQueue(compiled.PassQueues,
                                      compiled.TopologicalOrder,
                                      compiled.TopologicalLayerByPass,
                                      profile);
    }

    bool CompiledPassDeclarations::DeclaresTextureRead(const TextureRef ref) const
    {
        if (!ref.IsValid())
        {
            return false;
        }
        return ContainsSorted(ReadTextures, ref.Index);
    }

    bool CompiledPassDeclarations::DeclaresTextureWrite(const TextureRef ref) const
    {
        if (!ref.IsValid())
        {
            return false;
        }
        return ContainsSorted(WriteTextures, ref.Index);
    }

    bool CompiledPassDeclarations::DeclaresBufferRead(const BufferRef ref) const
    {
        if (!ref.IsValid())
        {
            return false;
        }
        return ContainsSorted(ReadBuffers, ref.Index);
    }

    bool CompiledPassDeclarations::DeclaresBufferWrite(const BufferRef ref) const
    {
        if (!ref.IsValid())
        {
            return false;
        }
        return ContainsSorted(WriteBuffers, ref.Index);
    }

    Core::Result CompiledPassDeclarations::RequireTextureRead(const TextureRef ref) const
    {
        return DeclaresTextureRead(ref) ? Core::Ok() : Core::Err(Core::ErrorCode::InvalidArgument);
    }

    Core::Result CompiledPassDeclarations::RequireTextureWrite(const TextureRef ref) const
    {
        return DeclaresTextureWrite(ref) ? Core::Ok() : Core::Err(Core::ErrorCode::InvalidArgument);
    }

    Core::Result CompiledPassDeclarations::RequireBufferRead(const BufferRef ref) const
    {
        return DeclaresBufferRead(ref) ? Core::Ok() : Core::Err(Core::ErrorCode::InvalidArgument);
    }

    Core::Result CompiledPassDeclarations::RequireBufferWrite(const BufferRef ref) const
    {
        return DeclaresBufferWrite(ref) ? Core::Ok() : Core::Err(Core::ErrorCode::InvalidArgument);
    }

    Core::Expected<CompiledRenderGraph> RenderGraphCompiler::Compile(
        const std::span<const RenderPassRecord> passes,
        const std::span<const TextureResourceDesc> textures,
        const std::span<const BufferResourceDesc> buffers)
    {
        g_LastCompileValidationResult.Findings.clear();
        const std::uint32_t passCount = static_cast<std::uint32_t>(passes.size());
        const std::uint32_t resourceCount = static_cast<std::uint32_t>(textures.size() + buffers.size());
        if (passCount == 0)
        {
            CompiledRenderGraph compiled{};
            compiled.PassCount = passCount;
            compiled.ResourceCount = resourceCount;
            compiled.TextureNames.resize(textures.size());
            compiled.BufferNames.resize(buffers.size());
            compiled.TextureLifetimes.resize(textures.size());
            compiled.BufferLifetimes.resize(buffers.size());
            compiled.TextureInitialStates.resize(textures.size(), TextureState::Undefined);
            compiled.TextureFinalStates.resize(textures.size(), TextureState::Undefined);
            compiled.BufferInitialStates.resize(buffers.size(), BufferState::Undefined);
            compiled.BufferFinalStates.resize(buffers.size(), BufferState::Undefined);
            compiled.TextureHandles.resize(textures.size());
            compiled.BufferHandles.resize(buffers.size());
            compiled.TextureImported.resize(textures.size(), false);
            compiled.TextureIsBackbuffer.resize(textures.size(), false);
            compiled.BufferImported.resize(buffers.size(), false);

            for (std::uint32_t textureIndex = 0; textureIndex < textures.size(); ++textureIndex)
            {
                compiled.TextureNames[textureIndex] = textures[textureIndex].Name;
                compiled.TextureInitialStates[textureIndex] = textures[textureIndex].InitialState;
                compiled.TextureFinalStates[textureIndex] = textures[textureIndex].FinalState;
                compiled.TextureImported[textureIndex] = textures[textureIndex].Imported;
                compiled.TextureIsBackbuffer[textureIndex] = textures[textureIndex].IsBackbuffer;
                if (textures[textureIndex].Imported)
                {
                    compiled.TextureHandles[textureIndex] = textures[textureIndex].ImportedHandle;
                }
            }
            for (std::uint32_t bufferIndex = 0; bufferIndex < buffers.size(); ++bufferIndex)
            {
                compiled.BufferNames[bufferIndex] = buffers[bufferIndex].Name;
                compiled.BufferInitialStates[bufferIndex] = buffers[bufferIndex].InitialState;
                compiled.BufferFinalStates[bufferIndex] = buffers[bufferIndex].FinalState;
                compiled.BufferImported[bufferIndex] = buffers[bufferIndex].Imported;
                if (buffers[bufferIndex].Imported)
                {
                    compiled.BufferHandles[bufferIndex] = buffers[bufferIndex].ImportedHandle;
                }
            }
            RenderGraphValidationResult validation = ValidateCompiledGraph(compiled);
            compiled.ValidationFindings = validation.Findings;
            g_LastCompileValidationResult = std::move(validation);
            return compiled;
        }

        std::vector<ResourceState> textureStates(textures.size());
        std::vector<ResourceState> bufferStates(buffers.size());
        std::vector<RHI::TextureHandle> textureHandles(textures.size());
        std::vector<RHI::BufferHandle> bufferHandles(buffers.size());
        std::vector<bool> textureImported(textures.size(), false);
        std::vector<bool> textureIsBackbuffer(textures.size(), false);
        std::vector<bool> bufferImported(buffers.size(), false);
        std::vector<std::string> textureNames(textures.size());
        std::vector<std::string> bufferNames(buffers.size());
        std::vector<TextureState> textureInitialStates(textures.size(), TextureState::Undefined);
        std::vector<TextureState> textureFinalStates(textures.size(), TextureState::Undefined);
        std::vector<BufferState> bufferInitialStates(buffers.size(), BufferState::Undefined);
        std::vector<BufferState> bufferFinalStates(buffers.size(), BufferState::Undefined);
        std::vector<std::string> passNames(passCount);
        std::vector<RenderQueue> passQueues(passCount, RenderQueue::Graphics);
        std::vector<bool> passSideEffects(passCount, false);
        std::vector<CompiledRenderPassAttachment> renderPassAttachments{};
        std::vector<ResourceLifetime> textureLifetimes(textures.size());
        std::vector<ResourceLifetime> bufferLifetimes(buffers.size());
        std::vector<std::vector<std::uint32_t>> adjacency(passCount);
        std::vector<std::vector<std::uint32_t>> reverseAdjacency(passCount);
        std::vector<std::uint32_t> indegree(passCount, 0u);
        std::vector<CompiledPassDeclarations> passDeclarations(passCount);
        std::unordered_set<std::uint64_t> dedup{};
        dedup.reserve(static_cast<std::size_t>(passCount) * 4u);
        std::vector<QueueHandoffDependency> queueHandoffs{};
        std::uint32_t queueHandoffEdgeCount = 0;

        for (std::uint32_t textureIndex = 0; textureIndex < textures.size(); ++textureIndex)
        {
            textureNames[textureIndex] = textures[textureIndex].Name;
            textureInitialStates[textureIndex] = textures[textureIndex].InitialState;
            textureFinalStates[textureIndex] = textures[textureIndex].FinalState;
            textureIsBackbuffer[textureIndex] = textures[textureIndex].IsBackbuffer;
            if (textures[textureIndex].Imported)
            {
                textureHandles[textureIndex] = textures[textureIndex].ImportedHandle;
                textureImported[textureIndex] = true;
            }
        }
        for (std::uint32_t bufferIndex = 0; bufferIndex < buffers.size(); ++bufferIndex)
        {
            bufferNames[bufferIndex] = buffers[bufferIndex].Name;
            bufferInitialStates[bufferIndex] = buffers[bufferIndex].InitialState;
            bufferFinalStates[bufferIndex] = buffers[bufferIndex].FinalState;
            if (buffers[bufferIndex].Imported)
            {
                bufferHandles[bufferIndex] = buffers[bufferIndex].ImportedHandle;
                bufferImported[bufferIndex] = true;
            }
        }

        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const RenderPassRecord& pass = passes[passIndex];
            passNames[passIndex] = pass.Name;
            passQueues[passIndex] = pass.Queue;
            passSideEffects[passIndex] = pass.SideEffect;
            passDeclarations[passIndex].PassIndex = passIndex;
            for (const PassRef dependency : pass.ExplicitDependencies)
            {
                if (!dependency.IsValid() || dependency.Index >= passCount)
                {
                    SetLastCompileFinding(RenderGraphValidationCode::InvalidExplicitDependency,
                                          "RenderGraph explicit dependency references an invalid pass: pass=\"" +
                                              pass.Name + "\" depends_on=" + std::to_string(dependency.Index) + ".",
                                          passIndex,
                                          pass.Name);
                    return std::unexpected(Core::ErrorCode::InvalidArgument);
                }
                AddEdge(dependency.Index, passIndex, adjacency, indegree, dedup);
            }

            for (const TextureAccess& access : pass.TextureAccesses)
            {
                if (access.Ref.Index >= textureStates.size())
                {
                    SetLastCompileFinding(RenderGraphValidationCode::InvalidTextureAccess,
                                          "RenderGraph texture access references an invalid texture resource: pass=\"" +
                                              pass.Name + "\" texture_index=" + std::to_string(access.Ref.Index) + ".",
                                          passIndex,
                                          pass.Name,
                                          true,
                                          access.Ref.Index);
                    return std::unexpected(Core::ErrorCode::OutOfRange);
                }
                if (access.Write)
                {
                    ProcessWrite(passIndex, pass.Queue, queueHandoffEdgeCount, textureStates[access.Ref.Index], adjacency, indegree, dedup, queueHandoffs);
                    passDeclarations[passIndex].WriteTextures.push_back(access.Ref.Index);
                }
                else
                {
                    ProcessRead(passIndex, pass.Queue, queueHandoffEdgeCount, textureStates[access.Ref.Index], adjacency, indegree, dedup, queueHandoffs);
                    passDeclarations[passIndex].ReadTextures.push_back(access.Ref.Index);
                }
                UpdateLifetime(textureLifetimes[access.Ref.Index], passIndex);
            }

            for (const BufferAccess& access : pass.BufferAccesses)
            {
                if (access.Ref.Index >= bufferStates.size())
                {
                    SetLastCompileFinding(RenderGraphValidationCode::InvalidBufferAccess,
                                          "RenderGraph buffer access references an invalid buffer resource: pass=\"" +
                                              pass.Name + "\" buffer_index=" + std::to_string(access.Ref.Index) + ".",
                                          passIndex,
                                          pass.Name,
                                          false,
                                          access.Ref.Index);
                    return std::unexpected(Core::ErrorCode::OutOfRange);
                }
                if (access.Write)
                {
                    ProcessWrite(passIndex, pass.Queue, queueHandoffEdgeCount, bufferStates[access.Ref.Index], adjacency, indegree, dedup, queueHandoffs);
                    passDeclarations[passIndex].WriteBuffers.push_back(access.Ref.Index);
                }
                else
                {
                    ProcessRead(passIndex, pass.Queue, queueHandoffEdgeCount, bufferStates[access.Ref.Index], adjacency, indegree, dedup, queueHandoffs);
                    passDeclarations[passIndex].ReadBuffers.push_back(access.Ref.Index);
                }
                UpdateLifetime(bufferLifetimes[access.Ref.Index], passIndex);
            }

            const bool hasColorAttachmentWrite = std::ranges::any_of(
                pass.TextureAccesses, [](const TextureAccess& access) { return access.Usage == TextureUsage::ColorAttachmentWrite; });
            const bool hasDepthAccess = std::ranges::any_of(pass.TextureAccesses, [](const TextureAccess& access) {
                return access.Usage == TextureUsage::DepthRead || access.Usage == TextureUsage::DepthWrite;
            });

            if (pass.HasRenderPassDesc)
            {
                if (!pass.RenderPass.ColorTargets.empty() && !hasColorAttachmentWrite)
                {
                    SetLastCompileFinding(RenderGraphValidationCode::RenderPassColorWriteMissing,
                                          "RenderGraph render-pass color attachment declaration is missing a color write usage: pass=\"" +
                                              pass.Name + "\".",
                                          passIndex,
                                          pass.Name);
                    return std::unexpected(Core::ErrorCode::InvalidArgument);
                }
                if (pass.RenderPass.Depth.Target.IsValid() && !hasDepthAccess)
                {
                    SetLastCompileFinding(RenderGraphValidationCode::RenderPassDepthAccessMissing,
                                          "RenderGraph render-pass depth attachment declaration is missing depth usage: pass=\"" +
                                              pass.Name + "\".",
                                          passIndex,
                                          pass.Name);
                    return std::unexpected(Core::ErrorCode::InvalidArgument);
                }

                std::vector<std::uint32_t> colorWriteTextures{};
                colorWriteTextures.reserve(pass.TextureAccesses.size());
                std::uint32_t depthTexture = std::numeric_limits<std::uint32_t>::max();
                for (const TextureAccess& access : pass.TextureAccesses)
                {
                    if (access.Write && access.Usage == TextureUsage::ColorAttachmentWrite)
                    {
                        colorWriteTextures.push_back(access.Ref.Index);
                    }
                    if (depthTexture == std::numeric_limits<std::uint32_t>::max() &&
                        (access.Usage == TextureUsage::DepthRead || access.Usage == TextureUsage::DepthWrite))
                    {
                        depthTexture = access.Ref.Index;
                    }
                }

                const std::size_t colorCount = std::min(pass.RenderPass.ColorTargets.size(), colorWriteTextures.size());
                for (std::size_t colorIndex = 0; colorIndex < colorCount; ++colorIndex)
                {
                    const std::uint32_t textureIndex = colorWriteTextures[colorIndex];
                    renderPassAttachments.push_back(CompiledRenderPassAttachment{
                        .PassIndex = passIndex,
                        .ResourceIndex = textureIndex,
                        .AttachmentIndex = static_cast<std::uint32_t>(colorIndex),
                        .IsTextureResource = true,
                        .IsDepthAttachment = false,
                        .Load = pass.RenderPass.ColorTargets[colorIndex].Load,
                        .Store = pass.RenderPass.ColorTargets[colorIndex].Store,
                        .Format = textureIndex < textures.size() ? textures[textureIndex].Desc.Fmt : RHI::Format::Undefined,
                    });
                }

                if (pass.RenderPass.Depth.Target.IsValid() && depthTexture != std::numeric_limits<std::uint32_t>::max())
                {
                    renderPassAttachments.push_back(CompiledRenderPassAttachment{
                        .PassIndex = passIndex,
                        .ResourceIndex = depthTexture,
                        .AttachmentIndex = 0u,
                        .IsTextureResource = true,
                        .IsDepthAttachment = true,
                        .Load = pass.RenderPass.Depth.Load,
                        .Store = pass.RenderPass.Depth.Store,
                        .Format = depthTexture < textures.size() ? textures[depthTexture].Desc.Fmt : RHI::Format::Undefined,
                    });
                }
            }

            auto sortUnique = [](std::vector<std::uint32_t>& values) {
                std::ranges::sort(values);
                values.erase(std::unique(values.begin(), values.end()), values.end());
            };
            sortUnique(passDeclarations[passIndex].ReadTextures);
            sortUnique(passDeclarations[passIndex].WriteTextures);
            sortUnique(passDeclarations[passIndex].ReadBuffers);
            sortUnique(passDeclarations[passIndex].WriteBuffers);
        }

        for (std::uint32_t from = 0; from < passCount; ++from)
        {
            for (const std::uint32_t to : adjacency[from])
            {
                reverseAdjacency[to].push_back(from);
            }
        }

        std::vector<bool> live(passCount, false);
        std::stack<std::uint32_t> rootStack{};
        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const RenderPassRecord& pass = passes[passIndex];
            const bool hasPresentUse = std::ranges::any_of(
                pass.TextureAccesses, [](const TextureAccess& access) { return access.Usage == TextureUsage::Present; });
            const bool writesImportedTexture = std::ranges::any_of(pass.TextureAccesses, [textures](const TextureAccess& access) {
                return access.Write && access.Ref.Index < textures.size() && textures[access.Ref.Index].Imported;
            });
            const bool writesImportedBuffer = std::ranges::any_of(pass.BufferAccesses, [buffers](const BufferAccess& access) {
                return access.Write && access.Ref.Index < buffers.size() && buffers[access.Ref.Index].Imported;
            });

            if (pass.SideEffect || hasPresentUse || writesImportedTexture || writesImportedBuffer)
            {
                rootStack.push(passIndex);
            }
        }

        if (rootStack.empty())
        {
            for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
            {
                if (adjacency[passIndex].empty() && !reverseAdjacency[passIndex].empty())
                {
                    rootStack.push(passIndex);
                }
            }
            if (rootStack.empty())
            {
                for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
                {
                    rootStack.push(passIndex);
                }
            }
        }

        while (!rootStack.empty())
        {
            const std::uint32_t node = rootStack.top();
            rootStack.pop();
            if (live[node])
            {
                continue;
            }
            live[node] = true;
            for (const std::uint32_t predecessor : reverseAdjacency[node])
            {
                rootStack.push(predecessor);
            }
        }

        std::vector<std::uint32_t> liveIndegree = indegree;
        std::uint32_t livePassCount = 0;
        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            if (live[passIndex])
            {
                ++livePassCount;
                continue;
            }

            for (const std::uint32_t succ : adjacency[passIndex])
            {
                if (live[succ] && liveIndegree[succ] > 0)
                {
                    --liveIndegree[succ];
                }
            }
        }

        std::vector<std::uint32_t> ready{};
        ready.reserve(livePassCount);
        for (std::uint32_t i = 0; i < passCount; ++i)
        {
            if (live[i] && liveIndegree[i] == 0)
            {
                ready.push_back(i);
            }
        }
        std::ranges::sort(ready);

        std::vector<std::uint32_t> order{};
        order.reserve(livePassCount);
        std::vector<std::uint32_t> layerByPass(passCount, 0u);
        std::vector<std::uint32_t> layerMaxPred(passCount, 0u);

        while (!ready.empty())
        {
            const std::uint32_t node = ready.front();
            ready.erase(ready.begin());
            order.push_back(node);

            std::vector<std::uint32_t> newlyReady{};
            for (const std::uint32_t succ : adjacency[node])
            {
                if (!live[succ])
                {
                    continue;
                }
                layerMaxPred[succ] = std::max(layerMaxPred[succ], layerByPass[node] + 1u);
                if (--liveIndegree[succ] == 0)
                {
                    layerByPass[succ] = layerMaxPred[succ];
                    newlyReady.push_back(succ);
                }
            }
            if (!newlyReady.empty())
            {
                std::ranges::sort(newlyReady);
                ready.insert(ready.end(), newlyReady.begin(), newlyReady.end());
            }
        }

        if (order.size() != livePassCount)
        {
            const bool crossQueueCycle = HasCrossQueueCycleEdge(queueHandoffs, live, liveIndegree);
            const RenderGraphValidationCode cycleCode = crossQueueCycle ? RenderGraphValidationCode::CrossQueueCycle
                                                                        : RenderGraphValidationCode::CycleDetected;
            std::ostringstream cycle;
            cycle << (crossQueueCycle ? "RenderGraph cross-queue cycle detected among live passes: "
                                      : "RenderGraph cycle detected among live passes: ");
            bool first = true;
            for (std::uint32_t i = 0; i < passCount; ++i)
            {
                if (!live[i] || liveIndegree[i] == 0u)
                {
                    continue;
                }
                if (!first)
                {
                    cycle << ", ";
                }
                first = false;
                cycle << '"' << passes[i].Name << "\"(index=" << i << ")";
            }
            if (first)
            {
                cycle << "<unknown>";
            }
            const std::string cycleMessage = cycle.str();
            g_LastCompileValidationResult.Findings.clear();
            for (std::uint32_t i = 0; i < passCount; ++i)
            {
                if (!live[i] || liveIndegree[i] == 0u)
                {
                    continue;
                }
                AddFinding(g_LastCompileValidationResult,
                           RenderGraphValidationSeverity::Error,
                           cycleCode,
                           cycleMessage,
                           i,
                           passes[i].Name);
            }
            if (g_LastCompileValidationResult.Findings.empty())
            {
                AddFinding(g_LastCompileValidationResult,
                           RenderGraphValidationSeverity::Error,
                           cycleCode,
                           cycleMessage,
                           InvalidValidationIndex(),
                           std::string{});
            }
            SortValidationFindings(g_LastCompileValidationResult);

            CompiledRenderGraph failed{
                .PassCount = livePassCount,
                .CulledPassCount = passCount - livePassCount,
                .ResourceCount = resourceCount,
                .EdgeCount = static_cast<std::uint32_t>(dedup.size()),
                .QueueHandoffEdgeCount = queueHandoffEdgeCount,
                .TopologicalOrder = order,
                .TopologicalLayerByPass = layerByPass,
                .PassNames = std::move(passNames),
                .PassQueues = std::move(passQueues),
                .PassSideEffects = std::move(passSideEffects),
                .PassDeclarations = std::move(passDeclarations),
                .TextureNames = std::move(textureNames),
                .BufferNames = std::move(bufferNames),
                .TextureLifetimes = std::move(textureLifetimes),
                .BufferLifetimes = std::move(bufferLifetimes),
                .TextureInitialStates = std::move(textureInitialStates),
                .TextureFinalStates = std::move(textureFinalStates),
                .BufferInitialStates = std::move(bufferInitialStates),
                .BufferFinalStates = std::move(bufferFinalStates),
                .ValidationFindings = g_LastCompileValidationResult.Findings,
            };
            return std::unexpected(Core::ErrorCode::InvalidState);
        }

        std::uint32_t activeEdgeCount = 0;
        for (std::uint32_t from = 0; from < passCount; ++from)
        {
            if (!live[from])
            {
                continue;
            }
            for (const std::uint32_t to : adjacency[from])
            {
                if (live[to])
                {
                    ++activeEdgeCount;
                }
            }
        }

        std::uint32_t activeQueueHandoffEdgeCount = 0;
        for (const QueueHandoffDependency& handoff : queueHandoffs)
        {
            if (handoff.From < live.size() && handoff.To < live.size() && live[handoff.From] && live[handoff.To])
            {
                ++activeQueueHandoffEdgeCount;
            }
        }

        std::vector<CrossQueueTimelineSignal> crossQueueTimelineSignals{};
        std::vector<CrossQueueTimelineWait> crossQueueTimelineWaits{};
        std::vector<CrossQueueTimelineEdge> crossQueueTimelineEdges{};
        SynthesizeCrossQueueTimeline(queueHandoffs,
                                     live,
                                     layerByPass,
                                     crossQueueTimelineSignals,
                                     crossQueueTimelineWaits,
                                     crossQueueTimelineEdges);

        std::vector<BarrierPacket> barrierPackets{};
        barrierPackets.reserve(order.size());
        std::vector<TextureBarrierState> textureStateByRef(textures.size(), TextureBarrierState::Undefined);
        std::vector<BufferBarrierState> bufferStateByRef(buffers.size(), BufferBarrierState::Undefined);

        for (std::uint32_t i = 0; i < textures.size(); ++i)
        {
            if (textures[i].Imported)
            {
                textureStateByRef[i] = ToTextureBarrierState(textures[i].InitialState);
            }
        }
        for (std::uint32_t i = 0; i < buffers.size(); ++i)
        {
            if (buffers[i].Imported)
            {
                bufferStateByRef[i] = ToBufferBarrierState(buffers[i].InitialState);
            }
        }

        for (const std::uint32_t passIndex : order)
        {
            const auto& pass = passes[passIndex];
            BarrierPacket packet{};
            packet.PassIndex = passIndex;

            for (const auto& access : pass.TextureAccesses)
            {
                const auto next = ToTextureBarrierState(access.Usage);
                const auto prev = textureStateByRef[access.Ref.Index];
                if (prev != next)
                {
                    packet.TextureBarriers.push_back(TextureBarrierPacket{
                        .TextureIndex = access.Ref.Index,
                        .Before = prev,
                        .After = next,
                    });
                    textureStateByRef[access.Ref.Index] = next;
                }
            }

            for (const auto& access : pass.BufferAccesses)
            {
                const auto next = ToBufferBarrierState(access.Usage);
                const auto prev = bufferStateByRef[access.Ref.Index];
                if (prev != next)
                {
                    packet.BufferBarriers.push_back(BufferBarrierPacket{
                        .BufferIndex = access.Ref.Index,
                        .Before = prev,
                        .After = next,
                    });
                    bufferStateByRef[access.Ref.Index] = next;
                }
            }

            if (!packet.TextureBarriers.empty() || !packet.BufferBarriers.empty())
            {
                barrierPackets.push_back(std::move(packet));
            }
        }

        BarrierPacket importedFinalPacket{};
        importedFinalPacket.PassIndex = passCount;
        for (std::uint32_t textureIndex = 0; textureIndex < textures.size(); ++textureIndex)
        {
            if (!textures[textureIndex].Imported)
            {
                continue;
            }

            const auto targetState = ToTextureBarrierState(textures[textureIndex].FinalState);
            const auto currentState = textureStateByRef[textureIndex];
            if (currentState == targetState)
            {
                continue;
            }

            importedFinalPacket.TextureBarriers.push_back(TextureBarrierPacket{
                .TextureIndex = textureIndex,
                .Before = currentState,
                .After = targetState,
            });
            textureStateByRef[textureIndex] = targetState;
        }

        for (std::uint32_t bufferIndex = 0; bufferIndex < buffers.size(); ++bufferIndex)
        {
            if (!buffers[bufferIndex].Imported)
            {
                continue;
            }

            const auto targetState = ToBufferBarrierState(buffers[bufferIndex].FinalState);
            const auto currentState = bufferStateByRef[bufferIndex];
            if (currentState == targetState)
            {
                continue;
            }

            importedFinalPacket.BufferBarriers.push_back(BufferBarrierPacket{
                .BufferIndex = bufferIndex,
                .Before = currentState,
                .After = targetState,
            });
            bufferStateByRef[bufferIndex] = targetState;
        }

        if (!importedFinalPacket.TextureBarriers.empty() || !importedFinalPacket.BufferBarriers.empty())
        {
            barrierPackets.push_back(std::move(importedFinalPacket));
        }

        CompiledRenderGraph compiled{
            .PassCount = livePassCount,
            .CulledPassCount = passCount - livePassCount,
            .ResourceCount = resourceCount,
            .EdgeCount = activeEdgeCount,
            .QueueHandoffEdgeCount = activeQueueHandoffEdgeCount,
            .CrossQueueTimelineEdgeCount = static_cast<std::uint32_t>(crossQueueTimelineEdges.size()),
            .TopologicalOrder = std::move(order),
            .TopologicalLayerByPass = std::move(layerByPass),
            .PassNames = std::move(passNames),
            .PassQueues = std::move(passQueues),
            .PassSideEffects = std::move(passSideEffects),
            .PassDeclarations = std::move(passDeclarations),
            .TextureNames = std::move(textureNames),
            .BufferNames = std::move(bufferNames),
            .TextureLifetimes = std::move(textureLifetimes),
            .BufferLifetimes = std::move(bufferLifetimes),
            .TextureInitialStates = std::move(textureInitialStates),
            .TextureFinalStates = std::move(textureFinalStates),
            .BufferInitialStates = std::move(bufferInitialStates),
            .BufferFinalStates = std::move(bufferFinalStates),
            .TextureHandles = std::move(textureHandles),
            .BufferHandles = std::move(bufferHandles),
            .TextureImported = std::move(textureImported),
            .TextureIsBackbuffer = std::move(textureIsBackbuffer),
            .BufferImported = std::move(bufferImported),
            .RenderPassAttachments = std::move(renderPassAttachments),
            .CrossQueueTimelineSignals = std::move(crossQueueTimelineSignals),
            .CrossQueueTimelineWaits = std::move(crossQueueTimelineWaits),
            .CrossQueueTimelineEdges = std::move(crossQueueTimelineEdges),
            .BarrierPackets = std::move(barrierPackets),
        };
        RenderGraphValidationResult validation = ValidateCompiledGraph(compiled);
        compiled.ValidationFindings = validation.Findings;
        g_LastCompileValidationResult = std::move(validation);
        return compiled;
    }

    const RenderGraphValidationResult& RenderGraphCompiler::GetLastCompileValidationResult()
    {
        return g_LastCompileValidationResult;
    }

    std::string BuildRenderGraphDebugDump(const CompiledRenderGraph& compiled)
    {
        struct ResourceDebugUse
        {
            std::uint32_t FirstWritePass = InvalidValidationIndex();
            std::uint32_t LastReadPass = InvalidValidationIndex();
            std::uint32_t ProducerCount = 0;
            std::uint32_t ConsumerCount = 0;
        };

        std::vector<ResourceDebugUse> textureUses(compiled.TextureLifetimes.size());
        std::vector<ResourceDebugUse> bufferUses(compiled.BufferLifetimes.size());
        for (const std::uint32_t passIndex : compiled.TopologicalOrder)
        {
            if (passIndex >= compiled.PassDeclarations.size())
            {
                continue;
            }

            const CompiledPassDeclarations& declarations = compiled.PassDeclarations[passIndex];
            for (const std::uint32_t textureIndex : declarations.WriteTextures)
            {
                if (textureIndex >= textureUses.size())
                {
                    continue;
                }
                ResourceDebugUse& use = textureUses[textureIndex];
                if (use.FirstWritePass == InvalidValidationIndex())
                {
                    use.FirstWritePass = passIndex;
                }
                ++use.ProducerCount;
            }
            for (const std::uint32_t textureIndex : declarations.ReadTextures)
            {
                if (textureIndex >= textureUses.size())
                {
                    continue;
                }
                ResourceDebugUse& use = textureUses[textureIndex];
                use.LastReadPass = passIndex;
                ++use.ConsumerCount;
            }
            for (const std::uint32_t bufferIndex : declarations.WriteBuffers)
            {
                if (bufferIndex >= bufferUses.size())
                {
                    continue;
                }
                ResourceDebugUse& use = bufferUses[bufferIndex];
                if (use.FirstWritePass == InvalidValidationIndex())
                {
                    use.FirstWritePass = passIndex;
                }
                ++use.ProducerCount;
            }
            for (const std::uint32_t bufferIndex : declarations.ReadBuffers)
            {
                if (bufferIndex >= bufferUses.size())
                {
                    continue;
                }
                ResourceDebugUse& use = bufferUses[bufferIndex];
                use.LastReadPass = passIndex;
                ++use.ConsumerCount;
            }
        }

        auto writeOptionalPass = [](std::ostringstream& out, const std::uint32_t passIndex) {
            if (passIndex == InvalidValidationIndex())
            {
                out << "none";
                return;
            }
            out << passIndex;
        };

        std::ostringstream out;
        out << "RenderGraph\n";
        out << "  pass_count=" << compiled.PassCount
            << " culled_pass_count=" << compiled.CulledPassCount
            << " resource_count=" << compiled.ResourceCount
            << " edge_count=" << compiled.EdgeCount
            << " queue_handoff_edges=" << compiled.QueueHandoffEdgeCount
            << " cross_queue_timeline_edges=" << compiled.CrossQueueTimelineEdgeCount
            << " barrier_packet_count=" << compiled.BarrierPackets.size() << '\n';

        out << "  passes:\n";
        for (std::size_t orderIndex = 0; orderIndex < compiled.TopologicalOrder.size(); ++orderIndex)
        {
            const auto passIndex = compiled.TopologicalOrder[orderIndex];
            out << "    [" << orderIndex << "] pass=" << passIndex;
            if (passIndex < compiled.PassNames.size())
            {
                out << " name=\"" << compiled.PassNames[passIndex] << '"';
            }
            if (passIndex < compiled.TopologicalLayerByPass.size())
            {
                out << " layer=" << compiled.TopologicalLayerByPass[passIndex];
            }
            if (passIndex < compiled.PassQueues.size())
            {
                out << " queue=" << ToString(compiled.PassQueues[passIndex]);
            }
            out << " side_effect=" << (BoolAt(compiled.PassSideEffects, passIndex) ? "true" : "false");
            out << '\n';

            out << "      color_targets:";
            bool wroteColorHeader = false;
            for (const CompiledRenderPassAttachment& attachment : compiled.RenderPassAttachments)
            {
                if (attachment.PassIndex != passIndex || attachment.IsDepthAttachment)
                {
                    continue;
                }
                if (!wroteColorHeader)
                {
                    out << '\n';
                    wroteColorHeader = true;
                }
                out << "        [" << attachment.AttachmentIndex << "] texture=" << attachment.ResourceIndex;
                if (attachment.ResourceIndex < compiled.TextureNames.size() && !compiled.TextureNames[attachment.ResourceIndex].empty())
                {
                    out << " name=\"" << compiled.TextureNames[attachment.ResourceIndex] << '"';
                }
                out << " load=" << ToString(attachment.Load)
                    << " store=" << ToString(attachment.Store)
                    << " format=" << ToString(attachment.Format) << '\n';
            }
            if (!wroteColorHeader)
            {
                out << " none\n";
            }

            out << "      depth_target:";
            bool wroteDepth = false;
            for (const CompiledRenderPassAttachment& attachment : compiled.RenderPassAttachments)
            {
                if (attachment.PassIndex != passIndex || !attachment.IsDepthAttachment)
                {
                    continue;
                }
                wroteDepth = true;
                out << " texture=" << attachment.ResourceIndex;
                if (attachment.ResourceIndex < compiled.TextureNames.size() && !compiled.TextureNames[attachment.ResourceIndex].empty())
                {
                    out << " name=\"" << compiled.TextureNames[attachment.ResourceIndex] << '"';
                }
                out << " load=" << ToString(attachment.Load)
                    << " store=" << ToString(attachment.Store)
                    << " format=" << ToString(attachment.Format) << '\n';
            }
            if (!wroteDepth)
            {
                out << " none\n";
            }
        }

        out << "  textures:\n";
        for (std::size_t index = 0; index < compiled.TextureLifetimes.size(); ++index)
        {
            const auto& lifetime = compiled.TextureLifetimes[index];
            out << "    texture[" << index << "]";
            if (index < compiled.TextureNames.size() && !compiled.TextureNames[index].empty())
            {
                out << " name=\"" << compiled.TextureNames[index] << '"';
            }
            out << " used=" << (lifetime.HasUse ? "true" : "false")
                << " imported=" << (BoolAt(compiled.TextureImported, static_cast<std::uint32_t>(index)) ? "true" : "false")
                << " final_state=" << (index < compiled.TextureFinalStates.size() ? ToString(compiled.TextureFinalStates[index]) : "Unknown")
                << " first_write_pass=";
            writeOptionalPass(out, textureUses[index].FirstWritePass);
            out << " last_read_pass=";
            writeOptionalPass(out, textureUses[index].LastReadPass);
            out << " producer_count=" << textureUses[index].ProducerCount
                << " consumer_count=" << textureUses[index].ConsumerCount;
            if (lifetime.HasUse)
            {
                out << " first_use_pass=" << lifetime.FirstUsePass << " last_use_pass=" << lifetime.LastUsePass;
            }
            out << '\n';
        }

        out << "  buffers:\n";
        for (std::size_t index = 0; index < compiled.BufferLifetimes.size(); ++index)
        {
            const auto& lifetime = compiled.BufferLifetimes[index];
            out << "    buffer[" << index << "]";
            if (index < compiled.BufferNames.size() && !compiled.BufferNames[index].empty())
            {
                out << " name=\"" << compiled.BufferNames[index] << '"';
            }
            out << " used=" << (lifetime.HasUse ? "true" : "false")
                << " imported=" << (BoolAt(compiled.BufferImported, static_cast<std::uint32_t>(index)) ? "true" : "false")
                << " final_state=" << (index < compiled.BufferFinalStates.size() ? ToString(compiled.BufferFinalStates[index]) : "Unknown")
                << " first_write_pass=";
            writeOptionalPass(out, bufferUses[index].FirstWritePass);
            out << " last_read_pass=";
            writeOptionalPass(out, bufferUses[index].LastReadPass);
            out << " producer_count=" << bufferUses[index].ProducerCount
                << " consumer_count=" << bufferUses[index].ConsumerCount;
            if (lifetime.HasUse)
            {
                out << " first_use_pass=" << lifetime.FirstUsePass << " last_use_pass=" << lifetime.LastUsePass;
            }
            out << '\n';
        }
        return out.str();
    }

    RenderGraphValidationResult ValidateCompiledGraph(
        const CompiledRenderGraph& compiled,
        const std::span<const ImportedResourceAuthorization> authorizations)
    {
        RenderGraphValidationResult result{};

        std::vector<std::uint32_t> passRank(compiled.PassDeclarations.size(), InvalidValidationIndex());
        for (std::uint32_t orderIndex = 0; orderIndex < compiled.TopologicalOrder.size(); ++orderIndex)
        {
            const std::uint32_t passIndex = compiled.TopologicalOrder[orderIndex];
            if (passIndex < passRank.size())
            {
                passRank[passIndex] = orderIndex;
            }
        }

        std::vector<std::vector<std::uint32_t>> textureWriters(compiled.TextureLifetimes.size());
        std::vector<std::vector<std::uint32_t>> bufferWriters(compiled.BufferLifetimes.size());
        std::vector<bool> textureReaders(compiled.TextureLifetimes.size(), false);
        std::vector<bool> bufferReaders(compiled.BufferLifetimes.size(), false);

        for (const std::uint32_t passIndex : compiled.TopologicalOrder)
        {
            if (passIndex >= compiled.PassDeclarations.size())
            {
                AddFinding(result,
                           RenderGraphValidationSeverity::Error,
                           RenderGraphValidationCode::InvalidExplicitDependency,
                           "Compiled render graph topological order references an invalid pass.",
                           compiled,
                           passIndex);
                continue;
            }

            const CompiledPassDeclarations& declarations = compiled.PassDeclarations[passIndex];
            for (const std::uint32_t textureIndex : declarations.ReadTextures)
            {
                if (textureIndex >= textureReaders.size())
                {
                    AddFinding(result,
                               RenderGraphValidationSeverity::Error,
                               RenderGraphValidationCode::InvalidTextureAccess,
                               "Compiled render graph pass reads an invalid texture resource.",
                               compiled,
                               passIndex,
                               true,
                               textureIndex);
                    continue;
                }
                textureReaders[textureIndex] = true;
            }
            for (const std::uint32_t textureIndex : declarations.WriteTextures)
            {
                if (textureIndex >= textureWriters.size())
                {
                    AddFinding(result,
                               RenderGraphValidationSeverity::Error,
                               RenderGraphValidationCode::InvalidTextureAccess,
                               "Compiled render graph pass writes an invalid texture resource.",
                               compiled,
                               passIndex,
                               true,
                               textureIndex);
                    continue;
                }
                textureWriters[textureIndex].push_back(passIndex);
            }
            for (const std::uint32_t bufferIndex : declarations.ReadBuffers)
            {
                if (bufferIndex >= bufferReaders.size())
                {
                    AddFinding(result,
                               RenderGraphValidationSeverity::Error,
                               RenderGraphValidationCode::InvalidBufferAccess,
                               "Compiled render graph pass reads an invalid buffer resource.",
                               compiled,
                               passIndex,
                               false,
                               bufferIndex);
                    continue;
                }
                bufferReaders[bufferIndex] = true;
            }
            for (const std::uint32_t bufferIndex : declarations.WriteBuffers)
            {
                if (bufferIndex >= bufferWriters.size())
                {
                    AddFinding(result,
                               RenderGraphValidationSeverity::Error,
                               RenderGraphValidationCode::InvalidBufferAccess,
                               "Compiled render graph pass writes an invalid buffer resource.",
                               compiled,
                               passIndex,
                               false,
                               bufferIndex);
                    continue;
                }
                bufferWriters[bufferIndex].push_back(passIndex);
            }
        }

        for (std::uint32_t textureIndex = 0; textureIndex < textureWriters.size(); ++textureIndex)
        {
            if (!BoolAt(compiled.TextureImported, textureIndex) && textureReaders[textureIndex] && textureWriters[textureIndex].empty())
            {
                AddFinding(result,
                           RenderGraphValidationSeverity::Error,
                           RenderGraphValidationCode::TransientTextureWithoutProducer,
                           "Transient texture is read but has no producing writer.",
                           compiled,
                           InvalidValidationIndex(),
                           true,
                           textureIndex);
            }
        }
        for (std::uint32_t bufferIndex = 0; bufferIndex < bufferWriters.size(); ++bufferIndex)
        {
            if (!BoolAt(compiled.BufferImported, bufferIndex) && bufferReaders[bufferIndex] && bufferWriters[bufferIndex].empty())
            {
                AddFinding(result,
                           RenderGraphValidationSeverity::Error,
                           RenderGraphValidationCode::TransientBufferWithoutProducer,
                           "Transient buffer is read but has no producing writer.",
                           compiled,
                           InvalidValidationIndex(),
                           false,
                           bufferIndex);
            }
        }

        auto hasEarlierWriter = [&passRank](const std::vector<std::uint32_t>& writers, const std::uint32_t readPass) {
            const std::uint32_t readRank = readPass < passRank.size() ? passRank[readPass] : InvalidValidationIndex();
            return std::ranges::any_of(writers, [readRank, &passRank](const std::uint32_t writerPass) {
                return writerPass < passRank.size() && passRank[writerPass] < readRank;
            });
        };

        for (const std::uint32_t passIndex : compiled.TopologicalOrder)
        {
            if (passIndex >= compiled.PassDeclarations.size())
            {
                continue;
            }

            const CompiledPassDeclarations& declarations = compiled.PassDeclarations[passIndex];
            for (const std::uint32_t textureIndex : declarations.ReadTextures)
            {
                if (textureIndex >= textureWriters.size() || BoolAt(compiled.TextureImported, textureIndex) || textureWriters[textureIndex].empty() ||
                    hasEarlierWriter(textureWriters[textureIndex], passIndex))
                {
                    continue;
                }

                AddFinding(result,
                           RenderGraphValidationSeverity::Error,
                           RenderGraphValidationCode::MissingTextureProducer,
                           "Texture is read before any guaranteed producing writer.",
                           compiled,
                           passIndex,
                           true,
                           textureIndex);
            }
            for (const std::uint32_t bufferIndex : declarations.ReadBuffers)
            {
                if (bufferIndex >= bufferWriters.size() || BoolAt(compiled.BufferImported, bufferIndex) || bufferWriters[bufferIndex].empty() ||
                    hasEarlierWriter(bufferWriters[bufferIndex], passIndex))
                {
                    continue;
                }

                AddFinding(result,
                           RenderGraphValidationSeverity::Error,
                           RenderGraphValidationCode::MissingBufferProducer,
                           "Buffer is read before any guaranteed producing writer.",
                           compiled,
                           passIndex,
                           false,
                           bufferIndex);
            }
        }

        for (const CompiledRenderPassAttachment& attachment : compiled.RenderPassAttachments)
        {
            if (attachment.Load != RHI::LoadOp::Load)
            {
                continue;
            }
            if (!attachment.IsTextureResource || attachment.ResourceIndex >= textureWriters.size())
            {
                AddFinding(result,
                           RenderGraphValidationSeverity::Error,
                           RenderGraphValidationCode::InvalidTextureAccess,
                           "Render-pass LOAD attachment references an invalid texture resource.",
                           compiled,
                           attachment.PassIndex,
                           true,
                           attachment.ResourceIndex);
                continue;
            }

            if (hasEarlierWriter(textureWriters[attachment.ResourceIndex], attachment.PassIndex))
            {
                continue;
            }

            const bool imported = BoolAt(compiled.TextureImported, attachment.ResourceIndex);
            const bool wellDefinedImportedState = imported && attachment.ResourceIndex < compiled.TextureInitialStates.size() &&
                                                 compiled.TextureInitialStates[attachment.ResourceIndex] != TextureState::Undefined;
            AddFinding(result,
                       wellDefinedImportedState ? RenderGraphValidationSeverity::Info : RenderGraphValidationSeverity::Warning,
                       RenderGraphValidationCode::LoadWithoutGuaranteedWriter,
                       wellDefinedImportedState ? "Render-pass LOAD uses an imported texture with a defined initial state."
                                               : "Render-pass LOAD has no earlier guaranteed writer.",
                       compiled,
                       attachment.PassIndex,
                       true,
                       attachment.ResourceIndex);
        }

        for (std::uint32_t textureIndex = 0; textureIndex < compiled.TextureInitialStates.size(); ++textureIndex)
        {
            if (!BoolAt(compiled.TextureImported, textureIndex) || !BoolAt(compiled.TextureIsBackbuffer, textureIndex))
            {
                continue;
            }
            if (textureIndex < compiled.TextureFinalStates.size() && compiled.TextureFinalStates[textureIndex] == TextureState::Present)
            {
                continue;
            }

            AddFinding(result,
                       RenderGraphValidationSeverity::Error,
                       RenderGraphValidationCode::ImportedTextureFinalStateMismatch,
                       "Imported backbuffer texture must finish in Present state.",
                       compiled,
                       InvalidValidationIndex(),
                       true,
                       textureIndex);
        }

        auto validateImportedWrites = [&](const bool isTexture,
                                          const std::uint32_t resourceIndex,
                                          const std::vector<std::uint32_t>& writers) {
            if (writers.empty())
            {
                return;
            }

            const ImportedResourceAuthorization* auth = FindAuthorization(authorizations, isTexture, resourceIndex);
            const bool isBackbuffer = isTexture && BoolAt(compiled.TextureIsBackbuffer, resourceIndex);
            std::uint32_t finalizerPass = writers.back();
            for (const std::uint32_t writerPass : writers)
            {
                if (BoolAt(compiled.PassSideEffects, writerPass))
                {
                    finalizerPass = writerPass;
                }
            }

            for (const std::uint32_t writerPass : writers)
            {
                const std::string writerName = PassNameFor(compiled, writerPass);
                bool allowed = false;
                if (auth != nullptr)
                {
                    switch (auth->Policy)
                    {
                    case ImportedResourceWritePolicy::Disallow:
                        allowed = false;
                        break;
                    case ImportedResourceWritePolicy::AllowAny:
                        allowed = true;
                        break;
                    case ImportedResourceWritePolicy::AllowFinalizerOnly:
                        allowed = auth->AuthorizedWriterPassNames.empty()
                                      ? writerPass == finalizerPass
                                      : ContainsPassName(auth->AuthorizedWriterPassNames, writerName);
                        break;
                    }
                }
                else
                {
                    allowed = BoolAt(compiled.PassSideEffects, writerPass) && (!isBackbuffer || writerPass == finalizerPass);
                }

                if (allowed)
                {
                    continue;
                }

                const RenderGraphValidationCode code = isBackbuffer
                                                            ? RenderGraphValidationCode::BackbufferWrittenByNonFinalizer
                                                            : (isTexture ? RenderGraphValidationCode::UnauthorizedImportedTextureWrite
                                                                         : RenderGraphValidationCode::UnauthorizedImportedBufferWrite);
                AddFinding(result,
                           RenderGraphValidationSeverity::Error,
                           code,
                           isBackbuffer ? "Imported backbuffer is written by a non-finalizer pass."
                                        : "Imported resource write is not authorized.",
                           compiled,
                           writerPass,
                           isTexture,
                           resourceIndex);
            }
        };

        for (std::uint32_t textureIndex = 0; textureIndex < textureWriters.size(); ++textureIndex)
        {
            if (BoolAt(compiled.TextureImported, textureIndex))
            {
                validateImportedWrites(true, textureIndex, textureWriters[textureIndex]);
            }
        }
        for (std::uint32_t bufferIndex = 0; bufferIndex < bufferWriters.size(); ++bufferIndex)
        {
            if (BoolAt(compiled.BufferImported, bufferIndex))
            {
                validateImportedWrites(false, bufferIndex, bufferWriters[bufferIndex]);
            }
        }

        SortValidationFindings(result);
        return result;
    }
}
