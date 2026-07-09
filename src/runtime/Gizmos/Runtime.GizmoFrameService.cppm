module;

#include <span>
#include <vector>

export module Extrinsic.Runtime.GizmoFrameService;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Platform.Window;
export import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.SelectionController;

export namespace Extrinsic::Runtime
{
    struct GizmoFrameServiceInput
    {
        ECS::Scene::Registry& Scene;
        SelectionController& Selection;
        const Platform::IWindow& Window;
        Platform::Extent2D Viewport{};
        bool ImGuiCapturesInput{false};
        bool ImGuiCapturesMouse{false};
        const Graphics::CameraViewInput& Camera;
    };

    class GizmoFrameService
    {
    public:
        GizmoFrameService() = default;

        GizmoFrameService(const GizmoFrameService&) = delete;
        GizmoFrameService& operator=(const GizmoFrameService&) = delete;
        GizmoFrameService(GizmoFrameService&&) = delete;
        GizmoFrameService& operator=(GizmoFrameService&&) = delete;

        void DriveInputForFrame(const GizmoFrameServiceInput& input);
        [[nodiscard]] std::span<const Graphics::TransformGizmoRenderPacket>
            BuildRenderPackets(const ECS::Scene::Registry& scene);

        [[nodiscard]] GizmoInteraction& Interaction() noexcept;
        [[nodiscard]] const GizmoInteraction& Interaction() const noexcept;
        [[nodiscard]] GizmoUndoStack& UndoStack() noexcept;
        [[nodiscard]] const GizmoUndoStack& UndoStack() const noexcept;

    private:
        GizmoInteraction m_Interaction{};
        GizmoUndoStack m_UndoStack{};
        TransformGizmoRenderPacketBuilder m_PacketBuilder{};
        std::vector<ECS::EntityHandle> m_SelectedEntities{};
    };
}
