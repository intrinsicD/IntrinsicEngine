module;

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ProgressivePoissonReference.hpp"

module Extrinsic.Runtime.VisualizationEditingOperations;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.GeometryPayload;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Structure;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.UvView;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetWorkflowRecipePolicies;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Graph;
import Geometry.Graph.Vertex.Normals;
import Geometry.Curvature;
import Geometry.CatmullClark;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.AdaptiveRemeshing;
import Geometry.HalfedgeMesh.SubdivisionSqrt3;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.Mesh.Conversion;
import Geometry.MeshOperator;
import Geometry.MeshSoup;
import Geometry.PointCloud;
import Geometry.PointCloud.Normals;
import Geometry.PointCloud.SurfaceSampling;
import Geometry.PointCloud.Utils;
import Geometry.Properties;
import Geometry.Registration;
import Geometry.Remeshing;
import Geometry.Simplification;
import Geometry.Smoothing;
import Geometry.Subdivision;
import Geometry.UvAtlas;

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"

namespace Extrinsic::Runtime {
namespace {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;
        namespace GN = Geometry::HalfedgeMesh::VertexNormals;
        namespace GraphNormals = Geometry::Graph::VertexNormals;

        using EditorModelBuildClock = std::chrono::steady_clock;

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
            const entt::registry& raw,
            std::uint32_t stableId);

        [[nodiscard]] std::uint64_t EditorElapsedNs(
            const EditorModelBuildClock::time_point start) noexcept
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    EditorModelBuildClock::now() - start)
                    .count();
            return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 1u;
        }

        class ScopedEditorStatTimer final
        {
        public:
            explicit ScopedEditorStatTimer(std::uint64_t* target) noexcept
                : m_Target(target)
            {
                if (m_Target != nullptr)
                    m_Start = EditorModelBuildClock::now();
            }

            ScopedEditorStatTimer(const ScopedEditorStatTimer&) = delete;
            ScopedEditorStatTimer& operator=(const ScopedEditorStatTimer&) = delete;

            ~ScopedEditorStatTimer()
            {
                if (m_Target != nullptr)
                    *m_Target += EditorElapsedNs(m_Start);
            }

        private:
            std::uint64_t* m_Target{nullptr};
            EditorModelBuildClock::time_point m_Start{};
        };


        [[nodiscard]] G::VisualizationConfig ToVisualizationConfig(
            const EditorVisualizationConfigCommand& command)
        {
            G::VisualizationConfig config{};
            config.Source = command.Source;
            config.Color = command.Color;
            config.ScalarFieldName = command.ScalarFieldName;
            config.ScalarDomain = command.ScalarDomain;
            config.ColorBufferName = command.ColorBufferName;
            config.Scalar.AutoRange = command.ScalarAutoRange;
            config.Scalar.RangeMin = command.ScalarRangeMin;
            config.Scalar.RangeMax = command.ScalarRangeMax;
            config.Scalar.BinCount = command.ScalarBinCount;
            config.Scalar.Map = command.ScalarColormap;
            config.Scalar.Isolines.Num = command.IsolineCount;
            config.Scalar.Isolines.Width = command.IsolineWidth;
            config.Scalar.Isolines.Color = command.IsolineColor;
            config.Scalar.Isolines.Values = command.IsolineValues;
            config.Scalar.Isolines.ValueCount = std::min<std::uint32_t>(
                command.IsolineValueCount,
                G::ScalarFieldConfig::kMaxIsolineValues);
            return config;
        }

        [[nodiscard]] const std::optional<G::VisualizationConfig>*
        LaneOverrideForTarget(const G::VisualizationLaneOverrides& overrides,
                              const EditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case EditorVisualizationTarget::Surface:
                return &overrides.Surface;
            case EditorVisualizationTarget::Edges:
                return &overrides.Edges;
            case EditorVisualizationTarget::Points:
                return &overrides.Points;
            case EditorVisualizationTarget::Entity:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>*
        MutableLaneOverrideForTarget(G::VisualizationLaneOverrides& overrides,
                                     const EditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case EditorVisualizationTarget::Surface:
                return &overrides.Surface;
            case EditorVisualizationTarget::Edges:
                return &overrides.Edges;
            case EditorVisualizationTarget::Points:
                return &overrides.Points;
            case EditorVisualizationTarget::Entity:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>
        StoredVisualizationConfigForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const EditorVisualizationTarget target)
        {
            if (target == EditorVisualizationTarget::Entity)
            {
                if (const auto* config = raw.try_get<G::VisualizationConfig>(entity))
                    return *config;
                return std::nullopt;
            }

            const auto* overrides =
                raw.try_get<G::VisualizationLaneOverrides>(entity);
            if (overrides == nullptr)
                return std::nullopt;

            const std::optional<G::VisualizationConfig>* lane =
                LaneOverrideForTarget(*overrides, target);
            return lane != nullptr ? *lane : std::nullopt;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>
        EffectiveVisualizationConfigForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const EditorVisualizationTarget target)
        {
            if (std::optional<G::VisualizationConfig> stored =
                    StoredVisualizationConfigForTarget(raw, entity, target);
                stored.has_value())
            {
                return stored;
            }
            if (target == EditorVisualizationTarget::Entity)
                return std::nullopt;
            return StoredVisualizationConfigForTarget(
                raw,
                entity,
                EditorVisualizationTarget::Entity);
        }


        [[nodiscard]] EditorCommandHistoryStatus ApplyVisualizationConfigTarget(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const EditorVisualizationTarget target,
            const std::optional<G::VisualizationConfig>& config)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            if (target == EditorVisualizationTarget::Entity)
            {
                if (config.has_value())
                    raw.emplace_or_replace<G::VisualizationConfig>(entity, *config);
                else
                    raw.remove<G::VisualizationConfig>(entity);
                return EditorCommandHistoryStatus::Applied;
            }

            if (!config.has_value() &&
                !raw.all_of<G::VisualizationLaneOverrides>(entity))
            {
                return EditorCommandHistoryStatus::Applied;
            }

            auto& overrides =
                raw.get_or_emplace<G::VisualizationLaneOverrides>(entity);
            std::optional<G::VisualizationConfig>* lane =
                MutableLaneOverrideForTarget(overrides, target);
            if (lane == nullptr)
                return EditorCommandHistoryStatus::UnsupportedOperation;

            *lane = config;
            if (overrides.Empty())
                raw.remove<G::VisualizationLaneOverrides>(entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] bool SameVisualizationConfig(
            const G::VisualizationConfig& lhs,
            const G::VisualizationConfig& rhs) noexcept
        {
            if (lhs.Scalar.Isolines.ValueCount != rhs.Scalar.Isolines.ValueCount)
                return false;
            for (std::uint32_t i = 0u; i < lhs.Scalar.Isolines.ValueCount; ++i)
            {
                if (lhs.Scalar.Isolines.Values[i] != rhs.Scalar.Isolines.Values[i])
                    return false;
            }
            return lhs.Source == rhs.Source &&
                   lhs.Color.x == rhs.Color.x &&
                   lhs.Color.y == rhs.Color.y &&
                   lhs.Color.z == rhs.Color.z &&
                   lhs.Color.w == rhs.Color.w &&
                   lhs.ScalarFieldName == rhs.ScalarFieldName &&
                   lhs.ScalarDomain == rhs.ScalarDomain &&
                   lhs.ColorBufferName == rhs.ColorBufferName &&
                   lhs.Scalar.Map == rhs.Scalar.Map &&
                   lhs.Scalar.AutoRange == rhs.Scalar.AutoRange &&
                   lhs.Scalar.RangeMin == rhs.Scalar.RangeMin &&
                   lhs.Scalar.RangeMax == rhs.Scalar.RangeMax &&
                   lhs.Scalar.BinCount == rhs.Scalar.BinCount &&
                   lhs.Scalar.Isolines.Num == rhs.Scalar.Isolines.Num &&
                   lhs.Scalar.Isolines.Width == rhs.Scalar.Isolines.Width &&
                   lhs.Scalar.Isolines.Color == rhs.Scalar.Isolines.Color;
        }

        [[nodiscard]] bool SameOptionalVisualizationConfig(
            const std::optional<G::VisualizationConfig>& lhs,
            const std::optional<G::VisualizationConfig>& rhs) noexcept
        {
            if (lhs.has_value() != rhs.has_value())
                return false;
            return !lhs.has_value() || SameVisualizationConfig(*lhs, *rhs);
        }

        struct EditorVisualizationMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            EditorVisualizationTarget Target{
                EditorVisualizationTarget::Entity};
        };

        [[nodiscard]] EditorCommandHistoryResult
        ExecuteEditorVisualizationMutation(
            EditorCommandHistory& history,
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const EditorVisualizationTarget target,
            const std::optional<G::VisualizationConfig>& before,
            const std::optional<G::VisualizationConfig>& after,
            std::string label)
        {
            return Internal::ExecuteUndoableEntityMutation(
                history,
                std::move(label),
                EditorVisualizationMutationIdentity{
                    .Scene = scene,
                    .World = world,
                    .StableEntityId = stableEntityId,
                    .Target = target,
                },
                before,
                before,
                after,
                [](
                    const EditorVisualizationMutationIdentity& identity,
                    const std::optional<G::VisualizationConfig>& expected,
                    const std::optional<G::VisualizationConfig>&)
                {
                    if (identity.Scene == nullptr || !identity.World.IsValid())
                        return EditorCommandHistoryStatus::MissingScene;

                    entt::registry& raw = identity.Scene->Raw();
                    const ECS::EntityHandle entity =
                        SelectionController::ToEntityHandle(
                            identity.StableEntityId);
                    if (entity == ECS::InvalidEntityHandle ||
                        !raw.valid(entity))
                    {
                        return EditorCommandHistoryStatus::StaleEntity;
                    }

                    return SameOptionalVisualizationConfig(
                               StoredVisualizationConfigForTarget(
                                   raw,
                                   entity,
                                   identity.Target),
                               expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                },
                [](
                    const EditorVisualizationMutationIdentity& identity,
                    const std::optional<G::VisualizationConfig>& targetConfig)
                {
                    return ApplyVisualizationConfigTarget(
                        identity.Scene,
                        identity.StableEntityId,
                        identity.Target,
                        targetConfig);
                },
                [](
                    const EditorVisualizationMutationIdentity&,
                    const std::optional<G::VisualizationConfig>&,
                    const std::optional<G::VisualizationConfig>& targetConfig)
                {
                    return targetConfig;
                });
        }

        [[nodiscard]] bool IsInternalVisualizationProperty(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == GS::PropertyNames::kNormal ||
                   name == GS::PropertyNames::kVertexConnectivity ||
                   name == GS::PropertyNames::kEdgeV0 ||
                   name == GS::PropertyNames::kEdgeV1 ||
                   name == GS::PropertyNames::kHalfedgeToVertex ||
                   name == GS::PropertyNames::kHalfedgeNext ||
                   name == GS::PropertyNames::kHalfedgeFace ||
                   name == GS::PropertyNames::kHalfedgeConnectivity ||
                   name == GS::PropertyNames::kFaceHalfedge ||
                   name == "v:point" ||
                   name == "v:tex" ||
                   name == "v:texcoord" ||
                   // BUG-137 — same reserved property, on the domain that can
                   // carry a seam.
                   name == "h:texcoord" ||
                   name == "h:normal" ||
                   name == "p:position" ||
                   name == "p:normal";
        }

        [[nodiscard]] bool IsConnectivityVisualizationProperty(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == GS::PropertyNames::kVertexConnectivity ||
                   name == GS::PropertyNames::kEdgeV0 ||
                   name == GS::PropertyNames::kEdgeV1 ||
                   name == GS::PropertyNames::kHalfedgeToVertex ||
                   name == GS::PropertyNames::kHalfedgeNext ||
                   name == GS::PropertyNames::kHalfedgeFace ||
                   name == GS::PropertyNames::kHalfedgeConnectivity ||
                   name == GS::PropertyNames::kFaceHalfedge ||
                   name == "v:point" ||
                   name == "v:tex" ||
                   name == "v:texcoord" ||
                   // BUG-137 — same reserved property, on the domain that can
                   // carry a seam.
                   name == "h:texcoord" ||
                   name == "h:normal" ||
                   name == "p:position";
        }

        [[nodiscard]] bool IsScalarVisualizationKind(
            const Geometry::PropertyValueKind kind) noexcept
        {
            return kind == Geometry::PropertyValueKind::Float ||
                   kind == Geometry::PropertyValueKind::Double;
        }

        [[nodiscard]] bool DomainSupportsVisualizationConfig(
            const EditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = EditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
            case Domain::MeshEdges:
            case Domain::MeshFaces:
            case Domain::GraphVertices:
            case Domain::GraphEdges:
            case Domain::PointCloudPoints:
                return true;
            }
            return false;
        }

        [[nodiscard]] G::VisualizationConfig::Domain ToVisualizationConfigDomain(
            const EditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = EditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshEdges:
            case Domain::GraphEdges:
                return G::VisualizationConfig::Domain::Edge;
            case Domain::MeshFaces:
                return G::VisualizationConfig::Domain::Face;
            case Domain::MeshVertices:
            case Domain::GraphVertices:
            case Domain::PointCloudPoints:
                return G::VisualizationConfig::Domain::Vertex;
            }
            return G::VisualizationConfig::Domain::Vertex;
        }

        [[nodiscard]] G::VisualizationConfig::ColorSource ToColorBufferSource(
            const EditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = EditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshEdges:
            case Domain::GraphEdges:
                return G::VisualizationConfig::ColorSource::PerEdgeBuffer;
            case Domain::MeshFaces:
                return G::VisualizationConfig::ColorSource::PerFaceBuffer;
            case Domain::MeshVertices:
            case Domain::GraphVertices:
            case Domain::PointCloudPoints:
                return G::VisualizationConfig::ColorSource::PerVertexBuffer;
            }
            return G::VisualizationConfig::ColorSource::PerVertexBuffer;
        }

        [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
            const EditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = EditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return GeometryElementDomain::MeshVertex;
            case Domain::MeshEdges:
                return GeometryElementDomain::MeshEdge;
            case Domain::MeshFaces:
                return GeometryElementDomain::MeshFace;
            case Domain::GraphVertices:
                return GeometryElementDomain::GraphNode;
            case Domain::GraphEdges:
                return GeometryElementDomain::GraphEdge;
            case Domain::PointCloudPoints:
                return GeometryElementDomain::PointCloudPoint;
            }
            return GeometryElementDomain::Unknown;
        }

        [[nodiscard]] const Geometry::PropertySet* PropertySetForVisualizationDomain(
            const GeometryEntityAvailability& availability,
            const EditorVisualizationPropertyDomain domain) noexcept
        {
            return ResolveGeometryPropertySet(
                availability,
                ToGeometryElementDomain(domain));
        }

        void AppendVisualizationPropertiesForDomain(
            std::vector<EditorVisualizationPropertyInfo>& out,
            const Geometry::PropertySet& properties,
            EditorVisualizationPropertyDomain domain);




        void AppendVisualizationPropertiesForDomain(
            std::vector<EditorVisualizationPropertyInfo>& out,
            const Geometry::PropertySet& properties,
            const EditorVisualizationPropertyDomain domain)
        {
            if (!DomainSupportsVisualizationConfig(domain))
                return;

            for (const std::string& name : properties.Properties())
            {
                // Kinds outside the visualization-capable set (Bool, Int32,
                // UInt64, Vec2) fall through every predicate below and are
                // skipped, exactly as the retired editor-local enum did by
                // returning nullopt for them.
                const Geometry::PropertyValueKind kind =
                    DetectGeometryPropertyValueKind(properties, name);
                if (kind == Geometry::PropertyValueKind::Unknown)
                    continue;

                const bool internal = IsInternalVisualizationProperty(name);
                const bool connectivity =
                    IsConnectivityVisualizationProperty(name);
                const bool scalar =
                    !internal && IsScalarVisualizationKind(kind);
                const bool color =
                    !internal && kind == Geometry::PropertyValueKind::Vec4;
                const bool vector =
                    !connectivity && kind == Geometry::PropertyValueKind::Vec3;
                const bool integer =
                    !internal && !connectivity &&
                    kind == Geometry::PropertyValueKind::UInt32;
                if (!scalar && !color && !vector && !integer)
                {
                    continue;
                }

                out.push_back(EditorVisualizationPropertyInfo{
                    .Name = name,
                    .Domain = domain,
                    .ValueKind = kind,
                    .ElementCount = properties.Size(),
                    .ScalarPresetAvailable = scalar,
                    .IsolinePresetAvailable = scalar,
                    .ColorBufferPresetAvailable = color,
                    .VectorFieldCandidate = vector,
                });
            }
        }


        [[nodiscard]] const Geometry::PropertySet* PropertySetForCatalogDomain(
            const GeometryEntityAvailability& availability,
            const EditorPropertyCatalogDomain domain) noexcept
        {
            using Domain = EditorPropertyCatalogDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshVertex);
            case Domain::MeshEdges:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshEdge);
            case Domain::MeshHalfedges:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshHalfedge);
            case Domain::MeshFaces:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshFace);
            case Domain::GraphVertices:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::GraphNode);
            case Domain::GraphHalfedges:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::GraphHalfedge);
            case Domain::GraphEdges:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::GraphEdge);
            case Domain::PointCloudPoints:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::PointCloudPoint);
            }
            return nullptr;
        }

        [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
            const EditorPropertyCatalogDomain domain) noexcept
        {
            using Domain = EditorPropertyCatalogDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return GeometryElementDomain::MeshVertex;
            case Domain::MeshEdges:
                return GeometryElementDomain::MeshEdge;
            case Domain::MeshHalfedges:
                return GeometryElementDomain::MeshHalfedge;
            case Domain::MeshFaces:
                return GeometryElementDomain::MeshFace;
            case Domain::GraphVertices:
                return GeometryElementDomain::GraphNode;
            case Domain::GraphHalfedges:
                return GeometryElementDomain::GraphHalfedge;
            case Domain::GraphEdges:
                return GeometryElementDomain::GraphEdge;
            case Domain::PointCloudPoints:
                return GeometryElementDomain::PointCloudPoint;
            }
            return GeometryElementDomain::Unknown;
        }

        // Kinds the property catalog surfaces. Bool/Int32/UInt64 were never
        // representable in the retired editor-local enum (they collapsed to
        // Unknown), so they stay unsupported here rather than silently becoming
        // bindable now that the canonical vocabulary can name them.
        [[nodiscard]] bool IsPropertyCatalogSupportedKind(
            const Geometry::PropertyValueKind kind) noexcept
        {
            switch (kind)
            {
            case Geometry::PropertyValueKind::Float:
            case Geometry::PropertyValueKind::Double:
            case Geometry::PropertyValueKind::UInt32:
            case Geometry::PropertyValueKind::Vec2:
            case Geometry::PropertyValueKind::Vec3:
            case Geometry::PropertyValueKind::Vec4:
                return true;
            case Geometry::PropertyValueKind::Unknown:
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
                break;
            }
            return false;
        }








        [[nodiscard]] std::optional<EditorPropertyCatalogDomain>
        VertexChannelCatalogDomainForView(
            const GS::ConstSourceView& view) noexcept
        {
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            using Domain = EditorPropertyCatalogDomain;
            switch (availability.ProvenanceDomain)
            {
            case GS::Domain::Mesh:
                return Domain::MeshVertices;
            case GS::Domain::Graph:
                return Domain::GraphVertices;
            case GS::Domain::PointCloud:
                return Domain::PointCloudPoints;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return std::nullopt;
        }

        [[nodiscard]] const Geometry::PropertySet*
        VertexChannelPropertySetForView(
            const GS::ConstSourceView& view,
            const EditorPropertyCatalogDomain domain) noexcept
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            return PropertySetForCatalogDomain(availability, domain);
        }

        [[nodiscard]] std::optional<AttributeSourceType>
        ToAttributeSourceType(
            const Geometry::PropertyValueKind kind) noexcept
        {
            using Kind = Geometry::PropertyValueKind;
            switch (kind)
            {
            case Kind::Float:
                return AttributeSourceType::Float32;
            case Kind::Vec2:
                return AttributeSourceType::Vec2;
            case Kind::Vec3:
                return AttributeSourceType::Vec3;
            case Kind::Vec4:
                return AttributeSourceType::Vec4;
            case Kind::Double:
            case Kind::UInt32:
            case Kind::Unknown:
            case Kind::Bool:
            case Kind::Int32:
            case Kind::UInt64:
                break;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool SourceTypeAllowedForVertexChannel(
            const VertexChannel channel,
            const AttributeSourceType type) noexcept
        {
            switch (channel)
            {
            case VertexChannel::Normal:
                return type == AttributeSourceType::Vec3;
            case VertexChannel::Color:
                return type == AttributeSourceType::Vec3 ||
                       type == AttributeSourceType::Vec4;
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return false;
        }


        void RecordVertexChannelResolverScratch(
            EditorWorkspaceSnapshotStats* stats,
            const std::size_t byteCount)
        {
            if (stats == nullptr)
                return;

            ++stats->VertexChannelResolverScans;
            ++stats->VertexChannelScratchAllocations;
            stats->VertexChannelScratchBytes +=
                static_cast<std::uint64_t>(byteCount);
        }

        [[nodiscard]] AttributeBindResult EvaluateVertexChannelBinding(
            const Geometry::PropertySet& properties,
            const VertexChannel channel,
            const std::string_view propertyName,
            const AttributeSourceType sourceType,
            const std::size_t elementCount,
            EditorWorkspaceSnapshotStats* modelBuildStats)
        {
            ScopedEditorStatTimer timer{
                modelBuildStats != nullptr
                    ? &modelBuildStats->VertexChannelValidationTimeNs
                    : nullptr};
            if (propertyName.empty())
            {
                return AttributeBindResult{
                    .Status = AttributeBindStatus::EmptyBinding,
                    .FullyPopulated = false,
                };
            }
            if (elementCount > std::numeric_limits<std::uint32_t>::max())
            {
                return AttributeBindResult{
                    .Status = AttributeBindStatus::CountMismatch,
                    .FullyPopulated = false,
                };
            }
            if (!SourceTypeAllowedForVertexChannel(channel, sourceType))
            {
                return AttributeBindResult{
                    .Status = AttributeBindStatus::TypeMismatch,
                    .FullyPopulated = false,
                };
            }

            const std::uint32_t count =
                static_cast<std::uint32_t>(elementCount);
            const VertexAttributeBinding binding{
                .Channel = channel,
                .SourceType = sourceType,
                .SourceProperty = propertyName,
                .AllowFallback = false,
                .Normalize = channel == VertexChannel::Normal,
                .Fallback = channel == VertexChannel::Normal
                    ? glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}
                    : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            };

            if (channel == VertexChannel::Normal)
            {
                RecordVertexChannelResolverScratch(
                    modelBuildStats,
                    elementCount * sizeof(glm::vec3));
                std::vector<glm::vec3> scratch(elementCount);
                return ResolveVec3Channel(properties, binding, count, scratch);
            }
            if (channel == VertexChannel::Color)
            {
                RecordVertexChannelResolverScratch(
                    modelBuildStats,
                    elementCount * sizeof(std::uint32_t));
                std::vector<std::uint32_t> scratch(elementCount);
                return ResolveColorChannelPackedUnorm8(
                    properties,
                    binding,
                    count,
                    scratch);
            }
            return AttributeBindResult{
                .Status = AttributeBindStatus::TypeMismatch,
                .FullyPopulated = false,
            };
        }



        [[nodiscard]] VertexChannelSourceBinding*
        FindMutableVertexChannelBinding(
            VertexChannelBindingSet& bindings,
            const VertexChannel channel) noexcept
        {
            switch (channel)
            {
            case VertexChannel::Normal:
                return &bindings.Normal;
            case VertexChannel::Color:
                return &bindings.Color;
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] bool AnyVertexChannelBindingEnabled(
            const VertexChannelBindingSet& bindings) noexcept
        {
            return IsVertexChannelBindingEnabled(bindings.Normal) ||
                   IsVertexChannelBindingEnabled(bindings.Color);
        }

        [[nodiscard]] bool SameVertexChannelSourceBinding(
            const VertexChannelSourceBinding& lhs,
            const VertexChannelSourceBinding& rhs) noexcept
        {
            return lhs.Enabled == rhs.Enabled &&
                   lhs.Property == rhs.Property;
        }

        [[nodiscard]] bool SameVertexChannelBindingSet(
            const VertexChannelBindingSet& lhs,
            const VertexChannelBindingSet& rhs) noexcept
        {
            return SameVertexChannelSourceBinding(lhs.Normal, rhs.Normal) &&
                   SameVertexChannelSourceBinding(lhs.Color, rhs.Color) &&
                   lhs.BindingGeneration == rhs.BindingGeneration;
        }

        [[nodiscard]] bool SameOptionalVertexChannelBindingSet(
            const std::optional<VertexChannelBindingSet>& lhs,
            const std::optional<VertexChannelBindingSet>& rhs) noexcept
        {
            if (lhs.has_value() != rhs.has_value())
                return false;
            return !lhs.has_value() ||
                   SameVertexChannelBindingSet(*lhs, *rhs);
        }

        void MarkVertexChannelDirty(entt::registry& raw,
                                    const entt::entity entity,
                                    const VertexChannel channel)
        {
            switch (channel)
            {
            case VertexChannel::Position:
                Dirty::MarkVertexPositionsDirty(raw, entity);
                break;
            case VertexChannel::Texcoord:
                Dirty::MarkVertexTexcoordsDirty(raw, entity);
                break;
            case VertexChannel::Normal:
                Dirty::MarkVertexNormalsDirty(raw, entity);
                break;
            case VertexChannel::Color:
                Dirty::MarkVertexColorsDirty(raw, entity);
                break;
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                Dirty::MarkVertexAttributesDirty(raw, entity);
                break;
            }
        }

        [[nodiscard]] std::optional<VertexChannelBindingSet>
        StoredVertexChannelBindingSet(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            if (const auto* bindings =
                    raw.try_get<VertexChannelBindingSet>(entity))
            {
                return *bindings;
            }
            return std::nullopt;
        }

        [[nodiscard]] EditorCommandHistoryStatus
        ApplyVertexChannelBindingSet(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const std::optional<VertexChannelBindingSet>& bindings)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            if (bindings.has_value())
                raw.emplace_or_replace<VertexChannelBindingSet>(
                    *entity,
                    *bindings);
            else
                raw.remove<VertexChannelBindingSet>(*entity);
            return EditorCommandHistoryStatus::Applied;
        }

        struct VertexChannelBindingMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            VertexChannel Channel{VertexChannel::Custom};
        };

        [[nodiscard]] EditorCommandHistoryResult
        ExecuteVertexChannelBindingMutation(
            EditorCommandHistory& history,
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const VertexChannel channel,
            const std::optional<VertexChannelBindingSet>& before,
            const std::optional<VertexChannelBindingSet>& after)
        {
            return Internal::ExecuteUndoableEntityMutation(
                history,
                "Change vertex channel binding",
                VertexChannelBindingMutationIdentity{
                    .Scene = scene,
                    .World = world,
                    .StableEntityId = stableEntityId,
                    .Channel = channel,
                },
                before,
                before,
                after,
                [](
                    const VertexChannelBindingMutationIdentity& identity,
                    const std::optional<VertexChannelBindingSet>& expected,
                    const std::optional<VertexChannelBindingSet>&)
                {
                    if (identity.Scene == nullptr ||
                        !identity.World.IsValid())
                    {
                        return EditorCommandHistoryStatus::MissingScene;
                    }

                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(
                            raw,
                            identity.StableEntityId);
                    if (!entity.has_value())
                        return EditorCommandHistoryStatus::StaleEntity;

                    return SameOptionalVertexChannelBindingSet(
                               StoredVertexChannelBindingSet(raw, *entity),
                               expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                },
                [](
                    const VertexChannelBindingMutationIdentity& identity,
                    const std::optional<VertexChannelBindingSet>& target)
                {
                    return ApplyVertexChannelBindingSet(
                        identity.Scene,
                        identity.StableEntityId,
                        target);
                },
                [](
                    const VertexChannelBindingMutationIdentity& identity,
                    const std::optional<VertexChannelBindingSet>&,
                    const std::optional<VertexChannelBindingSet>&)
                {
                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(
                            raw,
                            identity.StableEntityId);
                    if (entity.has_value())
                    {
                        MarkVertexChannelDirty(
                            raw,
                            *entity,
                            identity.Channel);
                        return StoredVertexChannelBindingSet(raw, *entity);
                    }
                    return std::optional<VertexChannelBindingSet>{};
                });
        }
        [[nodiscard]] bool PropertySupportsPreset(
            const EditorVisualizationPropertyInfo& property,
            const EditorVisualizationPropertyPreset preset) noexcept
        {
            switch (preset)
            {
            case EditorVisualizationPropertyPreset::Scalar:
                return property.ScalarPresetAvailable;
            case EditorVisualizationPropertyPreset::Isoline:
                return property.IsolinePresetAvailable;
            case EditorVisualizationPropertyPreset::ColorBuffer:
                return property.ColorBufferPresetAvailable;
            }
            return false;
        }





        struct EditorRenderHintState
        {
            std::optional<G::RenderSurface> Surface{};
            std::optional<G::RenderEdges> Edges{};
            std::optional<G::RenderPoints> Points{};
        };

        [[nodiscard]] EditorRenderHintState ReadRenderHintState(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            EditorRenderHintState state{};
            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
                state.Surface = *surface;
            if (const auto* lines = raw.try_get<G::RenderEdges>(entity))
                state.Edges = *lines;
            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
                state.Points = *points;
            return state;
        }

        [[nodiscard]] bool SameRenderSurface(
            const G::RenderSurface& lhs,
            const G::RenderSurface& rhs) noexcept
        {
            return lhs.Domain == rhs.Domain;
        }

        [[nodiscard]] bool SameRenderScalarSource(
            const std::variant<float, std::string>& lhs,
            const std::variant<float, std::string>& rhs) noexcept
        {
            if (lhs.index() != rhs.index())
                return false;
            if (const auto* lhsUniform = std::get_if<float>(&lhs))
            {
                const auto* rhsUniform = std::get_if<float>(&rhs);
                return rhsUniform != nullptr &&
                       std::bit_cast<std::uint32_t>(*lhsUniform) ==
                           std::bit_cast<std::uint32_t>(*rhsUniform);
            }
            return std::get<std::string>(lhs) == std::get<std::string>(rhs);
        }

        [[nodiscard]] bool SameRenderEdges(
            const G::RenderEdges& lhs,
            const G::RenderEdges& rhs)
        {
            return lhs.Domain == rhs.Domain &&
                   SameRenderScalarSource(lhs.WidthSource, rhs.WidthSource);
        }

        [[nodiscard]] bool SameRenderPoints(
            const G::RenderPoints& lhs,
            const G::RenderPoints& rhs)
        {
            return lhs.Type == rhs.Type &&
                   SameRenderScalarSource(lhs.SizeSource, rhs.SizeSource);
        }

        template <typename T, typename SameFn>
        [[nodiscard]] bool SameOptionalRenderComponent(
            const std::optional<T>& lhs,
            const std::optional<T>& rhs,
            SameFn same)
        {
            if (lhs.has_value() != rhs.has_value())
                return false;
            if (!lhs.has_value())
                return true;
            return same(*lhs, *rhs);
        }

        [[nodiscard]] bool SameRenderHintState(
            const EditorRenderHintState& lhs,
            const EditorRenderHintState& rhs)
        {
            return SameOptionalRenderComponent(
                       lhs.Surface, rhs.Surface, SameRenderSurface) &&
                   SameOptionalRenderComponent(
                       lhs.Edges, rhs.Edges, SameRenderEdges) &&
                   SameOptionalRenderComponent(
                       lhs.Points, rhs.Points, SameRenderPoints);
        }

        [[nodiscard]] bool IsFinitePositive(const float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] bool AnyRenderHintEdit(
            const EditorRenderHintCommand& command) noexcept
        {
            return command.SetSurface ||
                   command.SetEdges ||
                   command.SetUniformEdgeWidth ||
                   command.SetPoints ||
                   command.SetPointRenderType ||
                   command.SetUniformPointSize;
        }

        [[nodiscard]] bool RenderHintCommandMatchesDomain(
            const EditorRenderHintCommand& command,
            const GeometryEntityAvailability& availability) noexcept
        {
            const bool editsSurface = command.SetSurface;
            const bool editsEdges =
                command.SetEdges || command.SetUniformEdgeWidth;
            const bool editsPoints =
                command.SetPoints ||
                command.SetPointRenderType ||
                command.SetUniformPointSize;

            if (!editsSurface && !editsEdges && !editsPoints)
                return false;

            if (editsSurface &&
                availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
                return false;

            if (editsSurface &&
                (!availability.Sources.Has(GS::SourceCapability::Vertices) ||
                 !availability.Sources.Has(GS::SourceCapability::Halfedges) ||
                 !availability.Sources.Has(GS::SourceCapability::Faces)))
                return false;

            if (editsEdges)
            {
                if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud ||
                    !availability.Sources.HasPointSource())
                    return false;

                const bool hasExplicitEdges =
                    availability.Sources.Has(GS::SourceCapability::Edges);
                const bool hasMeshWireTopology =
                    availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
                    availability.Sources.Has(GS::SourceCapability::Halfedges) &&
                    availability.Sources.Has(GS::SourceCapability::Faces);
                if (!hasExplicitEdges && !hasMeshWireTopology)
                    return false;
            }

            if (editsPoints && !availability.Sources.HasPointSource())
                return false;

            return true;
        }

        [[nodiscard]] EditorRenderHintState ApplyRenderHintCommandToState(
            EditorRenderHintState state,
            const EditorRenderHintCommand& command)
        {
            if (command.SetSurface)
            {
                if (command.EnableSurface)
                {
                    G::RenderSurface surface =
                        state.Surface.value_or(G::RenderSurface{});
                    surface.Domain = command.SurfaceDomain;
                    state.Surface = surface;
                }
                else
                {
                    state.Surface.reset();
                }
            }

            if (command.SetEdges)
            {
                if (command.EnableEdges)
                {
                    G::RenderEdges lines =
                        state.Edges.value_or(G::RenderEdges{});
                    lines.Domain = command.EdgeDomain;
                    if (command.SetUniformEdgeWidth)
                        lines.WidthSource = command.UniformEdgeWidth;
                    state.Edges = lines;
                }
                else
                {
                    state.Edges.reset();
                }
            }
            else if (command.SetUniformEdgeWidth && state.Edges.has_value())
            {
                state.Edges->WidthSource = command.UniformEdgeWidth;
            }

            if (command.SetPoints)
            {
                if (command.EnablePoints)
                {
                    G::RenderPoints points =
                        state.Points.value_or(G::RenderPoints{});
                    points.Type = command.PointType;
                    if (command.SetUniformPointSize)
                        points.SizeSource = command.UniformPointSize;
                    state.Points = points;
                }
                else
                {
                    state.Points.reset();
                }
            }
            else if (state.Points.has_value())
            {
                if (command.SetPointRenderType)
                    state.Points->Type = command.PointType;
                if (command.SetUniformPointSize)
                    state.Points->SizeSource = command.UniformPointSize;
            }

            return state;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyRenderHintState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const EditorRenderHintState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            if (state.Surface.has_value())
                raw.emplace_or_replace<G::RenderSurface>(entity, *state.Surface);
            else if (raw.all_of<G::RenderSurface>(entity))
                raw.remove<G::RenderSurface>(entity);

            if (state.Edges.has_value())
                raw.emplace_or_replace<G::RenderEdges>(entity, *state.Edges);
            else if (raw.all_of<G::RenderEdges>(entity))
                raw.remove<G::RenderEdges>(entity);

            if (state.Points.has_value())
                raw.emplace_or_replace<G::RenderPoints>(entity, *state.Points);
            else if (raw.all_of<G::RenderPoints>(entity))
                raw.remove<G::RenderPoints>(entity);

            return EditorCommandHistoryStatus::Applied;
        }

        struct EditorRenderHintMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        [[nodiscard]] EditorCommandHistoryResult ExecuteEditorRenderHintMutation(
            EditorCommandHistory& history,
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const EditorRenderHintState& before,
            const EditorRenderHintState& after)
        {
            return Internal::ExecuteUndoableEntityMutation(
                history,
                "Change Render Hints",
                EditorRenderHintMutationIdentity{
                    .Scene = scene,
                    .World = world,
                    .StableEntityId = stableEntityId,
                },
                before,
                before,
                after,
                [](
                    const EditorRenderHintMutationIdentity& identity,
                    const EditorRenderHintState& expected,
                    const EditorRenderHintState&)
                {
                    if (identity.Scene == nullptr || !identity.World.IsValid())
                        return EditorCommandHistoryStatus::MissingScene;

                    const entt::registry& raw = identity.Scene->Raw();
                    const ECS::EntityHandle entity =
                        SelectionController::ToEntityHandle(
                            identity.StableEntityId);
                    if (entity == ECS::InvalidEntityHandle ||
                        !raw.valid(entity))
                    {
                        return EditorCommandHistoryStatus::StaleEntity;
                    }
                    return SameRenderHintState(
                               ReadRenderHintState(raw, entity),
                               expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                },
                [](
                    const EditorRenderHintMutationIdentity& identity,
                    const EditorRenderHintState& target)
                {
                    return ApplyRenderHintState(
                        identity.Scene,
                        identity.StableEntityId,
                        target);
                },
                [](
                    const EditorRenderHintMutationIdentity&,
                    const EditorRenderHintState&,
                    const EditorRenderHintState& target)
                {
                    return target;
                });
        }

        [[nodiscard]] bool SameVec4(
            const glm::vec4 lhs,
            const glm::vec4 rhs) noexcept
        {
            return lhs.x == rhs.x &&
                   lhs.y == rhs.y &&
                   lhs.z == rhs.z &&
                   lhs.w == rhs.w;
        }

        [[nodiscard]] bool SameGeometryPresentationDefaultValue(
            const GeometryPresentationDefaultValue& lhs,
            const GeometryPresentationDefaultValue& rhs) noexcept
        {
            return lhs.Kind == rhs.Kind &&
                   SameVec4(lhs.Vector, rhs.Vector) &&
                   lhs.Scalar == rhs.Scalar &&
                   lhs.UInt == rhs.UInt;
        }

        [[nodiscard]] bool IsFiniteDefaultValue(
            const GeometryPresentationDefaultValue& value) noexcept
        {
            return std::isfinite(value.Vector.x) &&
                   std::isfinite(value.Vector.y) &&
                   std::isfinite(value.Vector.z) &&
                   std::isfinite(value.Vector.w) &&
                   std::isfinite(value.Scalar);
        }

        [[nodiscard]] bool SameGeometryPresentationPropertyDescriptor(
            const GeometryPropertyRef& lhs,
            const GeometryPropertyRef& rhs) noexcept
        {
            return lhs == rhs;
        }
        struct GeometryPresentationEditorState
        {
            GeometryPresentationRecipe Recipe{};
            GeometryPresentationRuntimeState Runtime{};
        };

        struct GeometryPresentationMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        [[nodiscard]] EditorCommandHistoryStatus ApplyGeometryPresentationState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const GeometryPresentationEditorState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            const auto* currentRuntime =
                raw.try_get<GeometryPresentationRuntimeState>(entity);
            GeometryPresentationRuntimeState runtime = state.Runtime;
            runtime.RecipeGeneration = currentRuntime != nullptr
                ? currentRuntime->RecipeGeneration
                : GeometryPresentationRuntimeState{}.RecipeGeneration;
            raw.emplace_or_replace<GeometryPresentationRecipe>(
                entity,
                state.Recipe);
            raw.emplace_or_replace<GeometryPresentationRuntimeState>(
                entity,
                std::move(runtime));
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] std::uint64_t StampGeometryPresentationGeneration(
            ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId)
        {
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            auto& runtime =
                raw.get<GeometryPresentationRuntimeState>(entity);
            return ++runtime.RecipeGeneration;
        }

        struct GeometryPresentationSlotLookup
        {
            GeometryPresentationBindingRecipe* Presentation{nullptr};
            GeometryPresentationSlotRecipe* Slot{nullptr};
        };

        [[nodiscard]] GeometryPresentationSlotLookup FindMutableGeometryPresentationSlot(
            GeometryPresentationRecipe& bindings,
            const std::string& presentationKey,
            const GeometryPresentationSlotSemantic semantic)
        {
            if (!presentationKey.empty())
            {
                GeometryPresentationBindingRecipe* presentation =
                    FindGeometryPresentationBinding(bindings, presentationKey);
                if (presentation == nullptr)
                    return {};
                return GeometryPresentationSlotLookup{
                    .Presentation = presentation,
                    .Slot = FindGeometryPresentationSlot(*presentation, semantic),
                };
            }

            for (GeometryPresentationBindingRecipe& presentation :
                 bindings.Presentations)
            {
                if (GeometryPresentationSlotRecipe* slot =
                        FindGeometryPresentationSlot(presentation, semantic))
                {
                    return GeometryPresentationSlotLookup{
                        .Presentation = &presentation,
                        .Slot = slot,
                    };
                }
            }
            return {};
        }

        GeometryPresentationSlotStatus& ResetGeometryPresentationSlotStatus(
            GeometryPresentationRuntimeState& state,
            const std::string& presentationKey,
            const GeometryPresentationSlotSemantic semantic,
            const GeometryPresentationReadiness readiness,
            const GeometryPresentationProvenance provenance)
        {
            GeometryPresentationSlotStatus* status =
                FindGeometryPresentationSlotStatus(
                    state,
                    presentationKey,
                    semantic);
            if (status == nullptr)
            {
                state.Slots.push_back(GeometryPresentationSlotStatus{
                    .PresentationKey = presentationKey,
                    .Semantic = semantic,
                });
                status = &state.Slots.back();
            }
            *status = GeometryPresentationSlotStatus{
                .PresentationKey = presentationKey,
                .Semantic = semantic,
                .Readiness = readiness,
                .Provenance = provenance,
            };
            return *status;
        }

        [[nodiscard]] EditorCommandStatus ToEditorCommandStatus(
            EditorCommandHistoryStatus status) noexcept;

        [[nodiscard]] EditorCommandStatus CommitGeometryPresentationChange(
            const EditorVisualizationEditingContext& context,
            const std::uint32_t stableEntityId,
            GeometryPresentationEditorState before,
            GeometryPresentationEditorState after)
        {
            if (context.CommandHistory != nullptr)
            {
                const std::uint64_t expectedGeneration =
                    before.Runtime.RecipeGeneration;
                const EditorCommandHistoryResult result =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        "Change Geometry Presentation",
                        GeometryPresentationMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        expectedGeneration,
                        std::move(before),
                        std::move(after),
                        [](
                            const GeometryPresentationMutationIdentity& identity,
                            const std::uint64_t expectedGeneration,
                            const GeometryPresentationEditorState&)
                        {
                            if (identity.Scene == nullptr ||
                                !identity.World.IsValid())
                            {
                                return EditorCommandHistoryStatus::MissingScene;
                            }

                            entt::registry& raw = identity.Scene->Raw();
                            const ECS::EntityHandle entity =
                                SelectionController::ToEntityHandle(
                                    identity.StableEntityId);
                            if (entity == ECS::InvalidEntityHandle ||
                                !raw.valid(entity))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            if (!raw.all_of<GeometryPresentationRecipe>(entity))
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }

                            const auto* runtime =
                                raw.try_get<GeometryPresentationRuntimeState>(
                                    entity);
                            const std::uint64_t currentGeneration =
                                runtime != nullptr
                                    ? runtime->RecipeGeneration
                                    : GeometryPresentationRuntimeState{}
                                          .RecipeGeneration;
                            return currentGeneration == expectedGeneration
                                ? EditorCommandHistoryStatus::Applied
                                : EditorCommandHistoryStatus::StaleEntity;
                        },
                        [](
                            const GeometryPresentationMutationIdentity& identity,
                            const GeometryPresentationEditorState& target)
                        {
                            return ApplyGeometryPresentationState(
                                identity.Scene,
                                identity.StableEntityId,
                                target);
                        },
                        [](
                            const GeometryPresentationMutationIdentity& identity,
                            const std::uint64_t,
                            const GeometryPresentationEditorState&)
                        {
                            return StampGeometryPresentationGeneration(
                                *identity.Scene,
                                identity.StableEntityId);
                        });
                return ToEditorCommandStatus(result.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyGeometryPresentationState(
                    context.Scene,
                    stableEntityId,
                    after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            (void)StampGeometryPresentationGeneration(
                *context.Scene,
                stableEntityId);
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] bool PropertySourceKindAllowedForGeometryPresentationSlotCommand(
            const GeometryPresentationSourceKind sourceKind) noexcept
        {
            return sourceKind == GeometryPresentationSourceKind::PropertyBake ||
                   sourceKind == GeometryPresentationSourceKind::PropertyBuffer;
        }
        [[nodiscard]] EditorCommandStatus ToEditorCommandStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            switch (status)
            {
            case EditorCommandHistoryStatus::Applied:
            case EditorCommandHistoryStatus::Recorded:
            case EditorCommandHistoryStatus::Undone:
            case EditorCommandHistoryStatus::Redone:
                return EditorCommandStatus::Applied;
            case EditorCommandHistoryStatus::NoChange:
                return EditorCommandStatus::NoChange;
            case EditorCommandHistoryStatus::MissingScene:
                return EditorCommandStatus::MissingScene;
            case EditorCommandHistoryStatus::MissingSelectionController:
                return EditorCommandStatus::MissingSelectionController;
            case EditorCommandHistoryStatus::StaleEntity:
                return EditorCommandStatus::StaleEntity;
            case EditorCommandHistoryStatus::MissingTransform:
                return EditorCommandStatus::MissingTransform;
            case EditorCommandHistoryStatus::EmptyUndoStack:
            case EditorCommandHistoryStatus::EmptyRedoStack:
            case EditorCommandHistoryStatus::InvalidCommand:
            case EditorCommandHistoryStatus::CommandFailed:
            case EditorCommandHistoryStatus::UndoFailed:
            case EditorCommandHistoryStatus::RedoFailed:
            case EditorCommandHistoryStatus::UnsupportedOperation:
                return EditorCommandStatus::NoChange;
            }
            return EditorCommandStatus::NoChange;
        }
        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
            const entt::registry& raw,
            const std::uint32_t stableId)
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableId);
            if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                return entity;
            return std::nullopt;
        }

        void InvalidateSelectedModelCache(const EditorVisualizationEditingContext& context)
        {
            if (context.InvalidateWorkspaceSnapshotCache)
                context.InvalidateWorkspaceSnapshotCache();
        }

        [[nodiscard]] EditorCommandStatus
        InvalidateSelectedModelCacheIfApplied(const EditorVisualizationEditingContext& context,
                                              const EditorCommandStatus status)
        {
            if (status == EditorCommandStatus::Applied)
                InvalidateSelectedModelCache(context);
            return status;
        }
        constexpr std::string_view kUvRegenerationJobOutputName{
            "uv_regeneration"};

        [[nodiscard]] EditorJobDependencyModel
        ToEditorJobDependencyModel(
            const EditorJobDependency& dependency)
        {
            return EditorJobDependencyModel{
                .Job = dependency.Job,
                .Reason = dependency.Reason,
            };
        }

        [[nodiscard]] EditorJobModel ToEditorJobModel(
            const EditorJobRecord& job)
        {
            EditorJobModel model{
                .Handle = job.Token,
                .Key = job.Identity,
                .Name = job.Name,
                .RequestedJobDomain = job.RequestedJobDomain,
                .ResolvedJobDomain = job.ResolvedJobDomain,
                .Status = job.State,
                .NormalizedProgress = job.NormalizedProgress,
                .ProgressDeterminate = job.ProgressDeterminate,
                .PreviousOutputRetained = job.PreviousOutputRetained,
                .PayloadToken = job.PayloadToken,
                .ElapsedMilliseconds = job.ElapsedMilliseconds,
                .Diagnostic = job.Diagnostic,
            };
            model.Dependencies.reserve(job.Dependencies.size());
            for (const EditorJobDependency& dependency : job.Dependencies)
                model.Dependencies.push_back(
                    ToEditorJobDependencyModel(dependency));
            return model;
        }

        [[nodiscard]] std::optional<EditorJobModel>
        FindDerivedJobModelForOutput(
            const EditorVisualizationEditingContext& context,
            const std::uint32_t stableEntityId,
            const std::string_view outputName)
        {
            if (!context.JobCommands.SnapshotEntity)
                return std::nullopt;

            const std::vector<EditorJobRecord> jobs =
                context.JobCommands.SnapshotEntity(stableEntityId);
            const EditorJobRecord* selected = nullptr;
            for (const EditorJobRecord& job : jobs)
            {
                if (job.Identity.EntityId != stableEntityId ||
                    std::string_view{job.Identity.OutputName} != outputName)
                {
                    continue;
                }

                if (selected == nullptr)
                {
                    selected = &job;
                    continue;
                }

                const bool jobActive = IsActiveEditorJobState(job.State);
                const bool selectedActive = IsActiveEditorJobState(selected->State);
                if (jobActive || !selectedActive)
                    selected = &job;
            }

            if (selected == nullptr)
                return std::nullopt;
            return ToEditorJobModel(*selected);
        }

        [[nodiscard]] bool IsTextureBakeSourceDomain(
            const EditorPropertyCatalogDomain domain) noexcept
        {
            return domain == EditorPropertyCatalogDomain::MeshVertices ||
                   domain == EditorPropertyCatalogDomain::MeshEdges ||
                   domain == EditorPropertyCatalogDomain::MeshFaces;
        }

        [[nodiscard]] bool IsNonBakeableMeshAttribute(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == "v:point" ||
                   name == "v:texcoord" ||
                   name == "v:tex";
        }

        [[nodiscard]] EditorTextureBakeSourceRow BuildTextureBakeSourceRow(
            const EditorPropertyCatalogRow& row)
        {
            EditorTextureBakeSourceRow out{
                .Name = row.Name,
                .CatalogDomain = row.Domain,
                .BakeDomain = ToGeometryElementDomain(row.Domain),
                .ValueKind = row.ValueKind,
                .ExpectedValueKind = row.ValueKind,
                .ElementCount = row.ElementCount,
                .Descriptor = row.Descriptor,
            };

            if (!IsTextureBakeSourceDomain(row.Domain))
            {
                out.Category = EditorTextureBakeSourceCategory::WrongDomain;
                out.DisabledReason =
                    "texture baking supports mesh vertex, edge, and face properties";
                return out;
            }
            if (row.Connectivity || IsNonBakeableMeshAttribute(row.Name))
            {
                out.Category = row.Connectivity
                    ? EditorTextureBakeSourceCategory::Connectivity
                    : EditorTextureBakeSourceCategory::Internal;
                out.DisabledReason =
                    row.Connectivity
                        ? "connectivity properties are visible but not texture-bake sources"
                        : "internal mesh coordinate properties are visible but not bake "
                          "sources";
                return out;
            }
            if (!row.Supported ||
                !IsPropertyCatalogSupportedKind(row.ValueKind))
            {
                out.Category = EditorTextureBakeSourceCategory::Unsupported;
                out.DisabledReason = row.UnsupportedReason.empty()
                    ? "unsupported property value type"
                    : row.UnsupportedReason;
                return out;
            }

            out.Category = row.Internal
                ? EditorTextureBakeSourceCategory::Internal
                : EditorTextureBakeSourceCategory::Bakeable;
            out.Bakeable = true;
            return out;
        }

        [[nodiscard]] EditorUvDiagnosticsModel BuildUvDiagnosticsModel(
            const EditorVisualizationEditingContext& context,
            const GS::ConstSourceView& view)
        {
            ScopedEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->UvDiagnosticsModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->UvDiagnosticsModelBuilds;
            }
            EditorUvDiagnosticsModel model{};
            model.HasSelectedEntity = true;
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            model.IsMesh =
                availability.ProvenanceDomain == GS::Domain::Mesh &&
                availability.Has(GS::SourceCapability::Vertices) &&
                availability.Has(GS::SourceCapability::Halfedges) &&
                availability.Has(GS::SourceCapability::Faces);
            if (!model.IsMesh)
            {
                model.Provenance = "unavailable";
                model.UvRegenerationDisabledReason =
                    "UV diagnostics require a selected mesh";
                return model;
            }

            model.VertexCount = view.VerticesAlive();
            model.FaceCount = view.FacesAlive();
            model.BackendId = "xatlas";
            model.Provenance = "missing";
            model.UvRegenerationAvailable = true;

            if (view.VertexSource == nullptr)
                return model;

            // BUG-137 — UVs follow the canonical corner-over-vertex resolution
            // order. A seam-carrying mesh has no `v:texcoord`, and reporting it
            // as "no resolved texcoord property" disabled the UV view for
            // exactly the meshes an atlas produces.
            const auto cornerTexcoords =
                view.HalfedgeSource != nullptr
                    ? view.HalfedgeSource->Properties.Get<glm::vec2>("h:texcoord")
                    : Geometry::ConstProperty<glm::vec2>{};
            const bool cornerOwned = cornerTexcoords.IsValid() &&
                cornerTexcoords.Vector().size() == view.HalfedgesTotal();

            const auto vertexTexcoords =
                view.VertexSource->Properties.Get<glm::vec2>(
                    model.TexcoordPropertyName);
            const auto& texcoords = cornerOwned ? cornerTexcoords : vertexTexcoords;
            model.HasTexcoords = texcoords.IsValid();
            if (model.HasTexcoords)
            {
                model.TexcoordCount = texcoords.Vector().size();
                model.TexcoordCountMatchesVertices = cornerOwned
                    ? model.TexcoordCount == view.HalfedgesTotal()
                    : model.TexcoordCount == model.VertexCount;
                model.Provenance = cornerOwned
                    ? "geometry property (corner domain)"
                    : "geometry property";
                model.CheckerPreviewAvailable =
                    model.TexcoordCountMatchesVertices;
                if (!model.TexcoordCountMatchesVertices)
                {
                    model.LastFailure = cornerOwned
                        ? "corner texcoord count does not match mesh halfedge count"
                        : "texcoord count does not match mesh vertex count";
                }
                else
                {
                    model.TexcoordsFinite = true;
                    EditorWorkspaceSnapshotStats* stats =
                        context.ModelBuildStats;
                    for (const glm::vec2 uv : texcoords.Vector())
                    {
                        if (stats != nullptr)
                            ++stats->UvDiagnosticsTexcoordElementsScanned;
                        if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                        {
                            model.TexcoordsFinite = false;
                            model.LastFailure =
                                "texcoord property contains non-finite values";
                            break;
                        }
                    }
                    model.CheckerPreviewAvailable = model.TexcoordsFinite;
                }
            }
            return model;
        }

} // namespace

        [[nodiscard]] EditorTextureBakeControlsModel
        BuildEditorTextureBakeControlsModel(
            const EditorVisualizationEditingContext& context,
            const GS::ConstSourceView& view,
            const EditorPropertyCatalogModel& catalog,
            const std::uint32_t stableEntityId)
        {
            ScopedEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->TextureBakeModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->TextureBakeModelBuilds;
            }
            EditorTextureBakeControlsModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = stableEntityId;
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            model.IsMesh =
                availability.ProvenanceDomain == GS::Domain::Mesh &&
                availability.Has(GS::SourceCapability::Vertices) &&
                availability.Has(GS::SourceCapability::Halfedges) &&
                availability.Has(GS::SourceCapability::Faces);
            model.HasRuntimeBakeCommand =
                context.TextureBake != nullptr &&
                context.TextureBake->Available();
            const bool hasOperationalGpu = context.OperationalGpuAvailable;
            model.Uv = BuildUvDiagnosticsModel(context, view);
            model.Uv.UvRegenerationJob = FindDerivedJobModelForOutput(
                context,
                stableEntityId,
                kUvRegenerationJobOutputName);
            if (context.TextureBake != nullptr)
            {
                TextureBakeSnapshot snapshot =
                    context.TextureBake->Snapshot(stableEntityId);
                model.BakedTextures = std::move(snapshot.Textures);
            }
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (context.Scene != nullptr &&
                entity != ECS::InvalidEntityHandle &&
                context.Scene->Raw().valid(entity))
            {
                if (const auto* recipe =
                        context.Scene->Raw()
                            .try_get<GeometryPresentationRecipe>(entity))
                {
                    for (const auto& presentation : recipe->Presentations)
                    {
                        for (const auto& slot : presentation.Slots)
                        {
                            if (slot.SourceKind !=
                                    GeometryPresentationSourceKind::
                                        PropertyBake ||
                                slot.GeneratedOutputName.empty())
                            {
                                continue;
                            }
                            auto found = std::ranges::find(
                                model.TextureBakeTargets,
                                slot.GeneratedOutputName,
                                &EditorTextureBakeTargetSnapshot::
                                    OutputName);
                            if (found == model.TextureBakeTargets.end())
                            {
                                model.TextureBakeTargets.push_back(
                                    EditorTextureBakeTargetSnapshot{
                                        .OutputName = slot.GeneratedOutputName,
                                    });
                                found =
                                    std::prev(model.TextureBakeTargets.end());
                            }
                            found->Targets.push_back(
                                EditorTextureBakeTarget{
                                    .PresentationKey = presentation.Key,
                                    .Semantic = slot.Semantic,
                                    .Colormap = slot.TextureColormap,
                                    .NormalSpace = slot.NormalSpace,
                                });
                        }
                    }
                }
            }

            model.Sources.reserve(catalog.Rows.size());
            EditorWorkspaceSnapshotStats* stats = context.ModelBuildStats;
            for (const EditorPropertyCatalogRow& row : catalog.Rows)
            {
                if (stats != nullptr)
                    ++stats->TextureBakeSourceRowsEnumerated;
                model.Sources.push_back(BuildTextureBakeSourceRow(row));
            }

            const bool hasBakeableSource =
                std::any_of(
                    model.Sources.begin(),
                    model.Sources.end(),
                    [](const EditorTextureBakeSourceRow& row)
                    {
                        return row.Bakeable;
                    });

            model.CanBake = model.IsMesh &&
                            model.HasRuntimeBakeCommand &&
                            hasOperationalGpu &&
                            model.Uv.HasTexcoords &&
                            model.Uv.TexcoordCountMatchesVertices &&
                            model.Uv.TexcoordsFinite &&
                            hasBakeableSource;
            if (!model.IsMesh)
                model.DisabledReason = "texture baking requires a selected mesh";
            else if (!hasOperationalGpu)
                model.DisabledReason = "texture baking requires an operational GPU backend";
            else if (!model.HasRuntimeBakeCommand)
                model.DisabledReason = "runtime selected-mesh bake command is unavailable";
            else if (!model.Uv.HasTexcoords)
                model.DisabledReason = "selected mesh has no resolved texcoord property";
            else if (!model.Uv.TexcoordCountMatchesVertices)
                model.DisabledReason = model.Uv.LastFailure;
            else if (!model.Uv.TexcoordsFinite)
                model.DisabledReason = model.Uv.LastFailure;
            else if (!hasBakeableSource)
                model.DisabledReason = "no bakeable mesh vertex or face properties";
            return model;
        }


    EditorCommandStatus
ApplyEditorRenderHintCommand(
        const EditorVisualizationEditingContext& context,
        const EditorRenderHintCommand& command)
    {
        if (!AnyRenderHintEdit(command))
            return EditorCommandStatus::NoChange;
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;
        if ((command.SetUniformEdgeWidth &&
             !IsFinitePositive(command.UniformEdgeWidth)) ||
            (command.SetUniformPointSize &&
             !IsFinitePositive(command.UniformPointSize)))
        {
            return EditorCommandStatus::InvalidProcessingParameters;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (!RenderHintCommandMatchesDomain(command, availability))
            return EditorCommandStatus::UnsupportedGeometryDomain;

        const EditorRenderHintState before =
            ReadRenderHintState(raw, entity);
        const EditorRenderHintState after =
            ApplyRenderHintCommandToState(before, command);
        if (SameRenderHintState(before, after))
            return EditorCommandStatus::NoChange;

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult result =
                ExecuteEditorRenderHintMutation(
                    *context.CommandHistory,
                    context.Scene,
                    context.World,
                    command.StableEntityId,
                    before,
                    after);
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToEditorCommandStatus(result.Status));
        }

        return InvalidateSelectedModelCacheIfApplied(
            context,
            ToEditorCommandStatus(
                ApplyRenderHintState(context.Scene, command.StableEntityId, after)));
    }

    EditorCommandStatus ApplyEditorVisualizationConfigCommand(
        const EditorVisualizationEditingContext& context,
        const EditorVisualizationConfigCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return EditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;

        const std::optional<G::VisualizationConfig> before =
            StoredVisualizationConfigForTarget(raw, entity, command.Target);
        const std::optional<G::VisualizationConfig> effectiveBefore =
            EffectiveVisualizationConfigForTarget(raw, entity, command.Target);
        const std::optional<G::VisualizationConfig> after =
            command.EnableConfig
                ? std::optional<G::VisualizationConfig>{ToVisualizationConfig(command)}
                : std::nullopt;

        if (!after.has_value())
        {
            if (!before.has_value())
                return EditorCommandStatus::NoChange;
        }
        else if (before.has_value() && SameVisualizationConfig(*before, *after))
        {
            return EditorCommandStatus::NoChange;
        }
        else if (!before.has_value() &&
                 effectiveBefore.has_value() &&
                 SameVisualizationConfig(*effectiveBefore, *after))
        {
            return EditorCommandStatus::NoChange;
        }

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult result =
                ExecuteEditorVisualizationMutation(
                    *context.CommandHistory,
                    context.Scene,
                    context.World,
                    command.StableEntityId,
                    command.Target,
                    before,
                    after,
                    "Change Visualization");
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToEditorCommandStatus(result.Status));
        }

        return InvalidateSelectedModelCacheIfApplied(
            context,
            ToEditorCommandStatus(
                ApplyVisualizationConfigTarget(
                    context.Scene,
                    command.StableEntityId,
                    command.Target,
                    after)));
    }

    EditorCommandStatus ApplyEditorVisualizationPropertyCommand(
        const EditorVisualizationEditingContext& context,
        const EditorVisualizationPropertyCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return EditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;
        if (command.PropertyName.empty())
            return EditorCommandStatus::InvalidVisualizationProperty;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        const Geometry::PropertySet* properties =
            PropertySetForVisualizationDomain(availability, command.Domain);
        if (properties == nullptr)
            return EditorCommandStatus::UnsupportedGeometryDomain;

        std::optional<EditorVisualizationPropertyInfo> matched{};
        std::vector<EditorVisualizationPropertyInfo> allProperties{};
        AppendVisualizationPropertiesForDomain(
            allProperties,
            *properties,
            command.Domain);
        for (const EditorVisualizationPropertyInfo& property :
             allProperties)
        {
            if (property.Name == command.PropertyName)
            {
                matched = property;
                break;
            }
        }
        if (!matched.has_value() ||
            !PropertySupportsPreset(*matched, command.Preset))
        {
            return EditorCommandStatus::InvalidVisualizationProperty;
        }

        EditorVisualizationConfigCommand configCommand{
            .StableEntityId = command.StableEntityId,
            .Target = command.Target,
            .EnableConfig = true,
            .ScalarFieldName = command.PropertyName,
            .ScalarDomain = ToVisualizationConfigDomain(command.Domain),
            .ColorBufferName = command.PropertyName,
            .ScalarAutoRange = command.ScalarAutoRange,
            .ScalarRangeMin = command.ScalarRangeMin,
            .ScalarRangeMax = command.ScalarRangeMax,
            .ScalarBinCount = command.ScalarBinCount,
        };
        // UI-032 — preset buttons switch the source/property but must not
        // clobber styling the user already configured on this target
        // (colormap, isoline width/color, highlight isovalues).
        if (const std::optional<G::VisualizationConfig> existing =
                EffectiveVisualizationConfigForTarget(raw, entity, command.Target);
            existing.has_value())
        {
            configCommand.ScalarColormap = existing->Scalar.Map;
            configCommand.IsolineWidth = existing->Scalar.Isolines.Width;
            configCommand.IsolineColor = existing->Scalar.Isolines.Color;
            configCommand.IsolineValues = existing->Scalar.Isolines.Values;
            configCommand.IsolineValueCount = existing->Scalar.Isolines.ValueCount;
        }

        switch (command.Preset)
        {
        case EditorVisualizationPropertyPreset::Scalar:
            if (!command.ScalarAutoRange &&
                !(command.ScalarRangeMin < command.ScalarRangeMax))
            {
                return EditorCommandStatus::InvalidVisualizationProperty;
            }
            configCommand.Source =
                G::VisualizationConfig::ColorSource::ScalarField;
            configCommand.IsolineCount = 0u;
            break;
        case EditorVisualizationPropertyPreset::Isoline:
            if (command.IsolineCount == 0u ||
                (!command.ScalarAutoRange &&
                 !(command.ScalarRangeMin < command.ScalarRangeMax)))
            {
                return EditorCommandStatus::InvalidVisualizationProperty;
            }
            configCommand.Source =
                G::VisualizationConfig::ColorSource::ScalarField;
            configCommand.IsolineCount = command.IsolineCount;
            break;
        case EditorVisualizationPropertyPreset::ColorBuffer:
            configCommand.Source = ToColorBufferSource(command.Domain);
            configCommand.IsolineCount = 0u;
            break;
        }

        return ApplyEditorVisualizationConfigCommand(
            context,
            configCommand);
    }

    EditorCommandStatus ApplyEditorVisualizationRecipeCommand(
        const EditorVisualizationEditingContext& context,
        const EditorVisualizationRecipeCommand& command)
    {
        if (!context.VisualizationCommandsAvailable ||
            !context.VisualizationRecipes.Available())
            return EditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (!availability.HasGeometry())
            return EditorCommandStatus::UnsupportedGeometryDomain;

        const std::optional<VisualizationRecipe> current =
            context.VisualizationRecipes.GetRecipe(command.StableEntityId);
        if (!command.EnableRecipe)
        {
            if (!current.has_value())
                return EditorCommandStatus::NoChange;
            context.VisualizationRecipes.ClearRecipe(command.StableEntityId);
            InvalidateSelectedModelCache(context);
            return EditorCommandStatus::Applied;
        }

        if (current.has_value() &&
            SameVisualizationRecipe(*current, command.Recipe))
            return EditorCommandStatus::NoChange;

        context.VisualizationRecipes.SetRecipe(
            command.StableEntityId,
            command.Recipe);
        InvalidateSelectedModelCache(context);
        return EditorCommandStatus::Applied;
    }

    EditorCommandStatus ApplyEditorVertexChannelBindingCommand(
        const EditorVisualizationEditingContext& context,
        const EditorVertexChannelBindingCommand& command)
    {
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;
        if (command.Channel != VertexChannel::Normal &&
            command.Channel != VertexChannel::Color)
        {
            return EditorCommandStatus::InvalidVertexChannelBinding;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
            return EditorCommandStatus::StaleEntity;

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        const std::optional<EditorPropertyCatalogDomain> domain =
            VertexChannelCatalogDomainForView(view);
        if (!domain.has_value())
            return EditorCommandStatus::UnsupportedGeometryDomain;

        const Geometry::PropertySet* properties =
            VertexChannelPropertySetForView(view, *domain);
        if (properties == nullptr)
            return EditorCommandStatus::UnsupportedGeometryDomain;

        const std::optional<VertexChannelBindingSet> before =
            StoredVertexChannelBindingSet(raw, *entity);
        VertexChannelBindingSet after = before.has_value()
            ? *before
            : VertexChannelBindingSet{};
        VertexChannelSourceBinding* target =
            FindMutableVertexChannelBinding(after, command.Channel);
        if (target == nullptr)
            return EditorCommandStatus::InvalidVertexChannelBinding;

        if (!command.EnableBinding)
        {
            if (!before.has_value() ||
                !IsVertexChannelBindingEnabled(*target))
            {
                return EditorCommandStatus::NoChange;
            }

            *target = {};
            ++after.BindingGeneration;
        }
        else
        {
            if (command.PropertyName.empty())
                return EditorCommandStatus::InvalidVertexChannelBinding;

            const Geometry::PropertyValueKind valueKind =
                DetectGeometryPropertyValueKind(
                    *properties,
                    command.PropertyName);
            const std::optional<AttributeSourceType> sourceType =
                ToAttributeSourceType(valueKind);
            if (!sourceType.has_value())
                return EditorCommandStatus::InvalidVertexChannelBinding;

            const AttributeBindResult resolver =
                EvaluateVertexChannelBinding(
                    *properties,
                    command.Channel,
                    command.PropertyName,
                    *sourceType,
                    properties->Size(),
                    context.ModelBuildStats);
            if (!resolver.Ok())
                return EditorCommandStatus::InvalidVertexChannelBinding;

            const VertexChannelSourceBinding next{
                .Enabled = true,
                .Property = GeometryPropertyRef{
                    .Domain = ToGeometryElementDomain(*domain),
                    .Name = command.PropertyName,
                    .ValueKind = valueKind,
                },
            };
            if (before.has_value() &&
                SameVertexChannelSourceBinding(*target, next))
            {
                return EditorCommandStatus::NoChange;
            }

            *target = next;
            ++after.BindingGeneration;
        }

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult result =
                ExecuteVertexChannelBindingMutation(
                    *context.CommandHistory,
                    context.Scene,
                    context.World,
                    command.StableEntityId,
                    command.Channel,
                    before,
                    AnyVertexChannelBindingEnabled(after)
                        ? std::optional<VertexChannelBindingSet>{after}
                        : std::nullopt);
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToEditorCommandStatus(result.Status));
        }

        const EditorCommandHistoryStatus applied =
            ApplyVertexChannelBindingSet(
                context.Scene,
                command.StableEntityId,
                AnyVertexChannelBindingEnabled(after)
                    ? std::optional<VertexChannelBindingSet>{after}
                    : std::nullopt);
        if (applied == EditorCommandHistoryStatus::Applied)
            MarkVertexChannelDirty(raw, *entity, command.Channel);
        return InvalidateSelectedModelCacheIfApplied(
            context,
            ToEditorCommandStatus(applied));
    }

    EditorCommandStatus ApplyEditorGeometryPresentationSlotDefaultCommand(
        const EditorVisualizationEditingContext& context,
        const EditorGeometryPresentationSlotDefaultCommand& command)
    {
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;
        if (!IsFiniteDefaultValue(command.Value))
            return EditorCommandStatus::InvalidProcessingParameters;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;

        const auto* current =
            raw.try_get<GeometryPresentationRecipe>(entity);
        if (current == nullptr)
            return EditorCommandStatus::UnsupportedGeometryDomain;

        const auto* currentRuntime =
            raw.try_get<GeometryPresentationRuntimeState>(entity);
        GeometryPresentationEditorState before{
            .Recipe = *current,
            .Runtime = currentRuntime != nullptr
                ? *currentRuntime
                : GeometryPresentationRuntimeState{},
        };
        GeometryPresentationEditorState after = before;
        const GeometryPresentationSlotLookup lookup = FindMutableGeometryPresentationSlot(
            after.Recipe,
            command.PresentationKey,
            command.Semantic);
        if (lookup.Presentation == nullptr || lookup.Slot == nullptr)
            return EditorCommandStatus::InvalidVisualizationProperty;

        GeometryPresentationSlotRecipe& slot = *lookup.Slot;
        if (slot.SourceKind == GeometryPresentationSourceKind::UniformDefault &&
            slot.Enabled == command.Enabled &&
            SameGeometryPresentationDefaultValue(slot.UniformDefault, command.Value))
        {
            return EditorCommandStatus::NoChange;
        }

        slot.SourceKind = GeometryPresentationSourceKind::UniformDefault;
        slot.UniformDefault = command.Value;
        slot.Property = {};
        slot.TextureAsset = {};
        slot.GeneratedPolicy =
            DefaultGeometryGeneratedOutputPolicyFor(slot.SourceKind);
        slot.Enabled = command.Enabled;
        ResetGeometryPresentationSlotStatus(
            after.Runtime,
            lookup.Presentation->Key,
            slot.Semantic,
            GeometryPresentationReadiness::DefaultValue,
            GeometryPresentationProvenance::UniformDefault);

        return InvalidateSelectedModelCacheIfApplied(
            context,
            CommitGeometryPresentationChange(
                context,
                command.StableEntityId,
                std::move(before),
                std::move(after)));
    }

    EditorCommandStatus ApplyEditorGeometryPresentationSlotPropertyCommand(
        const EditorVisualizationEditingContext& context,
        const EditorGeometryPresentationSlotPropertyCommand& command)
    {
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;
        if (!PropertySourceKindAllowedForGeometryPresentationSlotCommand(command.SourceKind) ||
            command.PropertyName.empty())
        {
            return EditorCommandStatus::InvalidVisualizationProperty;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;

        const auto* current =
            raw.try_get<GeometryPresentationRecipe>(entity);
        if (current == nullptr)
            return EditorCommandStatus::UnsupportedGeometryDomain;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        const std::size_t expectedCount =
            ResolveGeometryElementCount(availability, command.Domain);
        GeometryPropertyRef descriptor{
            .Domain = command.Domain,
            .Name = command.PropertyName,
            .ValueKind = command.ExpectedValueKind.value_or(
                Geometry::PropertyValueKind::Unknown),
        };
        GeometryPropertyResolution resolution =
            ResolveGeometryProperty(
                availability,
                descriptor,
                expectedCount);
        if (!resolution.Resolved())
            return EditorCommandStatus::InvalidVisualizationProperty;
        descriptor.ValueKind = resolution.ResolvedValueKind;

        const auto* currentRuntime =
            raw.try_get<GeometryPresentationRuntimeState>(entity);
        GeometryPresentationEditorState before{
            .Recipe = *current,
            .Runtime = currentRuntime != nullptr
                ? *currentRuntime
                : GeometryPresentationRuntimeState{},
        };
        GeometryPresentationEditorState after = before;
        const GeometryPresentationSlotLookup lookup = FindMutableGeometryPresentationSlot(
            after.Recipe,
            command.PresentationKey,
            command.Semantic);
        if (lookup.Presentation == nullptr || lookup.Slot == nullptr)
            return EditorCommandStatus::InvalidVisualizationProperty;

        GeometryPresentationSlotRecipe& slot = *lookup.Slot;
        const GeometryPresentationReadiness nextReadiness =
            command.SourceKind == GeometryPresentationSourceKind::PropertyBuffer
                ? GeometryPresentationReadiness::Ready
                : GeometryPresentationReadiness::Pending;
        const GeometryPresentationProvenance nextProvenance =
            command.SourceKind == GeometryPresentationSourceKind::PropertyBuffer
                ? GeometryPresentationProvenance::PropertyBuffer
                : GeometryPresentationProvenance::PropertyBinding;

        if (slot.SourceKind == command.SourceKind && slot.Enabled &&
            SameGeometryPresentationPropertyDescriptor(slot.Property, descriptor))
        {
            return EditorCommandStatus::NoChange;
        }

        slot.SourceKind = command.SourceKind;
        slot.Property = std::move(descriptor);
        slot.TextureAsset = {};
        slot.GeneratedPolicy =
            DefaultGeometryGeneratedOutputPolicyFor(slot.SourceKind);
        slot.Enabled = true;
        ResetGeometryPresentationSlotStatus(
            after.Runtime,
            lookup.Presentation->Key,
            slot.Semantic,
            nextReadiness,
            nextProvenance);

        return InvalidateSelectedModelCacheIfApplied(
            context,
            CommitGeometryPresentationChange(
                context,
                command.StableEntityId,
                std::move(before),
                std::move(after)));
    }

    bool IsEditorTextureBakeTargetCompatible(
        const EditorTextureBakeTarget& target,
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage storage,
        const PropertyTextureBakeEncoding encoding) noexcept
    {
        if (!IsPropertyTextureBakeRepresentationCompatible(
                valueKind, storage, encoding))
        {
            return false;
        }
        const bool raw = storage == PropertyTextureBakeStorage::RawFloat;
        const bool scalar = valueKind == Geometry::PropertyValueKind::Float ||
                            valueKind == Geometry::PropertyValueKind::Double;
        switch (target.Semantic)
        {
        case GeometryPresentationSlotSemantic::Normal:
            return valueKind == Geometry::PropertyValueKind::Vec3 &&
                   storage == PropertyTextureBakeStorage::EncodedRgba &&
                   encoding == PropertyTextureBakeEncoding::Normal;
        case GeometryPresentationSlotSemantic::Roughness:
        case GeometryPresentationSlotSemantic::Metallic:
            return scalar &&
                   (raw ||
                    encoding == PropertyTextureBakeEncoding::LinearScalar);
        case GeometryPresentationSlotSemantic::ScalarField:
            return (scalar &&
                    (raw ||
                     encoding == PropertyTextureBakeEncoding::LinearScalar ||
                     encoding ==
                         PropertyTextureBakeEncoding::ScalarColormap)) ||
                   (valueKind == Geometry::PropertyValueKind::UInt32 && !raw &&
                    encoding == PropertyTextureBakeEncoding::LabelPalette);
        case GeometryPresentationSlotSemantic::Albedo:
            if (scalar)
            {
                return raw ||
                       encoding == PropertyTextureBakeEncoding::LinearScalar ||
                       encoding == PropertyTextureBakeEncoding::ScalarColormap;
            }
            if (valueKind == Geometry::PropertyValueKind::UInt32)
            {
                return !raw &&
                       encoding == PropertyTextureBakeEncoding::LabelPalette;
            }
            if (valueKind == Geometry::PropertyValueKind::Vec2)
            {
                return raw ||
                       encoding == PropertyTextureBakeEncoding::Vector2 ||
                       encoding == PropertyTextureBakeEncoding::RgbaColor;
            }
            if (valueKind == Geometry::PropertyValueKind::Vec3)
            {
                return raw ||
                       encoding == PropertyTextureBakeEncoding::Vector3 ||
                       encoding == PropertyTextureBakeEncoding::RgbaColor ||
                       encoding == PropertyTextureBakeEncoding::Normal;
            }
            return valueKind == Geometry::PropertyValueKind::Vec4 &&
                   (raw || encoding == PropertyTextureBakeEncoding::RgbaColor);
        case GeometryPresentationSlotSemantic::Displacement:
        case GeometryPresentationSlotSemantic::PointColor:
        case GeometryPresentationSlotSemantic::PointScalarField:
        case GeometryPresentationSlotSemantic::PointSize:
        case GeometryPresentationSlotSemantic::PointNormalOrientation:
        case GeometryPresentationSlotSemantic::LineColor:
        case GeometryPresentationSlotSemantic::LineScalarField:
        case GeometryPresentationSlotSemantic::LineWidth:
            return false;
        }
        return false;
    }

    [[nodiscard]] bool ValidateEditorTextureBakeTargetSet(
        const PropertyTextureBakeRecord& output,
        const std::span<const EditorTextureBakeTarget> targets,
        std::string& diagnostic)
    {
        std::optional<Graphics::Colormap::Type> scalarColormap{};
        for (const EditorTextureBakeTarget& target : targets)
        {
            if (target.Colormap >= Graphics::Colormap::Type::Count)
            {
                diagnostic = "generated texture target colormap is invalid";
                return false;
            }
            if (!IsEditorTextureBakeTargetCompatible(
                    target,
                    output.Source.ValueKind,
                    output.Storage,
                    output.Encoding))
            {
                diagnostic =
                    "generated texture target is incompatible with the baked "
                    "representation";
                return false;
            }
            if (target.Semantic != GeometryPresentationSlotSemantic::Albedo &&
                target.Semantic !=
                    GeometryPresentationSlotSemantic::ScalarField)
            {
                continue;
            }
            if (scalarColormap.has_value() &&
                *scalarColormap != target.Colormap)
            {
                diagnostic =
                    "one scalar texture cannot use different colormaps in the "
                    "same surface material";
                return false;
            }
            scalarColormap = target.Colormap;
        }
        return true;
    }

    PropertyTextureBakeRepresentation
ResolveEditorTextureBakeTargetRepresentation(
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage requestedStorage,
        const PropertyTextureBakeEncoding requestedEncoding,
        const std::span<const EditorTextureBakeTarget> targets) noexcept
    {
        PropertyTextureBakeStorage storage = requestedStorage;
        PropertyTextureBakeEncoding encoding = requestedEncoding;
        const bool hasNormalTarget = std::ranges::any_of(
            targets,
            [](const EditorTextureBakeTarget& target)
            {
                return target.Semantic ==
                       GeometryPresentationSlotSemantic::Normal;
            });
        const bool hasLinearPbrTarget = std::ranges::any_of(
            targets,
            [](const EditorTextureBakeTarget& target)
            {
                return target.Semantic ==
                           GeometryPresentationSlotSemantic::Roughness ||
                       target.Semantic ==
                           GeometryPresentationSlotSemantic::Metallic;
            });
        if (storage == PropertyTextureBakeStorage::Auto &&
            (hasNormalTarget ||
             valueKind == Geometry::PropertyValueKind::UInt32))
        {
            storage = PropertyTextureBakeStorage::EncodedRgba;
        }
        if (encoding == PropertyTextureBakeEncoding::Auto)
        {
            if (hasNormalTarget)
            {
                encoding = PropertyTextureBakeEncoding::Normal;
            }
            else if (hasLinearPbrTarget &&
                     (valueKind == Geometry::PropertyValueKind::Float ||
                      valueKind == Geometry::PropertyValueKind::Double))
            {
                encoding = PropertyTextureBakeEncoding::LinearScalar;
            }
        }
        return ResolvePropertyTextureBakeRepresentation(
            valueKind, storage, encoding);
    }

    EditorTextureBakeCommandResult
ApplyEditorTextureBakeCommand(
        const EditorVisualizationEditingContext& context,
        const EditorTextureBakeCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return EditorTextureBakeCommandResult{
                .Status = EditorCommandStatus::MissingScene,
                .BakeStatus = PropertyTextureBakeStatus::MissingScene,
                .Diagnostic = "Scene registry is unavailable.",
            };
        }
        if (context.TextureBake == nullptr)
        {
            return EditorTextureBakeCommandResult{
                .Status = EditorCommandStatus::AssetImportFailed,
                .BakeStatus =
                    PropertyTextureBakeStatus::NonOperationalBackend,
                .Diagnostic = "Texture-bake runtime module is unavailable.",
            };
        }
        if (!context.TextureBake->Available())
        {
            return EditorTextureBakeCommandResult{
                .Status = EditorCommandStatus::InvalidVisualizationProperty,
                .BakeStatus =
                    PropertyTextureBakeStatus::NonOperationalBackend,
                .Diagnostic =
                    "Texture baking requires an operational GPU runtime module.",
            };
        }
        if (command.PropertyName.empty() ||
            command.Width == 0u ||
            command.Height == 0u)
        {
            return EditorTextureBakeCommandResult{
                .Status = EditorCommandStatus::InvalidVisualizationProperty,
                .BakeStatus = command.PropertyName.empty()
                    ? PropertyTextureBakeStatus::MissingProperty
                    : PropertyTextureBakeStatus::InvalidResolution,
                .Diagnostic = "Texture bake command has invalid parameters.",
            };
        }

        std::vector<EditorTextureBakeTarget> targets =
            command.Targets;
        if (targets.empty() && command.BindGeneratedTexture)
        {
            targets.push_back(EditorTextureBakeTarget{
                .PresentationKey = command.PresentationKey,
                .Semantic = command.TargetSemantic,
                .Colormap = command.EncodingColormap,
                .NormalSpace = command.NormalSpace,
            });
        }
        for (EditorTextureBakeTarget& target : targets)
        {
            if (target.Semantic ==
                GeometryPresentationSlotSemantic::Normal)
            {
                target.NormalSpace = command.NormalSpace;
            }
        }

        PropertyTextureBakeStorage storage = command.Storage;
        PropertyTextureBakeEncoding encoding = command.Encoder;
        const bool normalTarget = std::ranges::any_of(
            targets,
            [](const EditorTextureBakeTarget& target)
            {
                return target.Semantic ==
                    GeometryPresentationSlotSemantic::Normal;
            });
        if (normalTarget)
        {
            if (storage == PropertyTextureBakeStorage::Auto)
                storage = PropertyTextureBakeStorage::EncodedRgba;
            if (encoding == PropertyTextureBakeEncoding::Auto)
                encoding = PropertyTextureBakeEncoding::Normal;
        }
        else if (command.ExpectedValueKind.has_value())
        {
            const PropertyTextureBakeRepresentation representation =
                ResolvePropertyTextureBakeRepresentation(
                    *command.ExpectedValueKind,
                    storage,
                    encoding);
            storage = representation.Storage;
            encoding = representation.Encoding;
        }

        PropertyTextureBakeRequest request{};
        request.World = context.World;
        request.StableEntityId = command.StableEntityId;
        request.Source = GeometryPropertyRef{
            .Domain = command.SourceDomain,
            .Name = command.PropertyName,
            .ValueKind = command.ExpectedValueKind.value_or(
                Geometry::PropertyValueKind::Unknown),
        };
        request.Encoding = encoding;
        request.RangePolicy = command.RangePolicy;
        request.RangeMin = command.RangeMin;
        request.RangeMax = command.RangeMax;
        request.Width = command.Width;
        request.Height = command.Height;
        request.PaddingTexels = command.PaddingTexels;
        request.OutputName = command.OutputName.empty()
            ? command.PropertyName
            : command.OutputName;
        request.Storage = storage;
        request.EncodingColormap = command.EncodingColormap;

        const PropertyTextureBakeResult bake =
            context.TextureBake->Bake(request);
        std::optional<PropertyTextureBakeRecord> scheduledOutput{};
        if (bake.Succeeded())
        {
            const TextureBakeSnapshot snapshot =
                context.TextureBake->Snapshot(command.StableEntityId);
            const auto output =
                std::ranges::find(snapshot.Textures,
                                  bake.OutputName,
                                  &PropertyTextureBakeRecord::OutputName);
            if (output != snapshot.Textures.end())
                scheduledOutput = *output;
        }

        EditorCommandStatus status =
            EditorCommandStatus::Applied;
        if (!bake.Succeeded())
        {
            switch (bake.Status)
            {
            case PropertyTextureBakeStatus::MissingScene:
                status = EditorCommandStatus::MissingScene;
                break;
            case PropertyTextureBakeStatus::MissingAssetService:
            case PropertyTextureBakeStatus::AssetLoadFailed:
                status = EditorCommandStatus::AssetImportFailed;
                break;
            case PropertyTextureBakeStatus::StaleEntity:
                status = EditorCommandStatus::StaleEntity;
                break;
            case PropertyTextureBakeStatus::NonMeshSource:
            case PropertyTextureBakeStatus::UnsupportedSourceDomain:
                status = EditorCommandStatus::UnsupportedGeometryDomain;
                break;
            case PropertyTextureBakeStatus::CommandFailed:
                status = EditorCommandStatus::GeometryProcessingFailed;
                break;
            case PropertyTextureBakeStatus::Success:
            case PropertyTextureBakeStatus::Scheduled:
            case PropertyTextureBakeStatus::NonOperationalBackend:
            case PropertyTextureBakeStatus::InvalidResolution:
            case PropertyTextureBakeStatus::InvalidPadding:
            case PropertyTextureBakeStatus::InvalidRange:
            case PropertyTextureBakeStatus::MissingProperty:
            case PropertyTextureBakeStatus::UnsupportedPropertyType:
            case PropertyTextureBakeStatus::MismatchedPropertyCount:
            case PropertyTextureBakeStatus::MissingTexcoords:
            case PropertyTextureBakeStatus::NonFiniteTexcoord:
            case PropertyTextureBakeStatus::NonFinitePropertyValue:
            case PropertyTextureBakeStatus::DegenerateAllTriangles:
            case PropertyTextureBakeStatus::DegenerateUvTriangles:
            case PropertyTextureBakeStatus::ZeroCoverageBake:
            case PropertyTextureBakeStatus::BakeFailed:
            case PropertyTextureBakeStatus::JobSubmitFailed:
            case PropertyTextureBakeStatus::StaleCompletion:
                status = EditorCommandStatus::InvalidVisualizationProperty;
                break;
            }
        }
        if (status == EditorCommandStatus::Applied &&
            !targets.empty())
        {
            TextureBakeMutationResult bound{};
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(command.StableEntityId);
            auto& raw = context.Scene->Raw();
            const auto* current =
                raw.try_get<GeometryPresentationRecipe>(entity);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            {
                bound = TextureBakeMutationResult{
                    .Status = TextureBakeMutationStatus::StaleEntity,
                    .Diagnostic = "entity is stale",
                };
            }
            else if (!scheduledOutput.has_value())
            {
                bound = TextureBakeMutationResult{
                    .Status = TextureBakeMutationStatus::MissingTexture,
                    .Diagnostic =
                        "scheduled generated texture output is unavailable",
                };
            }
            else if (current == nullptr)
            {
                bound = TextureBakeMutationResult{
                    .Status = TextureBakeMutationStatus::IncompatibleTarget,
                    .Diagnostic =
                        "generated texture targets require a geometry "
                        "presentation recipe",
                };
            }
            else
            {
                GeometryPresentationRecipe next = *current;
                GeometryPresentationRuntimeState nextRuntime =
                    raw.try_get<GeometryPresentationRuntimeState>(entity) !=
                            nullptr
                        ? *raw.try_get<GeometryPresentationRuntimeState>(entity)
                        : GeometryPresentationRuntimeState{};
                std::string targetDiagnostic{};
                bool compatible = ValidateEditorTextureBakeTargetSet(
                    *scheduledOutput, targets, targetDiagnostic);
                std::vector<EditorTextureBakeTarget> resolvedTargets{};
                for (EditorTextureBakeTarget& target : targets)
                {
                    if (!compatible)
                        break;
                    GeometryPresentationBindingRecipe* presentation = nullptr;
                    if (!target.PresentationKey.empty())
                    {
                        presentation = FindGeometryPresentationBinding(
                            next, target.PresentationKey);
                    }
                    else
                    {
                        for (auto& candidate : next.Presentations)
                        {
                            if (FindGeometryPresentationSlot(
                                    candidate, target.Semantic) != nullptr)
                            {
                                presentation = &candidate;
                                target.PresentationKey = candidate.Key;
                                break;
                            }
                        }
                    }
                    GeometryPresentationSlotRecipe* slot =
                        presentation != nullptr
                            ? FindGeometryPresentationSlot(*presentation,
                                                           target.Semantic)
                            : nullptr;
                    if (presentation == nullptr ||
                        presentation->Kind !=
                            GeometryPresentationKind::SurfaceMaterial ||
                        slot == nullptr)
                    {
                        compatible = false;
                        break;
                    }
                    if (std::ranges::any_of(
                            resolvedTargets,
                            [&](const EditorTextureBakeTarget& existing)
                            {
                                return existing.PresentationKey ==
                                           presentation->Key &&
                                       existing.Semantic == target.Semantic;
                            }))
                    {
                        compatible = false;
                        targetDiagnostic = "generated texture target list "
                                           "contains a duplicate slot";
                        break;
                    }
                    target.PresentationKey = presentation->Key;
                    resolvedTargets.push_back(target);

                    slot->SourceKind =
                        GeometryPresentationSourceKind::PropertyBake;
                    slot->Property = scheduledOutput->Source;
                    slot->GeneratedOutputName = bake.OutputName;
                    slot->TextureColormap = target.Colormap;
                    slot->NormalSpace = target.NormalSpace;
                    slot->GeneratedPolicy =
                        GeometryGeneratedOutputPolicy::DeterministicChildAsset;
                    slot->Enabled = true;

                    GeometryPresentationSlotStatus* slotStatus =
                        FindGeometryPresentationSlotStatus(
                            nextRuntime, presentation->Key, target.Semantic);
                    if (slotStatus == nullptr)
                    {
                        nextRuntime.Slots.push_back(
                            GeometryPresentationSlotStatus{
                                .PresentationKey = presentation->Key,
                                .Semantic = target.Semantic,
                            });
                        slotStatus = &nextRuntime.Slots.back();
                    }
                    if (slotStatus->GeneratedOutputName != bake.OutputName)
                    {
                        slotStatus->GeneratedTexture = {};
                    }
                    slotStatus->GeneratedOutputName = bake.OutputName;
                    slotStatus->Readiness =
                        GeometryPresentationReadiness::Pending;
                    slotStatus->Provenance =
                        GeometryPresentationProvenance::PropertyBinding;
                    slotStatus->Diagnostic = bake.Diagnostic;
                }
                if (compatible)
                {
                    ++nextRuntime.RecipeGeneration;
                    raw.emplace_or_replace<GeometryPresentationRecipe>(
                        entity, std::move(next));
                    raw.emplace_or_replace<GeometryPresentationRuntimeState>(
                        entity, std::move(nextRuntime));
                    bound = TextureBakeMutationResult{
                        .Status = TextureBakeMutationStatus::Success,
                        .Diagnostic = "generated texture targets updated",
                    };
                }
                else
                {
                    bound = TextureBakeMutationResult{
                        .Status = TextureBakeMutationStatus::IncompatibleTarget,
                        .Diagnostic = targetDiagnostic.empty()
                                          ? "generated texture target has no "
                                            "compatible surface slot"
                                          : std::move(targetDiagnostic),
                    };
                }
            }
            if (!bound.Succeeded())
            {
                (void)context.TextureBake->Remove(command.StableEntityId,
                                                  bake.OutputName);
                return EditorTextureBakeCommandResult{
                    .Status = EditorCommandStatus::
                        InvalidVisualizationProperty,
                    .BakeStatus = PropertyTextureBakeStatus::CommandFailed,
                    .OutputName = bake.OutputName,
                        .Diagnostic = bound.Diagnostic,
                };
            }
        }

        EditorTextureBakeCommandResult result{
            .Status = status,
            .BakeStatus = bake.Status,
            .GeneratedTexture = bake.GeneratedTexture,
            .Job = bake.Job,
            .Scheduled = bake.Status == PropertyTextureBakeStatus::Scheduled,
            .BoundGeneratedTexture = false,
            .GeneratedAssetPath = bake.GeneratedAssetPath,
            .OutputName = bake.OutputName,
            .Diagnostic = bake.Diagnostic,
        };
        if (result.Status == EditorCommandStatus::Applied)
            InvalidateSelectedModelCache(context);
        return result;
    }

    TextureBakeMutationResult RenameEditorBakedTexture(
        const EditorVisualizationEditingContext& context,
        const std::uint32_t stableEntityId,
        const std::string_view currentName,
        const std::string_view newName)
    {
        if (context.TextureBake == nullptr)
        {
            return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::MissingScene,
                .Diagnostic = "Texture-bake runtime module is unavailable.",
            };
        }
        const TextureBakeMutationResult result =
            context.TextureBake->Rename(stableEntityId, currentName, newName);
        if (result.Succeeded())
        {
            if (context.Scene != nullptr)
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableEntityId);
                auto& raw = context.Scene->Raw();
                if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                {
                    if (auto* recipe =
                            raw.try_get<GeometryPresentationRecipe>(entity))
                    {
                        for (auto& presentation : recipe->Presentations)
                        {
                            for (auto& slot : presentation.Slots)
                            {
                                if (slot.GeneratedOutputName == currentName)
                                {
                                    slot.GeneratedOutputName =
                                        std::string{newName};
                                }
                            }
                        }
                    }
                    if (auto* runtimeState =
                            raw.try_get<GeometryPresentationRuntimeState>(
                                entity))
                    {
                        for (auto& status : runtimeState->Slots)
                        {
                            if (status.GeneratedOutputName == currentName)
                            {
                                status.GeneratedOutputName =
                                    std::string{newName};
                            }
                        }
                    }
                }
            }
            InvalidateSelectedModelCache(context);
        }
        return result;
    }

    TextureBakeMutationResult
RemoveEditorBakedTexture(
        const EditorVisualizationEditingContext& context,
        const std::uint32_t stableEntityId,
        const std::string_view outputName)
    {
        if (context.TextureBake == nullptr)
        {
            return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::MissingScene,
                .Diagnostic = "Texture-bake runtime module is unavailable.",
            };
        }
        const TextureBakeMutationResult result = context.TextureBake->Remove(
            stableEntityId, outputName);
        if (result.Succeeded())
        {
            if (context.Scene != nullptr)
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableEntityId);
                auto& raw = context.Scene->Raw();
                if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                {
                    if (auto* recipe =
                            raw.try_get<GeometryPresentationRecipe>(entity))
                    {
                        for (auto& presentation : recipe->Presentations)
                        {
                            for (auto& slot : presentation.Slots)
                            {
                                if (slot.GeneratedOutputName != outputName)
                                {
                                    continue;
                                }
                                slot.SourceKind =
                                    GeometryPresentationSourceKind::
                                        PropertyBuffer;
                                slot.GeneratedOutputName.clear();
                            }
                        }
                    }
                    if (auto* runtimeState =
                            raw.try_get<GeometryPresentationRuntimeState>(
                                entity))
                    {
                        for (auto& status : runtimeState->Slots)
                        {
                            if (status.GeneratedOutputName != outputName)
                            {
                                continue;
                            }
                            status.Readiness =
                                GeometryPresentationReadiness::Ready;
                            status.Provenance =
                                GeometryPresentationProvenance::PropertyBuffer;
                            status.Diagnostic =
                                "generated texture removed; property-buffer "
                                "source restored";
                        }
                    }
                }
            }
            InvalidateSelectedModelCache(context);
        }
        return result;
    }

    TextureBakeMutationResult SetEditorBakedTextureTargets(
        const EditorVisualizationEditingContext& context,
        const EditorTextureBakeTargetUpdateRequest& request)
    {
        if (context.TextureBake == nullptr || context.Scene == nullptr)
        {
            return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::MissingScene,
                .Diagnostic =
                    "Texture-bake runtime module or scene is unavailable.",
            };
        }

        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(request.StableEntityId);
        auto& raw = context.Scene->Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
        {
            return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::StaleEntity,
                .Diagnostic = "entity is stale",
            };
        }

        const TextureBakeSnapshot bakeSnapshot = context.TextureBake->Snapshot(request.StableEntityId);
        const auto output =
            std::ranges::find(bakeSnapshot.Textures,
                              request.OutputName,
                              &PropertyTextureBakeRecord::OutputName);
        if (output == bakeSnapshot.Textures.end())
        {
            return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::MissingTexture,
                .Diagnostic = "baked texture was not found",
            };
        }
        std::string targetDiagnostic{};
        if (!ValidateEditorTextureBakeTargetSet(
                *output, request.Targets, targetDiagnostic))
        {
            return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::IncompatibleTarget,
                .Diagnostic = std::move(targetDiagnostic),
            };
        }

        const auto* current = raw.try_get<GeometryPresentationRecipe>(entity);
        if (current == nullptr)
        {
            return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::IncompatibleTarget,
                .Diagnostic = "generated texture targets require a geometry "
                              "presentation recipe",
            };
        }

        GeometryPresentationRecipe next = *current;
        GeometryPresentationRuntimeState nextRuntime =
            raw.try_get<GeometryPresentationRuntimeState>(entity) != nullptr
                ? *raw.try_get<GeometryPresentationRuntimeState>(entity)
                : GeometryPresentationRuntimeState{};
        std::vector<EditorTextureBakeTarget> resolvedTargets{};

        for (GeometryPresentationBindingRecipe& presentation :
             next.Presentations)
        {
            for (GeometryPresentationSlotRecipe& slot : presentation.Slots)
            {
                if (slot.GeneratedOutputName != request.OutputName)
                {
                    continue;
                }
                slot.SourceKind =
                    GeometryPresentationSourceKind::PropertyBuffer;
                slot.GeneratedOutputName.clear();
                if (GeometryPresentationSlotStatus* status =
                        FindGeometryPresentationSlotStatus(
                            nextRuntime, presentation.Key, slot.Semantic))
                {
                    status->Readiness = GeometryPresentationReadiness::Ready;
                    status->Provenance =
                        GeometryPresentationProvenance::PropertyBuffer;
                    status->Diagnostic = "generated texture target removed; "
                                         "property-buffer source restored";
                }
            }
        }

        for (const EditorTextureBakeTarget& target : request.Targets)
        {
            GeometryPresentationBindingRecipe* presentation =
                target.PresentationKey.empty()
                    ? nullptr
                    : FindGeometryPresentationBinding(next,
                                                      target.PresentationKey);
            if (presentation == nullptr && target.PresentationKey.empty())
            {
                for (auto& candidate : next.Presentations)
                {
                    if (FindGeometryPresentationSlot(
                            candidate, target.Semantic) != nullptr)
                    {
                        presentation = &candidate;
                        break;
                    }
                }
            }
            GeometryPresentationSlotRecipe* slot =
                presentation != nullptr ? FindGeometryPresentationSlot(
                                              *presentation, target.Semantic)
                                        : nullptr;
            if (presentation == nullptr ||
                presentation->Kind !=
                    GeometryPresentationKind::SurfaceMaterial ||
                slot == nullptr)
            {
                return TextureBakeMutationResult{
                    .Status = TextureBakeMutationStatus::IncompatibleTarget,
                    .Diagnostic = "generated texture target has no compatible "
                                  "surface slot",
                };
            }
            if (std::ranges::any_of(
                    resolvedTargets,
                    [&](const EditorTextureBakeTarget& existing)
                    {
                        return existing.PresentationKey == presentation->Key &&
                               existing.Semantic == target.Semantic;
                    }))
            {
                return TextureBakeMutationResult{
                    .Status = TextureBakeMutationStatus::IncompatibleTarget,
                    .Diagnostic = "generated texture target list contains a "
                                  "duplicate slot",
                };
            }
            resolvedTargets.push_back(EditorTextureBakeTarget{
                .PresentationKey = presentation->Key,
                .Semantic = target.Semantic,
                .Colormap = target.Colormap,
                .NormalSpace = target.NormalSpace,
            });

            slot->SourceKind = GeometryPresentationSourceKind::PropertyBake;
            slot->Property = output->Source;
            slot->GeneratedOutputName = request.OutputName;
            slot->TextureColormap = target.Colormap;
            slot->NormalSpace = target.NormalSpace;
            slot->GeneratedPolicy =
                GeometryGeneratedOutputPolicy::DeterministicChildAsset;
            slot->Enabled = true;

            GeometryPresentationSlotStatus* status =
                FindGeometryPresentationSlotStatus(
                    nextRuntime, presentation->Key, target.Semantic);
            if (status == nullptr)
            {
                nextRuntime.Slots.push_back(GeometryPresentationSlotStatus{
                    .PresentationKey = presentation->Key,
                    .Semantic = target.Semantic,
                });
                status = &nextRuntime.Slots.back();
            }
            status->GeneratedOutputName = request.OutputName;
            status->Readiness =
                output->State == PropertyTextureBakeOutputState::Ready
                    ? GeometryPresentationReadiness::Ready
                    : GeometryPresentationReadiness::Pending;
            status->Provenance =
                output->State == PropertyTextureBakeOutputState::Ready
                    ? GeometryPresentationProvenance::GeneratedTextureAsset
                    : GeometryPresentationProvenance::PropertyBinding;
            status->GeneratedTexture =
                output->State == PropertyTextureBakeOutputState::Ready
                    ? output->Texture
                    : Assets::AssetId{};
            status->SourceGeneration = output->SourceGeneration;
            status->OutputGeneration = output->Generation;
            status->Diagnostic = output->Diagnostic;
        }

        ++nextRuntime.RecipeGeneration;
        raw.emplace_or_replace<GeometryPresentationRecipe>(entity,
                                                           std::move(next));
        raw.emplace_or_replace<GeometryPresentationRuntimeState>(
            entity, std::move(nextRuntime));
        InvalidateSelectedModelCache(context);
        return TextureBakeMutationResult{
                .Status = TextureBakeMutationStatus::Success,
                .Diagnostic = "generated texture targets updated",
        };
    }

} // namespace Extrinsic::Runtime
