// GRAPHICS-120 - framegraph barrier emission traversal smoke benchmark.

#include "Bench.FramegraphBarrierEmissionSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

import Extrinsic.Graphics.RenderGraph;

namespace Intrinsic::Bench::Rendering
{
    namespace
    {
        namespace Graphics = Extrinsic::Graphics;

        constexpr std::uint32_t kWarmupIterations = 4u;
        constexpr std::uint32_t kMeasuredIterations = 128u;
        constexpr std::uint32_t kPassCount = 256u;
        constexpr std::uint32_t kTextureCount = 64u;
        constexpr std::uint32_t kBufferCount = 64u;

        struct TraversalSummary
        {
            std::uint64_t PacketComparisons{0u};
            std::uint64_t RangePacketVisits{0u};
            std::uint64_t TextureBarrierVisits{0u};
            std::uint64_t BufferBarrierVisits{0u};
            std::uint64_t Checksum{0u};
        };

        [[nodiscard]] std::vector<Graphics::BarrierPacket> BuildSyntheticPackets()
        {
            std::vector<Graphics::BarrierPacket> packets{};
            packets.reserve((kPassCount * 2u) + 1u);

            for (std::uint32_t passIndex = 0u; passIndex < kPassCount; ++passIndex)
            {
                Graphics::BarrierPacket before{
                    .PassIndex = passIndex,
                    .Stage = Graphics::BarrierPacketStage::BeforePass,
                };
                before.TextureBarriers.push_back(Graphics::TextureBarrierPacket{
                    .TextureIndex = passIndex % kTextureCount,
                    .Before = Graphics::TextureBarrierState::Undefined,
                    .After = Graphics::TextureBarrierState::ShaderRead,
                });
                before.BufferBarriers.push_back(Graphics::BufferBarrierPacket{
                    .BufferIndex = passIndex % kBufferCount,
                    .Before = Graphics::BufferBarrierState::Undefined,
                    .After = Graphics::BufferBarrierState::ShaderRead,
                });
                packets.push_back(std::move(before));

                if ((passIndex % 3u) == 0u)
                {
                    Graphics::BarrierPacket after{
                        .PassIndex = passIndex,
                        .Stage = Graphics::BarrierPacketStage::AfterPass,
                    };
                    after.BufferBarriers.push_back(Graphics::BufferBarrierPacket{
                        .BufferIndex = (passIndex + 1u) % kBufferCount,
                        .Before = Graphics::BufferBarrierState::ShaderWrite,
                        .After = Graphics::BufferBarrierState::IndirectRead,
                    });
                    packets.push_back(std::move(after));
                }
            }

            Graphics::BarrierPacket final{
                .PassIndex = kPassCount,
                .Stage = Graphics::BarrierPacketStage::BeforePass,
            };
            final.TextureBarriers.push_back(Graphics::TextureBarrierPacket{
                .TextureIndex = 0u,
                .Before = Graphics::TextureBarrierState::ColorAttachmentWrite,
                .After = Graphics::TextureBarrierState::Present,
            });
            packets.push_back(std::move(final));
            return packets;
        }

        void AccumulatePacket(const Graphics::BarrierPacket& packet,
                              TraversalSummary& summary)
        {
            ++summary.RangePacketVisits;
            summary.TextureBarrierVisits += packet.TextureBarriers.size();
            summary.BufferBarrierVisits += packet.BufferBarriers.size();
            summary.Checksum += static_cast<std::uint64_t>(packet.PassIndex + 1u) *
                                static_cast<std::uint64_t>(
                                    3u + Graphics::BarrierPacketStageSortKey(packet.Stage));
            for (const Graphics::TextureBarrierPacket& barrier : packet.TextureBarriers)
            {
                summary.Checksum += static_cast<std::uint64_t>(barrier.TextureIndex + 1u) * 17u;
            }
            for (const Graphics::BufferBarrierPacket& barrier : packet.BufferBarriers)
            {
                summary.Checksum += static_cast<std::uint64_t>(barrier.BufferIndex + 1u) * 31u;
            }
        }

        [[nodiscard]] TraversalSummary TraverseLegacyFullScan(
            const std::vector<Graphics::BarrierPacket>& packets)
        {
            TraversalSummary summary{};
            const auto emit = [&](const std::uint32_t passIndex,
                                  const Graphics::BarrierPacketStage stage)
            {
                for (const Graphics::BarrierPacket& packet : packets)
                {
                    ++summary.PacketComparisons;
                    if (packet.PassIndex == passIndex && packet.Stage == stage)
                    {
                        AccumulatePacket(packet, summary);
                    }
                }
            };

            for (std::uint32_t passIndex = 0u; passIndex < kPassCount; ++passIndex)
            {
                emit(passIndex, Graphics::BarrierPacketStage::BeforePass);
                emit(passIndex, Graphics::BarrierPacketStage::AfterPass);
            }
            emit(kPassCount, Graphics::BarrierPacketStage::BeforePass);
            return summary;
        }

        [[nodiscard]] TraversalSummary TraverseIndexedRange(
            const std::vector<Graphics::BarrierPacket>& packets)
        {
            TraversalSummary summary{};
            const auto emit = [&](const std::uint32_t passIndex,
                                  const Graphics::BarrierPacketStage stage)
            {
                const Graphics::BarrierPacketRange range =
                    Graphics::FindBarrierPacketRange(packets, passIndex, stage);
                for (std::size_t packetIndex = range.Begin; packetIndex < range.End; ++packetIndex)
                {
                    AccumulatePacket(packets[packetIndex], summary);
                }
            };

            for (std::uint32_t passIndex = 0u; passIndex < kPassCount; ++passIndex)
            {
                emit(passIndex, Graphics::BarrierPacketStage::BeforePass);
                emit(passIndex, Graphics::BarrierPacketStage::AfterPass);
            }
            emit(kPassCount, Graphics::BarrierPacketStage::BeforePass);
            return summary;
        }

        [[nodiscard]] double CountQualityError(const TraversalSummary& reference,
                                               const TraversalSummary& sample)
        {
            const auto squaredDelta = [](const std::uint64_t lhs,
                                         const std::uint64_t rhs) -> double
            {
                const double delta = static_cast<double>(std::max(lhs, rhs) - std::min(lhs, rhs));
                return delta * delta;
            };

            return squaredDelta(reference.RangePacketVisits, sample.RangePacketVisits) +
                   squaredDelta(reference.TextureBarrierVisits, sample.TextureBarrierVisits) +
                   squaredDelta(reference.BufferBarrierVisits, sample.BufferBarrierVisits) +
                   squaredDelta(reference.Checksum, sample.Checksum);
        }

        template <typename TraverseFn>
        [[nodiscard]] double MeasureTraversalMilliseconds(TraverseFn traverse,
                                                          const std::vector<Graphics::BarrierPacket>& packets,
                                                          TraversalSummary& summary)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
            {
                summary = traverse(packets);
            }
            const auto t1 = std::chrono::steady_clock::now();

            const auto totalNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            return (static_cast<double>(totalNs) /
                    static_cast<double>(kMeasuredIterations)) *
                   1.0e-6;
        }
    } // namespace

    FramegraphBarrierEmissionSmokeMetrics RunFramegraphBarrierEmissionSmoke()
    {
        const std::vector<Graphics::BarrierPacket> packets = BuildSyntheticPackets();

        for (std::uint32_t i = 0u; i < kWarmupIterations; ++i)
        {
            (void)TraverseLegacyFullScan(packets);
            (void)TraverseIndexedRange(packets);
        }

        TraversalSummary legacy{};
        TraversalSummary indexed{};
        const double legacyMs = MeasureTraversalMilliseconds(TraverseLegacyFullScan, packets, legacy);
        const double indexedMs = MeasureTraversalMilliseconds(TraverseIndexedRange, packets, indexed);
        const double qualityErrorL2 = std::sqrt(CountQualityError(legacy, indexed));

        FramegraphBarrierEmissionSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = indexedMs;
        metrics.LegacyFullScanMilliseconds = legacyMs;
        metrics.IndexedRangeMilliseconds = indexedMs;
        metrics.QualityErrorL2 = qualityErrorL2;
        metrics.PassCount = kPassCount;
        metrics.BarrierPacketCount = static_cast<std::uint32_t>(packets.size());
        metrics.WarmupIterations = kWarmupIterations;
        metrics.MeasuredIterations = kMeasuredIterations;
        metrics.LegacyPacketComparisons = legacy.PacketComparisons;
        metrics.IndexedRangePacketVisits = indexed.RangePacketVisits;
        metrics.TextureBarrierVisits = indexed.TextureBarrierVisits;
        metrics.BufferBarrierVisits = indexed.BufferBarrierVisits;
        metrics.Succeeded = Graphics::AreBarrierPacketsSortedByPassAndStage(packets) &&
            metrics.BarrierPacketCount > 0u &&
            legacy.RangePacketVisits == indexed.RangePacketVisits &&
            metrics.IndexedRangePacketVisits < metrics.LegacyPacketComparisons &&
            qualityErrorL2 == 0.0 &&
            legacyMs > 0.0 &&
            indexedMs > 0.0;
        return metrics;
    }
} // namespace Intrinsic::Bench::Rendering
