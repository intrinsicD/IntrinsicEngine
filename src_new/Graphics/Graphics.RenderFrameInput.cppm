module;

export module Extrinsic.Graphics.RenderFrameInput;

import Extrinsic.Platform.Window;

// ============================================================
// RenderFrameInput — immutable snapshot handed to the renderer
// at the start of each frame's extraction phase.
//
// Constructed by Runtime::Engine after all simulation ticks
// have committed.  The renderer consumes it read-only inside
// ExtractRenderWorld(); no pointer to mutable engine state
// survives beyond that call.
// ============================================================

namespace Extrinsic::Graphics
{
    export struct RenderFrameInput
    {
        /// Interpolation blend factor in [0, 1).
        /// alpha = accumulator / fixed_dt
        /// 0 = purely at last committed tick, approaching 1 = nearly at next tick.
        double Alpha{0.0};

        /// Framebuffer extent at the moment extraction begins.
        /// May differ from window client area on HiDPI displays.
        Platform::Extent2D Viewport{};

        // Future expansion slots (zero-cost when unused):
        //   WorldSnapshot  World{};        — authoritative ECS snapshot
        //   InputSnapshot  Input{};        — input state at extraction time
        //   CameraParams   Camera{};       — interpolated camera
    };
}

