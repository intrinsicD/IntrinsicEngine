module;

#include <cstdint>

export module Extrinsic.Runtime.MeshPrimitiveView;

export namespace Extrinsic::Runtime
{
    enum class MeshVertexViewRenderMode : std::uint8_t
    {
        FlatCircle,
        SurfaceAlignedCircle,
        ImpostorSphere,
    };

    // Pointer-free runtime/editor value used by command history and editor
    // models. Extraction itself is component-driven by RenderEdges and
    // RenderPoints; no GPU handle or packer surface is exposed here.
    struct MeshPrimitiveViewSettings
    {
        bool EnableEdgeView = false;
        bool EnableVertexView = false;
        MeshVertexViewRenderMode VertexRenderMode =
            MeshVertexViewRenderMode::ImpostorSphere;
        float VertexPointRadiusPx = 6.0f;

        [[nodiscard]] bool AnyEnabled() const noexcept
        {
            return EnableEdgeView || EnableVertexView;
        }
    };
}
