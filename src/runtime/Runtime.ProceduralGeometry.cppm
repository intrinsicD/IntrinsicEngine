module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

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
        std::uint32_t FreeRetires = 0;
        std::uint32_t RetireCancellations = 0;
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
        using FreeFn = std::function<void(Extrinsic::Graphics::GpuGeometryHandle)>;

        ProceduralGeometryCache() = default;
        ~ProceduralGeometryCache() = default;

        ProceduralGeometryCache(const ProceduralGeometryCache&) = delete;
        ProceduralGeometryCache& operator=(const ProceduralGeometryCache&) = delete;

        [[nodiscard]] Extrinsic::Graphics::GpuGeometryHandle EnsureResident(
            const ProceduralGeometryKey& key,
            const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc& uploadDesc,
            const UploadFn& upload);

        bool Release(const ProceduralGeometryKey& key);

        // Mirrors `Graphics::GpuAssetCache::Tick`: anchors deadlines on
        // newly-retired entries (`deadline = currentFrame + framesInFlight`)
        // and frees entries whose deadline has been reached.  `freeFn` is the
        // sink that owns the underlying GPU geometry — usually a closure over
        // `GpuWorld::FreeGeometry`.  Safe to call with an empty `freeFn`
        // (frees are simply skipped), but production callers must supply one.
        void Tick(std::uint64_t currentFrame,
                  std::uint32_t framesInFlight,
                  const FreeFn& freeFn);

        [[nodiscard]] const ProceduralGeometryCacheEntry* Find(const ProceduralGeometryKey& key) const noexcept;

        [[nodiscard]] std::size_t Size() const noexcept { return m_Entries.size(); }
        [[nodiscard]] std::size_t PendingRetireCount() const noexcept { return m_Retire.size(); }
        [[nodiscard]] const ProceduralGeometryCacheStats& Stats() const noexcept { return m_Stats; }

        // Test seam for refcount-saturation coverage.  Drives the refcount of
        // an existing entry directly so the saturation rejection path can be
        // exercised without 2^32 EnsureResident calls.  No production caller
        // should use this.
        bool PrimeRefCountForTest(const ProceduralGeometryKey& key, std::uint32_t refCount) noexcept;

    private:
        struct RetireRecord
        {
            ProceduralGeometryKey Key{};
            std::uint64_t Deadline = 0;
            bool DeadlineSet = false;
        };

        void EnqueueRetire(const ProceduralGeometryKey& key);
        bool CancelPendingRetire(const ProceduralGeometryKey& key);

        std::unordered_map<ProceduralGeometryKey, ProceduralGeometryCacheEntry, ProceduralGeometryKeyHash> m_Entries;
        std::vector<RetireRecord> m_Retire;
        ProceduralGeometryCacheStats m_Stats{};
    };
}
