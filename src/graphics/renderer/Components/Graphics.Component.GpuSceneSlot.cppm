// Canonical GPU scene handles and per-renderable residency records for lifecycle sidecars.
module;

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

export module Extrinsic.Graphics.Component.GpuSceneSlot;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    struct GpuInstanceTag;
    struct GpuGeometryTag;
    using GpuInstanceHandle = Core::StrongHandle<GpuInstanceTag>;
    using GpuGeometryHandle = Core::StrongHandle<GpuGeometryTag>;
}

export namespace Extrinsic::Graphics::Components
{
    enum class GpuSceneSlotAssetRebindDecision : std::uint8_t
    {
        NoSourceAsset,
        AssetMismatch,
        GenerationUnavailable,
        UpToDate,
        RebindRequired,
    };

    // GPU buffer metadata keyed by name in GpuSceneSlot::NamedBufferEntries.
    // Lifecycle sidecars populate it for render passes and visualization.
    //
    // Well-known canonical names (convention, not enforced by type):
    //   "positions"   — vec3 vertex / node / point positions
    //   "normals"     — vec3 vertex normals
    //   "edges"       — uint32 edge index pairs (e0v0, e0v1, e1v0, ...)
    //   "colors"      — vec4 per-vertex or per-edge colors
    //   "scalars"     — float per-element scalar field values
    //   "sizes"       — float per-point world-space radii
    //   RenderEdges::WidthSource names — float per-edge screen-space widths
    struct BufferEntry
    {
        RHI::BufferHandle Handle;
        uint32_t       ElementCount = 0;  // number of elements (not bytes)
        uint32_t       Stride       = 0;  // bytes per element
    };

    // Written by runtime/graphics lifecycle sidecars and read by render passes.
    // UINT32_MAX marks an unregistered instance/geometry slot. Named buffers
    // connect extracted property sources to their uploaded GPU storage.
    struct GpuSceneSlot
    {
        std::uint32_t InstanceSlot = UINT32_MAX;
        std::uint32_t InstanceGeneration = 0;
        std::uint32_t GeometrySlot = UINT32_MAX;
        std::uint32_t GeometryGeneration = 0;
        Assets::AssetId SourceAsset{};
        std::uint64_t LastSeenAssetGeneration = 0;

        std::unordered_map<std::string, BufferEntry> NamedBufferEntries;

        [[nodiscard]] bool HasInstance() const noexcept
        {
            return InstanceSlot != UINT32_MAX;
        }

        [[nodiscard]] bool HasGeometry() const noexcept
        {
            return GeometrySlot != UINT32_MAX;
        }

        [[nodiscard]] bool IsRegistered() const noexcept
        {
            return HasInstance();
        }

        [[nodiscard]] bool HasSourceAsset() const noexcept
        {
            return SourceAsset.IsValid();
        }

        // Returns the handle for the named buffer, or a null handle if not found.
        [[nodiscard]] RHI::BufferHandle Find(std::string_view name) const noexcept;

        // Returns the full NamedBuffer entry, or nullptr if not found.
        [[nodiscard]] const BufferEntry* FindEntry(std::string_view name) const noexcept;

        // Inserts or updates a buffer entry. Called by lifecycle systems only.
        void Upsert(std::string name, RHI::BufferHandle handle,
                    uint32_t elementCount, uint32_t stride);

        void Remove(std::string_view name);

        [[nodiscard]] GpuInstanceHandle ToInstanceHandle() const noexcept
        {
            return {InstanceSlot, InstanceGeneration};
        }

        [[nodiscard]] GpuGeometryHandle ToGeometryHandle() const noexcept
        {
            return {GeometrySlot, GeometryGeneration};
        }

        void SetInstanceHandle(GpuInstanceHandle h) noexcept
        {
            InstanceSlot = h.Index;
            InstanceGeneration = h.Generation;
        }

        void SetGeometryHandle(GpuGeometryHandle h) noexcept
        {
            GeometrySlot = h.Index;
            GeometryGeneration = h.Generation;
        }

        void SetSourceAsset(Assets::AssetId asset, std::uint64_t generation = 0) noexcept;

        void UpdateLastSeenAssetGeneration(std::uint64_t generation) noexcept;

        [[nodiscard]] GpuSceneSlotAssetRebindDecision EvaluateSourceAssetRebind(
            Assets::AssetId observedAsset,
            std::uint64_t observedGeneration) const noexcept;

        [[nodiscard]] bool NeedsSourceAssetRebind(Assets::AssetId observedAsset,
                                                  std::uint64_t observedGeneration) const noexcept;

        void ClearSourceAsset() noexcept;
    };
}
