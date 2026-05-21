module;

#include <cstdint>
#include <span>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.RenderWorld;

import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.Types;

// ============================================================
// RenderWorld — immutable extracted render state for one frame.
//
// Produced by IRenderer::ExtractRenderWorld(RenderFrameInput).
// Consumed (read-only) by PrepareFrame, ExecuteFrame, EndFrame.
// Lifetime: created per-frame, destroyed before the next
// BeginFrame call.  No mutable references to ECS or asset state
// survive inside this type.
//
// Currently a minimal skeleton; grows as GpuWorld, draw-packet
// lists, light environment, and picking snapshots are wired in.
// ============================================================

namespace Extrinsic::Graphics
{
    export struct RenderableSnapshot
    {
        std::uint32_t     StableId{0u};
        GpuInstanceHandle Instance{};
        GpuGeometryHandle Geometry{};
        glm::mat4         Model{1.f};
        RHI::GpuBounds    Bounds{};
        std::uint32_t     RenderFlags{RHI::GpuRender_Visible | RHI::GpuRender_Opaque};
        std::uint32_t     MaterialSlot{0u};
        bool              HasMaterialSlot{false};
    };

    export struct PickRequestSnapshot
    {
        bool Pending{false};
        std::uint32_t X{0u};
        std::uint32_t Y{0u};
        glm::vec3 RayOrigin{0.f};
        glm::vec3 RayDirection{0.f, 0.f, -1.f};
        bool HasRay{false};
    };

    export struct SelectionSnapshot
    {
        std::span<const std::uint32_t> SelectedStableIds{};
        std::uint32_t HoveredStableId{0u};
        bool HasHovered{false};

        // GRAPHICS-074 Slice D.4 — outline visual style routed through the
        // `selection_outline.frag` push block. Defaults mirror the legacy
        // `Graphics::Passes::SelectionOutlineSettings`: orange selection,
        // light-blue semi-transparent hover, 2-texel solid outline, subtle
        // hover fill, no selection fill, and pulse/glow ranges that remain
        // ignored while `OutlineMode == 0` (Solid). The runtime fills these
        // when it has user-controlled outline preferences; default-snapshot
        // frames still produce a visible outline once the recipe declares
        // `SelectionOutlinePass` (i.e. when a hover or selection exists).
        glm::vec4     OutlineColor{1.0f, 0.6f, 0.0f, 1.0f};
        glm::vec4     HoverColor{0.3f, 0.7f, 1.0f, 0.8f};
        float         OutlineWidth{2.0f};
        std::uint32_t OutlineMode{0u}; // 0=Solid, 1=Pulse, 2=Glow
        float         SelectionFillAlpha{0.0f};
        float         HoverFillAlpha{0.08f};
        float         PulsePhase{0.0f};
        float         PulseMin{0.4f};
        float         PulseMax{1.0f};
        float         GlowFalloff{2.0f};
    };

    export struct ShadowSnapshot
    {
        bool Enabled{false};
        std::uint32_t CascadeCount{0u};
        std::uint32_t AtlasResolution{2048u};
    };

    export struct DebugLinePacket
    {
        glm::vec3 Start{0.f};
        glm::vec3 End{0.f};
        glm::vec4 Color{1.f};
        float Width{1.f};
        bool DepthTested{true};
    };

    export struct DebugPointPacket
    {
        glm::vec3 Position{0.f};
        glm::vec4 Color{1.f};
        float Radius{0.01f};
        bool DepthTested{true};
    };

    export struct DebugTrianglePacket
    {
        glm::vec3 A{0.f};
        glm::vec3 B{0.f};
        glm::vec3 C{0.f};
        glm::vec4 Color{1.f};
        bool DepthTested{true};
    };

    export struct DebugPrimitiveSnapshot
    {
        std::span<const DebugLinePacket> Lines{};
        std::span<const DebugPointPacket> Points{};
        std::span<const DebugTrianglePacket> Triangles{};
        std::uint32_t LineCount{0u};
        std::uint32_t PointCount{0u};
        std::uint32_t TriangleCount{0u};
        bool HasTransientDebug{false};
    };

    export struct TransformGizmoRenderPacket
    {
        std::uint32_t StableId{0u};
        glm::mat4 Transform{1.f};
        float AxisLength{1.f};
        glm::vec4 XAxisColor{1.f, 0.f, 0.f, 1.f};
        glm::vec4 YAxisColor{0.f, 1.f, 0.f, 1.f};
        glm::vec4 ZAxisColor{0.f, 0.25f, 1.f, 1.f};
        bool ShowTranslate{true};
        bool ShowRotate{false};
        bool ShowScale{false};
    };

    export struct GizmoRenderSnapshot
    {
        std::span<const TransformGizmoRenderPacket> TransformGizmos{};
        std::uint32_t TransformGizmoCount{0u};
        bool HasGizmos{false};
    };

    export struct VisualizationSnapshot
    {
        std::span<const VisualizationAttributeBufferPacket> AttributeBuffers{};
        std::span<const ScalarAttributePacket> Scalars{};
        std::span<const ColorAttributePacket> Colors{};
        std::span<const VectorFieldOverlayPacket> VectorFields{};
        std::span<const IsolineOverlayPacket> Isolines{};
        std::span<const HtexPatchPreviewAtlasPacket> HtexAtlases{};
        std::span<const FragmentBakeAtlasPacket> FragmentBakeAtlases{};
        VisualizationDiagnostics Diagnostics{};
        VisualizationOverlaySummary OverlaySummary{};
        bool HasVisualizationPackets{false};
    };

    export struct PostProcessSnapshot
    {
        bool Enabled{false};
        bool RequiresReadback{false};
    };

    export struct RenderWorld
    {
        /// Forwarded from RenderFrameInput — needed by passes for
        /// viewport-dependent resource sizing and aspect ratio.
        Core::Extent2D Viewport{};

        /// Interpolation alpha forwarded for motion-vector / TAA use.
        double Alpha{0.0};

        /// Extracted pick request state for this frame.
        bool HasPendingPick{false};

        /// Enables optional overlay/debug visualization chain.
        bool DebugOverlayEnabled{false};

        CameraViewSnapshot Camera{};

        /// Runtime-submitted renderable values copied by the renderer before
        /// extraction. These spans never reference ECS storage.
        std::span<const RenderableSnapshot> Renderables{};

        /// Runtime-submitted lights copied by the renderer before extraction.
        std::span<const LightSnapshot> Lights{};

        PickRequestSnapshot     PickRequest{};
        SelectionSnapshot       Selection{};
        ShadowSnapshot          Shadows{};
        DebugPrimitiveSnapshot  DebugPrimitives{};
        GizmoRenderSnapshot     Gizmos{};
        VisualizationSnapshot   Visualization{};
        PostProcessSnapshot     PostProcess{};

        /// Number of malformed/invalid runtime records dropped while building
        /// the immutable render-world spans.
        std::uint32_t InvalidSnapshotRecordCount{0};

        /// Timeline value of the most recently completed GPU frame.
        /// Populated by EndFrame and made available here for downstream
        /// maintenance queries (deferred deletion, transfer GC).
        std::uint64_t LastCompletedGpuValue{0};

        // Future expansion slots (zero-cost when unused):
        //   std::span<const DrawPacket>  DrawPackets{};
        //   DebugViewSnapshot            DebugView{};
    };
}
