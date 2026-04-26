module;

#include <cstdint>

export module Extrinsic.Graphics.RenderWorld;

import Extrinsic.Platform.Window;

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
    export struct RenderWorld
    {
        /// Forwarded from RenderFrameInput — needed by passes for
        /// viewport-dependent resource sizing and aspect ratio.
        Platform::Extent2D Viewport{};

        /// Interpolation alpha forwarded for motion-vector / TAA use.
        double Alpha{0.0};

        /// Extracted pick request state for this frame.
        bool HasPendingPick{false};

        /// Timeline value of the most recently completed GPU frame.
        /// Populated by EndFrame and made available here for downstream
        /// maintenance queries (deferred deletion, transfer GC).
        std::uint64_t LastCompletedGpuValue{0};

        // Future expansion slots (zero-cost when unused):
        //   std::span<const DrawPacket>  DrawPackets{};
        //   std::span<const LightData>   Lights{};
        //   PickRequestSnapshot          PickRequest{};
        //   DebugViewSnapshot            DebugView{};
    };
}
