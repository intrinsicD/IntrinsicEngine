module;

#include <cstdint>
#include <span>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.RenderWorld;

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Platform.Window;
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
    };

    export struct SelectionSnapshot
    {
        std::span<const std::uint32_t> SelectedStableIds{};
        std::uint32_t HoveredStableId{0u};
        bool HasHovered{false};
    };

    export struct ShadowSnapshot
    {
        bool Enabled{false};
        std::uint32_t CascadeCount{0u};
        std::uint32_t AtlasResolution{2048u};
    };

    export struct DebugPrimitiveSnapshot
    {
        std::uint32_t LineCount{0u};
        std::uint32_t PointCount{0u};
        bool HasTransientDebug{false};
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
        Platform::Extent2D Viewport{};

        /// Interpolation alpha forwarded for motion-vector / TAA use.
        double Alpha{0.0};

        /// Extracted pick request state for this frame.
        bool HasPendingPick{false};

        /// Enables optional overlay/debug visualization chain.
        bool DebugOverlayEnabled{false};

        /// Runtime-submitted renderable values copied by the renderer before
        /// extraction. These spans never reference ECS storage.
        std::span<const RenderableSnapshot> Renderables{};

        /// Runtime-submitted lights copied by the renderer before extraction.
        std::span<const LightSnapshot> Lights{};

        PickRequestSnapshot     PickRequest{};
        SelectionSnapshot       Selection{};
        ShadowSnapshot          Shadows{};
        DebugPrimitiveSnapshot  DebugPrimitives{};
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
