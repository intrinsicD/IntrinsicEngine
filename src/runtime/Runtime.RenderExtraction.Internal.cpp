module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.RenderExtraction:Internal;

import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GeometryResidency;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPlanBuilders;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.WorldHandle;

// Single non-exported implementation-partition unit for the cache state shared by
// the independently compiled base, geometry, and adapter implementation units.
// Keeping this definition in one module unit avoids duplicate named-module
// class definitions while keeping private state out of the primary interface.

namespace Extrinsic::Runtime
{
    enum class RenderExtractionGeometryResidencyKind : std::uint64_t
    {
        Mesh = 1u,
        Graph = 2u,
        PointCloud = 3u,
        Procedural = 4u,
        MeshPrimitiveView = 5u,
    };

    [[nodiscard]] Graphics::GeometryResidencyKey
        BuildRenderExtractionGeometryResidencyKey(
            RenderExtractionGeometryResidencyKind kind,
            std::uint64_t identity,
            std::uint32_t lane = 0u) noexcept;

    struct RenderExtractionGeometryDirtyPlan
    {
        bool Dirty = false;
        bool RequiresFullUpload = false;
        bool MeshPrimitiveViewDirty = false;
        Graphics::GpuWorld::GeometryChannelUpdateMask Channels{};
    };

    struct RenderExtractionMeshTexcoordFallbackDiagnostics
    {
        bool MissingOrMismatched = false;
        bool NonFinite = false;
    };

    [[nodiscard]] RenderExtractionGeometryDirtyPlan
        BuildRenderExtractionMeshGeometryDirtyPlan(
            const entt::registry& registry,
            entt::entity entity);
    [[nodiscard]] RenderExtractionGeometryDirtyPlan
        BuildRenderExtractionGraphGeometryDirtyPlan(
            const entt::registry& registry,
            entt::entity entity);
    [[nodiscard]] RenderExtractionGeometryDirtyPlan
        BuildRenderExtractionPointCloudGeometryDirtyPlan(
            const entt::registry& registry,
            entt::entity entity);
    [[nodiscard]] RenderExtractionMeshTexcoordFallbackDiagnostics
        DiagnoseRenderExtractionMeshTexcoordFallback(
            const ECS::Components::GeometrySources::ConstSourceView& view) noexcept;
    [[nodiscard]] RHI::GpuEntityConfig
        BuildRenderExtractionImmediateLaneConfig(
            const Graphics::Components::VisualizationConfig* visualization,
            const Graphics::Components::RenderEdges* edges,
            const Graphics::Components::RenderPoints* points) noexcept;
    [[nodiscard]] bool IsRenderExtractionScalarVisualizationSource(
        const Graphics::Components::VisualizationConfig* visualization) noexcept;
    [[nodiscard]] bool IsRenderExtractionColorBufferVisualizationSource(
        const Graphics::Components::VisualizationConfig* visualization) noexcept;
    struct RenderExtractionCache::State
    {
        using RenderableSidecarView =
            RenderExtractionCache::RenderableSidecarView;
        using GpuRenderableAvailabilityView =
            RenderExtractionCache::GpuRenderableAvailabilityView;
        State();
        ~State();

        State(const State&) = delete;
        State& operator=(const State&) = delete;

        [[nodiscard]] RuntimeRenderExtractionStats ExtractAndSubmit(
            ECS::Scene::Registry& scene,
            Graphics::IRenderer& renderer,
            Graphics::GpuAssetCache* gpuAssets,
            std::uint32_t runtimeSnapshotStorageSlot,
            WorldHandle world);
        void SubmitSceneInteractionSnapshot(
            const RuntimeSceneInteractionRenderSnapshot& snapshot);
        void ClearSceneState(Graphics::IRenderer& renderer);
        void Shutdown(Graphics::IRenderer& renderer);

        void TickGeometryResidency(std::uint64_t currentFrame,
                                   std::uint32_t framesInFlight,
                                   Graphics::IRenderer& renderer);

        void SetMaterialTextureAssetBindings(
            std::uint32_t stableEntityId,
            Graphics::MaterialTextureAssetBindings bindings);
        void ClearMaterialTextureAssetBindings(
            std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<Graphics::MaterialTextureAssetBindings>
            GetMaterialTextureAssetBindings(
                std::uint32_t stableEntityId) const noexcept;

        [[nodiscard]] const RuntimeRenderExtractionStats& GetLastStats() const noexcept;
        [[nodiscard]] std::uint32_t GetTrackedRenderableCount() const noexcept;
        [[nodiscard]] std::size_t
            GetLiveRenderableKeyScratchBucketCountForTest() const noexcept;
        [[nodiscard]] std::optional<RenderableSidecarView>
            FindRenderableSidecarForTest(
                std::uint32_t stableEntityId) const noexcept;
        [[nodiscard]] std::optional<GpuRenderableAvailabilityView>
            FindGpuRenderableAvailability(
                std::uint32_t stableEntityId) const noexcept;
        void SetVisualizationRecipe(
            std::uint32_t stableEntityId,
            VisualizationRecipe recipe);
        void ClearVisualizationRecipe(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<VisualizationRecipe>
            GetVisualizationRecipe(std::uint32_t stableEntityId) const noexcept;
        [[nodiscard]] std::uint64_t GetVisualizationRecipeRevision() const noexcept;

        struct RenderableSidecar
        {
            Graphics::GpuInstanceHandle Instance{};
            Graphics::Components::GpuSceneSlot GpuSlot{};
            Graphics::Components::MaterialInstance Material{};
            Graphics::Components::VisualizationConfig Visualization{};
            bool HasVisualization{false};
            Graphics::Components::VisualizationLaneOverrides
                VisualizationOverrides{};
            bool HasVisualizationOverrides{false};
            Graphics::GpuGeometryHandle Geometry{};
            std::optional<Graphics::GeometryResidencyKey> ProceduralKey{};
            Graphics::GpuGeometryHandle MeshGeometry{};
            Graphics::GpuGeometryHandle GraphGeometry{};
            bool GraphPackedLines{false};
            bool GraphPackedPoints{false};
            Graphics::GpuInstanceHandle GraphPointLaneInstance{};
            Graphics::GpuGeometryHandle PointCloudGeometry{};
            Graphics::GpuInstanceHandle MeshEdgeViewInstance{};
            Graphics::GpuGeometryHandle MeshEdgeViewGeometry{};
            Graphics::GpuInstanceHandle MeshVertexViewInstance{};
            Graphics::GpuGeometryHandle MeshVertexViewGeometry{};
        };

        enum class MeshPrimitiveViewKind : std::uint8_t
        {
            Edge,
            Vertex,
        };

        [[nodiscard]] RenderableSidecar* EnsureRenderable(
            std::uint32_t stableId,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        void ApplyMaterialTextureBindings(
            std::uint32_t stableId,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            Graphics::GpuAssetCache* gpuAssets,
            RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool ApplyGeometryPresentation(
            entt::registry& registry,
            entt::entity entity,
            std::uint32_t stableId,
            const GeometryEntityAvailability& availability,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            Graphics::GpuAssetCache* gpuAssets,
            RuntimeRenderExtractionStats& stats);
        void RetireMissingRenderables(
            const std::unordered_set<std::uint32_t>& liveKeys,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);

        [[nodiscard]] bool BindProceduralGeometry(
            const ECS::Components::ProceduralGeometryRef& ref,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool BindMeshGeometry(
            entt::registry& registry,
            entt::entity entity,
            std::uint32_t stableId,
            const ECS::Components::GeometrySources::ConstSourceView& view,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool BindGraphGeometry(
            entt::registry& registry,
            entt::entity entity,
            std::uint32_t stableId,
            const ECS::Components::GeometrySources::ConstSourceView& view,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool EnsureGraphPointLaneInstance(
            RenderableSidecar& sidecar,
            std::uint32_t stableId,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        void ReleaseGraphPointLaneInstance(
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool BindPointCloudGeometry(
            entt::registry& registry,
            entt::entity entity,
            std::uint32_t stableId,
            const ECS::Components::GeometrySources::ConstSourceView& view,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool ReconcileMeshPrimitiveView(
            MeshPrimitiveViewKind kind,
            const ECS::Components::GeometrySources::ConstSourceView& view,
            RenderableSidecar& sidecar,
            const glm::mat4& model,
            std::uint32_t materialSlot,
            const RHI::GpuBounds& bounds,
            std::uint32_t stableId,
            bool desired,
            const Graphics::Components::RenderEdges* edges,
            const Graphics::Components::RenderPoints* points,
            const Graphics::Components::VisualizationConfig* visualization,
            bool meshDirty,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        void ReleaseMeshPrimitiveView(
            MeshPrimitiveViewKind kind,
            std::uint32_t stableId,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);

        void AppendVisualizationRecipe(
            const GeometryEntityAvailability& availability,
            const VisualizationRecipe& recipe,
            RuntimeRenderExtractionStats& stats);
        void ExtractLightsForEntity(
            entt::registry& registry,
            entt::entity entity,
            const glm::mat4& worldMatrix);
        void ReconcileRenderableEntity(
            entt::registry& registry,
            entt::entity entity,
            const glm::mat4& worldMatrix,
            Graphics::IRenderer& renderer,
            Graphics::GpuAssetCache* gpuAssets,
            RuntimeRenderExtractionStats& stats);
        void FinalizeAndSubmitSnapshot(
            Graphics::IRenderer& renderer,
            std::uint32_t runtimeSnapshotStorageSlot,
            RuntimeRenderExtractionStats& stats);

        [[nodiscard]] Graphics::GeometryResidencyCoordinator&
            EnsureGeometryResidency(Graphics::IRenderer& renderer);
        [[nodiscard]] std::uint64_t IssueGeometryPlanGeneration() noexcept;
        [[nodiscard]] bool ReleaseGeometryResidency(
            Graphics::GeometryResidencyKey key);

        std::unordered_map<std::uint32_t, RenderableSidecar> m_Renderables{};
        std::unordered_set<std::uint32_t> m_LiveRenderableKeys{};
        std::vector<Graphics::TransformSyncRecord> m_Transforms{};
        std::vector<Graphics::VisualizationSyncRecord> m_Visualizations{};
        std::vector<Graphics::LightSnapshot> m_Lights{};
        std::unique_ptr<Graphics::GeometryResidencyCoordinator>
            m_GeometryResidency{};
        Graphics::GpuWorld* m_GeometryResidencyWorld{nullptr};
        std::uint64_t m_NextGeometryPlanGeneration{1u};
        ProceduralGeometryPackBuffer m_ProceduralPack{};
        std::uint32_t m_ProceduralFreeRetires{0};
        std::uint32_t m_PrevProceduralFreeRetires{0};
        MeshPackBuffer m_MeshPack{};

        std::uint32_t m_MeshFreeRetires{0};
        std::uint32_t m_PrevMeshFreeRetires{0};

        GraphPackBuffer m_GraphPack{};
        std::uint32_t m_GraphFreeRetires{0};
        std::uint32_t m_PrevGraphFreeRetires{0};

        PointCloudPackBuffer m_PointCloudPack{};
        std::uint32_t m_PointCloudFreeRetires{0};
        std::uint32_t m_PrevPointCloudFreeRetires{0};

        MeshPrimitiveViewBuffer m_MeshPrimitiveViewPack{};
        std::uint32_t m_MeshPrimitiveViewFreeRetires{0};
        std::uint32_t m_PrevMeshPrimitiveViewFreeRetires{0};
        std::unordered_map<
            std::uint32_t,
            Graphics::MaterialTextureAssetBindings>
            m_MaterialTextureBindings{};

        struct VisualizationRecipeState
        {
            std::unordered_map<std::uint32_t, VisualizationRecipe> Recipes{};
            std::uint64_t RecipeRevision{0u};
            VisualizationEncodingBatch Batch{};
        };
        std::unique_ptr<VisualizationRecipeState> m_VisualizationState{};

        RuntimeSceneInteractionRenderSnapshot m_SceneInteraction{};
        RuntimeRenderExtractionStats m_LastStats{};
    };
}
