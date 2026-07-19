module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.GizmoFrameService;

import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Platform.Input;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr int kGizmoMouseButton = 0;
        constexpr int kSelectionMouseButton = 0;

        [[nodiscard]] std::uint32_t ClampCursorPixel(
            const float value,
            const std::uint32_t extent) noexcept
        {
            if (extent == 0u || !std::isfinite(value))
                return 0u;
            const float clamped =
                std::clamp(value, 0.0f, static_cast<float>(extent - 1u));
            return static_cast<std::uint32_t>(clamped);
        }

        [[nodiscard]] bool CursorInsideViewport(
            const Platform::Input::Context::XY cursor,
            const Core::Extent2D viewport) noexcept
        {
            return viewport.Width > 0 &&
                   viewport.Height > 0 &&
                   std::isfinite(cursor.x) &&
                   std::isfinite(cursor.y) &&
                   cursor.x >= 0.0f &&
                   cursor.y >= 0.0f &&
                   cursor.x < static_cast<float>(viewport.Width) &&
                   cursor.y < static_cast<float>(viewport.Height);
        }

        [[nodiscard]] Platform::Input::Context::XY WindowToFramebufferCursor(
            const Platform::Input::Context::XY cursor,
            const Core::Extent2D windowExtent,
            const Core::Extent2D framebufferExtent) noexcept
        {
            if (windowExtent.Width <= 0 || windowExtent.Height <= 0 ||
                framebufferExtent.Width <= 0 || framebufferExtent.Height <= 0)
            {
                return cursor;
            }
            const float scaleX = static_cast<float>(framebufferExtent.Width) /
                                 static_cast<float>(windowExtent.Width);
            const float scaleY = static_cast<float>(framebufferExtent.Height) /
                                 static_cast<float>(windowExtent.Height);
            return Platform::Input::Context::XY{cursor.x * scaleX,
                                                cursor.y * scaleY};
        }

        void SubmitViewportSelectionClickForFrame(
            SelectionController& selection,
            const Platform::Input::Context& input,
            const Core::Extent2D windowExtent,
            const Core::Extent2D viewport,
            const bool imguiCapturesMouse,
            const bool gizmoCapturesMouse) noexcept
        {
            if (imguiCapturesMouse || gizmoCapturesMouse || Core::IsEmpty(viewport) ||
                !input.IsMouseButtonJustPressed(kSelectionMouseButton))
            {
                return;
            }

            const Platform::Input::Context::XY cursor =
                WindowToFramebufferCursor(
                    input.GetMousePosition(),
                    windowExtent,
                    viewport);
            if (!CursorInsideViewport(cursor, viewport))
                return;

            selection.RequestClickPick(
                ClampCursorPixel(cursor.x, viewport.Width),
                ClampCursorPixel(cursor.y, viewport.Height));
        }

        [[nodiscard]] std::uint32_t BuildGizmoModifierMask(
            const Platform::Input::Context& input) noexcept
        {
            std::uint32_t mask = 0u;
            if (input.IsKeyPressed(Platform::Input::Key::LeftShift))
                mask |= static_cast<std::uint32_t>(GizmoModifier::Snap);
            return mask;
        }

        void RebuildSelectedGizmoEntities(
            const SelectionController& selection,
            ECS::Scene::Registry& scene,
            std::vector<ECS::EntityHandle>& outSelected)
        {
            outSelected.clear();
            for (const std::uint32_t stableId : selection.SelectedStableIds())
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableId);
                if (scene.IsValid(entity))
                    outSelected.push_back(entity);
            }
        }

        void DriveGizmoInteractionForFrame(
            GizmoInteraction& gizmo,
            GizmoUndoStack& undo,
            ECS::Scene::Registry& scene,
            const Platform::Input::Context& input,
            const Graphics::CameraViewInput& cameraInput,
            const Core::Extent2D windowExtent,
            const Core::Extent2D viewport,
            const bool imguiCapturesInput,
            std::span<const ECS::EntityHandle> selected)
        {
            if (imguiCapturesInput)
            {
                gizmo.SetModifierMask(0u);
                if (gizmo.IsDragging())
                    gizmo.DragCancel(scene);
                return;
            }

            gizmo.SetModifierMask(BuildGizmoModifierMask(input));
            if (Core::IsEmpty(viewport))
            {
                if (gizmo.IsDragging())
                    gizmo.DragCancel(scene);
                return;
            }

            const Platform::Input::Context::XY cursor =
                WindowToFramebufferCursor(input.GetMousePosition(),
                                          windowExtent,
                                          viewport);
            const std::uint32_t pixelX =
                ClampCursorPixel(cursor.x, viewport.Width);
            const std::uint32_t pixelY =
                ClampCursorPixel(cursor.y, viewport.Height);
            const Graphics::CameraViewSnapshot camera =
                Graphics::BuildCameraViewSnapshot(
                    cameraInput,
                    viewport,
                    Graphics::PickPixelRequest{
                        .X = pixelX,
                        .Y = pixelY,
                        .Pending = true,
                    });
            if (!camera.Valid || !camera.HasPickRay)
            {
                if (!input.IsMouseButtonPressed(kGizmoMouseButton) &&
                    gizmo.IsDragging())
                {
                    (void)gizmo.DragCommit(scene, undo);
                }
                return;
            }

            const PickRay ray{
                .Origin = camera.PickRayOrigin,
                .Direction = camera.PickRayDirection,
            };

            if (input.IsMouseButtonJustPressed(kGizmoMouseButton))
            {
                const GizmoHitResult hit =
                    gizmo.HitTest(scene,
                                  camera,
                                  glm::vec2{cursor.x, cursor.y},
                                  viewport,
                                  selected);
                if (hit.Hit)
                    (void)gizmo.BeginDrag(scene, hit, ray, selected);
            }
            else if (input.IsMouseButtonPressed(kGizmoMouseButton) &&
                     gizmo.IsDragging())
            {
                (void)gizmo.DragTick(scene, ray);
            }
            else if (!input.IsMouseButtonPressed(kGizmoMouseButton) &&
                     gizmo.IsDragging())
            {
                (void)gizmo.DragCommit(scene, undo);
            }
        }
    }

    void GizmoFrameService::DriveInputForFrame(
        const GizmoFrameServiceInput& input)
    {
        RebuildSelectedGizmoEntities(
            input.Selection,
            input.Scene,
            m_SelectedEntities);

        const Platform::Extent2D windowExtent = input.Window.GetWindowExtent();
        DriveGizmoInteractionForFrame(m_Interaction,
                                      m_UndoStack,
                                      input.Scene,
                                      input.Window.GetInput(),
                                      input.Camera,
                                      windowExtent,
                                      input.Viewport,
                                      input.ImGuiCapturesInput,
                                      m_SelectedEntities);
        SubmitViewportSelectionClickForFrame(input.Selection,
                                             input.Window.GetInput(),
                                             windowExtent,
                                             input.Viewport,
                                             input.ImGuiCapturesMouse,
                                             m_Interaction.IsDragging());
    }

    std::span<const Graphics::TransformGizmoRenderPacket>
    GizmoFrameService::BuildRenderPackets(const ECS::Scene::Registry& scene)
    {
        return m_PacketBuilder.Build(scene,
                                     m_SelectedEntities,
                                     m_Interaction.Mode(),
                                     m_Interaction.Orientation(),
                                     m_Interaction.Config().AxisLength);
    }

    void GizmoFrameService::ClearSceneState(
        ECS::Scene::Registry* const scene)
    {
        const GizmoConfig config = m_Interaction.Config();
        const GizmoMode mode = m_Interaction.Mode();
        const GizmoOrientation orientation =
            m_Interaction.Orientation();

        if (scene != nullptr && m_Interaction.IsDragging())
            m_Interaction.DragCancel(*scene);

        m_Interaction = GizmoInteraction{config};
        m_Interaction.SetMode(mode);
        m_Interaction.SetOrientation(orientation);
        m_UndoStack.Clear();
        m_SelectedEntities.clear();
        m_PacketBuilder = TransformGizmoRenderPacketBuilder{};
    }

    GizmoInteraction& GizmoFrameService::Interaction() noexcept
    {
        return m_Interaction;
    }

    const GizmoInteraction& GizmoFrameService::Interaction() const noexcept
    {
        return m_Interaction;
    }

    GizmoUndoStack& GizmoFrameService::UndoStack() noexcept
    {
        return m_UndoStack;
    }

    const GizmoUndoStack& GizmoFrameService::UndoStack() const noexcept
    {
        return m_UndoStack;
    }
}
