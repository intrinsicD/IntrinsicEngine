module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.SandboxEditorUi;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.SelectionController;

export namespace Extrinsic::Runtime
{
    enum class SandboxEditorDiagnosticCode : std::uint8_t
    {
        MissingScene,
        MissingSelectionController,
        MissingImGuiAdapter,
        AssetImportUnavailable,
        NoSelectedEntity,
        UnsupportedGeometryDomain,
        CameraRenderCommandsUnavailable,
        VisualizationCommandsUnavailable,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorDiagnosticCode(
        SandboxEditorDiagnosticCode code) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorGeometryDomain(
        ECS::Components::GeometrySources::Domain domain) noexcept;

    struct SandboxEditorDiagnostic
    {
        SandboxEditorDiagnosticCode Code{SandboxEditorDiagnosticCode::MissingScene};
        std::string                 Message{};
    };

    struct SandboxEditorEntityRow
    {
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t     StableEntityId{0u};
        std::string       Name{};
        bool              Selectable{false};
        bool              Selected{false};
        bool              Hovered{false};
        bool              HasDurableStableId{false};
        ECS::Components::StableId DurableStableId{};
    };

    struct SandboxEditorTransformModel
    {
        bool      HasLocalTransform{false};
        glm::vec3 LocalPosition{0.0f};
        glm::vec3 LocalScale{1.0f};
        bool      HasWorldTransform{false};
        glm::vec3 WorldPosition{0.0f};
    };

    struct SandboxEditorRenderHintModel
    {
        bool HasRenderSurface{false};
        bool HasRenderLines{false};
        bool HasRenderPoints{false};
    };

    struct SandboxEditorGeometryDomainModel
    {
        ECS::Components::GeometrySources::Domain Domain{
            ECS::Components::GeometrySources::Domain::None};
        bool        Valid{false};
        std::size_t VertexCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t HalfedgeCount{0u};
        std::size_t FaceCount{0u};
        std::size_t NodeCount{0u};
    };

    struct SandboxEditorInspectorModel
    {
        bool                             HasEntity{false};
        SandboxEditorEntityRow          Entity{};
        SandboxEditorTransformModel     Transform{};
        SandboxEditorRenderHintModel    RenderHints{};
        SandboxEditorGeometryDomainModel Geometry{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorSelectionModel
    {
        std::vector<std::uint32_t> SelectedStableIds{};
        bool                       HasHovered{false};
        std::uint32_t              HoveredStableId{0u};
        bool                       HasPrimitive{false};
        PrimitiveSelectionResult   Primitive{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorFileImportModel
    {
        bool        Enabled{false};
        std::string StatusText{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorCameraRenderModel
    {
        bool CameraControlsAvailable{false};
        bool RenderSettingsAvailable{false};
        bool PrimitiveViewControlsAvailable{false};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorVisualizationModel
    {
        bool GeometryDomainControlsAvailable{false};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorPanelFrame
    {
        std::vector<SandboxEditorEntityRow> Hierarchy{};
        SandboxEditorInspectorModel         Inspector{};
        SandboxEditorSelectionModel         Selection{};
        SandboxEditorFileImportModel        FileImport{};
        SandboxEditorCameraRenderModel      CameraRender{};
        SandboxEditorVisualizationModel     Visualization{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        SelectionController*  Selection{nullptr};
        const std::optional<PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        bool ImGuiAdapterAvailable{false};
        bool AssetImportCommandsAvailable{false};
        bool CameraRenderCommandsAvailable{false};
        bool VisualizationCommandsAvailable{false};
    };

    [[nodiscard]] SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context);

    bool SelectSandboxEditorEntity(const SandboxEditorContext& context,
                                   std::uint32_t stableEntityId);

    void DrawSandboxEditorPanelFrame(const SandboxEditorPanelFrame& frame);

    class SandboxEditorUi
    {
    public:
        SandboxEditorUi() = default;
        ~SandboxEditorUi();

        SandboxEditorUi(const SandboxEditorUi&)            = delete;
        SandboxEditorUi& operator=(const SandboxEditorUi&) = delete;
        SandboxEditorUi(SandboxEditorUi&&)                 = delete;
        SandboxEditorUi& operator=(SandboxEditorUi&&)      = delete;

        void Attach(Engine& engine);
        void Detach();

        [[nodiscard]] bool IsAttached() const noexcept { return m_Engine != nullptr; }
        [[nodiscard]] const SandboxEditorPanelFrame& GetLastFrame() const noexcept
        {
            return m_LastFrame;
        }

    private:
        Engine*                 m_Engine{nullptr};
        SandboxEditorPanelFrame m_LastFrame{};
    };
}
