module;

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

export module Extrinsic.Graphics.Component.GpuSceneSlot;

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
    // A named GPU buffer view owned by this entity's GpuSceneSlot.
    // The lifecycle system populates these; render passes and visualization
    // components reference them by name.
    //
    // Well-known canonical names (convention, not enforced by type):
    //   "positions"   — vec3 vertex / node / point positions
    //   "normals"     — vec3 vertex normals
    //   "edges"       — uint32 edge index pairs (e0v0, e0v1, e1v0, ...)
    //   "colors"      — vec4 per-vertex or per-edge colors
    //   "scalars"     — float per-element scalar field values
    //   "sizes"       — float per-point world-space radii
    struct BufferEntry
    {
        std::string    Name;
        RHI::BufferHandle Handle;
        uint32_t       ElementCount = 0;  // number of elements (not bytes)
        uint32_t       Stride       = 0;  // bytes per element
    };

    // Per-entity GPU scene state. Written exclusively by lifecycle systems; read by render passes.
    //
    // CullingSlotIndex
    //   Index into the GPU-side per-instance SSBO consumed by instance_cull.comp.
    //   The compute shader reads each slot's bounding sphere + model matrix, tests
    //   against the camera frustum, and writes surviving draw commands into the
    //   indirect draw buffer.  UINT32_MAX = not yet registered (entity not in GPU scene).
    //   Only surface geometry that participates in GPU-driven culling needs this;
    //   point clouds and line draws that go through BDA directly can leave it unset.
    //
    // Buffers
    //   All uploaded geometry buffers for this entity, keyed by canonical name.
    //   Render geometry components (RenderPoints, RenderLines, RenderSurface) and
    //   visualization configs (ScalarFieldDataSource, ColorDataSource) name their
    //   required buffer here; the render pass resolves name → handle at extraction time.
    struct GpuSceneSlot
    {
        std::uint32_t InstanceSlot = UINT32_MAX;
        std::uint32_t InstanceGeneration = 0;
        std::uint32_t GeometrySlot = UINT32_MAX;
        std::uint32_t GeometryGeneration = 0;

        std::unordered_map<std::string, RHI::BufferHandle> NamedBuffers;
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

        // Returns the handle for the named buffer, or a null handle if not found.
        [[nodiscard]] RHI::BufferHandle Find(std::string_view name) const noexcept
        {
            if (auto it = NamedBuffers.find(std::string{name}); it != NamedBuffers.end())
                return it->second;
            return {};
        }

        // Returns the full NamedBuffer entry, or nullptr if not found.
        [[nodiscard]] const BufferEntry* FindEntry(std::string_view name) const noexcept
        {
            if (auto it = NamedBufferEntries.find(std::string{name}); it != NamedBufferEntries.end())
                return &it->second;
            return nullptr;
        }

        // Inserts or updates a buffer entry. Called by lifecycle systems only.
        void Upsert(std::string name, RHI::BufferHandle handle,
                    uint32_t elementCount, uint32_t stride)
        {
            NamedBuffers.insert_or_assign(name, handle);
            NamedBufferEntries.insert_or_assign(name,
                BufferEntry{name, handle, elementCount, stride});
        }

        void Remove(std::string_view name)
        {
            NamedBuffers.erase(std::string{name});
            NamedBufferEntries.erase(std::string{name});
        }

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
    };
}


