// GRAPHICS-120 - framegraph compiler indexing smoke benchmark.

#include "Bench.FramegraphCompilerIndexingSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

import Extrinsic.Graphics.RenderGraph;

namespace Intrinsic::Bench::Rendering
{
    namespace
    {
        namespace Graphics = Extrinsic::Graphics;

        constexpr std::uint32_t kWarmupIterations = 4u;
        constexpr std::uint32_t kMeasuredIterations = 96u;
        constexpr std::uint32_t kPassCount = 512u;
        constexpr std::uint32_t kTextureCount = 128u;
        constexpr std::uint32_t kBufferCount = 128u;
        constexpr std::uint32_t kInvalidPacketIndex = std::numeric_limits<std::uint32_t>::max();

        struct PacketRequest
        {
            std::uint32_t PassIndex = 0u;
            Graphics::BarrierPacketStage Stage = Graphics::BarrierPacketStage::BeforePass;
            bool Texture = true;
            std::uint32_t ResourceIndex = 0u;
        };

        struct Workload
        {
            std::vector<Graphics::FramePassId> PassIds{};
            std::vector<PacketRequest> Requests{};
        };

        struct IndexingSummary
        {
            std::uint64_t PassIdComparisons{0u};
            std::uint64_t PacketComparisons{0u};
            std::uint64_t PacketLookups{0u};
            std::uint64_t TextureBarrierCount{0u};
            std::uint64_t BufferBarrierCount{0u};
            std::uint64_t Checksum{0u};
            std::uint32_t BarrierPacketCount{0u};
            bool DuplicatePassId{false};
        };

        [[nodiscard]] constexpr std::size_t PacketKey(const std::uint32_t passIndex,
                                                      const Graphics::BarrierPacketStage stage) noexcept
        {
            return (static_cast<std::size_t>(passIndex) * 2u) +
                   static_cast<std::size_t>(Graphics::BarrierPacketStageSortKey(stage));
        }

        [[nodiscard]] Workload BuildSyntheticWorkload()
        {
            Workload workload{};
            workload.PassIds.reserve(kPassCount);
            workload.Requests.reserve(kPassCount * 4u);

            for (std::uint32_t passIndex = 0u; passIndex < kPassCount; ++passIndex)
            {
                workload.PassIds.push_back(Graphics::FramePassId{passIndex + 1u});
                workload.Requests.push_back(PacketRequest{
                    .PassIndex = passIndex,
                    .Stage = Graphics::BarrierPacketStage::BeforePass,
                    .Texture = true,
                    .ResourceIndex = passIndex % kTextureCount,
                });
                workload.Requests.push_back(PacketRequest{
                    .PassIndex = passIndex,
                    .Stage = Graphics::BarrierPacketStage::BeforePass,
                    .Texture = false,
                    .ResourceIndex = passIndex % kBufferCount,
                });

                if ((passIndex % 3u) == 0u)
                {
                    workload.Requests.push_back(PacketRequest{
                        .PassIndex = passIndex,
                        .Stage = Graphics::BarrierPacketStage::AfterPass,
                        .Texture = false,
                        .ResourceIndex = (passIndex + 1u) % kBufferCount,
                    });
                }
                if ((passIndex % 7u) == 0u)
                {
                    workload.Requests.push_back(PacketRequest{
                        .PassIndex = passIndex,
                        .Stage = Graphics::BarrierPacketStage::BeforePass,
                        .Texture = true,
                        .ResourceIndex = (passIndex + 3u) % kTextureCount,
                    });
                }
            }

            workload.Requests.push_back(PacketRequest{
                .PassIndex = kPassCount,
                .Stage = Graphics::BarrierPacketStage::BeforePass,
                .Texture = true,
                .ResourceIndex = 0u,
            });
            return workload;
        }

        void SortPackets(std::vector<Graphics::BarrierPacket>& packets)
        {
            std::ranges::stable_sort(packets, [](const Graphics::BarrierPacket& lhs,
                                                 const Graphics::BarrierPacket& rhs) {
                return std::tuple{lhs.PassIndex, Graphics::BarrierPacketStageSortKey(lhs.Stage)} <
                       std::tuple{rhs.PassIndex, Graphics::BarrierPacketStageSortKey(rhs.Stage)};
            });
        }

        void AppendRequest(Graphics::BarrierPacket& packet, const PacketRequest request)
        {
            if (request.Texture)
            {
                packet.TextureBarriers.push_back(Graphics::TextureBarrierPacket{
                    .TextureIndex = request.ResourceIndex,
                    .Before = Graphics::TextureBarrierState::Undefined,
                    .After = Graphics::TextureBarrierState::ShaderRead,
                });
                return;
            }

            packet.BufferBarriers.push_back(Graphics::BufferBarrierPacket{
                .BufferIndex = request.ResourceIndex,
                .Before = Graphics::BufferBarrierState::Undefined,
                .After = Graphics::BufferBarrierState::ShaderRead,
            });
        }

        [[nodiscard]] IndexingSummary SummarizePackets(
            const std::vector<Graphics::BarrierPacket>& packets,
            IndexingSummary summary)
        {
            summary.BarrierPacketCount = static_cast<std::uint32_t>(packets.size());
            for (const Graphics::BarrierPacket& packet : packets)
            {
                summary.TextureBarrierCount += packet.TextureBarriers.size();
                summary.BufferBarrierCount += packet.BufferBarriers.size();
                summary.Checksum += static_cast<std::uint64_t>(packet.PassIndex + 1u) *
                                    static_cast<std::uint64_t>(
                                        5u + Graphics::BarrierPacketStageSortKey(packet.Stage));
                for (const Graphics::TextureBarrierPacket& barrier : packet.TextureBarriers)
                {
                    summary.Checksum += static_cast<std::uint64_t>(barrier.TextureIndex + 1u) * 19u;
                }
                for (const Graphics::BufferBarrierPacket& barrier : packet.BufferBarriers)
                {
                    summary.Checksum += static_cast<std::uint64_t>(barrier.BufferIndex + 1u) * 37u;
                }
            }
            return summary;
        }

        [[nodiscard]] IndexingSummary RunLegacyScan(const Workload& workload)
        {
            IndexingSummary summary{};
            for (std::uint32_t passIndex = 0u; passIndex < workload.PassIds.size(); ++passIndex)
            {
                const Graphics::FramePassId id = workload.PassIds[passIndex];
                if (!id.IsValid())
                {
                    continue;
                }

                for (std::uint32_t priorIndex = 0u; priorIndex < passIndex; ++priorIndex)
                {
                    ++summary.PassIdComparisons;
                    if (workload.PassIds[priorIndex] == id)
                    {
                        summary.DuplicatePassId = true;
                        break;
                    }
                }
                if (summary.DuplicatePassId)
                {
                    break;
                }
            }

            std::vector<Graphics::BarrierPacket> packets{};
            packets.reserve(workload.Requests.size());
            for (const PacketRequest request : workload.Requests)
            {
                auto packetIt = packets.end();
                for (auto it = packets.begin(); it != packets.end(); ++it)
                {
                    ++summary.PacketComparisons;
                    if (it->PassIndex == request.PassIndex && it->Stage == request.Stage)
                    {
                        packetIt = it;
                        break;
                    }
                }

                if (packetIt == packets.end())
                {
                    packets.push_back(Graphics::BarrierPacket{
                        .PassIndex = request.PassIndex,
                        .Stage = request.Stage,
                    });
                    packetIt = std::prev(packets.end());
                }
                AppendRequest(*packetIt, request);
            }

            SortPackets(packets);
            return SummarizePackets(packets, summary);
        }

        [[nodiscard]] IndexingSummary RunIndexed(const Workload& workload)
        {
            struct PassIdEntry
            {
                Graphics::FramePassId Id{};
                std::uint32_t PassIndex = 0u;
            };

            IndexingSummary summary{};
            std::vector<PassIdEntry> entries{};
            entries.reserve(workload.PassIds.size());
            for (std::uint32_t passIndex = 0u; passIndex < workload.PassIds.size(); ++passIndex)
            {
                if (workload.PassIds[passIndex].IsValid())
                {
                    entries.push_back(PassIdEntry{
                        .Id = workload.PassIds[passIndex],
                        .PassIndex = passIndex,
                    });
                }
            }
            std::ranges::sort(entries, [](const PassIdEntry lhs, const PassIdEntry rhs) {
                return std::tuple{lhs.Id.Value, lhs.PassIndex} <
                       std::tuple{rhs.Id.Value, rhs.PassIndex};
            });
            for (std::size_t entryIndex = 1u; entryIndex < entries.size(); ++entryIndex)
            {
                ++summary.PassIdComparisons;
                if (entries[entryIndex - 1u].Id == entries[entryIndex].Id)
                {
                    summary.DuplicatePassId = true;
                    break;
                }
            }

            std::vector<Graphics::BarrierPacket> packets{};
            packets.reserve(workload.Requests.size());
            std::vector<std::uint32_t> packetIndexByKey(
                (static_cast<std::size_t>(kPassCount) + 1u) * 2u,
                kInvalidPacketIndex);
            for (const PacketRequest request : workload.Requests)
            {
                ++summary.PacketLookups;
                const std::size_t key = PacketKey(request.PassIndex, request.Stage);
                std::uint32_t packetIndex = packetIndexByKey[key];
                if (packetIndex == kInvalidPacketIndex)
                {
                    packetIndex = static_cast<std::uint32_t>(packets.size());
                    packetIndexByKey[key] = packetIndex;
                    packets.push_back(Graphics::BarrierPacket{
                        .PassIndex = request.PassIndex,
                        .Stage = request.Stage,
                    });
                }
                AppendRequest(packets[packetIndex], request);
            }

            SortPackets(packets);
            return SummarizePackets(packets, summary);
        }

        [[nodiscard]] double CountQualityError(const IndexingSummary& reference,
                                               const IndexingSummary& sample)
        {
            const auto squaredDelta = [](const std::uint64_t lhs,
                                         const std::uint64_t rhs) -> double
            {
                const double delta = static_cast<double>(std::max(lhs, rhs) - std::min(lhs, rhs));
                return delta * delta;
            };

            return squaredDelta(reference.BarrierPacketCount, sample.BarrierPacketCount) +
                   squaredDelta(reference.TextureBarrierCount, sample.TextureBarrierCount) +
                   squaredDelta(reference.BufferBarrierCount, sample.BufferBarrierCount) +
                   squaredDelta(reference.Checksum, sample.Checksum);
        }

        template <typename RunFn>
        [[nodiscard]] double MeasureMilliseconds(RunFn run,
                                                 const Workload& workload,
                                                 IndexingSummary& summary)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
            {
                summary = run(workload);
            }
            const auto t1 = std::chrono::steady_clock::now();

            const auto totalNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            return (static_cast<double>(totalNs) /
                    static_cast<double>(kMeasuredIterations)) *
                   1.0e-6;
        }
    } // namespace

    FramegraphCompilerIndexingSmokeMetrics RunFramegraphCompilerIndexingSmoke()
    {
        const Workload workload = BuildSyntheticWorkload();

        for (std::uint32_t i = 0u; i < kWarmupIterations; ++i)
        {
            (void)RunLegacyScan(workload);
            (void)RunIndexed(workload);
        }

        IndexingSummary legacy{};
        IndexingSummary indexed{};
        const double legacyMs = MeasureMilliseconds(RunLegacyScan, workload, legacy);
        const double indexedMs = MeasureMilliseconds(RunIndexed, workload, indexed);
        const double qualityErrorL2 = std::sqrt(CountQualityError(legacy, indexed));

        FramegraphCompilerIndexingSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = indexedMs;
        metrics.LegacyScanMilliseconds = legacyMs;
        metrics.IndexedMilliseconds = indexedMs;
        metrics.QualityErrorL2 = qualityErrorL2;
        metrics.PassCount = kPassCount;
        metrics.RequestCount = static_cast<std::uint32_t>(workload.Requests.size());
        metrics.BarrierPacketCount = indexed.BarrierPacketCount;
        metrics.WarmupIterations = kWarmupIterations;
        metrics.MeasuredIterations = kMeasuredIterations;
        metrics.LegacyPassIdComparisons = legacy.PassIdComparisons;
        metrics.IndexedPassIdComparisons = indexed.PassIdComparisons;
        metrics.LegacyPacketComparisons = legacy.PacketComparisons;
        metrics.IndexedPacketLookups = indexed.PacketLookups;
        metrics.Succeeded = !legacy.DuplicatePassId &&
            !indexed.DuplicatePassId &&
            metrics.BarrierPacketCount > 0u &&
            metrics.LegacyPassIdComparisons > metrics.IndexedPassIdComparisons &&
            metrics.LegacyPacketComparisons > metrics.IndexedPacketLookups &&
            qualityErrorL2 == 0.0 &&
            legacyMs > 0.0 &&
            indexedMs > 0.0;
        return metrics;
    }
} // namespace Intrinsic::Bench::Rendering
