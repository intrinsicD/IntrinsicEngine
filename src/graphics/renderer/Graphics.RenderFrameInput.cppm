module;

#include <algorithm>
#include <cstdint>

export module Extrinsic.Graphics.RenderFrameInput;

import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;

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

        /// Scene rectangle extent in framebuffer pixels. Scene targets, camera
        /// aspect, pick coordinates and gizmo projection all use this extent;
        /// it equals the framebuffer extent unless an editor layout reserves
        /// part of the window. May differ from window client area on HiDPI.
        Core::Extent2D Viewport{};

        /// Top-left of the scene rectangle inside the backbuffer, in
        /// framebuffer pixels. Presentation places the scene image here;
        /// pick coordinates are already relative to this origin.
        Core::Offset2D ViewportOffset{};

        /// True when a pick query is pending for this frame.
        /// Renderer pass registration may include the picking pass only
        /// when this is set.
        bool HasPendingPick{false};

        /// Pixel-space pick request owned by runtime/platform input handling.
        /// Graphics consumes it only as immutable data and may derive a world
        /// ray when a valid camera snapshot is present.
        PickPixelRequest Pick{};

        /// Interpolated camera/view data produced outside graphics.
        CameraViewInput Camera{};

        /// Enables debug/overlay post chain in the null renderer path.
        /// When false (default), presentation samples the selection output
        /// directly so optional debug visualization passes are culled.
        bool DebugOverlayEnabled{false};

        /// Immutable per-frame request for native GPU timestamp recording.
        /// Backends without native support keep this provenance-honest and
        /// report unavailable/unsupported results rather than CPU substitutes.
        bool EnableGpuProfiling{false};

        // Future expansion slots (zero-cost when unused):
        //   WorldSnapshot  World{};        — authoritative ECS snapshot
        //   InputSnapshot  Input{};        — input state at extraction time
    };

    /// Raster placement of one frame-graph pass. Scene targets are sized to
    /// the scene rectangle; the backbuffer spans the whole window.
    export enum class FramePassViewportPlacement : std::uint8_t
    {
        SceneTarget,         // scene-sized attachment, origin (0,0)
        BackbufferSceneRect, // scene image placed into the backbuffer (Present)
        BackbufferFull,      // full-window composition (ImGui)
    };

    /// Viewport/scissor rectangle for a pass. Extents are at least 1x1 and the
    /// scene rectangle is clipped to the backbuffer, so a stale layout can
    /// never address pixels outside the attachment.
    export [[nodiscard]] constexpr Core::Rect2D ResolveFramePassViewport(
        const FramePassViewportPlacement placement,
        const Core::Extent2D backbuffer,
        const Core::Extent2D scene,
        const Core::Offset2D sceneOffset) noexcept
    {
        const int backW = std::max(backbuffer.Width, 1);
        const int backH = std::max(backbuffer.Height, 1);
        switch (placement)
        {
        case FramePassViewportPlacement::SceneTarget:
            return {{0, 0}, {std::max(scene.Width, 1), std::max(scene.Height, 1)}};
        case FramePassViewportPlacement::BackbufferFull:
            return {{0, 0}, {backW, backH}};
        case FramePassViewportPlacement::BackbufferSceneRect:
            break;
        }
        const int x = std::clamp(sceneOffset.X, 0, backW - 1);
        const int y = std::clamp(sceneOffset.Y, 0, backH - 1);
        const int w = std::clamp(scene.Width, 1, backW - x);
        const int h = std::clamp(scene.Height, 1, backH - y);
        return {{x, y}, {w, h}};
    }
}
