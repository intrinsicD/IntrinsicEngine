module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>

export module Extrinsic.Runtime.ProceduralGeometry;

import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Runtime
{
    using Extrinsic::ECS::Components::ProceduralGeometryKind;
    using Extrinsic::ECS::Components::ProceduralGeometryParams;

    [[nodiscard]] std::uint64_t HashProceduralGeometryParams(const ProceduralGeometryParams& params) noexcept;

    struct ProceduralGeometryKey
    {
        ProceduralGeometryKind Kind = ProceduralGeometryKind::Triangle;
        std::uint64_t ParamsHash = 0;

        [[nodiscard]] constexpr bool operator==(const ProceduralGeometryKey&) const noexcept = default;
    };

    [[nodiscard]] ProceduralGeometryKey MakeProceduralGeometryKey(
        ProceduralGeometryKind kind,
        const ProceduralGeometryParams& params) noexcept;

    struct ProceduralGeometryKeyHash
    {
        [[nodiscard]] std::size_t operator()(const ProceduralGeometryKey& key) const noexcept;
    };

    struct ProceduralGeometryCacheStats
    {
        std::uint32_t Uploads = 0;
        std::uint32_t ReuseHits = 0;
        std::uint32_t Releases = 0;
        std::uint32_t FailedUploads = 0;
        std::uint32_t RefCountSaturated = 0;
    };

    struct ProceduralGeometryCacheEntry
    {
        ProceduralGeometryKey Key{};
        Extrinsic::Graphics::GpuGeometryHandle Handle{};
        std::uint32_t RefCount = 0;
    };

    class ProceduralGeometryCache
    {
    public:
        using UploadFn = std::function<Extrinsic::Graphics::GpuGeometryHandle(
            const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc&)>;

        ProceduralGeometryCache() = default;
        ~ProceduralGeometryCache() = default;

        ProceduralGeometryCache(const ProceduralGeometryCache&) = delete;
        ProceduralGeometryCache& operator=(const ProceduralGeometryCache&) = delete;

        [[nodiscard]] Extrinsic::Graphics::GpuGeometryHandle EnsureResident(
            const ProceduralGeometryKey& key,
            const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc& uploadDesc,
            const UploadFn& upload);

        bool Release(const ProceduralGeometryKey& key);

        [[nodiscard]] const ProceduralGeometryCacheEntry* Find(const ProceduralGeometryKey& key) const noexcept;

        [[nodiscard]] std::size_t Size() const noexcept { return m_Entries.size(); }
        [[nodiscard]] const ProceduralGeometryCacheStats& Stats() const noexcept { return m_Stats; }

    private:
        std::unordered_map<ProceduralGeometryKey, ProceduralGeometryCacheEntry, ProceduralGeometryKeyHash> m_Entries;
        ProceduralGeometryCacheStats m_Stats{};
    };
}
