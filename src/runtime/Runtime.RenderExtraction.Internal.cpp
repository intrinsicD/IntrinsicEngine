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
import Extrinsic.Runtime.GraphGeometryPacker;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.PointCloudGeometryPacker;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.ProceduralGeometryPacker;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.SpatialDebugAdapters;
import Extrinsic.Runtime.VisualizationAdapters;
import Extrinsic.Runtime.WorldHandle;

// Single non-exported implementation-partition unit for the cache state shared by
// the independently compiled base, geometry, and adapter implementation units.
// Keeping this definition in one module unit avoids duplicate named-module
// class definitions while keeping private state out of the primary interface.

namespace Extrinsic::Runtime
{
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
    [[nodiscard]] VisualizationAdapterOptions
        BuildRenderExtractionVisualizationAdapterOptions(
            std::uint32_t stableId,
            const RenderExtractionCache::VisualizationAdapterBinding& binding,
            const Graphics::Components::VisualizationConfig* visualization);

    struct RenderExtractionCache::State
    {
        using RenderableSidecarView =
            RenderExtractionCache::RenderableSidecarView;
        using GpuRenderableAvailabilityView =
            RenderExtractionCache::GpuRenderableAvailabilityView;
        using VisualizationAdapterBindingKind =
            RenderExtractionCache::VisualizationAdapterBindingKind;
        using VisualizationAdapterBinding =
            RenderExtractionCache::VisualizationAdapterBinding;

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

        void TickProceduralGeometry(std::uint64_t currentFrame,
                                    std::uint32_t framesInFlight,
                                    Graphics::IRenderer& renderer);
        void TickMeshGeometry(std::uint64_t currentFrame,
                              std::uint32_t framesInFlight,
                              Graphics::IRenderer& renderer);
        void TickGraphGeometry(std::uint64_t currentFrame,
                               std::uint32_t framesInFlight,
                               Graphics::IRenderer& renderer);
        void TickPointCloudGeometry(std::uint64_t currentFrame,
                                    std::uint32_t framesInFlight,
                                    Graphics::IRenderer& renderer);
        void TickMeshPrimitiveViewGeometry(std::uint64_t currentFrame,
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
        [[nodiscard]] const ProceduralGeometryCache&
            GetProceduralGeometryCacheForTest() const noexcept;

        void RegisterSpatialDebugAdapter(
            std::uint64_t key,
            std::unique_ptr<ISpatialDebugAdapter> adapter);
        bool UnregisterSpatialDebugAdapter(std::uint64_t key) noexcept;
        [[nodiscard]] std::size_t GetSpatialDebugAdapterCount() const noexcept;
        [[nodiscard]] const SpatialDebugAdapterRegistry&
            GetSpatialDebugRegistryForTest() const noexcept;

        void RegisterVisualizationAdapter(
            std::uint64_t key,
            std::unique_ptr<IVisualizationAdapter> adapter);
        bool UnregisterVisualizationAdapter(std::uint64_t key) noexcept;
        [[nodiscard]] std::size_t GetVisualizationAdapterCount() const noexcept;
        [[nodiscard]] const VisualizationAdapterRegistry&
            GetVisualizationAdapterRegistryForTest() const noexcept;
        void SetVisualizationAdapterBinding(
            std::uint32_t stableEntityId,
            VisualizationAdapterBinding binding);
        void ClearVisualizationAdapterBinding(
            std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<VisualizationAdapterBinding>
            GetVisualizationAdapterBinding(
                std::uint32_t stableEntityId) const noexcept;
        [[nodiscard]] std::uint64_t
            GetVisualizationAdapterBindingRevision() const noexcept;

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
            std::optional<ProceduralGeometryKey> ProceduralKey{};
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

        struct GeometryRetireRecord
        {
            Graphics::GpuGeometryHandle Handle{};
            std::uint64_t Deadline = 0;
            bool DeadlineSet = false;
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
        void ApplyProgressivePresentationBindings(
            entt::registry& registry,
            entt::entity entity,
            const ECS::Components::GeometrySources::ConstSourceView& view,
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
            const ECS::Components::GeometrySources::ConstSourceView& view,
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool BindGraphGeometry(
            entt::registry& registry,
            entt::entity entity,
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
            RenderableSidecar& sidecar,
            Graphics::IRenderer& renderer,
            RuntimeRenderExtractionStats& stats);

        void AppendVisualizationAdapters(
            std::uint32_t stableId,
            const RenderableSidecar& sidecar,
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
        void ExtractSpatialDebug(
            entt::registry& registry,
            RuntimeRenderExtractionStats& stats);
        void FinalizeAndSubmitSnapshot(
            Graphics::IRenderer& renderer,
            std::uint32_t runtimeSnapshotStorageSlot,
            RuntimeRenderExtractionStats& stats);

        void EnqueueMeshRetire(Graphics::GpuGeometryHandle handle);
        void EnqueueGraphRetire(Graphics::GpuGeometryHandle handle);
        void EnqueuePointCloudRetire(Graphics::GpuGeometryHandle handle);
        void EnqueueMeshPrimitiveViewRetire(
            Graphics::GpuGeometryHandle handle);

        std::unordered_map<std::uint32_t, RenderableSidecar> m_Renderables{};
        std::unordered_set<std::uint32_t> m_LiveRenderableKeys{};
        std::vector<Graphics::TransformSyncRecord> m_Transforms{};
        std::vector<Graphics::VisualizationSyncRecord> m_Visualizations{};
        std::vector<Graphics::LightSnapshot> m_Lights{};
        ProceduralGeometryCache m_ProceduralGeometry{};
        ProceduralGeometryPackBuffer m_ProceduralPack{};
        ProceduralGeometryCacheStats m_PrevProceduralStats{};
        MeshPackBuffer m_MeshPack{};

        std::vector<GeometryRetireRecord> m_MeshRetire{};
        std::uint32_t m_MeshFreeRetires{0};
        std::uint32_t m_PrevMeshFreeRetires{0};

        GraphPackBuffer m_GraphPack{};
        std::vector<GeometryRetireRecord> m_GraphRetire{};
        std::uint32_t m_GraphFreeRetires{0};
        std::uint32_t m_PrevGraphFreeRetires{0};

        PointCloudPackBuffer m_PointCloudPack{};
        std::vector<GeometryRetireRecord> m_PointCloudRetire{};
        std::uint32_t m_PointCloudFreeRetires{0};
        std::uint32_t m_PrevPointCloudFreeRetires{0};

        MeshPrimitiveViewBuffer m_MeshPrimitiveViewPack{};
        std::vector<GeometryRetireRecord> m_MeshPrimitiveViewRetire{};
        std::uint32_t m_MeshPrimitiveViewFreeRetires{0};
        std::uint32_t m_PrevMeshPrimitiveViewFreeRetires{0};
        std::unordered_map<
            std::uint32_t,
            Graphics::MaterialTextureAssetBindings>
            m_MaterialTextureBindings{};

        std::unordered_map<
            std::uint64_t,
            std::unique_ptr<ISpatialDebugAdapter>>
            m_SpatialDebugAdapters{};
        SpatialDebugAdapterRegistry m_SpatialDebugRegistry{};
        SpatialDebugSnapshotBatch m_SpatialDebugBatch{};

        struct VisualizationAdapterState
        {
            std::unordered_map<
                std::uint64_t,
                std::unique_ptr<IVisualizationAdapter>>
                Adapters{};
            VisualizationAdapterRegistry Registry{};
            std::unordered_map<
                std::uint32_t,
                VisualizationAdapterBinding>
                Bindings{};
            std::uint64_t BindingRevision{0u};
            VisualizationAdapterBatch Batch{};
        };
        std::unique_ptr<VisualizationAdapterState> m_VisualizationState{};

        RuntimeSceneInteractionRenderSnapshot m_SceneInteraction{};
        RuntimeRenderExtractionStats m_LastStats{};
    };
}
