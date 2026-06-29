// RUNTIME-125 - rendering vertex-fetch layout smoke benchmark.
//
// The workload is CPU-side and deterministic by design: it is a PR-fast
// baseline/probe for the current uniform-SoA vertex fetch shape, not evidence
// that the optional GPU AoS lane should be implemented. A later GPU/nightly
// benchmark must prove the actual renderer bottleneck before shader/storage
// variants land.

#include "Bench.VertexFetchLayoutSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Intrinsic::Bench::Rendering
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 6;
        constexpr std::size_t kVertexCount = 65'536u;
        constexpr std::size_t kTriangleCount = 131'072u;
        constexpr std::size_t kIndexCount = kTriangleCount * 3u;

        struct Vec2
        {
            float X{0.0f};
            float Y{0.0f};
        };

        struct Vec3
        {
            float X{0.0f};
            float Y{0.0f};
            float Z{0.0f};
        };

        struct InterleavedVertex
        {
            Vec3 Position{};
            Vec3 Normal{};
            Vec2 Texcoord{};
            std::uint32_t Color{0u};
        };

        struct StaticMeshProbe
        {
            std::vector<Vec3> Positions{};
            std::vector<Vec3> Normals{};
            std::vector<Vec2> Texcoords{};
            std::vector<std::uint32_t> Colors{};
            std::vector<InterleavedVertex> Interleaved{};
            std::vector<std::uint32_t> Indices{};
        };

        [[nodiscard]] StaticMeshProbe MakeStaticMeshProbe()
        {
            StaticMeshProbe mesh{};
            mesh.Positions.reserve(kVertexCount);
            mesh.Normals.reserve(kVertexCount);
            mesh.Texcoords.reserve(kVertexCount);
            mesh.Colors.reserve(kVertexCount);
            mesh.Interleaved.reserve(kVertexCount);
            mesh.Indices.reserve(kIndexCount);

            constexpr std::size_t gridWidth = 256u;
            for (std::size_t i = 0; i < kVertexCount; ++i)
            {
                const std::size_t x = i % gridWidth;
                const std::size_t y = i / gridWidth;
                const float fx = static_cast<float>(x) / static_cast<float>(gridWidth - 1u);
                const float fy = static_cast<float>(y) / static_cast<float>(gridWidth - 1u);
                const Vec3 position{fx, fy, (fx * fy) * 0.125f};
                const Vec3 normal{0.0f, 0.0f, 1.0f};
                const Vec2 texcoord{fx, fy};
                const std::uint32_t color =
                    (0xffu << 24u) |
                    ((static_cast<std::uint32_t>(x) & 0xffu) << 16u) |
                    ((static_cast<std::uint32_t>(y) & 0xffu) << 8u) |
                    (static_cast<std::uint32_t>(i) & 0xffu);

                mesh.Positions.push_back(position);
                mesh.Normals.push_back(normal);
                mesh.Texcoords.push_back(texcoord);
                mesh.Colors.push_back(color);
                mesh.Interleaved.push_back(InterleavedVertex{
                    .Position = position,
                    .Normal = normal,
                    .Texcoord = texcoord,
                    .Color = color,
                });
            }

            for (std::size_t i = 0; i < kTriangleCount; ++i)
            {
                const std::uint32_t a =
                    static_cast<std::uint32_t>((i * 37u) % kVertexCount);
                const std::uint32_t b =
                    static_cast<std::uint32_t>((a + 1u + (i % 13u)) % kVertexCount);
                const std::uint32_t c =
                    static_cast<std::uint32_t>((a + gridWidth + 3u) % kVertexCount);
                mesh.Indices.push_back(a);
                mesh.Indices.push_back(b);
                mesh.Indices.push_back(c);
            }

            return mesh;
        }

        [[nodiscard]] double AccumulateVertex(const Vec3& position,
                                              const Vec3& normal,
                                              const Vec2& texcoord,
                                              const std::uint32_t color) noexcept
        {
            const double r = static_cast<double>((color >> 16u) & 0xffu) / 255.0;
            const double g = static_cast<double>((color >> 8u) & 0xffu) / 255.0;
            return (static_cast<double>(position.X) * 0.25) +
                   (static_cast<double>(position.Y) * 0.5) +
                   (static_cast<double>(position.Z) * 0.75) +
                   (static_cast<double>(normal.Z) * 0.125) +
                   (static_cast<double>(texcoord.X + texcoord.Y) * 0.0625) +
                   (r * 0.03125) +
                   (g * 0.015625);
        }

        [[nodiscard]] double FetchSoa(const StaticMeshProbe& mesh)
        {
            double checksum = 0.0;
            for (const std::uint32_t index : mesh.Indices)
            {
                checksum += AccumulateVertex(mesh.Positions[index],
                                             mesh.Normals[index],
                                             mesh.Texcoords[index],
                                             mesh.Colors[index]);
            }
            return checksum;
        }

        [[nodiscard]] double FetchInterleaved(const StaticMeshProbe& mesh)
        {
            double checksum = 0.0;
            for (const std::uint32_t index : mesh.Indices)
            {
                const InterleavedVertex& vertex = mesh.Interleaved[index];
                checksum += AccumulateVertex(vertex.Position,
                                             vertex.Normal,
                                             vertex.Texcoord,
                                             vertex.Color);
            }
            return checksum;
        }

        struct TimedFetch
        {
            double RuntimeMilliseconds{0.0};
            double Checksum{0.0};
        };

        template <typename Fn>
        [[nodiscard]] TimedFetch MeasureFetch(Fn&& fn)
        {
            for (int i = 0; i < kWarmupIterations; ++i)
            {
                (void)fn();
            }

            double checksum = 0.0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < kMeasuredIterations; ++i)
            {
                checksum = fn();
            }
            const auto t1 = std::chrono::steady_clock::now();

            const auto totalNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            return TimedFetch{
                .RuntimeMilliseconds =
                    (static_cast<double>(totalNs) /
                     static_cast<double>(kMeasuredIterations)) *
                    1.0e-6,
                .Checksum = checksum,
            };
        }
    } // namespace

    VertexFetchLayoutSmokeMetrics RunVertexFetchLayoutSmoke()
    {
        const StaticMeshProbe mesh = MakeStaticMeshProbe();
        const TimedFetch soa = MeasureFetch([&mesh]() { return FetchSoa(mesh); });
        const TimedFetch interleaved =
            MeasureFetch([&mesh]() { return FetchInterleaved(mesh); });

        const double qualityError = std::abs(soa.Checksum - interleaved.Checksum);
        const double safeSoaMs = std::max(soa.RuntimeMilliseconds, 1.0e-9);

        VertexFetchLayoutSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = soa.RuntimeMilliseconds;
        metrics.SoaRuntimeMilliseconds = soa.RuntimeMilliseconds;
        metrics.InterleavedRuntimeMilliseconds = interleaved.RuntimeMilliseconds;
        metrics.ThroughputItemsPerSecond =
            static_cast<double>(mesh.Indices.size()) / (safeSoaMs * 1.0e-3);
        metrics.InterleavedToSoaRuntimeRatio =
            interleaved.RuntimeMilliseconds / safeSoaMs;
        metrics.QualityErrorL2 = qualityError;
        metrics.VertexCount = mesh.Positions.size();
        metrics.IndexCount = mesh.Indices.size();
        metrics.Succeeded =
            mesh.Positions.size() == kVertexCount &&
            mesh.Normals.size() == kVertexCount &&
            mesh.Texcoords.size() == kVertexCount &&
            mesh.Colors.size() == kVertexCount &&
            mesh.Interleaved.size() == kVertexCount &&
            mesh.Indices.size() == kIndexCount &&
            qualityError <= 1.0e-9;
        return metrics;
    }
} // namespace Intrinsic::Bench::Rendering
