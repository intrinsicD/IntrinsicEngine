// GRAPHICS-120 - framegraph reset/redeclare scratch reuse smoke benchmark.

#include "Bench.FramegraphScratchReuseSmoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Descriptors;

namespace
{
    struct AllocationProbe
    {
        std::uint64_t Count{0u};
        std::uint64_t Bytes{0u};
    };

    thread_local AllocationProbe* g_ActiveAllocationProbe = nullptr;

    void RecordAllocation(const std::size_t size) noexcept
    {
        if (g_ActiveAllocationProbe)
        {
            ++g_ActiveAllocationProbe->Count;
            g_ActiveAllocationProbe->Bytes += static_cast<std::uint64_t>(size);
        }
    }

    [[nodiscard]] void* AllocateBytesOrNull(std::size_t size) noexcept
    {
        if (size == 0u)
        {
            size = 1u;
        }

        if (void* memory = std::malloc(size))
        {
            RecordAllocation(size);
            return memory;
        }
        return nullptr;
    }

    [[nodiscard]] void* AllocateBytes(std::size_t size) noexcept
    {
        if (void* memory = AllocateBytesOrNull(size))
        {
            return memory;
        }
        std::abort();
    }

    [[nodiscard]] void* AllocateAlignedBytesOrNull(std::size_t size, const std::size_t alignment) noexcept
    {
        if (size == 0u)
        {
            size = 1u;
        }

        void* memory = nullptr;
#if defined(_WIN32)
        memory = _aligned_malloc(size, alignment);
#else
        if (posix_memalign(&memory, alignment, size) != 0)
        {
            memory = nullptr;
        }
#endif
        if (memory)
        {
            RecordAllocation(size);
            return memory;
        }
        return nullptr;
    }

    [[nodiscard]] void* AllocateAlignedBytes(std::size_t size, const std::size_t alignment) noexcept
    {
        if (void* memory = AllocateAlignedBytesOrNull(size, alignment))
        {
            return memory;
        }
        std::abort();
    }

    void FreeAlignedBytes(void* memory) noexcept
    {
#if defined(_WIN32)
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    }

    class ScopedAllocationProbe final
    {
    public:
        explicit ScopedAllocationProbe(AllocationProbe& probe) noexcept
            : m_Previous(g_ActiveAllocationProbe)
        {
            g_ActiveAllocationProbe = &probe;
        }

        ~ScopedAllocationProbe()
        {
            g_ActiveAllocationProbe = m_Previous;
        }

        ScopedAllocationProbe(const ScopedAllocationProbe&) = delete;
        ScopedAllocationProbe& operator=(const ScopedAllocationProbe&) = delete;

    private:
        AllocationProbe* m_Previous = nullptr;
    };
}

void* operator new(const std::size_t size)
{
    return AllocateBytes(size);
}

void* operator new[](const std::size_t size)
{
    return AllocateBytes(size);
}

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept
{
    return AllocateBytesOrNull(size);
}

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept
{
    return AllocateBytesOrNull(size);
}

void* operator new(const std::size_t size, const std::align_val_t alignment)
{
    return AllocateAlignedBytes(size, static_cast<std::size_t>(alignment));
}

void* operator new[](const std::size_t size, const std::align_val_t alignment)
{
    return AllocateAlignedBytes(size, static_cast<std::size_t>(alignment));
}

void* operator new(const std::size_t size,
                   const std::align_val_t alignment,
                   const std::nothrow_t&) noexcept
{
    return AllocateAlignedBytesOrNull(size, static_cast<std::size_t>(alignment));
}

void* operator new[](const std::size_t size,
                     const std::align_val_t alignment,
                     const std::nothrow_t&) noexcept
{
    return AllocateAlignedBytesOrNull(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, const std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, const std::size_t) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, const std::align_val_t) noexcept
{
    FreeAlignedBytes(memory);
}

void operator delete[](void* memory, const std::align_val_t) noexcept
{
    FreeAlignedBytes(memory);
}

void operator delete(void* memory, const std::size_t, const std::align_val_t) noexcept
{
    FreeAlignedBytes(memory);
}

void operator delete[](void* memory, const std::size_t, const std::align_val_t) noexcept
{
    FreeAlignedBytes(memory);
}

void operator delete(void* memory, const std::align_val_t, const std::nothrow_t&) noexcept
{
    FreeAlignedBytes(memory);
}

void operator delete[](void* memory, const std::align_val_t, const std::nothrow_t&) noexcept
{
    FreeAlignedBytes(memory);
}

namespace Intrinsic::Bench::Rendering
{
    namespace
    {
        namespace Graphics = Extrinsic::Graphics;
        namespace RHI = Extrinsic::RHI;

        constexpr std::uint32_t kWarmupIterations = 3u;
        constexpr std::uint32_t kMeasuredIterations = 32u;
        constexpr std::uint32_t kPassCount = 192u;
        constexpr std::uint32_t kTextureCount = 48u;
        constexpr std::uint32_t kBufferCount = 32u;

        struct CompileSummary
        {
            std::uint32_t PassCount{0u};
            std::uint32_t ResourceCount{0u};
            std::uint32_t BarrierPacketCount{0u};
            std::uint64_t Checksum{0u};
            bool Succeeded{false};
        };

        struct CompileMeasurement
        {
            double Milliseconds{0.0};
            AllocationProbe Allocations{};
            CompileSummary Summary{};
        };

        [[nodiscard]] std::string IndexedName(const char* prefix, const std::uint32_t index)
        {
            return std::string{prefix} + std::to_string(index);
        }

        void DeclareSyntheticGraph(Graphics::RenderGraph& graph)
        {
            std::array<Graphics::TextureRef, kTextureCount> textures{};
            std::array<Graphics::BufferRef, kBufferCount> buffers{};

            for (std::uint32_t textureIndex = 0u; textureIndex < kTextureCount; ++textureIndex)
            {
                RHI::TextureDesc desc{};
                desc.Width = 64u + (textureIndex % 4u);
                desc.Height = 64u + (textureIndex % 3u);
                desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::Storage;
                textures[textureIndex] = graph.CreateTexture(IndexedName("ScratchTexture", textureIndex), desc);
            }

            for (std::uint32_t bufferIndex = 0u; bufferIndex < kBufferCount; ++bufferIndex)
            {
                RHI::BufferDesc desc{};
                desc.SizeBytes = 4096u + (static_cast<std::uint64_t>(bufferIndex) * 16u);
                desc.Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst;
                buffers[bufferIndex] = graph.CreateBuffer(IndexedName("ScratchBuffer", bufferIndex), desc);
            }

            for (std::uint32_t passIndex = 0u; passIndex < kPassCount; ++passIndex)
            {
                (void)graph.AddPass(IndexedName("ScratchPass", passIndex),
                                    [&, passIndex](Graphics::RenderGraphBuilder& builder) {
                    if (passIndex > 0u)
                    {
                        (void)builder.Read(textures[(passIndex - 1u) % kTextureCount], Graphics::TextureUsage::ShaderRead);
                        (void)builder.Read(buffers[(passIndex - 1u) % kBufferCount], Graphics::BufferUsage::ShaderRead);
                    }
                    if (passIndex > 1u)
                    {
                        (void)builder.Read(textures[(passIndex - 2u) % kTextureCount], Graphics::TextureUsage::ShaderRead);
                    }
                    if (passIndex > 2u)
                    {
                        (void)builder.Read(buffers[(passIndex - 3u) % kBufferCount], Graphics::BufferUsage::ShaderRead);
                    }

                    (void)builder.Write(textures[passIndex % kTextureCount], Graphics::TextureUsage::ShaderWrite);
                    (void)builder.Write(buffers[passIndex % kBufferCount], Graphics::BufferUsage::ShaderWrite);
                    builder.SideEffect();
                });
            }
        }

        [[nodiscard]] CompileSummary SummarizeCompiledGraph(Graphics::RenderGraph& graph)
        {
            const auto compiled = graph.Compile();
            if (!compiled.has_value())
            {
                return {};
            }

            CompileSummary summary{};
            summary.PassCount = compiled->PassCount;
            summary.ResourceCount = compiled->ResourceCount;
            summary.BarrierPacketCount = static_cast<std::uint32_t>(compiled->BarrierPackets.size());
            summary.Succeeded = compiled->ValidationFindings.empty();
            summary.Checksum = static_cast<std::uint64_t>(compiled->PassCount + 1u) * 11u;
            summary.Checksum += static_cast<std::uint64_t>(compiled->ResourceCount + 1u) * 17u;
            summary.Checksum += static_cast<std::uint64_t>(compiled->EdgeCount + 1u) * 23u;
            summary.Checksum += static_cast<std::uint64_t>(compiled->BarrierPackets.size() + 1u) * 31u;

            for (const std::uint32_t passIndex : compiled->TopologicalOrder)
            {
                summary.Checksum += static_cast<std::uint64_t>(passIndex + 1u) * 37u;
            }
            for (const Graphics::BarrierPacket& packet : compiled->BarrierPackets)
            {
                summary.Checksum += static_cast<std::uint64_t>(packet.PassIndex + 1u) *
                                    static_cast<std::uint64_t>(
                                        41u + Graphics::BarrierPacketStageSortKey(packet.Stage));
                for (const Graphics::TextureBarrierPacket& barrier : packet.TextureBarriers)
                {
                    summary.Checksum += static_cast<std::uint64_t>(barrier.TextureIndex + 1u) * 43u;
                }
                for (const Graphics::BufferBarrierPacket& barrier : packet.BufferBarriers)
                {
                    summary.Checksum += static_cast<std::uint64_t>(barrier.BufferIndex + 1u) * 47u;
                }
            }
            return summary;
        }

        [[nodiscard]] AllocationProbe MeasureFreshDeclareAllocations()
        {
            AllocationProbe allocations{};
            {
                ScopedAllocationProbe scoped{allocations};
                for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
                {
                    Graphics::RenderGraph graph;
                    DeclareSyntheticGraph(graph);
                }
            }
            return allocations;
        }

        [[nodiscard]] AllocationProbe MeasureReusedDeclareAllocations()
        {
            Graphics::RenderGraph graph;
            DeclareSyntheticGraph(graph);
            graph.Reset();

            AllocationProbe allocations{};
            {
                ScopedAllocationProbe scoped{allocations};
                for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
                {
                    DeclareSyntheticGraph(graph);
                    graph.Reset();
                }
            }
            return allocations;
        }

        [[nodiscard]] CompileMeasurement MeasureFreshDeclareCompile()
        {
            CompileMeasurement measurement{};
            const auto t0 = std::chrono::steady_clock::now();
            {
                ScopedAllocationProbe scoped{measurement.Allocations};
                for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
                {
                    Graphics::RenderGraph graph;
                    DeclareSyntheticGraph(graph);
                    measurement.Summary = SummarizeCompiledGraph(graph);
                }
            }
            const auto t1 = std::chrono::steady_clock::now();

            const auto totalNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            measurement.Milliseconds = (static_cast<double>(totalNs) /
                                        static_cast<double>(kMeasuredIterations)) *
                                       1.0e-6;
            return measurement;
        }

        [[nodiscard]] CompileMeasurement MeasureReusedDeclareCompile()
        {
            Graphics::RenderGraph graph;
            DeclareSyntheticGraph(graph);
            (void)SummarizeCompiledGraph(graph);
            graph.Reset();

            CompileMeasurement measurement{};
            const auto t0 = std::chrono::steady_clock::now();
            {
                ScopedAllocationProbe scoped{measurement.Allocations};
                for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
                {
                    DeclareSyntheticGraph(graph);
                    measurement.Summary = SummarizeCompiledGraph(graph);
                    graph.Reset();
                }
            }
            const auto t1 = std::chrono::steady_clock::now();

            const auto totalNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            measurement.Milliseconds = (static_cast<double>(totalNs) /
                                        static_cast<double>(kMeasuredIterations)) *
                                       1.0e-6;
            return measurement;
        }

        [[nodiscard]] double CountQualityError(const CompileSummary& reference,
                                               const CompileSummary& sample)
        {
            const auto squaredDelta = [](const std::uint64_t lhs,
                                         const std::uint64_t rhs) -> double
            {
                const double delta = static_cast<double>(std::max(lhs, rhs) - std::min(lhs, rhs));
                return delta * delta;
            };

            return squaredDelta(reference.PassCount, sample.PassCount) +
                   squaredDelta(reference.ResourceCount, sample.ResourceCount) +
                   squaredDelta(reference.BarrierPacketCount, sample.BarrierPacketCount) +
                   squaredDelta(reference.Checksum, sample.Checksum);
        }
    } // namespace

    FramegraphScratchReuseSmokeMetrics RunFramegraphScratchReuseSmoke()
    {
        for (std::uint32_t i = 0u; i < kWarmupIterations; ++i)
        {
            Graphics::RenderGraph fresh;
            DeclareSyntheticGraph(fresh);
            (void)SummarizeCompiledGraph(fresh);

            Graphics::RenderGraph reused;
            DeclareSyntheticGraph(reused);
            (void)SummarizeCompiledGraph(reused);
            reused.Reset();
            DeclareSyntheticGraph(reused);
            (void)SummarizeCompiledGraph(reused);
        }

        const AllocationProbe freshDeclare = MeasureFreshDeclareAllocations();
        const AllocationProbe reusedDeclare = MeasureReusedDeclareAllocations();
        const CompileMeasurement fresh = MeasureFreshDeclareCompile();
        const CompileMeasurement reused = MeasureReusedDeclareCompile();
        const double qualityErrorL2 = std::sqrt(CountQualityError(fresh.Summary, reused.Summary));

        FramegraphScratchReuseSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = reused.Milliseconds;
        metrics.FreshDeclareCompileMilliseconds = fresh.Milliseconds;
        metrics.ReusedDeclareCompileMilliseconds = reused.Milliseconds;
        metrics.QualityErrorL2 = qualityErrorL2;
        metrics.PassCount = reused.Summary.PassCount;
        metrics.ResourceCount = reused.Summary.ResourceCount;
        metrics.BarrierPacketCount = reused.Summary.BarrierPacketCount;
        metrics.WarmupIterations = kWarmupIterations;
        metrics.MeasuredIterations = kMeasuredIterations;
        metrics.FreshDeclareAllocations = freshDeclare.Count;
        metrics.ReusedDeclareAllocations = reusedDeclare.Count;
        metrics.FreshDeclareBytes = freshDeclare.Bytes;
        metrics.ReusedDeclareBytes = reusedDeclare.Bytes;
        metrics.FreshDeclareCompileAllocations = fresh.Allocations.Count;
        metrics.ReusedDeclareCompileAllocations = reused.Allocations.Count;
        metrics.FreshDeclareCompileBytes = fresh.Allocations.Bytes;
        metrics.ReusedDeclareCompileBytes = reused.Allocations.Bytes;
        metrics.Succeeded = fresh.Summary.Succeeded &&
            reused.Summary.Succeeded &&
            metrics.PassCount == kPassCount &&
            metrics.ResourceCount == (kTextureCount + kBufferCount) &&
            metrics.BarrierPacketCount > 0u &&
            metrics.ReusedDeclareAllocations < metrics.FreshDeclareAllocations &&
            metrics.ReusedDeclareCompileAllocations < metrics.FreshDeclareCompileAllocations &&
            qualityErrorL2 == 0.0 &&
            fresh.Milliseconds > 0.0 &&
            reused.Milliseconds > 0.0;
        return metrics;
    }
} // namespace Intrinsic::Bench::Rendering
