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

module Extrinsic.Runtime.GeometryProcessingOperations;

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
import Extrinsic.Runtime.MeshSurfaceTopology;
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
import Geometry.HalfedgeMesh.Utils;
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
#include "Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp"

namespace Extrinsic::Runtime {
namespace {
        constexpr std::uint64_t kEditorSignatureOffset =
            1469598103934665603ull;
        constexpr std::uint64_t kEditorSignaturePrime =
            1099511628211ull;

        inline constexpr std::array<EditorMeshRemeshMode, 2>
            kMeshRemeshModes{{
                EditorMeshRemeshMode::Uniform,
                EditorMeshRemeshMode::Adaptive,
            }};
        inline constexpr std::array<EditorMeshDenoiseStage, 1>
            kMeshDenoiseStages{{
                EditorMeshDenoiseStage::FullBilateral,
            }};
        inline constexpr std::array<EditorMeshCurvatureOutput, 4>
            kMeshCurvatureOutputs{{
                EditorMeshCurvatureOutput::All,
                EditorMeshCurvatureOutput::Mean,
                EditorMeshCurvatureOutput::Gaussian,
                EditorMeshCurvatureOutput::PrincipalDirections,
            }};
        inline constexpr std::array<EditorMeshRemeshSizingLaw, 2>
            kMeshRemeshSizingLaws{{
                EditorMeshRemeshSizingLaw::MeanCurvature,
                EditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
            }};
        inline constexpr std::array<EditorMeshSimplifyMetric, 2>
            kMeshSimplifyMetrics{{
                EditorMeshSimplifyMetric::ClassicalQEM,
                EditorMeshSimplifyMetric::FA_QEM,
            }};
        inline constexpr std::array<EditorICPVariant, 2>
            kEditorICPVariants{{
                EditorICPVariant::PointToPoint,
                EditorICPVariant::PointToPlane,
            }};
        inline constexpr std::array<EditorMeshSubdivideOperator, 3>
            kMeshSubdivideOperators{{
                EditorMeshSubdivideOperator::Loop,
                EditorMeshSubdivideOperator::CatmullClark,
                EditorMeshSubdivideOperator::Sqrt3,
            }};
        constexpr std::string_view kUvRegenerationJobOutputName{
            "uv_regeneration"};

        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;
        namespace GN = Geometry::HalfedgeMesh::VertexNormals;
        namespace GraphNormals = Geometry::Graph::VertexNormals;
namespace Reg = Geometry::Registration;

struct RegistrationAlignmentOutcome {
  bool HasResult{false};
  Reg::RegistrationResult Result{};
  std::vector<Reg::IterationTrace> Traces{};

  [[nodiscard]] std::size_t IterationCount() const noexcept {
    return Traces.size();
  }
};

[[nodiscard]] RegistrationAlignmentOutcome
AlignPointClouds(const std::span<const glm::vec3> sourcePoints,
                 const std::span<const glm::vec3> targetPoints,
                 const std::span<const glm::vec3> targetNormals,
                 const Reg::RegistrationParams &params) {
  RegistrationAlignmentOutcome outcome;
  outcome.Traces.reserve(params.MaxIterations);
  const auto result =
      Reg::AlignICP(sourcePoints, targetPoints, targetNormals, params,
                    [&outcome](const Reg::IterationTrace &trace) {
                      outcome.Traces.push_back(trace);
                    });
  if (result) {
    outcome.HasResult = true;
    outcome.Result = *result;
  }
  return outcome;
}

[[nodiscard]] glm::mat4
TrajectoryPose(const RegistrationAlignmentOutcome &outcome,
               const std::size_t index) {
  if (index == 0u || outcome.Traces.empty())
    return glm::mat4(1.0f);

  const std::size_t clamped = std::min(index, outcome.Traces.size());
  return glm::mat4(outcome.Traces[clamped - 1u].Transform);
}
namespace PointNormals = Geometry::PointCloud::Normals;
        namespace SurfaceSampling = Geometry::PointCloud::SurfaceSampling;
        namespace Smooth = Geometry::Smoothing;
        namespace Curv = Geometry::Curvature;
        namespace Remesh = Geometry::Remeshing;
        namespace AdaptiveRemesh = Geometry::AdaptiveRemeshing;
        namespace LoopSubdivide = Geometry::Subdivision;
        namespace CatmullClark = Geometry::CatmullClark;
        namespace Sqrt3Subdivide = Geometry::SubdivisionSqrt3;
        namespace Simpl = Geometry::Simplification;
        namespace Reg = Geometry::Registration;
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

struct EditorJobResult { std::string Diagnostic{}; };
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

        constexpr EditorGeometryProcessingDomain kMeshTopologyDomains =
            EditorGeometryProcessingDomain::MeshVertices |
            EditorGeometryProcessingDomain::MeshEdges |
            EditorGeometryProcessingDomain::MeshHalfedges |
            EditorGeometryProcessingDomain::MeshFaces;

        [[nodiscard]] constexpr bool IsSurfaceTopologyAlgorithm(
            const EditorGeometryProcessingAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
            case EditorGeometryProcessingAlgorithm::MeshDenoise:
            case EditorGeometryProcessingAlgorithm::Curvature:
            case EditorGeometryProcessingAlgorithm::Remeshing:
            case EditorGeometryProcessingAlgorithm::Simplification:
            case EditorGeometryProcessingAlgorithm::Smoothing:
            case EditorGeometryProcessingAlgorithm::Subdivision:
            case EditorGeometryProcessingAlgorithm::Repair:
                return true;
            case EditorGeometryProcessingAlgorithm::KMeans:
            case EditorGeometryProcessingAlgorithm::NormalEstimation:
            case EditorGeometryProcessingAlgorithm::ShortestPath:
            case EditorGeometryProcessingAlgorithm::ConvexHull:
            case EditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            case EditorGeometryProcessingAlgorithm::VectorHeat:
            case EditorGeometryProcessingAlgorithm::Parameterization:
            case EditorGeometryProcessingAlgorithm::BooleanCSG:
            case EditorGeometryProcessingAlgorithm::Registration:
            case EditorGeometryProcessingAlgorithm::BilateralFilter:
            case EditorGeometryProcessingAlgorithm::OutlierEstimation:
            case EditorGeometryProcessingAlgorithm::KernelDensity:
            case EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            case EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            case EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
                return false;
            }
            return false;
        }

        [[nodiscard]] EditorGeometryProcessingDomain
        DomainsForSourceView(const GS::ConstSourceView& view) noexcept
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            EditorGeometryProcessingDomain domains =
                EditorGeometryProcessingDomain::None;

            if (availability.Sources.ProvenanceDomain == GS::Domain::Mesh)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshVertex))
                    domains |= EditorGeometryProcessingDomain::MeshVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshEdge))
                    domains |= EditorGeometryProcessingDomain::MeshEdges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshHalfedge))
                    domains |= EditorGeometryProcessingDomain::MeshHalfedges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshFace))
                    domains |= EditorGeometryProcessingDomain::MeshFaces;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::Graph)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphNode))
                    domains |= EditorGeometryProcessingDomain::GraphVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphEdge))
                    domains |= EditorGeometryProcessingDomain::GraphEdges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphHalfedge))
                    domains |= EditorGeometryProcessingDomain::GraphHalfedges;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::PointCloudPoint))
                    domains |= EditorGeometryProcessingDomain::PointCloudPoints;
            }

            return domains;
        }
        enum class MeshFaceRingStatus : std::uint8_t
        {
            Triangulate,
            Skip,
            Invalid,
        };

        [[nodiscard]] MeshFaceRingStatus BuildMeshFaceRing(
            const std::vector<std::uint32_t>& faceHalfedges,
            const std::vector<std::uint32_t>& halfedgeFaces,
            const std::vector<std::uint32_t>& nextHalfedges,
            const std::vector<std::uint32_t>& toVertices,
            const std::size_t faceIndex,
            const std::uint32_t vertexCount,
            std::vector<std::uint32_t>& outRing)
        {
            constexpr std::uint32_t invalid = std::numeric_limits<std::uint32_t>::max();
            outRing.clear();
            if (faceIndex >= faceHalfedges.size())
                return MeshFaceRingStatus::Invalid;

            const std::size_t halfedgeCount = toVertices.size();
            const std::uint32_t first = faceHalfedges[faceIndex];
            if (first == invalid)
                return MeshFaceRingStatus::Skip;
            if (first >= halfedgeCount)
                return MeshFaceRingStatus::Invalid;

            const std::uint32_t owner = halfedgeFaces[first];
            if (owner == invalid || owner >= faceHalfedges.size())
                return MeshFaceRingStatus::Skip;
            if (owner != static_cast<std::uint32_t>(faceIndex))
                return MeshFaceRingStatus::Skip;

            std::uint32_t halfedge = first;
            for (std::size_t step = 0u; step <= halfedgeCount; ++step)
            {
                if (halfedge >= halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                if (halfedgeFaces[halfedge] != static_cast<std::uint32_t>(faceIndex))
                    return MeshFaceRingStatus::Invalid;

                const std::uint32_t vertex = toVertices[halfedge];
                if (vertex >= vertexCount)
                    return MeshFaceRingStatus::Invalid;
                outRing.push_back(vertex);

                const std::uint32_t next = nextHalfedges[halfedge];
                if (next == first)
                    break;
                if (next == invalid || step == halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                halfedge = next;
            }

            return outRing.size() >= 3u
                ? MeshFaceRingStatus::Triangulate
                : MeshFaceRingStatus::Skip;
        }

        struct MeshSoupFromGeometrySourcesResult
        {
            Geometry::MeshSoup::IndexedMesh Mesh{};
            std::vector<std::uint32_t> SourceFaceForSoupFace{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshSoupFromGeometrySourcesResult BuildMeshSoupFromGeometrySources(
            const GS::ConstSourceView& view)
        {
            MeshSoupFromGeometrySourcesResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status = EditorCommandStatus::UnsupportedGeometryDomain;
                result.Diagnostic = "UV regeneration requires selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has no vertex position property";
                return result;
            }
            if (positions.Vector().size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has too many vertices for UV regeneration";
                return result;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces || !faceHalfedges ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().empty())
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has invalid halfedge/face topology";
                return result;
            }

            for (const glm::vec3 position : positions.Vector())
                (void)result.Mesh.AddVertex(position);

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid)
                {
                    result.Status = EditorCommandStatus::InvalidProcessingParameters;
                    result.Diagnostic = "selected mesh topology is not valid for UV regeneration";
                    return result;
                }
                if (status == MeshFaceRingStatus::Skip)
                    continue;

                for (std::size_t i = 1u; i + 1u < ring.size(); ++i)
                {
                    (void)result.Mesh.AddTriangle(ring[0u], ring[i], ring[i + 1u]);
                    result.SourceFaceForSoupFace.push_back(
                        static_cast<std::uint32_t>(faceIndex));
                }
            }

            if (result.Mesh.FaceCount() == 0u)
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has no valid surface faces";
                return result;
            }

            result.Status = EditorCommandStatus::Applied;
            return result;
        }

        template <typename T>
        void CopyTypedPropertyByXref(
            const Geometry::ConstPropertySet& source,
            const std::string& name,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            const auto sourceProperty = source.Get<T>(name);
            if (!sourceProperty)
                return;

            auto targetProperty = target.GetOrAdd<T>(name, T{});
            if (!targetProperty)
                return;
            for (std::size_t outputIndex = 0u; outputIndex < xrefs.size(); ++outputIndex)
            {
                const std::uint32_t sourceIndex = xrefs[outputIndex];
                if (sourceIndex < sourceProperty.Vector().size())
                    targetProperty[outputIndex] = sourceProperty[sourceIndex];
            }
        }

        void CopyKnownPropertiesByXref(
            const Geometry::ConstPropertySet& source,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            for (const std::string& name : source.Properties())
            {
                CopyTypedPropertyByXref<float>(source, name, xrefs, target);
                CopyTypedPropertyByXref<double>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::uint32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::uint64_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::int32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<bool>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec2>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec3>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec4>(source, name, xrefs, target);
            }
        }

        template <typename T>
        [[nodiscard]] bool SameKnownPropertyValue(
            const T& lhs,
            const T& rhs) noexcept
        {
            return lhs == rhs;
        }

        template <>
        [[nodiscard]] bool SameKnownPropertyValue<float>(
            const float& lhs,
            const float& rhs) noexcept
        {
            return std::bit_cast<std::uint32_t>(lhs) ==
                   std::bit_cast<std::uint32_t>(rhs);
        }

        template <>
        [[nodiscard]] bool SameKnownPropertyValue<double>(
            const double& lhs,
            const double& rhs) noexcept
        {
            return std::bit_cast<std::uint64_t>(lhs) ==
                   std::bit_cast<std::uint64_t>(rhs);
        }

        template <>
        [[nodiscard]] bool SameKnownPropertyValue<glm::vec2>(
            const glm::vec2& lhs,
            const glm::vec2& rhs) noexcept
        {
            return SameKnownPropertyValue(lhs.x, rhs.x) &&
                   SameKnownPropertyValue(lhs.y, rhs.y);
        }

        template <>
        [[nodiscard]] bool SameKnownPropertyValue<glm::vec3>(
            const glm::vec3& lhs,
            const glm::vec3& rhs) noexcept
        {
            return SameKnownPropertyValue(lhs.x, rhs.x) &&
                   SameKnownPropertyValue(lhs.y, rhs.y) &&
                   SameKnownPropertyValue(lhs.z, rhs.z);
        }

        template <>
        [[nodiscard]] bool SameKnownPropertyValue<glm::vec4>(
            const glm::vec4& lhs,
            const glm::vec4& rhs) noexcept
        {
            return SameKnownPropertyValue(lhs.x, rhs.x) &&
                   SameKnownPropertyValue(lhs.y, rhs.y) &&
                   SameKnownPropertyValue(lhs.z, rhs.z) &&
                   SameKnownPropertyValue(lhs.w, rhs.w);
        }

        template <typename T>
        [[nodiscard]] std::optional<bool> SameTypedPropertyValues(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected,
            const std::string_view name) noexcept
        {
            const auto expectedProperty = expected.Get<T>(name);
            if (!expectedProperty)
                return std::nullopt;

            const auto currentProperty = current.Get<T>(name);
            if (!currentProperty)
                return std::nullopt;
            if (currentProperty.Vector().size() !=
                expectedProperty.Vector().size())
            {
                return false;
            }
            for (std::size_t i = 0u;
                 i < expectedProperty.Vector().size();
                 ++i)
            {
                const T currentValue = currentProperty[i];
                const T expectedValue = expectedProperty[i];
                if (!SameKnownPropertyValue(
                        currentValue,
                        expectedValue))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SameKnownPropertyValues(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected) noexcept
        {
            for (const std::string& name : expected.Properties())
            {
                const auto matches =
                    [&]<typename T>()
                    {
                        const std::optional<bool> same =
                            SameTypedPropertyValues<T>(
                                current,
                                expected,
                                name);
                        return !same.has_value() || *same;
                    };
                if (!matches.template operator()<float>() ||
                    !matches.template operator()<double>() ||
                    !matches.template operator()<std::uint32_t>() ||
                    !matches.template operator()<std::uint64_t>() ||
                    !matches.template operator()<std::int32_t>() ||
                    !matches.template operator()<bool>() ||
                    !matches.template operator()<glm::vec2>() ||
                    !matches.template operator()<glm::vec3>() ||
                    !matches.template operator()<glm::vec4>())
                {
                    return false;
                }
            }
            return true;
        }

        // BUG-147 — publish regenerated UVs onto a mesh that kept its own
        // topology, on whichever domain can represent them.
        //
        // A chart split needs two or more UVs at a seam vertex, which a
        // per-vertex property cannot hold; those go on the corner domain. An
        // atlas that needed no seam stays on the vertex domain, so a mesh is
        // not promoted to corner UVs for nothing. Either way exactly one UV
        // authority survives, because the resolution order is corner-over-vertex
        // and a leftover disagreeing property would silently win or lose.
        //
        // Returns false only when the atlas cross-references do not line up
        // with the source mesh, which would otherwise publish UVs mapped to the
        // wrong corners.
        [[nodiscard]] bool PublishUvRegenerationTexcoords(
            const Geometry::UvAtlas::UvAtlasResult& atlas,
            const MeshSoupFromGeometrySourcesResult& soup,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            const auto removeProperty =
                [](Geometry::PropertySet& properties,
                   const std::string_view name)
            {
                if (const auto id = properties.Registry().Find(name))
                    (void)properties.Registry().Remove(*id);
            };

            const auto outputUvs =
                atlas.OutputMesh.VertexProperties().Get<glm::vec2>("v:texcoord");
            if (!outputUvs)
                return false;

            MeshCornerTexcoords corners{};
            if (!GatherSplitMeshCornerTexcoords(
                    atlas.OutputMesh,
                    outputUvs.Vector(),
                    atlas.SourceFaceForOutputFace,
                    atlas.SourceVertexForOutputVertex,
                    soup.Mesh.Faces(),
                    soup.Mesh.VertexCount(),
                    corners))
            {
                return false;
            }

            if (corners.HasSeam &&
                PublishMeshCornerTexcoords(
                    mesh,
                    soup.Mesh.Faces(),
                    soup.Mesh.VertexCount(),
                    corners.CornerUvs))
            {
                removeProperty(mesh.VertexProperties(), "v:texcoord");
                return true;
            }

            // No seam, or the corner publication could not map: the per-vertex
            // representatives are the exact answer in the first case and the
            // best available one in the second.
            if (corners.VertexUvs.size() != mesh.VerticesSize())
                return false;
            removeProperty(
                mesh.HalfedgeProperties(),
                Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
            auto target = mesh.VertexProperties().GetOrAdd<glm::vec2>(
                "v:texcoord",
                glm::vec2{0.0f});
            if (!target || target.Vector().size() != corners.VertexUvs.size())
                return false;
            target.Vector() = corners.VertexUvs;
            return true;
        }

        void CopyUvSourcePropertiesToHalfedgeMesh(
            const Geometry::ConstPropertySet& sourceVertexProperties,
            const bool hasSourceVertexProperties,
            const Geometry::ConstPropertySet& sourceFaceProperties,
            const bool hasSourceFaceProperties,
            const MeshSoupFromGeometrySourcesResult& soup,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (hasSourceVertexProperties)
            {
                std::vector<std::uint32_t> sourceVertexForMeshVertex;
                sourceVertexForMeshVertex.reserve(mesh.VerticesSize());
                for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
                {
                    sourceVertexForMeshVertex.push_back(
                        static_cast<std::uint32_t>(i));
                }
                CopyKnownPropertiesByXref(
                    sourceVertexProperties,
                    sourceVertexForMeshVertex,
                    mesh.VertexProperties());
            }

            if (hasSourceFaceProperties &&
                soup.SourceFaceForSoupFace.size() == mesh.FacesSize())
            {
                CopyKnownPropertiesByXref(
                    sourceFaceProperties,
                    soup.SourceFaceForSoupFace,
                    mesh.FaceProperties());
            }
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

        void InvalidateSelectedModelCache(const EditorGeometryProcessingContext& context)
        {
            if (context.InvalidateWorkspaceSnapshotCache)
                context.InvalidateWorkspaceSnapshotCache();
        }

        void MixSignatureByte(std::uint64_t& signature,
                              const std::uint8_t value) noexcept
        {
            signature ^= value;
            signature *= kEditorSignaturePrime;
        }

        void MixSignature(std::uint64_t& signature,
                          std::uint64_t value) noexcept
        {
            for (std::uint32_t i = 0u; i < 8u; ++i)
            {
                MixSignatureByte(
                    signature,
                    static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu));
            }
        }

        void MixSignatureString(std::uint64_t& signature,
                                const std::string_view value) noexcept
        {
            MixSignature(signature, static_cast<std::uint64_t>(value.size()));
            for (const char c : value)
            {
                MixSignatureByte(signature, static_cast<std::uint8_t>(c));
            }
        }

        void AppendPropertySetMetadataSignature(
            std::uint64_t& signature,
            const std::uint64_t domainTag,
            const Geometry::PropertySet* properties,
            const std::size_t deletedCount)
        {
            MixSignature(signature, domainTag);
            if (properties == nullptr)
            {
                MixSignature(signature, 0u);
                return;
            }

            MixSignature(signature, static_cast<std::uint64_t>(properties->Size()));
            MixSignature(signature, static_cast<std::uint64_t>(deletedCount));
            const std::vector<Geometry::PropertyDescriptor> descriptors =
                properties->Registry().Descriptors(false);
            MixSignature(signature, static_cast<std::uint64_t>(descriptors.size()));
            std::uint64_t order = 0u;
            for (const Geometry::PropertyDescriptor& descriptor : descriptors)
            {
                MixSignature(signature, order++);
                MixSignatureString(signature, descriptor.Name);
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 descriptor.ValueKind));
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 descriptor.ElementCount));
                MixSignature(signature,
                             descriptor.SupportsContiguousSpan ? 1u : 0u);
                MixSignature(signature, descriptor.SupportsRawData ? 1u : 0u);
            }
        }

        // BUG-138: a topology edit's apply gate has to answer "do the stored
        // GeometrySources still hold the mesh this job was computed from", and
        // it has to read the same data on both sides of that question. The gate
        // used to compare the stored halfedge arrays against the job's
        // `BeforeMesh`, which is those sources re-derived through a triangle
        // soup. A soup round-trip reproduces the numbering an import published,
        // but not the numbering a previous topology edit published, so the
        // first simplify/subdivide/remesh on an entity applied and every later
        // one was refused as stale forever. Hashing the stored arrays
        // themselves is representation-independent and still detects every real
        // change. Positions are covered separately by the snapshot comparison.
        [[nodiscard]] bool AppendTopologyValueSignature(
            std::uint64_t& signature,
            const Geometry::PropertySet* properties,
            const std::size_t deletedCount,
            const std::uint64_t domainTag,
            const std::span<const std::string_view> propertyNames)
        {
            MixSignature(signature, domainTag);
            if (properties == nullptr)
                return false;

            MixSignature(signature,
                         static_cast<std::uint64_t>(properties->Size()));
            MixSignature(signature,
                         static_cast<std::uint64_t>(deletedCount));
            for (const std::string_view name : propertyNames)
            {
                const auto values = properties->Get<std::uint32_t>(name);
                if (!values)
                    return false;
                MixSignatureString(signature, name);
                MixSignature(
                    signature,
                    static_cast<std::uint64_t>(values.Vector().size()));
                for (const std::uint32_t value : values.Vector())
                    MixSignature(signature, static_cast<std::uint64_t>(value));
            }
            return true;
        }

        // `std::nullopt` means the stored topology could not be read at all, so
        // no two readings may be treated as equal.
        [[nodiscard]] std::optional<std::uint64_t> MeshTopologyValueSignature(
            const GS::ConstSourceView& view)
        {
            static constexpr std::array<std::string_view, 2> kEdgeNames{
                GS::PropertyNames::kEdgeV0,
                GS::PropertyNames::kEdgeV1,
            };
            static constexpr std::array<std::string_view, 3> kHalfedgeNames{
                GS::PropertyNames::kHalfedgeToVertex,
                GS::PropertyNames::kHalfedgeNext,
                GS::PropertyNames::kHalfedgeFace,
            };
            static constexpr std::array<std::string_view, 1> kFaceNames{
                GS::PropertyNames::kFaceHalfedge,
            };

            std::uint64_t signature = kEditorSignatureOffset;
            if (!AppendTopologyValueSignature(
                    signature,
                    view.EdgeSource != nullptr ? &view.EdgeSource->Properties
                                               : nullptr,
                    view.EdgeSource != nullptr ? view.EdgeSource->NumDeleted
                                               : 0u,
                    1u,
                    kEdgeNames) ||
                !AppendTopologyValueSignature(
                    signature,
                    view.HalfedgeSource != nullptr
                        ? &view.HalfedgeSource->Properties
                        : nullptr,
                    0u,
                    2u,
                    kHalfedgeNames) ||
                !AppendTopologyValueSignature(
                    signature,
                    view.FaceSource != nullptr ? &view.FaceSource->Properties
                                               : nullptr,
                    view.FaceSource != nullptr ? view.FaceSource->NumDeleted
                                               : 0u,
                    3u,
                    kFaceNames))
            {
                return std::nullopt;
            }
            return signature;
        }

        [[nodiscard]] std::optional<std::uint64_t>
        StoredMeshTopologySignatureForEntity(
            entt::registry& raw,
            const std::uint32_t stableEntityId)
        {
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return std::nullopt;
            return MeshTopologyValueSignature(
                GS::BuildConstView(raw, *entity));
        }

        [[nodiscard]] std::uint64_t GeometryMetadataSignatureForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            std::uint64_t signature = kEditorSignatureOffset;
            MixSignature(signature,
                         static_cast<std::uint64_t>(view.ActiveDomain));
            MixSignature(signature, view.HasMeshTopologyMarker ? 1u : 0u);
            MixSignature(signature, view.HasGraphTopologyMarker ? 1u : 0u);
            AppendPropertySetMetadataSignature(
                signature,
                1u,
                view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr,
                view.VertexSource != nullptr ? view.VertexSource->NumDeleted
                                             : 0u);
            AppendPropertySetMetadataSignature(
                signature,
                2u,
                view.EdgeSource != nullptr ? &view.EdgeSource->Properties
                                           : nullptr,
                view.EdgeSource != nullptr ? view.EdgeSource->NumDeleted
                                           : 0u);
            AppendPropertySetMetadataSignature(
                signature,
                3u,
                view.HalfedgeSource != nullptr
                    ? &view.HalfedgeSource->Properties
                    : nullptr,
                0u);
            AppendPropertySetMetadataSignature(
                signature,
                4u,
                view.FaceSource != nullptr ? &view.FaceSource->Properties
                                           : nullptr,
                view.FaceSource != nullptr ? view.FaceSource->NumDeleted
                                           : 0u);
            return signature;
        }
        [[nodiscard]] bool IsFiniteGeometryPosition(
            const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>>
        CollectFiniteGeometryPositions(
            const Geometry::PropertySet& properties)
        {
            const auto positions =
                properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
                return std::nullopt;
            if (positions.Vector().size() != properties.Size())
                return std::nullopt;

            std::vector<glm::vec3> points{};
            points.reserve(positions.Vector().size());
            for (const glm::vec3& position : positions.Vector())
            {
                if (!IsFiniteGeometryPosition(position))
                    return std::nullopt;
                points.push_back(position);
            }
            return points;
        }

        // BUG-096: point-to-plane ICP needs the target's normals, and both
        // runtime branches used to hand the solver an empty span, which
        // `Geometry.Registration` silently treats as "run point-to-point". The
        // status distinguishes every rejection cause so the editor can say
        // which one applies rather than reporting a generic failure.
        enum class RegistrationNormalStatus : std::uint8_t
        {
            Ok,
            Absent,
            CountMismatch,
            NonFinite,
            ZeroLength,
            TargetTransformNotInvertible,
        };

        [[nodiscard]] const char* DescribeRegistrationNormalStatus(
            const RegistrationNormalStatus status) noexcept
        {
            switch (status)
            {
            case RegistrationNormalStatus::Ok:
                return "";
            case RegistrationNormalStatus::Absent:
                return "the target point cloud has no vec3 v:normal property";
            case RegistrationNormalStatus::CountMismatch:
                return "the target v:normal property does not carry exactly "
                       "one vector per target point";
            case RegistrationNormalStatus::NonFinite:
                return "the target v:normal property contains a non-finite "
                       "value";
            case RegistrationNormalStatus::ZeroLength:
                return "the target v:normal property contains a zero-length "
                       "vector";
            case RegistrationNormalStatus::TargetTransformNotInvertible:
                return "the target entity transform is not invertible, so "
                       "normals cannot be carried into world space";
            }
            return "the target normals are unusable";
        }

        // Local-space read and validation. World-space conversion is separate
        // because the queued path snapshots local normals at submit and
        // converts them in the worker, exactly as it does for positions.
        [[nodiscard]] RegistrationNormalStatus CollectTargetRegistrationNormals(
            const Geometry::PropertySet& properties,
            std::vector<glm::vec3>& out)
        {
            out.clear();
            const auto normals =
                properties.Get<glm::vec3>(GS::PropertyNames::kNormal);
            if (!normals || normals.Vector().empty())
                return RegistrationNormalStatus::Absent;
            if (normals.Vector().size() != properties.Size())
                return RegistrationNormalStatus::CountMismatch;

            out.reserve(normals.Vector().size());
            for (const glm::vec3& normal : normals.Vector())
            {
                if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
                    !std::isfinite(normal.z))
                {
                    out.clear();
                    return RegistrationNormalStatus::NonFinite;
                }
                if (glm::dot(normal, normal) <= 0.0f)
                {
                    out.clear();
                    return RegistrationNormalStatus::ZeroLength;
                }
                out.push_back(normal);
            }
            return RegistrationNormalStatus::Ok;
        }

        // Normals transform by the inverse transpose of the model's linear
        // part, not by the model matrix itself: under non-uniform scale the
        // two disagree, and using the position transform would tilt every
        // normal off the surface it describes.
        [[nodiscard]] RegistrationNormalStatus TransformRegistrationNormalsToWorld(
            const std::vector<glm::vec3>& localNormals,
            const glm::mat4& model,
            std::vector<glm::vec3>& out)
        {
            out.clear();
            const glm::mat3 linear{model};
            const float determinant = glm::determinant(linear);
            if (!std::isfinite(determinant) || determinant == 0.0f)
                return RegistrationNormalStatus::TargetTransformNotInvertible;

            const glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(linear));
            out.reserve(localNormals.size());
            for (const glm::vec3& local : localNormals)
            {
                const glm::vec3 world = normalMatrix * local;
                const float lengthSquared = glm::dot(world, world);
                if (!std::isfinite(lengthSquared))
                {
                    out.clear();
                    return RegistrationNormalStatus::NonFinite;
                }
                if (lengthSquared <= 0.0f)
                {
                    out.clear();
                    return RegistrationNormalStatus::ZeroLength;
                }
                const glm::vec3 normalized = world / std::sqrt(lengthSquared);
                if (!std::isfinite(normalized.x) ||
                    !std::isfinite(normalized.y) ||
                    !std::isfinite(normalized.z))
                {
                    out.clear();
                    return RegistrationNormalStatus::NonFinite;
                }
                out.push_back(normalized);
            }
            return RegistrationNormalStatus::Ok;
        }

        [[nodiscard]] std::string BuildRegistrationNormalRejectionMessage(
            const RegistrationNormalStatus status)
        {
            std::string message =
                "Point-to-plane ICP registration requires target normals: ";
            message += DescribeRegistrationNormalStatus(status);
            message += ". Estimate point-cloud normals on the target, or "
                       "select the point-to-point variant.";
            return message;
        }

        [[nodiscard]] bool SameGeometryPositions(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (lhs[i].x != rhs[i].x ||
                    lhs[i].y != rhs[i].y ||
                    lhs[i].z != rhs[i].z)
                {
                    return false;
                }
            }
            return true;
        }

        // The guard refuses a duplicate submission when the same entity+output
        // already has a non-terminal `JobService` job. Identity stays with the
        // editor session and is resolved through its active-output query.
        [[nodiscard]] std::optional<EditorJobRecord>
        FindActiveEditorJob(
            const EditorGeometryProcessingContext& context,
            const EditorJobIdentity& identity)
        {
            if (!context.JobCommands.FindActive)
                return std::nullopt;
            return context.JobCommands.FindActive(identity);
        }

        void AppendDerivedJobHandleToMessage(
            std::string& message,
            const JobToken handle)
        {
            if (!handle.IsValid())
                return;

            message += " (job ";
            message += std::to_string(handle.Index);
            message += ":";
            message += std::to_string(handle.Generation);
            message += ")";
        }

        [[nodiscard]] std::string BuildActiveDerivedJobMessage(
            const std::string_view label,
            const EditorJobRecord& job)
        {
            std::string message{label};
            message += " already has an active ";
            message += std::string{ToString(job.State)};
            message += " job";
            AppendDerivedJobHandleToMessage(message, job.Token);
            message += ".";
            return message;
        }

        struct MeshForVertexNormalsResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshForVertexNormalsResult
        BuildHalfedgeMeshForVertexNormalRecompute(const GS::MutableSourceView& view)
        {
            MeshForVertexNormalsResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status = EditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh vertex normals require selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty() ||
                positions.Vector().size() != view.VertexSource->Properties.Size())
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh requires count-matched v:position for normal recompute";
                return result;
            }
            if (positions.Vector().size() >
                static_cast<std::size_t>(
                    std::numeric_limits<Geometry::PropertyIndex>::max()))
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has too many vertices for normal recompute";
                return result;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces ||
                !faceHalfedges ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().size() !=
                    view.FaceSource->Properties.Size())
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has invalid halfedge/face topology for normal recompute";
                return result;
            }

            result.Mesh.Reserve(positions.Vector().size(),
                                view.EdgeSource != nullptr
                                    ? view.EdgeSource->Properties.Size()
                                    : 0u,
                                faceHalfedges.Vector().size());
            for (const glm::vec3 position : positions.Vector())
                (void)result.Mesh.AddVertex(position);

            std::vector<std::uint32_t> ring{};
            ring.reserve(8u);
            std::vector<Geometry::VertexHandle> faceVertices{};
            faceVertices.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid)
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "selected mesh topology is not valid for normal recompute";
                    return result;
                }
                if (status == MeshFaceRingStatus::Skip)
                    continue;

                faceVertices.clear();
                for (const std::uint32_t vertex : ring)
                {
                    faceVertices.push_back(
                        Geometry::VertexHandle{
                            static_cast<Geometry::PropertyIndex>(vertex)});
                }
                if (!result.Mesh.AddFace(faceVertices).has_value())
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic = "selected mesh face ring could not be reconstructed "
                                        "for normal recompute";
                    return result;
                }
            }

            result.Status = EditorCommandStatus::Applied;
            return result;
        }

        [[nodiscard]] EditorMeshVertexNormalsResult MakeMeshNormalsResult(
            const EditorCommandStatus status,
            const GN::RecomputeStatus normalStatus,
            const GN::AveragingMode weighting,
            const Core::ErrorCode error,
            std::string message)
        {
            return EditorMeshVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .Weighting = weighting,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyMeshNormalCounters(const GN::Result& source,
                                    EditorMeshVertexNormalsResult& target)
        {
            target.NormalStatus = source.Status;
            target.Weighting = source.Weighting;
            target.VertexSlotCount = source.VertexSlotCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalVertexCount = source.ValidNormalVertexCount;
            target.ProcessedFaceCount = source.ProcessedFaceCount;
            target.DegenerateFaceCount = source.DegenerateFaceCount;
            target.NonFiniteFaceCount = source.NonFiniteFaceCount;
            target.InvalidTopologyFaceCount = source.InvalidTopologyFaceCount;
            target.DegenerateCornerCount = source.DegenerateCornerCount;
            target.FallbackVertexCount = source.FallbackVertexCount;
            target.SkippedDeletedFaceCount = source.SkippedDeletedFaceCount;
            target.SkippedDeletedVertexCount = source.SkippedDeletedVertexCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] bool PublishCanonicalVec3Normals(
            Geometry::PropertySet& properties,
            const std::vector<glm::vec3>& normals)
        {
            if (normals.empty() || normals.size() != properties.Size())
                return false;
            if (properties.Exists(GS::PropertyNames::kNormal) &&
                !properties.Get<glm::vec3>(GS::PropertyNames::kNormal))
            {
                return false;
            }

            auto target = properties.GetOrAdd<glm::vec3>(
                std::string{GS::PropertyNames::kNormal},
                glm::vec3{0.0f, 0.0f, 1.0f});
            if (!target || target.Vector().size() != normals.size())
                return false;

            target.Vector() = normals;
            return true;
        }

        // BUG-145: how many published values differ from the ones already
        // stored. A property the previous run did not have counts as changed in
        // every slot, because every value is newly authored.
        template <typename T>
        [[nodiscard]] std::size_t CountChangedValues(
            const bool hadProperty,
            const std::vector<T>& before,
            const std::vector<T>& after) noexcept
        {
            if (!hadProperty || before.size() != after.size())
                return after.size();

            std::size_t changed = 0u;
            for (std::size_t i = 0u; i < after.size(); ++i)
            {
                if (after[i] != before[i])
                    ++changed;
            }
            return changed;
        }

        // BUG-145: `WrittenCount` is non-zero whenever the kernel ran at all,
        // so it cannot tell a recompute that changed something from one that
        // republished the values already stored. Compare exactly: these paths
        // write the kernel's own output back into the same `v:normal` storage
        // they read, so an unchanged normal is bit-identical, and an epsilon
        // would invent a tolerance the undo snapshot does not share. An absent
        // property is a change in every slot, because every value is new.
        [[nodiscard]] std::size_t CountChangedVertexNormals(
            const bool hadNormalProperty,
            const std::vector<glm::vec3>& before,
            const std::vector<glm::vec3>& after) noexcept
        {
            return CountChangedValues(hadNormalProperty, before, after);
        }

        [[nodiscard]] std::string BuildVertexNormalsNoChangeMessage(
            const char* const domain,
            const std::size_t writtenCount)
        {
            std::string message = domain;
            message += " vertex normals are already up to date (";
            message += std::to_string(writtenCount);
            message += " recomputed, 0 changed). Nothing was published and no "
                       "undo entry was created.";
            return message;
        }

        [[nodiscard]] std::string BuildMeshNormalsSuccessMessage(
            const EditorMeshVertexNormalsResult& result)
        {
            std::string message = "Mesh vertex normals recomputed (weighting=";
            message += std::string(GN::DebugName(result.Weighting));
            message += ", written=";
            message += std::to_string(result.WrittenCount);
            message += ", changed=";
            message += std::to_string(result.ChangedNormalCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackVertexCount);
            message += ").";
            return message;
        }

        [[nodiscard]] bool IsPositiveFinite(const double value) noexcept
        {
            return std::isfinite(value) && value > 0.0;
        }

        [[nodiscard]] EditorGraphVertexNormalsResult MakeGraphNormalsResult(
            const EditorCommandStatus status,
            const GraphNormals::RecomputeStatus normalStatus,
            const bool orientTowardFallback,
            const Core::ErrorCode error,
            std::string message)
        {
            return EditorGraphVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .OrientTowardFallback = orientTowardFallback,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyGraphNormalCounters(
            const GraphNormals::Diagnostics& source,
            EditorGraphVertexNormalsResult& target)
        {
            target.VertexSlotCount = source.VertexSlotCount;
            target.EdgeSlotCount = source.EdgeSlotCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalVertexCount = source.ValidNormalVertexCount;
            target.FallbackVertexCount = source.FallbackVertexCount;
            target.IsolatedVertexCount = source.IsolatedVertexCount;
            target.DegreeOneVertexCount = source.DegreeOneVertexCount;
            target.CollinearNeighborhoodCount =
                source.CollinearNeighborhoodCount;
            target.DuplicatePositionCount = source.DuplicatePositionCount;
            target.NonFinitePositionCount = source.NonFinitePositionCount;
            target.InvalidEdgeCount = source.InvalidEdgeCount;
            target.SkippedDeletedVertexCount =
                source.SkippedDeletedVertexCount;
            target.SkippedDeletedEdgeCount = source.SkippedDeletedEdgeCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] Core::ErrorCode ErrorForGraphNormalStatus(
            const GraphNormals::RecomputeStatus status) noexcept
        {
            using Status = GraphNormals::RecomputeStatus;
            switch (status)
            {
            case Status::Success:
                return Core::ErrorCode::Success;
            case Status::PropertyTypeConflict:
                return Core::ErrorCode::TypeMismatch;
            case Status::EmptyGraph:
            case Status::InvalidPositionProperty:
            case Status::InvalidTopologyProperty:
            case Status::InvalidOutputProperty:
            case Status::CountMismatch:
                return Core::ErrorCode::InvalidArgument;
            }
            return Core::ErrorCode::Unknown;
        }

        struct GraphForVertexNormalsResult
        {
            Geometry::Halfedges Halfedges{};
            std::size_t EdgeSlotCount{0};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] GraphForVertexNormalsResult
        BuildGraphConnectivityForVertexNormalRecompute(
            const GS::MutableSourceView& view)
        {
            GraphForVertexNormalsResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Graph ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.EdgeSource == nullptr)
            {
                result.Status =
                    EditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Graph vertex normals require selected graph GeometrySources.";
                return result;
            }

            const std::size_t edgeSlotCount =
                view.EdgeSource->Properties.Size();
            const auto connectivity =
                view.HalfedgeSource->Properties.Get<
                    Geometry::Graph::HalfedgeConnectivity>(
                    GS::PropertyNames::kHalfedgeConnectivity);
            if (!connectivity ||
                view.HalfedgeSource->Properties.Size() != edgeSlotCount * 2u ||
                connectivity.Vector().size() != edgeSlotCount * 2u)
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic = "selected graph requires count-matched real halfedge "
                                    "connectivity for normal recompute";
                return result;
            }

            result.Halfedges = view.HalfedgeSource->Properties;
            result.EdgeSlotCount = edgeSlotCount;
            result.Status = EditorCommandStatus::Applied;
            return result;
        }

        [[nodiscard]] std::string BuildGraphNormalsSuccessMessage(
            const EditorGraphVertexNormalsResult& result)
        {
            std::string message = "Graph vertex normals recomputed (written=";
            message += std::to_string(result.WrittenCount);
            message += ", changed=";
            message += std::to_string(result.ChangedNormalCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackVertexCount);
            message += ", invalidEdges=";
            message += std::to_string(result.InvalidEdgeCount);
            message += ").";
            return message;
        }

        [[nodiscard]] EditorPointCloudVertexNormalsResult
        MakePointCloudNormalsResult(
            const EditorCommandStatus status,
            const PointNormals::RecomputeStatus normalStatus,
            const EditorPointCloudVertexNormalsCommand& command,
            const Core::ErrorCode error,
            std::string message)
        {
            return EditorPointCloudVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .Orientation = command.Orientation,
                .KNeighbors = command.KNeighbors,
                .MinimumNeighbors = command.MinimumNeighbors,
                .UseRadiusSearch = command.UseRadiusSearch,
                .Radius = command.Radius,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyPointCloudNormalCounters(
            const PointNormals::Diagnostics& source,
            EditorPointCloudVertexNormalsResult& target)
        {
            target.PointSlotCount = source.PointSlotCount;
            target.FinitePointCount = source.FinitePointCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalPointCount = source.ValidNormalPointCount;
            target.FallbackPointCount = source.FallbackPointCount;
            target.DegenerateNeighborhoodCount =
                source.DegenerateNeighborhoodCount;
            target.TooFewNeighborCount = source.TooFewNeighborCount;
            target.CollinearNeighborhoodCount =
                source.CollinearNeighborhoodCount;
            target.DuplicatePositionCount = source.DuplicatePositionCount;
            target.NonFinitePointCount = source.NonFinitePointCount;
            target.SkippedDeletedPointCount =
                source.SkippedDeletedPointCount;
            target.SpatialQueryFailureCount =
                source.SpatialQueryFailureCount;
            target.FlippedOrientationCount = source.FlippedOrientationCount;
            target.KNNVisitedNodeCount = source.KNNVisitedNodeCount;
            target.KNNDistanceEvaluationCount =
                source.KNNDistanceEvaluationCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] Core::ErrorCode ErrorForPointCloudNormalStatus(
            const PointNormals::RecomputeStatus status) noexcept
        {
            using Status = PointNormals::RecomputeStatus;
            switch (status)
            {
            case Status::Success:
                return Core::ErrorCode::Success;
            case Status::PropertyTypeConflict:
                return Core::ErrorCode::TypeMismatch;
            case Status::EmptyInput:
            case Status::TooFewFinitePoints:
            case Status::InvalidPositionProperty:
            case Status::InvalidOutputProperty:
            case Status::CountMismatch:
                return Core::ErrorCode::InvalidArgument;
            case Status::SpatialIndexBuildFailed:
            case Status::SpatialIndexQueryFailed:
                return Core::ErrorCode::Unknown;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] std::string BuildPointCloudNormalsSuccessMessage(
            const EditorPointCloudVertexNormalsResult& result)
        {
            std::string message =
                "Point-cloud vertex normals recomputed (backend=";
            message += std::string(PointNormals::DebugName(result.Backend));
            message += ", written=";
            message += std::to_string(result.WrittenCount);
            message += ", changed=";
            message += std::to_string(result.ChangedNormalCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackPointCount);
            message += ").";
            return message;
        }

        [[nodiscard]] Core::ErrorCode ErrorForDenoiseStatus(
            const Smooth::DenoiseStatus status) noexcept
        {
            switch (status)
            {
            case Smooth::DenoiseStatus::Success:
                return Core::ErrorCode::Success;
            case Smooth::DenoiseStatus::EmptyMesh:
                return Core::ErrorCode::ResourceNotFound;
            case Smooth::DenoiseStatus::NonManifoldInput:
            case Smooth::DenoiseStatus::DegenerateGeometry:
            case Smooth::DenoiseStatus::NonFiniteInput:
            case Smooth::DenoiseStatus::InvalidParams:
                return Core::ErrorCode::InvalidArgument;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] bool IsFiniteVec3(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool AllFiniteVec3(
            const std::span<const glm::vec3> values) noexcept
        {
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteVec3(value))
                    return false;
            }
            return true;
        }

        struct MeshDenoiseSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> BeforePositions{};
            std::vector<bool> DeletedVertices{};
            std::vector<std::uint32_t> SourceFaceForMeshFace{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshDenoiseSourceResult BuildHalfedgeMeshForDenoise(
            const GS::ConstSourceView& view)
        {
            MeshDenoiseSourceResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status =
                    EditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise requires selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise requires a non-empty v:position property.";
                return result;
            }

            result.BeforePositions = positions.Vector();
            result.DeletedVertices.assign(result.BeforePositions.size(), false);
            if (const auto deleted =
                    view.VertexSource->Properties.Get<bool>("v:deleted"))
            {
                if (deleted.Vector().size() != result.BeforePositions.size())
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "Mesh denoise requires v:deleted to match v:position when present.";
                    return result;
                }
                for (std::size_t i = 0u; i < deleted.Vector().size(); ++i)
                    result.DeletedVertices[i] = deleted.Vector()[i];
            }

            MeshSoupFromGeometrySourcesResult soup =
                BuildMeshSoupFromGeometrySources(view);
            if (!soup.Succeeded())
            {
                result.Status = soup.Status;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic = soup.Diagnostic.empty()
                                        ? "Mesh denoise could not build a triangle soup "
                                          "from GeometrySources."
                                        : soup.Diagnostic;
                return result;
            }

            auto converted =
                Geometry::Mesh::Conversion::ToHalfedgeMesh(soup.Mesh);
            if (!converted.Succeeded())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic = "Mesh denoise could not convert selected "
                                    "GeometrySources to halfedge topology.";
                return result;
            }
            if (converted.Mesh.VerticesSize() != result.BeforePositions.size())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise conversion changed the vertex slot count.";
                return result;
            }
            if (converted.Mesh.FacesSize() !=
                soup.SourceFaceForSoupFace.size())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise conversion changed the face slot count.";
                return result;
            }

            result.Mesh = std::move(converted.Mesh);
            result.SourceFaceForMeshFace =
                std::move(soup.SourceFaceForSoupFace);
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] std::vector<glm::vec3> ExtractMeshPositions(
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            std::vector<glm::vec3> positions(mesh.VerticesSize());
            for (std::size_t i = 0u; i < positions.size(); ++i)
            {
                positions[i] = mesh.Position(
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(i)});
            }
            return positions;
        }

        using MeshPositionState =
            std::shared_ptr<const std::vector<glm::vec3>>;

        struct MeshPropertyMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        struct MeshDenoiseMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            MeshPositionState Positions{};
        };

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshDenoisePositionState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const std::vector<glm::vec3>& positions)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            auto currentPositions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!currentPositions ||
                currentPositions.Vector().size() != positions.size() ||
                !AllFiniteVec3(std::span<const glm::vec3>{
                    positions.data(),
                    positions.size()}))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            currentPositions.Vector() = positions;
            return EditorCommandHistoryStatus::Applied;
        }

        void StampMeshDenoisePositionDirty(
            ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId)
        {
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandStatus CommitMeshDenoisePositions(
            const EditorGeometryProcessingContext& context,
            const std::uint32_t stableEntityId,
            std::vector<glm::vec3> before,
            std::vector<glm::vec3> after)
        {
            if (context.CommandHistory != nullptr)
            {
                if (context.Scene == nullptr)
                    return EditorCommandStatus::MissingScene;
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableEntityId);
                if (entity == ECS::InvalidEntityHandle ||
                    !context.Scene->Raw().valid(entity))
                {
                    return EditorCommandStatus::StaleEntity;
                }

                const MeshPositionState beforeState =
                    std::make_shared<std::vector<glm::vec3>>(
                        std::move(before));
                const MeshPositionState afterState =
                    std::make_shared<std::vector<glm::vec3>>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        "Denoise mesh vertices",
                        MeshPropertyMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        MeshDenoiseMutationGeneration{
                            .GeometryMetadataSignature =
                                GeometryMetadataSignatureForEntity(
                                    context.Scene->Raw(),
                                    entity),
                            .Positions = beforeState,
                        },
                        beforeState,
                        afterState,
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshDenoiseMutationGeneration& expected,
                            const MeshPositionState&)
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

                            const GS::ConstSourceView view =
                                GS::BuildConstView(raw, entity);
                            const GS::SourceAvailability availability =
                                GS::BuildSourceAvailability(view);
                            if (availability.ProvenanceDomain !=
                                    GS::Domain::Mesh ||
                                view.VertexSource == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }
                            if (expected.Positions == nullptr ||
                                GeometryMetadataSignatureForEntity(raw, entity) !=
                                    expected.GeometryMetadataSignature)
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const auto positions =
                                view.VertexSource->Properties.Get<glm::vec3>(
                                    GS::PropertyNames::kPosition);
                            if (!positions ||
                                !SameGeometryPositions(
                                    positions.Vector(),
                                    *expected.Positions))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshPositionState& target)
                        {
                            if (target == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    CommandFailed;
                            }
                            return ApplyMeshDenoisePositionState(
                                identity.Scene,
                                identity.StableEntityId,
                                *target);
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshDenoiseMutationGeneration&,
                            const MeshPositionState& target)
                        {
                            StampMeshDenoisePositionDirty(
                                *identity.Scene,
                                identity.StableEntityId);
                            const ECS::EntityHandle entity =
                                SelectionController::ToEntityHandle(
                                    identity.StableEntityId);
                            return MeshDenoiseMutationGeneration{
                                .GeometryMetadataSignature =
                                    GeometryMetadataSignatureForEntity(
                                        identity.Scene->Raw(),
                                        entity),
                                .Positions = target,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyMeshDenoisePositionState(
                    context.Scene,
                    stableEntityId,
                    after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            StampMeshDenoisePositionDirty(
                *context.Scene,
                stableEntityId);
            return EditorCommandStatus::Applied;
        }

        void CopyMeshDenoiseCounters(
            const Smooth::BilateralDenoiseResult& source,
            EditorMeshDenoiseResult& target)
        {
            target.NormalIterations =
                static_cast<std::uint32_t>(
                    source.NormalIterationsPerformed);
            target.VertexIterations =
                static_cast<std::uint32_t>(
                    source.VertexIterationsPerformed);
            target.VertexSlotCount = source.VertexCount;
            target.MovedVertexCount = source.MovedVertexCount;
            target.ProcessedFaceCount = source.ProcessedFaceCount;
            target.DegenerateFaceCount = source.DegenerateFaceCount;
            target.NonFiniteFaceCount = source.NonFiniteFaceCount;
            target.SkippedDeletedFaceCount = source.SkippedDeletedFaceCount;
            target.PinnedBoundaryVertexCount =
                source.PinnedBoundaryVertexCount;
            target.SigmaSpatialUsed = source.SigmaSpatialUsed;
            target.SigmaRangeUsed = source.SigmaRangeUsed;
        }

        [[nodiscard]] std::string BuildMeshDenoiseSuccessMessage(
            const EditorMeshDenoiseResult& result)
        {
            std::string message = "Mesh denoise completed (written=";
            message += std::to_string(result.WrittenCount);
            message += ", moved=";
            message += std::to_string(result.MovedVertexCount);
            message += ", sigmaSpatial=";
            message += std::to_string(result.SigmaSpatialUsed);
            message += ", sigmaRange=";
            message += std::to_string(result.SigmaRangeUsed);
            message += ").";
            return message;
        }

        // A run that writes every slot but moves nothing is not a success. The
        // written count is slot-derived, so it stays non-zero whenever the
        // kernel ran at all and cannot distinguish the two.
        [[nodiscard]] std::string BuildMeshDenoiseNoChangeMessage(
            const EditorMeshDenoiseResult& result)
        {
            const std::size_t live =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;
            std::string message = "Mesh denoise moved no vertices; the mesh is unchanged";
            if (result.PinnedBoundaryVertexCount >= live && live > 0u)
            {
                message += " because all ";
                message += std::to_string(result.PinnedBoundaryVertexCount);
                message += " vertices are pinned as boundary, leaving nothing to smooth";
            }
            else if (result.PinnedBoundaryVertexCount > 0u)
            {
                message += " (pinned boundary vertices: ";
                message += std::to_string(result.PinnedBoundaryVertexCount);
                message += " of ";
                message += std::to_string(live);
                message += ")";
            }
            else
            {
                message += "; no vertex had an interior neighborhood that changed it";
            }
            message += ". Written=";
            message += std::to_string(result.WrittenCount);
            message += ", moved=0.";
            return message;
        }

        struct MeshCurvatureSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> SourcePositions{};
            std::size_t VertexSlotCount{0u};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshCurvatureSourceResult BuildHalfedgeMeshForCurvature(
            const GS::ConstSourceView& view)
        {
            MeshDenoiseSourceResult source = BuildHalfedgeMeshForDenoise(view);
            MeshCurvatureSourceResult result{};
            result.VertexSlotCount = source.BeforePositions.size();
            result.Status = source.Status;
            result.Error = source.Error;
            if (source.Succeeded())
            {
                result.Mesh = std::move(source.Mesh);
                result.SourcePositions =
                    std::move(source.BeforePositions);
                result.Diagnostic.clear();
            }
            else if (source.Status ==
                     EditorCommandStatus::UnsupportedGeometryDomain)
            {
                result.Diagnostic =
                    "Mesh curvature requires selected mesh GeometrySources.";
            }
            else if (source.Status ==
                     EditorCommandStatus::InvalidProcessingParameters)
            {
                result.Diagnostic = "Mesh curvature requires finite count-matched vertex "
                                    "positions and valid mesh topology.";
            }
            else
            {
                result.Diagnostic = source.Diagnostic.empty()
                                        ? "Mesh curvature could not build a halfedge mesh "
                                          "from GeometrySources."
                                        : source.Diagnostic;
            }
            return result;
        }

        struct MeshCurvaturePropertyState
        {
            bool HadMean{false};
            bool HadGaussian{false};
            bool HadDir1{false};
            bool HadDir2{false};
            std::vector<double> Mean{};
            std::vector<double> Gaussian{};
            std::vector<glm::vec3> Dir1{};
            std::vector<glm::vec3> Dir2{};
        };

        using MeshCurvaturePropertySnapshot =
            std::shared_ptr<const MeshCurvaturePropertyState>;

        [[nodiscard]] std::size_t CountChangedCurvatureValues(
            const MeshCurvaturePropertyState& before,
            const MeshCurvaturePropertyState& after) noexcept
        {
            return CountChangedValues(before.HadMean, before.Mean, after.Mean) +
                   CountChangedValues(
                       before.HadGaussian, before.Gaussian, after.Gaussian) +
                   CountChangedValues(before.HadDir1, before.Dir1, after.Dir1) +
                   CountChangedValues(before.HadDir2, before.Dir2, after.Dir2);
        }

        [[nodiscard]] std::string BuildMeshCurvatureNoChangeMessage(
            const EditorMeshCurvatureResult& result)
        {
            std::string message =
                "Mesh curvature is already up to date (";
            message += std::to_string(result.ScalarWrittenCount);
            message += " scalar and ";
            message += std::to_string(result.DirectionWrittenCount);
            message += " direction values recomputed, 0 changed). Nothing was "
                       "published and no undo entry was created.";
            return message;
        }

        struct MeshCurvatureMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            std::size_t VertexSlotCount{0u};
            MeshPositionState Positions{};
            MeshCurvaturePropertySnapshot Properties{};
        };

        [[nodiscard]] bool SameVec3PropertyValues(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (lhs[i].x != rhs[i].x ||
                    lhs[i].y != rhs[i].y ||
                    lhs[i].z != rhs[i].z)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SameMeshCurvaturePropertyState(
            const MeshCurvaturePropertyState& lhs,
            const MeshCurvaturePropertyState& rhs) noexcept
        {
            return lhs.HadMean == rhs.HadMean &&
                   lhs.HadGaussian == rhs.HadGaussian &&
                   lhs.HadDir1 == rhs.HadDir1 &&
                   lhs.HadDir2 == rhs.HadDir2 &&
                   lhs.Mean == rhs.Mean &&
                   lhs.Gaussian == rhs.Gaussian &&
                   SameVec3PropertyValues(lhs.Dir1, rhs.Dir1) &&
                   SameVec3PropertyValues(lhs.Dir2, rhs.Dir2);
        }

        [[nodiscard]] bool MeshCurvaturePropertyStateMatchesCount(
            const MeshCurvaturePropertyState& state,
            const std::size_t expectedCount) noexcept
        {
            return (state.HadMean
                        ? state.Mean.size() == expectedCount
                        : state.Mean.empty()) &&
                   (state.HadGaussian
                        ? state.Gaussian.size() == expectedCount
                        : state.Gaussian.empty()) &&
                   (state.HadDir1
                        ? state.Dir1.size() == expectedCount
                        : state.Dir1.empty()) &&
                   (state.HadDir2
                        ? state.Dir2.size() == expectedCount
                        : state.Dir2.empty());
        }

        template <typename T>
        [[nodiscard]] bool CaptureCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const std::size_t expectedCount,
            bool& hadProperty,
            std::vector<T>& values,
            std::string& diagnostic)
        {
            hadProperty = false;
            values.clear();
            if (!properties.Exists(name))
                return true;

            auto property = properties.Get<T>(name);
            if (!property || property.Vector().size() != expectedCount)
            {
                diagnostic = "existing curvature property has an incompatible type or count: ";
                diagnostic += std::string{name};
                return false;
            }

            hadProperty = true;
            values = property.Vector();
            return true;
        }

        [[nodiscard]] bool CaptureMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const std::size_t expectedCount,
            MeshCurvaturePropertyState& out,
            std::string& diagnostic)
        {
            return CaptureCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kMeanCurvature,
                       expectedCount,
                       out.HadMean,
                       out.Mean,
                       diagnostic) &&
                   CaptureCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kGaussianCurvature,
                       expectedCount,
                       out.HadGaussian,
                       out.Gaussian,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir1,
                       expectedCount,
                       out.HadDir1,
                       out.Dir1,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir2,
                       expectedCount,
                       out.HadDir2,
                       out.Dir2,
                       diagnostic);
        }

        template <typename T>
        [[nodiscard]] bool ApplyCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const bool hasProperty,
            const std::vector<T>& values,
            const T& defaultValue)
        {
            if (!hasProperty)
            {
                auto property = properties.Get<T>(name);
                if (property)
                {
                    properties.Remove(property);
                    return true;
                }
                return !properties.Exists(name);
            }

            auto property =
                properties.GetOrAdd<T>(std::string{name}, defaultValue);
            if (!property || property.Vector().size() != values.size())
                return false;
            property.Vector() = values;
            return true;
        }

        [[nodiscard]] bool ApplyMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const MeshCurvaturePropertyState& state)
        {
            return ApplyCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kMeanCurvature,
                       state.HadMean,
                       state.Mean,
                       0.0) &&
                   ApplyCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kGaussianCurvature,
                       state.HadGaussian,
                       state.Gaussian,
                       0.0) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir1,
                       state.HadDir1,
                       state.Dir1,
                       glm::vec3{0.0f}) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir2,
                       state.HadDir2,
                       state.Dir2,
                       glm::vec3{0.0f});
        }

        [[nodiscard]] std::size_t CountNonFiniteScalars(
            const std::span<const double> values) noexcept
        {
            std::size_t count = 0u;
            for (const double value : values)
            {
                if (!std::isfinite(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] std::size_t CountNonFiniteVectors(
            const std::span<const glm::vec3> values) noexcept
        {
            std::size_t count = 0u;
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteVec3(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] bool CurvatureOutputRequestsDirections(
            const EditorMeshCurvatureOutput output) noexcept
        {
            return output == EditorMeshCurvatureOutput::All ||
                   output == EditorMeshCurvatureOutput::PrincipalDirections;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshCurvatureState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const MeshCurvaturePropertyState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            if (!ApplyMeshCurvaturePropertyState(
                    view.VertexSource->Properties,
                    state))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            return EditorCommandHistoryStatus::Applied;
        }

        void StampMeshCurvaturePropertyDirty(
            ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId)
        {
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            Dirty::MarkVertexAttributesDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandStatus CommitMeshCurvatureProperties(
            const EditorGeometryProcessingContext& context,
            const std::uint32_t stableEntityId,
            std::vector<glm::vec3> positions,
            MeshCurvaturePropertyState before,
            MeshCurvaturePropertyState after)
        {
            if (context.CommandHistory != nullptr)
            {
                if (context.Scene == nullptr)
                    return EditorCommandStatus::MissingScene;
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableEntityId);
                if (entity == ECS::InvalidEntityHandle ||
                    !context.Scene->Raw().valid(entity))
                {
                    return EditorCommandStatus::StaleEntity;
                }

                const MeshPositionState positionState =
                    std::make_shared<std::vector<glm::vec3>>(
                        std::move(positions));
                const MeshCurvaturePropertySnapshot beforeState =
                    std::make_shared<MeshCurvaturePropertyState>(
                        std::move(before));
                const MeshCurvaturePropertySnapshot afterState =
                    std::make_shared<MeshCurvaturePropertyState>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        "Compute mesh curvature",
                        MeshPropertyMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        MeshCurvatureMutationGeneration{
                            .GeometryMetadataSignature =
                                GeometryMetadataSignatureForEntity(
                                    context.Scene->Raw(),
                                    entity),
                            .VertexSlotCount = positionState->size(),
                            .Positions = positionState,
                            .Properties = beforeState,
                        },
                        beforeState,
                        afterState,
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshCurvatureMutationGeneration& expected,
                            const MeshCurvaturePropertySnapshot& target)
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

                            GS::MutableSourceView view =
                                GS::BuildMutableView(raw, entity);
                            const GS::SourceAvailability availability =
                                GS::BuildSourceAvailability(view);
                            if (availability.ProvenanceDomain !=
                                    GS::Domain::Mesh ||
                                view.VertexSource == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }
                            if (expected.Positions == nullptr ||
                                expected.Properties == nullptr ||
                                target == nullptr ||
                                !MeshCurvaturePropertyStateMatchesCount(
                                    *target,
                                    expected.VertexSlotCount))
                            {
                                return EditorCommandHistoryStatus::CommandFailed;
                            }
                            if (GeometryMetadataSignatureForEntity(raw, entity) !=
                                expected.GeometryMetadataSignature)
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const auto currentPositions =
                                view.VertexSource->Properties.Get<glm::vec3>(
                                    GS::PropertyNames::kPosition);
                            if (!currentPositions ||
                                !SameGeometryPositions(
                                    currentPositions.Vector(),
                                    *expected.Positions))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            MeshCurvaturePropertyState currentProperties{};
                            std::string diagnostic{};
                            if (!CaptureMeshCurvaturePropertyState(
                                    view.VertexSource->Properties,
                                    expected.VertexSlotCount,
                                    currentProperties,
                                    diagnostic) ||
                                !SameMeshCurvaturePropertyState(
                                    currentProperties,
                                    *expected.Properties))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshCurvaturePropertySnapshot& target)
                        {
                            if (target == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    CommandFailed;
                            }
                            return ApplyMeshCurvatureState(
                                identity.Scene,
                                identity.StableEntityId,
                                *target);
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshCurvatureMutationGeneration& expected,
                            const MeshCurvaturePropertySnapshot& target)
                        {
                            StampMeshCurvaturePropertyDirty(
                                *identity.Scene,
                                identity.StableEntityId);
                            const ECS::EntityHandle entity =
                                SelectionController::ToEntityHandle(
                                    identity.StableEntityId);
                            return MeshCurvatureMutationGeneration{
                                .GeometryMetadataSignature =
                                    GeometryMetadataSignatureForEntity(
                                        identity.Scene->Raw(),
                                        entity),
                                .VertexSlotCount = expected.VertexSlotCount,
                                .Positions = expected.Positions,
                                .Properties = target,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyMeshCurvatureState(context.Scene, stableEntityId, after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            StampMeshCurvaturePropertyDirty(
                *context.Scene,
                stableEntityId);
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] std::string BuildMeshCurvatureSuccessMessage(
            const EditorMeshCurvatureResult& result)
        {
            std::string message = "Mesh curvature computed (vertices=";
            message += std::to_string(result.VertexSlotCount);
            message += ", scalars=";
            message += std::to_string(result.ScalarWrittenCount);
            message += ", changed=";
            message += std::to_string(result.ChangedValueCount);
            message += ", directions=";
            message += result.DirectionsPublished ? "published" : "not published";
            message += ").";
            if (result.DirectionsRequested && !result.DirectionsPublished)
                message += " Principal directions were not published for this run.";
            return message;
        }

        [[nodiscard]] bool ValidMeshRemeshMode(
            const EditorMeshRemeshMode mode) noexcept
        {
            return std::find(kMeshRemeshModes.begin(),
                             kMeshRemeshModes.end(),
                             mode) != kMeshRemeshModes.end();
        }

        [[nodiscard]] bool ValidMeshRemeshSizingLaw(
            const EditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            return std::find(kMeshRemeshSizingLaws.begin(),
                             kMeshRemeshSizingLaws.end(),
                             sizingLaw) != kMeshRemeshSizingLaws.end();
        }

        [[nodiscard]] bool ValidMeshSubdivideOperator(
            const EditorMeshSubdivideOperator op) noexcept
        {
            return std::find(kMeshSubdivideOperators.begin(),
                             kMeshSubdivideOperators.end(),
                             op) != kMeshSubdivideOperators.end();
        }

        [[nodiscard]] bool ValidMeshSimplifyMetric(
            const EditorMeshSimplifyMetric metric) noexcept
        {
            return std::find(kMeshSimplifyMetrics.begin(),
                             kMeshSimplifyMetrics.end(),
                             metric) != kMeshSimplifyMetrics.end();
        }

        [[nodiscard]] bool ValidEditorICPVariant(
            const EditorICPVariant variant) noexcept
        {
            return std::find(kEditorICPVariants.begin(),
                             kEditorICPVariants.end(),
                             variant) != kEditorICPVariants.end();
        }

        [[nodiscard]] Reg::ICPVariant ToGeometryICPVariant(
            const EditorICPVariant variant) noexcept
        {
            return variant == EditorICPVariant::PointToPlane
                       ? Reg::ICPVariant::PointToPlane
                       : Reg::ICPVariant::PointToPoint;
        }

        struct MeshTopologySourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshTopologySourceResult BuildHalfedgeMeshForTopologyEdit(
            const GS::ConstSourceView& view,
            std::string_view operationName)
        {
            MeshDenoiseSourceResult source = BuildHalfedgeMeshForDenoise(view);
            MeshTopologySourceResult result{};
            result.Status = source.Status;
            result.Error = source.Error;
            if (source.Succeeded())
            {
                result.Mesh = std::move(source.Mesh);
                return result;
            }

            result.Diagnostic = std::string{operationName};
            if (source.Status ==
                EditorCommandStatus::UnsupportedGeometryDomain)
            {
                result.Diagnostic +=
                    " requires selected mesh GeometrySources.";
            }
            else if (source.Status ==
                     EditorCommandStatus::InvalidProcessingParameters)
            {
                result.Diagnostic +=
                    " requires a non-empty finite mesh with valid topology.";
            }
            else
            {
                result.Diagnostic +=
                    " could not build a halfedge mesh from GeometrySources.";
            }
            if (!source.Diagnostic.empty())
            {
                result.Diagnostic += " ";
                result.Diagnostic += source.Diagnostic;
            }
            return result;
        }

        void MarkMeshTopologyReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkEdgeTopologyDirty(raw, entity);
            Dirty::MarkFaceTopologyDirty(raw, entity);
        }

        using MeshTopologySnapshot =
            std::shared_ptr<const Geometry::HalfedgeMesh::Mesh>;

        struct MeshTopologyMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            // `std::nullopt` means the stored topology could not be read, which
            // no later reading may match. See `MeshTopologyValueSignature`.
            std::optional<std::uint64_t> TopologySignature{};
            MeshTopologySnapshot Mesh{};
        };

        [[nodiscard]] bool SameStoredMeshTopology(
            const GS::ConstSourceView& view,
            const std::optional<std::uint64_t>& expected)
        {
            const std::optional<std::uint64_t> current =
                MeshTopologyValueSignature(view);
            return current.has_value() && expected.has_value() &&
                   *current == *expected;
        }

        // Vertex-slot state of the stored mesh, compared against the
        // halfedge mesh a job or history entry captured. Vertex numbering
        // survives the GeometrySources -> triangle-soup -> halfedge round-trip
        // that produces those captures, so this half of the old
        // `SameMeshTopologyState` is representation-faithful. Its edge,
        // halfedge, and face half was not, and now goes through
        // `MeshTopologyValueSignature` instead (BUG-138).
        [[nodiscard]] bool SameMeshVertexState(
            const GS::ConstSourceView& view,
            const Geometry::HalfedgeMesh::Mesh& mesh) noexcept
        {
            if (view.VertexSource == nullptr ||
                view.EdgeSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr ||
                view.VertexSource->Properties.Size() != mesh.VerticesSize() ||
                view.VertexSource->NumDeleted != mesh.DeletedVertexCount())
            {
                return false;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions)
                return false;

            if (!SameKnownPropertyValues(
                    Geometry::ConstPropertySet(
                        view.VertexSource->Properties),
                    mesh.VertexProperties()))
            {
                return false;
            }

            for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
            {
                const glm::vec3& current = positions.Vector()[i];
                const glm::vec3& expected =
                    mesh.Position(Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(i)});
                if (current.x != expected.x ||
                    current.y != expected.y ||
                    current.z != expected.z)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshTopologyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            Geometry::HalfedgeMesh::Mesh published = mesh;
            if (published.HasGarbage())
                published.GarbageCollection();
            GS::PopulateFromMesh(raw, *entity, published);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandStatus CommitMeshTopologyReplacement(
            const EditorGeometryProcessingContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            const std::uint64_t expectedGeometryMetadataSignature,
            Geometry::HalfedgeMesh::Mesh before,
            Geometry::HalfedgeMesh::Mesh after)
        {
            if (before.HasGarbage())
                before.GarbageCollection();
            if (after.HasGarbage())
                after.GarbageCollection();

            if (context.CommandHistory != nullptr)
            {
                // Read the stored topology before the apply rewrites it: this
                // is the "what we expect to still be there" side of the first
                // undo transition, and it has to come from the sources rather
                // than from `before`, which is a re-derivation of them
                // (BUG-138).
                const std::optional<std::uint64_t> beforeTopology =
                    context.Scene != nullptr
                        ? StoredMeshTopologySignatureForEntity(
                              context.Scene->Raw(),
                              stableEntityId)
                        : std::nullopt;
                const MeshTopologySnapshot beforeState =
                    std::make_shared<Geometry::HalfedgeMesh::Mesh>(
                        std::move(before));
                const MeshTopologySnapshot afterState =
                    std::make_shared<Geometry::HalfedgeMesh::Mesh>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        label,
                        MeshPropertyMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        MeshTopologyMutationGeneration{
                            .GeometryMetadataSignature =
                                expectedGeometryMetadataSignature,
                            .TopologySignature = beforeTopology,
                            .Mesh = beforeState,
                        },
                        beforeState,
                        afterState,
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshTopologyMutationGeneration& expected,
                            const MeshTopologySnapshot& target)
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
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const GS::ConstSourceView view =
                                GS::BuildConstView(raw, *entity);
                            const GS::SourceAvailability availability =
                                GS::BuildSourceAvailability(view);
                            if (availability.ProvenanceDomain !=
                                GS::Domain::Mesh)
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }
                            if (expected.Mesh == nullptr || target == nullptr)
                            {
                                return EditorCommandHistoryStatus::CommandFailed;
                            }
                            if (GeometryMetadataSignatureForEntity(
                                    raw,
                                    *entity) !=
                                    expected.GeometryMetadataSignature ||
                                !SameMeshVertexState(view, *expected.Mesh) ||
                                !SameStoredMeshTopology(
                                    view,
                                    expected.TopologySignature))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshTopologySnapshot& target)
                        {
                            if (target == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    CommandFailed;
                            }
                            return ApplyMeshTopologyState(
                                identity.Scene,
                                identity.StableEntityId,
                                *target);
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshTopologyMutationGeneration&,
                            const MeshTopologySnapshot& target)
                        {
                            entt::registry& raw = identity.Scene->Raw();
                            const std::optional<ECS::EntityHandle> entity =
                                ResolveStableEntity(
                                    raw,
                                    identity.StableEntityId);
                            if (entity.has_value())
                                MarkMeshTopologyReplacementDirty(raw, *entity);
                            return MeshTopologyMutationGeneration{
                                .GeometryMetadataSignature =
                                    entity.has_value()
                                        ? GeometryMetadataSignatureForEntity(
                                              raw,
                                              *entity)
                                        : 0u,
                                // Re-read from the sources the apply just
                                // wrote, so the next undo/redo transition
                                // compares against the numbering that is
                                // actually stored rather than a re-derivation
                                // of it (BUG-138).
                                .TopologySignature =
                                    entity.has_value()
                                        ? MeshTopologyValueSignature(
                                              GS::BuildConstView(raw, *entity))
                                        : std::nullopt,
                                .Mesh = target,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyMeshTopologyState(
                    context.Scene,
                    stableEntityId,
                    after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (entity.has_value())
                MarkMeshTopologyReplacementDirty(raw, *entity);
            return EditorCommandStatus::Applied;
        }

        // UI-027: outlier removal changes the point count, so the published
        // point GeometrySources must be fully rebuilt (mirrors the mesh
        // topology-replacement path but for point clouds). A full re-upload is
        // requested because the count changed.
        void MarkPointCloudReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            Dirty::MarkGpuDirty(raw, entity);
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkVertexNormalsDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyPointCloudPointState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::PointCloud::Cloud& cloud)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            // PopulateFromCloud takes a mutable cloud (it may garbage-collect);
            // operate on an owned copy so the captured undo/redo state is not
            // mutated.
            Geometry::PointCloud::Cloud published = cloud;
            GS::PopulateFromCloud(raw, *entity, published);
            return EditorCommandHistoryStatus::Applied;
        }

        struct PointCloudMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        struct PointCloudSourceState
        {
            Geometry::PropertySet Properties{};
            std::size_t NumDeleted{0u};
        };

        using PointCloudSourceSnapshot =
            std::shared_ptr<const PointCloudSourceState>;
        using PointCloudState =
            std::shared_ptr<const Geometry::PointCloud::Cloud>;

        struct PointCloudMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            PointCloudSourceSnapshot Source{};
        };

        [[nodiscard]] PointCloudSourceSnapshot CapturePointCloudSourceState(
            const GS::ConstSourceView& view)
        {
            if (view.VertexSource == nullptr)
                return nullptr;
            return std::make_shared<PointCloudSourceState>(
                PointCloudSourceState{
                    .Properties = view.VertexSource->Properties,
                    .NumDeleted = view.VertexSource->NumDeleted,
                });
        }

        [[nodiscard]] bool SamePointCloudSourceState(
            const GS::ConstSourceView& view,
            const PointCloudSourceState& expected) noexcept
        {
            return view.VertexSource != nullptr &&
                   view.VertexSource->NumDeleted == expected.NumDeleted &&
                   SameKnownPropertyValues(
                       Geometry::ConstPropertySet(
                           view.VertexSource->Properties),
                       Geometry::ConstPropertySet(
                           expected.Properties));
        }

        [[nodiscard]] bool MirrorGeometrySourcePositionsToPointCloudStorage(
            Geometry::PropertySet& properties)
        {
            const auto positions =
                properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().size() != properties.Size())
                return false;

            // GeometrySources uses `v:position`; Geometry.PointCloud::Cloud
            // uses `v:point`. Seed the cloud slot before borrowing the property
            // set so point-cloud algorithms see the promoted source positions.
            auto points = properties.GetOrAdd<glm::vec3>(
                "v:point",
                glm::vec3{0.0f});
            if (!points)
                return false;
            points.Vector() = positions.Vector();
            return true;
        }

        [[nodiscard]] EditorCommandStatus CommitPointCloudReplacement(
            const EditorGeometryProcessingContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            const std::uint64_t expectedGeometryMetadataSignature,
            PointCloudSourceSnapshot expectedSource,
            Geometry::PointCloud::Cloud before,
            Geometry::PointCloud::Cloud after)
        {
            if (context.CommandHistory != nullptr)
            {
                const PointCloudState beforeState =
                    std::make_shared<Geometry::PointCloud::Cloud>(
                        std::move(before));
                const PointCloudState afterState =
                    std::make_shared<Geometry::PointCloud::Cloud>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        label,
                        PointCloudMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        PointCloudMutationGeneration{
                            .GeometryMetadataSignature =
                                expectedGeometryMetadataSignature,
                            .Source = std::move(expectedSource),
                        },
                        beforeState,
                        afterState,
                        [](
                            const PointCloudMutationIdentity& identity,
                            const PointCloudMutationGeneration& expected,
                            const PointCloudState& target)
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
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const GS::ConstSourceView view =
                                GS::BuildConstView(raw, *entity);
                            const GS::SourceAvailability availability =
                                GS::BuildSourceAvailability(view);
                            if (availability.ProvenanceDomain !=
                                GS::Domain::PointCloud)
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }
                            if (expected.Source == nullptr || target == nullptr)
                            {
                                return EditorCommandHistoryStatus::CommandFailed;
                            }
                            if (GeometryMetadataSignatureForEntity(
                                    raw,
                                    *entity) !=
                                    expected.GeometryMetadataSignature ||
                                !SamePointCloudSourceState(
                                    view,
                                    *expected.Source))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const PointCloudMutationIdentity& identity,
                            const PointCloudState& target)
                        {
                            if (target == nullptr)
                                return EditorCommandHistoryStatus::CommandFailed;
                            return ApplyPointCloudPointState(
                                identity.Scene,
                                identity.StableEntityId,
                                *target);
                        },
                        [](
                            const PointCloudMutationIdentity& identity,
                            const PointCloudMutationGeneration&,
                            const PointCloudState&)
                        {
                            entt::registry& raw = identity.Scene->Raw();
                            const std::optional<ECS::EntityHandle> entity =
                                ResolveStableEntity(
                                    raw,
                                    identity.StableEntityId);
                            if (entity.has_value())
                                MarkPointCloudReplacementDirty(raw, *entity);
                            return PointCloudMutationGeneration{
                                .GeometryMetadataSignature =
                                    entity.has_value()
                                        ? GeometryMetadataSignatureForEntity(
                                              raw,
                                              *entity)
                                        : 0u,
                                .Source =
                                    entity.has_value()
                                        ? CapturePointCloudSourceState(
                                              GS::BuildConstView(
                                                  raw,
                                                  *entity))
                                        : nullptr,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyPointCloudPointState(
                    context.Scene,
                    stableEntityId,
                    after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (entity.has_value())
                MarkPointCloudReplacementDirty(raw, *entity);
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] const char* DebugNameForOutlierRemovalStatus(
            const Geometry::PointCloud::OutlierRemovalStatus status) noexcept
        {
            using Status = Geometry::PointCloud::OutlierRemovalStatus;
            switch (status)
            {
            case Status::Success:
                return "Success";
            case Status::EmptyInput:
                return "EmptyInput";
            case Status::InsufficientPoints:
                return "InsufficientPoints";
            case Status::InvalidParameters:
                return "InvalidParameters";
            case Status::BuildFailed:
                return "BuildFailed";
            }
            return "Unknown";
        }

        [[nodiscard]] AdaptiveRemesh::SizingLaw ToAdaptiveSizingLaw(
            const EditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            switch (sizingLaw)
            {
            case EditorMeshRemeshSizingLaw::MeanCurvature:
                return AdaptiveRemesh::SizingLaw::MeanCurvature;
            case EditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
                return AdaptiveRemesh::SizingLaw::ErrorBoundedTaubin;
            }
            return AdaptiveRemesh::SizingLaw::MeanCurvature;
        }

        void CopyRemeshCounters(
            const Geometry::RemeshingOperationResult& source,
            EditorMeshRemeshResult& target)
        {
            target.IterationsPerformed =
                static_cast<std::uint32_t>(source.IterationsPerformed);
            target.OutputVertexCount = source.FinalVertexCount;
            target.OutputFaceCount = source.FinalFaceCount;
            target.SplitCount = source.SplitCount;
            target.CollapseCount = source.CollapseCount;
            target.FlipCount = source.FlipCount;
        }

        // BUG-145: a topology operation commits a whole replacement mesh, so
        // the honest change signal is whether that mesh differs from the one it
        // replaced. Counts decide neither direction on their own: an edge flip
        // and a tangential relaxation pass each change the mesh while leaving
        // vertex and face counts identical, and reporting `NoChange` for either
        // would silently drop the user's edit. Storage sizes are compared
        // rather than live counts, so a mesh still carrying garbage reads as
        // different — the conservative direction, which reports `Applied`.
        [[nodiscard]] bool SameMeshTopologyAndPositions(
            const Geometry::HalfedgeMesh::Mesh& before,
            const Geometry::HalfedgeMesh::Mesh& after) noexcept
        {
            if (before.VerticesSize() != after.VerticesSize() ||
                before.HalfedgesSize() != after.HalfedgesSize() ||
                before.FacesSize() != after.FacesSize() ||
                before.DeletedVertexCount() != after.DeletedVertexCount() ||
                before.DeletedEdgeCount() != after.DeletedEdgeCount() ||
                before.DeletedFaceCount() != after.DeletedFaceCount())
            {
                return false;
            }

            for (std::size_t i = 0u; i < before.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (before.Position(vertex) != after.Position(vertex))
                    return false;
            }
            for (std::size_t i = 0u; i < before.HalfedgesSize(); ++i)
            {
                const Geometry::HalfedgeHandle halfedge{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (before.ToVertex(halfedge) != after.ToVertex(halfedge) ||
                    before.NextHalfedge(halfedge) !=
                        after.NextHalfedge(halfedge) ||
                    before.Face(halfedge) != after.Face(halfedge))
                {
                    return false;
                }
            }
            return true;
        }

        // BUG-145: UV regeneration also commits a replacement mesh, but its
        // point is the texcoord property, so the topology-and-positions
        // comparison alone is not enough: a run that rewrote only `v:texcoord`
        // would read as unchanged and be silently dropped. A texcoord present
        // on one side and absent on the other is a change.
        [[nodiscard]] bool SameUvRegenerationOutput(
            const Geometry::HalfedgeMesh::Mesh& before,
            const Geometry::HalfedgeMesh::Mesh& after)
        {
            if (!SameMeshTopologyAndPositions(before, after))
                return false;

            // BUG-147 — UVs now land on whichever domain can represent them, so
            // a comparison that reads only the vertex domain would call a
            // corner-UV rewrite "unchanged" and publish nothing.
            const auto sameProperty =
                [](const Geometry::ConstPropertySet& beforeSet,
                   const Geometry::ConstPropertySet& afterSet,
                   const std::string_view name)
            {
                const Geometry::ConstProperty<glm::vec2> beforeUvs =
                    beforeSet.Get<glm::vec2>(name);
                const Geometry::ConstProperty<glm::vec2> afterUvs =
                    afterSet.Get<glm::vec2>(name);
                if (static_cast<bool>(beforeUvs) != static_cast<bool>(afterUvs))
                    return false;
                if (!beforeUvs)
                    return true;
                return beforeUvs.Vector() == afterUvs.Vector();
            };

            return sameProperty(
                       Geometry::ConstPropertySet(before.VertexProperties()),
                       Geometry::ConstPropertySet(after.VertexProperties()),
                       "v:texcoord") &&
                   sameProperty(
                       Geometry::ConstPropertySet(before.HalfedgeProperties()),
                       Geometry::ConstPropertySet(after.HalfedgeProperties()),
                       Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
        }

        [[nodiscard]] std::string BuildMeshTopologyNoChangeMessage(
            const char* const operation,
            const std::size_t vertexCount,
            const std::size_t faceCount,
            const std::string& work)
        {
            std::string message = "Mesh ";
            message += operation;
            message += " left the mesh unchanged (";
            message += std::to_string(vertexCount);
            message += " vertices, ";
            message += std::to_string(faceCount);
            message += " faces, ";
            message += work;
            message += "). Nothing was published and no undo entry was "
                       "created.";
            return message;
        }

        [[nodiscard]] std::string BuildMeshRemeshNoChangeMessage(
            const EditorMeshRemeshResult& result)
        {
            std::string work = std::to_string(result.IterationsPerformed);
            work += " iterations, ";
            work += std::to_string(result.SplitCount);
            work += " splits, ";
            work += std::to_string(result.CollapseCount);
            work += " collapses, ";
            work += std::to_string(result.FlipCount);
            work += " flips";
            return BuildMeshTopologyNoChangeMessage(
                "remesh",
                result.OutputVertexCount,
                result.OutputFaceCount,
                work);
        }

        [[nodiscard]] std::string BuildMeshSimplifyNoChangeMessage(
            const EditorMeshSimplifyResult& result)
        {
            std::string work = std::to_string(result.CollapseCount);
            work += " collapses, ";
            work += std::to_string(result.CollapsesRejectedTopology);
            work += " rejected on topology, ";
            work += std::to_string(result.CollapsesRejectedQuality);
            work += " rejected on quality";
            return BuildMeshTopologyNoChangeMessage(
                "simplify",
                result.OutputVertexCount,
                result.OutputFaceCount,
                work);
        }

        // BUG-146 — a discarded parameterization is a user-visible loss, so it
        // is named in the message and not left to the enum alone.
        [[nodiscard]] std::string AppendedTexcoordDiscardSentence(
            const EditorMeshTexcoordOutcome outcome)
        {
            if (outcome != EditorMeshTexcoordOutcome::Discarded)
                return {};
            return " The mesh's UV parameterization was discarded: this "
                   "operation replaces the topology and cannot resample UVs "
                   "onto it. Re-parameterize the result if you need UVs.";
        }

        [[nodiscard]] std::string BuildMeshRemeshSuccessMessage(
            const EditorMeshRemeshResult& result)
        {
            std::string message = "Mesh remesh completed (mode=";
            message += DebugNameForEditorMeshRemeshMode(result.Mode);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ", ";
            message += DebugNameForEditorMeshTexcoordOutcome(
                result.TexcoordOutcome);
            message += ").";
            message += AppendedTexcoordDiscardSentence(result.TexcoordOutcome);
            return message;
        }

        [[nodiscard]] std::string BuildMeshSubdivideSuccessMessage(
            const EditorMeshSubdivideResult& result)
        {
            std::string message = "Mesh subdivide completed (operator=";
            message += DebugNameForEditorMeshSubdivideOperator(
                result.Operator);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ", ";
            message += DebugNameForEditorMeshTexcoordOutcome(
                result.TexcoordOutcome);
            message += ").";
            message += AppendedTexcoordDiscardSentence(result.TexcoordOutcome);
            return message;
        }

        [[nodiscard]] std::string BuildMeshSimplifySuccessMessage(
            const EditorMeshSimplifyResult& result)
        {
            std::string message = "Mesh simplify completed (metric=";
            message += DebugNameForEditorMeshSimplifyMetric(
                result.Metric);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", collapses=";
            message += std::to_string(result.CollapseCount);
            message += ", ";
            message += DebugNameForEditorMeshTexcoordOutcome(
                result.TexcoordOutcome);
            message += ").";
            message += AppendedTexcoordDiscardSentence(result.TexcoordOutcome);
            return message;
        }

        [[nodiscard]] EditorMeshCurvatureResult
        MakeMeshCurvatureBaseResult(
            const EditorMeshCurvatureCommand& command,
            const bool directionsAvailable)
        {
            return EditorMeshCurvatureResult{
                .Status = EditorCommandStatus::NoChange,
                .Output = command.Output,
                .DirectionsRequested =
                    command.PublishPrincipalDirections &&
                    CurvatureOutputRequestsDirections(command.Output),
                .DirectionsAvailable = directionsAvailable,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshDenoiseResult MakeMeshDenoiseBaseResult(
            const EditorMeshDenoiseCommand& command)
        {
            return EditorMeshDenoiseResult{
                .Status = EditorCommandStatus::NoChange,
                .DenoiseStatus = Smooth::DenoiseStatus::Success,
                .Stage = command.Stage,
                .NormalIterations = command.NormalIterations,
                .VertexIterations = command.VertexIterations,
                .SigmaSpatial = command.SigmaSpatial,
                .SigmaRange = command.SigmaRange,
                .PreserveBoundary = command.PreserveBoundary,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshRemeshResult MakeMeshRemeshBaseResult(
            const EditorMeshRemeshCommand& command)
        {
            return EditorMeshRemeshResult{
                .Status = EditorCommandStatus::NoChange,
                .Mode = command.Mode,
                .SizingLaw = command.SizingLaw,
                .IterationsRequested = command.Iterations,
                .TargetEdgeLength = command.TargetEdgeLength,
                .ProjectToSurface = command.ProjectToSurface,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshSubdivideResult
        MakeMeshSubdivideBaseResult(
            const EditorMeshSubdivideCommand& command)
        {
            return EditorMeshSubdivideResult{
                .Status = EditorCommandStatus::NoChange,
                .Operator = command.Operator,
                .IterationsRequested = command.Iterations,
                .PreserveLoopFeatureEdges = command.PreserveLoopFeatureEdges,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshSimplifyResult MakeMeshSimplifyBaseResult(
            const EditorMeshSimplifyCommand& command)
        {
            return EditorMeshSimplifyResult{
                .Status = EditorCommandStatus::NoChange,
                .Metric = command.Metric,
                .TargetFaces = command.TargetFaces,
                .MaxError = command.MaxError,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshCurvatureResult
        MakePendingMeshCurvatureResult(
            const EditorMeshCurvatureCommand& command,
            const bool directionsAvailable,
            const std::size_t vertexSlotCount,
            const JobToken handle)
        {
            EditorMeshCurvatureResult result =
                MakeMeshCurvatureBaseResult(command, directionsAvailable);
            result.Status = EditorCommandStatus::Pending;
            result.VertexSlotCount = vertexSlotCount;
            result.Message = "Mesh curvature CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorMeshDenoiseResult MakePendingMeshDenoiseResult(
            const EditorMeshDenoiseCommand& command,
            const MeshDenoiseSourceResult& source,
            const JobToken handle)
        {
            EditorMeshDenoiseResult result =
                MakeMeshDenoiseBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.VertexSlotCount = source.BeforePositions.size();
            result.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(source.DeletedVertices.begin(),
                               source.DeletedVertices.end(),
                               true));
            result.WrittenCount =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;
            result.Message = "Mesh denoise CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorMeshRemeshResult MakePendingMeshRemeshResult(
            const EditorMeshRemeshCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const JobToken handle)
        {
            EditorMeshRemeshResult result =
                MakeMeshRemeshBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh remesh CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorMeshSubdivideResult
        MakePendingMeshSubdivideResult(
            const EditorMeshSubdivideCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const JobToken handle)
        {
            EditorMeshSubdivideResult result =
                MakeMeshSubdivideBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh subdivide CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorMeshSimplifyResult
        MakePendingMeshSimplifyResult(
            const EditorMeshSimplifyCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const JobToken handle)
        {
            EditorMeshSimplifyResult result =
                MakeMeshSimplifyBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh simplify CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] Core::ErrorCode ResultErrorOrUnknown(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : error;
        }

        [[nodiscard]] EditorPointCloudOutlierRemovalResult
        MakePointCloudOutlierRemovalBaseResult(
            const EditorPointCloudOutlierRemovalCommand& command)
        {
            return EditorPointCloudOutlierRemovalResult{
                .Status = EditorCommandStatus::NoChange,
                .Method = command.Method,
                .GeometryStatus =
                    Geometry::PointCloud::OutlierRemovalStatus::Success,
                .Error = Core::ErrorCode::Success,
            };
        }

        void CopyPointCloudOutlierRemovalCounters(
            const Geometry::PointCloud::OutlierRemovalResult& source,
            EditorPointCloudOutlierRemovalResult& target)
        {
            target.GeometryStatus = source.Status;
            target.OriginalCount = source.OriginalCount;
            target.KeptCount = source.KeptCount;
            target.RejectedCount = source.RejectedCount;
            target.NonFiniteCount = source.NonFiniteCount;
            target.MeanDistance = source.MeanDistance;
            target.StdDevDistance = source.StdDevDistance;
            target.DistanceThreshold = source.DistanceThreshold;
        }

        // BUG-145: `RejectedIndices` is the change signal this operation already
        // computed and then ignored — an empty rejection set means the cloud is
        // unchanged, however many points were inspected.
        [[nodiscard]] std::string BuildPointCloudOutlierRemovalNoChangeMessage(
            const EditorPointCloudOutlierRemovalResult& result)
        {
            std::string message = "No outliers were rejected: all ";
            message += std::to_string(result.OriginalCount);
            message += " points passed the threshold (";
            message += std::to_string(result.DistanceThreshold);
            message += "). The cloud is unchanged and no undo entry was "
                       "created.";
            return message;
        }

        [[nodiscard]] std::string BuildPointCloudOutlierRemovalSuccessMessage(
            const EditorPointCloudOutlierRemovalResult& result)
        {
            std::string message = "Removed ";
            message += std::to_string(result.RejectedCount);
            message += " of ";
            message += std::to_string(result.OriginalCount);
            message += " points (kept ";
            message += std::to_string(result.KeptCount);
            if (result.NonFiniteCount > 0u)
            {
                message += ", non-finite ";
                message += std::to_string(result.NonFiniteCount);
            }
            message += ").";
            return message;
        }

        [[nodiscard]] EditorPointCloudOutlierRemovalResult
        MakePendingPointCloudOutlierRemovalResult(
            const EditorPointCloudOutlierRemovalCommand& command,
            const std::size_t livePointCount,
            const JobToken handle)
        {
            EditorPointCloudOutlierRemovalResult result =
                MakePointCloudOutlierRemovalBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.OriginalCount = livePointCount;
            result.KeptCount = livePointCount;
            result.Message = "Point-cloud outlier-removal CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        struct EditorPointCloudOutlierRemovalCpuJobState
        {
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            PointCloudSourceSnapshot Snapshot{};
            Geometry::PointCloud::Cloud BeforeCloud{};
            Geometry::PointCloud::Cloud WorkCloud{};
            Geometry::PointCloud::Cloud AfterCloud{};
            EditorPointCloudOutlierRemovalCommand Command{};
            EditorPointCloudOutlierRemovalResult Result{};
        };

        [[nodiscard]] JobApplyValidation
        ValidatePointCloudOutlierRemovalCpuJobApply(
            const EditorGeometryProcessingContext& context,
            const EditorPointCloudOutlierRemovalCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
                view.VertexSource == nullptr)
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                job.GeometryMetadataSignature)
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (job.Snapshot == nullptr ||
                !SamePointCloudSourceState(view, *job.Snapshot))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }

        void PublishPointCloudOutlierRemovalResultSink(
            const EditorGeometryProcessingContext& context,
            EditorPointCloudOutlierRemovalResult result)
        {
            if (context.MethodResultSinks.PointCloudOutlierRemoval)
            {
                context.MethodResultSinks.PointCloudOutlierRemoval(
                    std::move(result));
            }
        }

        [[nodiscard]] JobResultEnvelope
        RunPointCloudOutlierRemovalCpuWorker(
            const std::shared_ptr<
                EditorPointCloudOutlierRemovalCpuJobState>& state)
        {
            const bool statistical =
                state->Command.Method ==
                EditorPointCloudOutlierMethod::Statistical;
            Geometry::PointCloud::OutlierRemovalResult removal{};
            if (statistical)
            {
                Geometry::PointCloud::StatisticalOutlierRemovalParams params{};
                params.KNeighbors = state->Command.KNeighbors;
                params.StdDevMultiplier = state->Command.StdDevMultiplier;
                removal =
                    Geometry::PointCloud::RemoveStatisticalOutliers(
                        state->WorkCloud,
                        params);
            }
            else
            {
                Geometry::PointCloud::RadiusOutlierRemovalParams params{};
                params.SearchRadius = state->Command.SearchRadius;
                params.MinNeighbors = state->Command.MinNeighbors;
                removal =
                    Geometry::PointCloud::RemoveRadiusOutliers(
                        state->WorkCloud,
                        params);
            }

            EditorPointCloudOutlierRemovalResult& result =
                state->Result;
            CopyPointCloudOutlierRemovalCounters(removal, result);
            if (removal.Status !=
                Geometry::PointCloud::OutlierRemovalStatus::Success)
            {
                result.Status =
                    removal.Status ==
                            Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters
                        ? EditorCommandStatus::InvalidProcessingParameters
                        : EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.PointCloud outlier removal failed with ";
                result.Message +=
                    DebugNameForOutlierRemovalStatus(removal.Status);
                result.Message += ".";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            state->AfterCloud = state->WorkCloud;
            for (const std::size_t rejected : removal.RejectedIndices)
            {
                state->AfterCloud.DeletePoint(
                    Geometry::VertexHandle{
                        static_cast<std::uint32_t>(rejected)});
            }
            state->AfterCloud.GarbageCollection();
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Point-cloud outlier-removal CPU result ready",
                });
        }

        [[nodiscard]] Core::Result PublishPointCloudOutlierRemovalCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorPointCloudOutlierRemovalCpuJobState& job)
        {
            EditorPointCloudOutlierRemovalResult result = job.Result;
            if (!result.Succeeded())
            {
                PublishPointCloudOutlierRemovalResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (result.RejectedCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message =
                    BuildPointCloudOutlierRemovalNoChangeMessage(result);
                PublishPointCloudOutlierRemovalResultSink(context, result);
                return Core::Ok();
            }

            const bool statistical =
                job.Command.Method ==
                EditorPointCloudOutlierMethod::Statistical;
            const EditorCommandStatus commitStatus =
                CommitPointCloudReplacement(
                    context,
                    job.StableEntityId,
                    statistical
                        ? "Remove statistical point-cloud outliers"
                        : "Remove radius point-cloud outliers",
                    job.GeometryMetadataSignature,
                    std::move(job.Snapshot),
                    std::move(job.BeforeCloud),
                    std::move(job.AfterCloud));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Point-cloud outlier-removal publication failed during "
                                 "editor history commit.";
                PublishPointCloudOutlierRemovalResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message =
                BuildPointCloudOutlierRemovalSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishPointCloudOutlierRemovalResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] EditorJobIdentity
        MakePointCloudOutlierRemovalCpuJobIdentity(
            const EditorPointCloudOutlierRemovalCpuJobState& state)
        {
            return EditorJobIdentity{
                .EntityId = state.StableEntityId,
                .Scope = EditorJobScope::PointCloudPoint,
                .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                .OutputName = "point_cloud_outlier_removal",
            };
        }

        [[nodiscard]] JobDesc MakePointCloudOutlierRemovalCpuJobDesc(
            const EditorGeometryProcessingContext& context,
            const std::shared_ptr<
                EditorPointCloudOutlierRemovalCpuJobState>& state)
        {
            return JobDesc{
                .DebugName = "Sandbox.PointCloudOutlierRemoval.CPU",
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (state->WorkCloud.VertexCount() + 1023u) / 1024u)),
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunPointCloudOutlierRemovalCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        return ValidatePointCloudOutlierRemovalCpuJobApply(
                            context,
                            *state);
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorJobResult>() == nullptr)
                            return false;
                        return PublishPointCloudOutlierRemovalCpuJob(
                                   context,
                                   *state)
                            .has_value();
                    },
            };
        }

        [[nodiscard]] EditorPointCloudOutlierRemovalResult
        SubmitPointCloudOutlierRemovalCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorPointCloudOutlierRemovalCommand& command,
            Geometry::PointCloud::Cloud beforeCloud,
            Geometry::PointCloud::Cloud workCloud,
            PointCloudSourceSnapshot snapshot,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<
                EditorPointCloudOutlierRemovalCpuJobState>();
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->Snapshot = std::move(snapshot);
            state->BeforeCloud = std::move(beforeCloud);
            state->WorkCloud = std::move(workCloud);
            state->Command = command;
            state->Result = MakePointCloudOutlierRemovalBaseResult(command);
            state->Result.OriginalCount = state->WorkCloud.VertexCount();
            state->Result.KeptCount = state->WorkCloud.VertexCount();

            const EditorJobIdentity identity =
                MakePointCloudOutlierRemovalCpuJobIdentity(*state);
            JobDesc desc =
                MakePointCloudOutlierRemovalCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorPointCloudOutlierRemovalResult pending =
                    MakePendingPointCloudOutlierRemovalResult(
                        command,
                        state->WorkCloud.VertexCount(),
                        active->Token);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Point-cloud outlier-removal CPU",
                    *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorPointCloudOutlierRemovalResult result =
                    MakePointCloudOutlierRemovalBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.GeometryStatus =
                    Geometry::PointCloud::OutlierRemovalStatus::BuildFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Point-cloud outlier-removal CPU job submission was "
                                 "rejected by the runtime job lane.";
                return result;
            }

            return MakePendingPointCloudOutlierRemovalResult(
                command,
                state->WorkCloud.VertexCount(),
                handle);
        }

        enum class EditorVertexNormalsCpuJobKind : std::uint8_t
        {
            Mesh,
            Graph,
            PointCloud,
        };

        [[nodiscard]] const char* VertexNormalsCpuJobName(
            const EditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
                return "Sandbox.MeshVertexNormals.CPU";
            case EditorVertexNormalsCpuJobKind::Graph:
                return "Sandbox.GraphVertexNormals.CPU";
            case EditorVertexNormalsCpuJobKind::PointCloud:
                return "Sandbox.PointCloudVertexNormals.CPU";
            }
            return "Sandbox.VertexNormals.CPU";
        }

        [[nodiscard]] const char* VertexNormalsCpuJobOutputName(
            const EditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
                return "mesh_vertex_normals";
            case EditorVertexNormalsCpuJobKind::Graph:
                return "graph_vertex_normals";
            case EditorVertexNormalsCpuJobKind::PointCloud:
                return "point_cloud_vertex_normals";
            }
            return "vertex_normals";
        }

        [[nodiscard]] GeometryElementDomain VertexNormalsCpuJobDomain(
            const EditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
                return GeometryElementDomain::MeshVertex;
            case EditorVertexNormalsCpuJobKind::Graph:
                return GeometryElementDomain::GraphNode;
            case EditorVertexNormalsCpuJobKind::PointCloud:
                return GeometryElementDomain::PointCloudPoint;
            }
            return GeometryElementDomain::Unknown;
        }

        [[nodiscard]] GS::Domain VertexNormalsExpectedSourceDomain(
            const EditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
                return GS::Domain::Mesh;
            case EditorVertexNormalsCpuJobKind::Graph:
                return GS::Domain::Graph;
            case EditorVertexNormalsCpuJobKind::PointCloud:
                return GS::Domain::PointCloud;
            }
            return GS::Domain::None;
        }

        [[nodiscard]] EditorMeshVertexNormalsResult
        MakePendingMeshVertexNormalsResult(
            const EditorMeshVertexNormalsCommand& command,
            const std::size_t vertexSlotCount,
            const JobToken handle)
        {
            EditorMeshVertexNormalsResult result =
                MakeMeshNormalsResult(
                    EditorCommandStatus::Pending,
                    GN::RecomputeStatus::Success,
                    command.Weighting,
                    Core::ErrorCode::Success,
                    "Mesh vertex-normal CPU job queued");
            result.VertexSlotCount = vertexSlotCount;
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorGraphVertexNormalsResult
        MakePendingGraphVertexNormalsResult(
            const EditorGraphVertexNormalsCommand& command,
            const std::size_t vertexSlotCount,
            const std::size_t edgeSlotCount,
            const JobToken handle)
        {
            EditorGraphVertexNormalsResult result =
                MakeGraphNormalsResult(
                    EditorCommandStatus::Pending,
                    GraphNormals::RecomputeStatus::Success,
                    command.OrientTowardFallback,
                    Core::ErrorCode::Success,
                    "Graph vertex-normal CPU job queued");
            result.VertexSlotCount = vertexSlotCount;
            result.EdgeSlotCount = edgeSlotCount;
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorPointCloudVertexNormalsResult
        MakePendingPointCloudVertexNormalsResult(
            const EditorPointCloudVertexNormalsCommand& command,
            const std::size_t pointSlotCount,
            const JobToken handle)
        {
            EditorPointCloudVertexNormalsResult result =
                MakePointCloudNormalsResult(
                    EditorCommandStatus::Pending,
                    PointNormals::RecomputeStatus::Success,
                    command,
                    Core::ErrorCode::Success,
                    "Point-cloud vertex-normal CPU job queued");
            result.PointSlotCount = pointSlotCount;
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        struct VertexNormalPropertyState;
        struct VertexNormalsSourceState;

        struct EditorVertexNormalsCpuJobState
        {
            EditorVertexNormalsCpuJobKind Kind{
                EditorVertexNormalsCpuJobKind::Mesh};
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            std::vector<glm::vec3> SnapshotPositions{};
            std::vector<glm::vec3> Normals{};
            std::shared_ptr<const VertexNormalsSourceState> SourceState{};
            std::shared_ptr<const VertexNormalPropertyState> BeforeNormal{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            Geometry::Vertices GraphNodes{};
            Geometry::PropertySet GraphEdges{};
            Geometry::Halfedges GraphHalfedges{};
            std::size_t GraphEdgeSlotCount{0u};
            Geometry::Vertices PointCloudPoints{};
            EditorMeshVertexNormalsCommand MeshCommand{};
            EditorGraphVertexNormalsCommand GraphCommand{};
            EditorPointCloudVertexNormalsCommand PointCloudCommand{};
            EditorMeshVertexNormalsResult MeshResult{};
            EditorGraphVertexNormalsResult GraphResult{};
            EditorPointCloudVertexNormalsResult PointCloudResult{};
        };

        [[nodiscard]] const Geometry::PropertySet*
        VertexNormalsSourceProperties(
            const GS::ConstSourceView& view,
            const EditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
            case EditorVertexNormalsCpuJobKind::PointCloud:
            case EditorVertexNormalsCpuJobKind::Graph:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            }
            return nullptr;
        }

        [[nodiscard]] Geometry::PropertySet* VertexNormalsTargetProperties(
            GS::MutableSourceView& view,
            const EditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
            case EditorVertexNormalsCpuJobKind::PointCloud:
            case EditorVertexNormalsCpuJobKind::Graph:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            }
            return nullptr;
        }

        struct VertexNormalPropertyState
        {
            bool HadNormal{false};
            std::vector<glm::vec3> Normals{};
        };

        using VertexNormalPropertySnapshot =
            std::shared_ptr<const VertexNormalPropertyState>;

        struct VertexNormalsSourceState
        {
            Geometry::PropertySet Primary{};
            std::optional<Geometry::PropertySet> Edges{};
            std::optional<Geometry::PropertySet> Halfedges{};
            std::optional<Geometry::PropertySet> Faces{};
        };

        using VertexNormalsSourceSnapshot =
            std::shared_ptr<const VertexNormalsSourceState>;

        struct VertexNormalsMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            EditorVertexNormalsCpuJobKind Kind{
                EditorVertexNormalsCpuJobKind::Mesh};
        };

        struct VertexNormalsMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            VertexNormalsSourceSnapshot Source{};
            VertexNormalPropertySnapshot Normal{};
        };

        [[nodiscard]] bool CaptureVertexNormalPropertyState(
            const Geometry::PropertySet& properties,
            VertexNormalPropertyState& out)
        {
            out = {};
            if (!properties.Exists(GS::PropertyNames::kNormal))
                return true;

            const auto normals =
                properties.Get<glm::vec3>(GS::PropertyNames::kNormal);
            if (!normals ||
                normals.Vector().size() != properties.Size())
            {
                return false;
            }

            out.HadNormal = true;
            out.Normals = normals.Vector();
            return true;
        }

        [[nodiscard]] bool SameVertexNormalPropertyState(
            const VertexNormalPropertyState& lhs,
            const VertexNormalPropertyState& rhs) noexcept
        {
            if (lhs.HadNormal != rhs.HadNormal ||
                lhs.Normals.size() != rhs.Normals.size())
            {
                return false;
            }
            for (std::size_t i = 0u; i < lhs.Normals.size(); ++i)
            {
                if (!SameKnownPropertyValue(
                        lhs.Normals[i],
                        rhs.Normals[i]))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Geometry::PropertySet
        CopyVertexNormalSourceProperties(
            const Geometry::PropertySet& properties)
        {
            Geometry::PropertySet copy = properties;
            auto normals =
                copy.Get<glm::vec3>(GS::PropertyNames::kNormal);
            if (normals)
                copy.Remove(normals);
            return copy;
        }

        [[nodiscard]] bool CaptureVertexNormalsSourceState(
            const GS::ConstSourceView& view,
            const EditorVertexNormalsCpuJobKind kind,
            VertexNormalsSourceState& out)
        {
            out = {};
            switch (kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
                if (view.VertexSource == nullptr ||
                    view.HalfedgeSource == nullptr ||
                    view.FaceSource == nullptr)
                {
                    return false;
                }
                out.Primary = CopyVertexNormalSourceProperties(
                    view.VertexSource->Properties);
                if (view.EdgeSource != nullptr)
                    out.Edges = view.EdgeSource->Properties;
                out.Halfedges = view.HalfedgeSource->Properties;
                out.Faces = view.FaceSource->Properties;
                return true;
            case EditorVertexNormalsCpuJobKind::Graph:
                if (view.VertexSource == nullptr ||
                    view.HalfedgeSource == nullptr ||
                    view.EdgeSource == nullptr)
                {
                    return false;
                }
                out.Primary = CopyVertexNormalSourceProperties(
                    view.VertexSource->Properties);
                out.Edges = view.EdgeSource->Properties;
                out.Halfedges = view.HalfedgeSource->Properties;
                return true;
            case EditorVertexNormalsCpuJobKind::PointCloud:
                if (view.VertexSource == nullptr)
                    return false;
                out.Primary = CopyVertexNormalSourceProperties(
                    view.VertexSource->Properties);
                return true;
            }
            return false;
        }

        [[nodiscard]] bool SameOptionalKnownPropertySet(
            const std::optional<Geometry::PropertySet>& lhs,
            const std::optional<Geometry::PropertySet>& rhs) noexcept
        {
            if (lhs.has_value() != rhs.has_value())
                return false;
            if (!lhs.has_value())
                return true;
            return SameKnownPropertyValues(
                       Geometry::ConstPropertySet(*lhs),
                       Geometry::ConstPropertySet(*rhs)) &&
                   SameKnownPropertyValues(
                       Geometry::ConstPropertySet(*rhs),
                       Geometry::ConstPropertySet(*lhs));
        }

        [[nodiscard]] bool SameVertexNormalsSourceState(
            const VertexNormalsSourceState& lhs,
            const VertexNormalsSourceState& rhs) noexcept
        {
            return SameKnownPropertyValues(
                       Geometry::ConstPropertySet(lhs.Primary),
                       Geometry::ConstPropertySet(rhs.Primary)) &&
                   SameKnownPropertyValues(
                       Geometry::ConstPropertySet(rhs.Primary),
                       Geometry::ConstPropertySet(lhs.Primary)) &&
                   SameOptionalKnownPropertySet(lhs.Edges, rhs.Edges) &&
                   SameOptionalKnownPropertySet(
                       lhs.Halfedges,
                       rhs.Halfedges) &&
                   SameOptionalKnownPropertySet(lhs.Faces, rhs.Faces);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyVertexNormalPropertyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const EditorVertexNormalsCpuJobKind kind,
            const VertexNormalPropertyState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            if (GS::BuildSourceAvailability(view).ProvenanceDomain !=
                VertexNormalsExpectedSourceDomain(kind))
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            Geometry::PropertySet* properties =
                VertexNormalsTargetProperties(view, kind);
            if (properties == nullptr)
                return EditorCommandHistoryStatus::UnsupportedOperation;
            if (state.HadNormal)
            {
                return PublishCanonicalVec3Normals(
                           *properties,
                           state.Normals)
                    ? EditorCommandHistoryStatus::Applied
                    : EditorCommandHistoryStatus::CommandFailed;
            }

            if (!state.Normals.empty())
                return EditorCommandHistoryStatus::CommandFailed;
            if (!properties->Exists(GS::PropertyNames::kNormal))
                return EditorCommandHistoryStatus::Applied;
            auto normals =
                properties->Get<glm::vec3>(GS::PropertyNames::kNormal);
            if (!normals)
                return EditorCommandHistoryStatus::CommandFailed;
            properties->Remove(normals);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandStatus CommitVertexNormalProperty(
            const EditorGeometryProcessingContext& context,
            const std::uint32_t stableEntityId,
            const EditorVertexNormalsCpuJobKind kind,
            const char* label,
            const std::uint64_t expectedGeometryMetadataSignature,
            VertexNormalsSourceState source,
            VertexNormalPropertyState before,
            std::vector<glm::vec3> afterNormals)
        {
            VertexNormalPropertyState after{
                .HadNormal = true,
                .Normals = std::move(afterNormals),
            };

            if (context.CommandHistory != nullptr)
            {
                const VertexNormalsSourceSnapshot sourceState =
                    std::make_shared<VertexNormalsSourceState>(
                        std::move(source));
                const VertexNormalPropertySnapshot beforeState =
                    std::make_shared<VertexNormalPropertyState>(
                        std::move(before));
                const VertexNormalPropertySnapshot afterState =
                    std::make_shared<VertexNormalPropertyState>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        label,
                        VertexNormalsMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                            .Kind = kind,
                        },
                        VertexNormalsMutationGeneration{
                            .GeometryMetadataSignature =
                                expectedGeometryMetadataSignature,
                            .Source = sourceState,
                            .Normal = beforeState,
                        },
                        beforeState,
                        afterState,
                        [](
                            const VertexNormalsMutationIdentity& identity,
                            const VertexNormalsMutationGeneration& expected,
                            const VertexNormalPropertySnapshot& target)
                        {
                            if (identity.Scene == nullptr ||
                                !identity.World.IsValid())
                            {
                                return EditorCommandHistoryStatus::MissingScene;
                            }
                            if (expected.Source == nullptr ||
                                expected.Normal == nullptr ||
                                target == nullptr)
                            {
                                return EditorCommandHistoryStatus::CommandFailed;
                            }

                            entt::registry& raw = identity.Scene->Raw();
                            const std::optional<ECS::EntityHandle> entity =
                                ResolveStableEntity(
                                    raw,
                                    identity.StableEntityId);
                            if (!entity.has_value())
                                return EditorCommandHistoryStatus::StaleEntity;

                            const GS::ConstSourceView view =
                                GS::BuildConstView(raw, *entity);
                            if (GS::BuildSourceAvailability(view)
                                    .ProvenanceDomain !=
                                VertexNormalsExpectedSourceDomain(
                                    identity.Kind))
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }
                            if (GeometryMetadataSignatureForEntity(
                                    raw,
                                    *entity) !=
                                expected.GeometryMetadataSignature)
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            VertexNormalsSourceState currentSource{};
                            if (!CaptureVertexNormalsSourceState(
                                    view,
                                    identity.Kind,
                                    currentSource) ||
                                !SameVertexNormalsSourceState(
                                    currentSource,
                                    *expected.Source))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const Geometry::PropertySet* properties =
                                VertexNormalsSourceProperties(
                                    view,
                                    identity.Kind);
                            VertexNormalPropertyState currentNormal{};
                            if (properties == nullptr ||
                                !CaptureVertexNormalPropertyState(
                                    *properties,
                                    currentNormal) ||
                                !SameVertexNormalPropertyState(
                                    currentNormal,
                                    *expected.Normal))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            if (target->HadNormal &&
                                target->Normals.size() !=
                                    properties->Size())
                            {
                                return EditorCommandHistoryStatus::CommandFailed;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const VertexNormalsMutationIdentity& identity,
                            const VertexNormalPropertySnapshot& target)
                        {
                            if (target == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    CommandFailed;
                            }
                            return ApplyVertexNormalPropertyState(
                                identity.Scene,
                                identity.StableEntityId,
                                identity.Kind,
                                *target);
                        },
                        [](
                            const VertexNormalsMutationIdentity& identity,
                            const VertexNormalsMutationGeneration& expected,
                            const VertexNormalPropertySnapshot& target)
                        {
                            entt::registry& raw = identity.Scene->Raw();
                            const std::optional<ECS::EntityHandle> entity =
                                ResolveStableEntity(
                                    raw,
                                    identity.StableEntityId);
                            if (entity.has_value())
                                Dirty::MarkVertexNormalsDirty(raw, *entity);
                            return VertexNormalsMutationGeneration{
                                .GeometryMetadataSignature =
                                    entity.has_value()
                                        ? GeometryMetadataSignatureForEntity(
                                              raw,
                                              *entity)
                                        : 0u,
                                .Source = expected.Source,
                                .Normal = target,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyVertexNormalPropertyState(
                    context.Scene,
                    stableEntityId,
                    kind,
                    after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);

            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            Dirty::MarkVertexNormalsDirty(
                context.Scene->Raw(),
                entity);
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] JobApplyValidation ValidateVertexNormalsCpuJobApply(
            const EditorGeometryProcessingContext& context,
            const EditorVertexNormalsCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain !=
                VertexNormalsExpectedSourceDomain(job.Kind))
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                job.GeometryMetadataSignature)
            {
                return JobApplyValidation::StaleGeneration;
            }

            const Geometry::PropertySet* properties =
                VertexNormalsSourceProperties(view, job.Kind);
            if (properties == nullptr)
                return JobApplyValidation::StaleGeneration;

            std::optional<std::vector<glm::vec3>> current =
                CollectFiniteGeometryPositions(*properties);
            if (!current.has_value() ||
                !SameGeometryPositions(*current, job.SnapshotPositions))
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (job.SourceState == nullptr ||
                job.BeforeNormal == nullptr)
            {
                return JobApplyValidation::StaleGeneration;
            }

            VertexNormalsSourceState currentSource{};
            if (!CaptureVertexNormalsSourceState(
                    view,
                    job.Kind,
                    currentSource) ||
                !SameVertexNormalsSourceState(
                    currentSource,
                    *job.SourceState))
            {
                return JobApplyValidation::StaleGeneration;
            }

            VertexNormalPropertyState currentNormal{};
            if (!CaptureVertexNormalPropertyState(
                    *properties,
                    currentNormal) ||
                !SameVertexNormalPropertyState(
                    currentNormal,
                    *job.BeforeNormal))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }

        void PublishMeshVertexNormalsResultSink(
            const EditorGeometryProcessingContext& context,
            EditorMeshVertexNormalsResult result)
        {
            if (context.MethodResultSinks.MeshVertexNormals)
                context.MethodResultSinks.MeshVertexNormals(std::move(result));
        }

        void PublishGraphVertexNormalsResultSink(
            const EditorGeometryProcessingContext& context,
            EditorGraphVertexNormalsResult result)
        {
            if (context.MethodResultSinks.GraphVertexNormals)
                context.MethodResultSinks.GraphVertexNormals(std::move(result));
        }

        void PublishPointCloudVertexNormalsResultSink(
            const EditorGeometryProcessingContext& context,
            EditorPointCloudVertexNormalsResult result)
        {
            if (context.MethodResultSinks.PointCloudVertexNormals)
            {
                context.MethodResultSinks.PointCloudVertexNormals(
                    std::move(result));
            }
        }

        [[nodiscard]] JobResultEnvelope RunMeshVertexNormalsCpuWorker(
            const std::shared_ptr<EditorVertexNormalsCpuJobState>& state)
        {
            GN::Params params{};
            params.Weighting = state->MeshCommand.Weighting;
            params.OutputProperty = GN::kDefaultOutputProperty;
            params.FallbackNormal = state->MeshCommand.FallbackNormal;
            params.DegenerateNormalLengthEpsilon =
                state->MeshCommand.DegenerateNormalLengthEpsilon;
            params.SkipDeleted = true;

            const GN::Result normalResult = GN::Recompute(state->Mesh, params);
            EditorMeshVertexNormalsResult& result = state->MeshResult;
            result.Status = normalResult.Status == GN::RecomputeStatus::Success
                ? EditorCommandStatus::Applied
                : EditorCommandStatus::GeometryProcessingFailed;
            result.Error = normalResult.Status == GN::RecomputeStatus::Success
                ? Core::ErrorCode::Success
                : Core::ErrorCode::Unknown;
            CopyMeshNormalCounters(normalResult, result);

            if (normalResult.Status != GN::RecomputeStatus::Success)
            {
                result.Message =
                    "Geometry.HalfedgeMesh.Vertices.Normals failed with ";
                result.Message += std::string(GN::DebugName(normalResult.Status));
                result.Message += ".";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            if (!normalResult.Normals.IsValid() ||
                normalResult.Normals.Vector().size() !=
                    state->SnapshotPositions.size())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus = GN::RecomputeStatus::InvalidOutputProperty;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message      = "Geometry.HalfedgeMesh.Vertices.Normals produced missing "
                                      "or count-mismatched normals.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            state->Normals = normalResult.Normals.Vector();
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Mesh vertex-normal CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope RunGraphVertexNormalsCpuWorker(
            const std::shared_ptr<EditorVertexNormalsCpuJobState>& state)
        {
            GraphNormals::Params params{};
            params.PositionProperty = GS::PropertyNames::kPosition;
            params.OutputProperty = GraphNormals::kDefaultOutputProperty;
            params.FallbackNormal = state->GraphCommand.FallbackNormal;
            params.DegenerateNormalLengthEpsilon =
                state->GraphCommand.DegenerateNormalLengthEpsilon;
            params.CollinearEigenvalueRatioEpsilon =
                state->GraphCommand.CollinearEigenvalueRatioEpsilon;
            params.SkipDeleted = true;
            params.OrientTowardFallback =
                state->GraphCommand.OrientTowardFallback;

            const GraphNormals::PropertySetResult normalResult =
                GraphNormals::Recompute(
                    state->GraphNodes,
                    Geometry::ConstPropertySet(state->GraphNodes)
                        .Get<glm::vec3>(GS::PropertyNames::kPosition),
                    Geometry::ConstPropertySet(state->GraphHalfedges)
                        .Get<Geometry::Graph::HalfedgeConnectivity>(
                            GS::PropertyNames::kHalfedgeConnectivity),
                    state->GraphEdgeSlotCount,
                    params,
                    Geometry::ConstPropertySet(state->GraphNodes)
                        .Get<bool>("v:deleted"),
                    Geometry::ConstPropertySet(state->GraphEdges)
                        .Get<bool>("e:deleted"));

            EditorGraphVertexNormalsResult& result =
                state->GraphResult;
            result.Status = normalResult.Status ==
                    GraphNormals::RecomputeStatus::Success
                ? EditorCommandStatus::Applied
                : EditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus = normalResult.Status;
            result.OrientTowardFallback =
                state->GraphCommand.OrientTowardFallback;
            result.Error = ErrorForGraphNormalStatus(normalResult.Status);
            CopyGraphNormalCounters(normalResult.Diagnostics, result);

            if (normalResult.Status != GraphNormals::RecomputeStatus::Success)
            {
                result.Message = "Geometry.Graph.Vertex.Normals failed with ";
                result.Message +=
                    std::string(GraphNormals::DebugName(normalResult.Status));
                result.Message += ".";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            if (!normalResult.Normals.IsValid() ||
                normalResult.Normals.Vector().size() !=
                    state->SnapshotPositions.size())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus =
                    GraphNormals::RecomputeStatus::InvalidOutputProperty;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.Graph.Vertex.Normals produced missing or "
                                 "count-mismatched normals.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            state->Normals = normalResult.Normals.Vector();
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Graph vertex-normal CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope
        RunPointCloudVertexNormalsCpuWorker(
            const std::shared_ptr<EditorVertexNormalsCpuJobState>& state)
        {
            Geometry::PointCloud::Cloud scratchCloud{state->PointCloudPoints};
            PointNormals::Params params{};
            params.PositionProperty = GS::PropertyNames::kPosition;
            params.OutputProperty = PointNormals::kDefaultOutputProperty;
            params.KNeighbors = state->PointCloudCommand.KNeighbors;
            params.MinimumNeighbors = state->PointCloudCommand.MinimumNeighbors;
            params.UseRadiusSearch = state->PointCloudCommand.UseRadiusSearch;
            params.Radius = state->PointCloudCommand.Radius;
            params.Orientation = state->PointCloudCommand.Orientation;
            params.FallbackNormal = state->PointCloudCommand.FallbackNormal;
            params.DegenerateNormalLengthEpsilon =
                state->PointCloudCommand.DegenerateNormalLengthEpsilon;
            params.CollinearEigenvalueRatioEpsilon =
                state->PointCloudCommand.CollinearEigenvalueRatioEpsilon;
            params.SkipDeleted = true;

            const PointNormals::Result normalResult =
                PointNormals::Recompute(scratchCloud, params);

            EditorPointCloudVertexNormalsResult& result =
                state->PointCloudResult;
            result.Status = normalResult.Status ==
                    PointNormals::RecomputeStatus::Success
                ? EditorCommandStatus::Applied
                : EditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus = normalResult.Status;
            result.Backend = normalResult.Backend;
            result.Orientation = state->PointCloudCommand.Orientation;
            result.KNeighbors = state->PointCloudCommand.KNeighbors;
            result.MinimumNeighbors =
                state->PointCloudCommand.MinimumNeighbors;
            result.UseRadiusSearch = state->PointCloudCommand.UseRadiusSearch;
            result.Radius = state->PointCloudCommand.Radius;
            result.Error = ErrorForPointCloudNormalStatus(normalResult.Status);
            CopyPointCloudNormalCounters(normalResult.Diagnostics, result);

            if (normalResult.Status != PointNormals::RecomputeStatus::Success)
            {
                result.Message = "Geometry.PointCloud.Normals failed with ";
                result.Message +=
                    std::string(PointNormals::DebugName(normalResult.Status));
                result.Message += ".";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            if (!normalResult.Normals.IsValid() ||
                normalResult.Normals.Vector().size() !=
                    state->SnapshotPositions.size())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus =
                    PointNormals::RecomputeStatus::InvalidOutputProperty;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.PointCloud.Normals produced missing or "
                                 "count-mismatched normals.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            state->Normals = normalResult.Normals.Vector();
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Point-cloud vertex-normal CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope RunVertexNormalsCpuWorker(
            const std::shared_ptr<EditorVertexNormalsCpuJobState>& state)
        {
            switch (state->Kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
                return RunMeshVertexNormalsCpuWorker(state);
            case EditorVertexNormalsCpuJobKind::Graph:
                return RunGraphVertexNormalsCpuWorker(state);
            case EditorVertexNormalsCpuJobKind::PointCloud:
                return RunPointCloudVertexNormalsCpuWorker(state);
            }
            return JobResultEnvelope{};
        }

        [[nodiscard]] Core::Result PublishMeshVertexNormalsCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorVertexNormalsCpuJobState& job)
        {
            EditorMeshVertexNormalsResult result = job.MeshResult;
            if (!result.Succeeded())
            {
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }
            if (job.SourceState == nullptr ||
                job.BeforeNormal == nullptr)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh vertex-normal publication is missing its source snapshot.";
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.ChangedNormalCount = CountChangedVertexNormals(
                job.BeforeNormal->HadNormal,
                job.BeforeNormal->Normals,
                job.Normals);
            if (result.ChangedNormalCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildVertexNormalsNoChangeMessage(
                    "Mesh", result.WrittenCount);
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitVertexNormalProperty(
                    context,
                    job.StableEntityId,
                    EditorVertexNormalsCpuJobKind::Mesh,
                    "Recompute mesh vertex normals",
                    job.GeometryMetadataSignature,
                    *job.SourceState,
                    *job.BeforeNormal,
                    job.Normals);
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Mesh vertex-normal publication failed during "
                                 "editor history commit.";
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshNormalsSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshVertexNormalsResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishGraphVertexNormalsCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorVertexNormalsCpuJobState& job)
        {
            EditorGraphVertexNormalsResult result = job.GraphResult;
            if (!result.Succeeded())
            {
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }
            if (job.SourceState == nullptr ||
                job.BeforeNormal == nullptr)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Graph vertex-normal publication is missing its source snapshot.";
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.ChangedNormalCount = CountChangedVertexNormals(
                job.BeforeNormal->HadNormal,
                job.BeforeNormal->Normals,
                job.Normals);
            if (result.ChangedNormalCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildVertexNormalsNoChangeMessage(
                    "Graph", result.WrittenCount);
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitVertexNormalProperty(
                    context,
                    job.StableEntityId,
                    EditorVertexNormalsCpuJobKind::Graph,
                    "Recompute graph vertex normals",
                    job.GeometryMetadataSignature,
                    *job.SourceState,
                    *job.BeforeNormal,
                    job.Normals);
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Graph vertex-normal publication failed during "
                                 "editor history commit.";
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildGraphNormalsSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishGraphVertexNormalsResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishPointCloudVertexNormalsCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorVertexNormalsCpuJobState& job)
        {
            EditorPointCloudVertexNormalsResult result =
                job.PointCloudResult;
            if (!result.Succeeded())
            {
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }
            if (job.SourceState == nullptr ||
                job.BeforeNormal == nullptr)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Point-cloud vertex-normal publication is missing its source snapshot.";
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.ChangedNormalCount = CountChangedVertexNormals(
                job.BeforeNormal->HadNormal,
                job.BeforeNormal->Normals,
                job.Normals);
            if (result.ChangedNormalCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildVertexNormalsNoChangeMessage(
                    "Point-cloud", result.WrittenCount);
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitVertexNormalProperty(
                    context,
                    job.StableEntityId,
                    EditorVertexNormalsCpuJobKind::PointCloud,
                    "Recompute point-cloud vertex normals",
                    job.GeometryMetadataSignature,
                    *job.SourceState,
                    *job.BeforeNormal,
                    job.Normals);
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Point-cloud vertex-normal publication failed during "
                                 "editor history commit.";
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildPointCloudNormalsSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishPointCloudVertexNormalsResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishVertexNormalsCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorVertexNormalsCpuJobState& job)
        {
            switch (job.Kind)
            {
            case EditorVertexNormalsCpuJobKind::Mesh:
                return PublishMeshVertexNormalsCpuJob(context, job);
            case EditorVertexNormalsCpuJobKind::Graph:
                return PublishGraphVertexNormalsCpuJob(context, job);
            case EditorVertexNormalsCpuJobKind::PointCloud:
                return PublishPointCloudVertexNormalsCpuJob(context, job);
            }
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        [[nodiscard]] EditorJobIdentity MakeVertexNormalsCpuJobIdentity(
            const EditorVertexNormalsCpuJobState& state)
        {
            return EditorJobIdentity{
                .EntityId = state.StableEntityId,
                .Scope = ToEditorJobScope(
                    VertexNormalsCpuJobDomain(state.Kind)),
                .OutputSemantic = GeometryPresentationSlotSemantic::Normal,
                .OutputName = VertexNormalsCpuJobOutputName(state.Kind),
            };
        }

        [[nodiscard]] JobDesc MakeVertexNormalsCpuJobDesc(
            const EditorGeometryProcessingContext& context,
            const std::shared_ptr<EditorVertexNormalsCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SnapshotPositions.size(),
                                  state->GraphEdgeSlotCount) +
                         1023u) /
                        1024u));
            return JobDesc{
                .DebugName = VertexNormalsCpuJobName(state->Kind),
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = estimatedCost,
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunVertexNormalsCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        return ValidateVertexNormalsCpuJobApply(
                            context,
                            *state);
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorJobResult>() == nullptr)
                            return false;
                        return PublishVertexNormalsCpuJob(context, *state)
                            .has_value();
                    },
            };
        }

        [[nodiscard]] EditorMeshVertexNormalsResult
        SubmitMeshVertexNormalsCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorMeshVertexNormalsCommand& command,
            Geometry::HalfedgeMesh::Mesh mesh,
            VertexNormalsSourceState sourceState,
            VertexNormalPropertyState beforeNormal,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state =
                std::make_shared<EditorVertexNormalsCpuJobState>();
            state->Kind = EditorVertexNormalsCpuJobKind::Mesh;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(mesh);
            state->SourceState =
                std::make_shared<VertexNormalsSourceState>(
                    std::move(sourceState));
            state->BeforeNormal =
                std::make_shared<VertexNormalPropertyState>(
                    std::move(beforeNormal));
            state->Mesh = std::move(mesh);
            state->MeshCommand = command;
            state->MeshResult = MakeMeshNormalsResult(
                EditorCommandStatus::NoChange,
                GN::RecomputeStatus::Success,
                command.Weighting,
                Core::ErrorCode::Success,
                {});
            state->MeshResult.VertexSlotCount =
                state->SnapshotPositions.size();

            const EditorJobIdentity identity =
                MakeVertexNormalsCpuJobIdentity(*state);
            JobDesc desc = MakeVertexNormalsCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshVertexNormalsResult pending =
                    MakePendingMeshVertexNormalsResult(
                        command,
                        state->SnapshotPositions.size(),
                        active->Token);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Mesh vertex-normal CPU",
                    *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                return MakeMeshNormalsResult(
                    EditorCommandStatus::GeometryProcessingFailed,
                    GN::RecomputeStatus::InvalidOutputProperty, command.Weighting,
                    Core::ErrorCode::InvalidState,
                    "Mesh vertex-normal CPU job submission was rejected by the runtime job "
                    "lane.");
            }

            return MakePendingMeshVertexNormalsResult(
                command,
                state->SnapshotPositions.size(),
                handle);
        }

        [[nodiscard]] EditorGraphVertexNormalsResult
        SubmitGraphVertexNormalsCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorGraphVertexNormalsCommand& command,
            Geometry::Vertices nodes,
            Geometry::PropertySet edges,
            Geometry::Halfedges halfedges,
            const std::size_t edgeSlotCount,
            VertexNormalsSourceState sourceState,
            VertexNormalPropertyState beforeNormal,
            const std::uint64_t geometryMetadataSignature)
        {
            std::optional<std::vector<glm::vec3>> positions =
                CollectFiniteGeometryPositions(nodes);
            if (!positions.has_value())
            {
                return MakeGraphNormalsResult(
                    EditorCommandStatus::InvalidProcessingParameters,
                    GraphNormals::RecomputeStatus::InvalidPositionProperty,
                    command.OrientTowardFallback, Core::ErrorCode::InvalidArgument,
                    "selected graph requires count-matched finite v:position for normal "
                    "recompute");
            }

            auto state =
                std::make_shared<EditorVertexNormalsCpuJobState>();
            state->Kind = EditorVertexNormalsCpuJobKind::Graph;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(*positions);
            state->SourceState =
                std::make_shared<VertexNormalsSourceState>(
                    std::move(sourceState));
            state->BeforeNormal =
                std::make_shared<VertexNormalPropertyState>(
                    std::move(beforeNormal));
            state->GraphNodes = std::move(nodes);
            state->GraphEdges = std::move(edges);
            state->GraphHalfedges = std::move(halfedges);
            state->GraphEdgeSlotCount = edgeSlotCount;
            state->GraphCommand = command;
            state->GraphResult = MakeGraphNormalsResult(
                EditorCommandStatus::NoChange,
                GraphNormals::RecomputeStatus::Success,
                command.OrientTowardFallback,
                Core::ErrorCode::Success,
                {});
            state->GraphResult.VertexSlotCount =
                state->SnapshotPositions.size();
            state->GraphResult.EdgeSlotCount = edgeSlotCount;

            const EditorJobIdentity identity =
                MakeVertexNormalsCpuJobIdentity(*state);
            JobDesc desc = MakeVertexNormalsCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorGraphVertexNormalsResult pending =
                    MakePendingGraphVertexNormalsResult(
                        command,
                        state->SnapshotPositions.size(),
                        edgeSlotCount,
                        active->Token);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Graph vertex-normal CPU",
                    *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                return MakeGraphNormalsResult(
                    EditorCommandStatus::GeometryProcessingFailed,
                    GraphNormals::RecomputeStatus::InvalidOutputProperty,
                    command.OrientTowardFallback, Core::ErrorCode::InvalidState,
                    "Graph vertex-normal CPU job submission was rejected by the runtime "
                    "job lane.");
            }

            return MakePendingGraphVertexNormalsResult(
                command,
                state->SnapshotPositions.size(),
                edgeSlotCount,
                handle);
        }

        [[nodiscard]] EditorPointCloudVertexNormalsResult
        SubmitPointCloudVertexNormalsCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorPointCloudVertexNormalsCommand& command,
            Geometry::Vertices points,
            VertexNormalsSourceState sourceState,
            VertexNormalPropertyState beforeNormal,
            const std::uint64_t geometryMetadataSignature)
        {
            std::optional<std::vector<glm::vec3>> positions =
                CollectFiniteGeometryPositions(points);
            if (!positions.has_value())
            {
                return MakePointCloudNormalsResult(
                    EditorCommandStatus::InvalidProcessingParameters,
                    PointNormals::RecomputeStatus::InvalidPositionProperty, command,
                    Core::ErrorCode::InvalidArgument,
                    "selected point-cloud requires count-matched finite v:position for "
                    "normal recompute");
            }

            auto state =
                std::make_shared<EditorVertexNormalsCpuJobState>();
            state->Kind = EditorVertexNormalsCpuJobKind::PointCloud;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(*positions);
            state->SourceState =
                std::make_shared<VertexNormalsSourceState>(
                    std::move(sourceState));
            state->BeforeNormal =
                std::make_shared<VertexNormalPropertyState>(
                    std::move(beforeNormal));
            state->PointCloudPoints = std::move(points);
            state->PointCloudCommand = command;
            state->PointCloudResult = MakePointCloudNormalsResult(
                EditorCommandStatus::NoChange,
                PointNormals::RecomputeStatus::Success,
                command,
                Core::ErrorCode::Success,
                {});
            state->PointCloudResult.PointSlotCount =
                state->SnapshotPositions.size();

            const EditorJobIdentity identity =
                MakeVertexNormalsCpuJobIdentity(*state);
            JobDesc desc = MakeVertexNormalsCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorPointCloudVertexNormalsResult pending =
                    MakePendingPointCloudVertexNormalsResult(
                        command,
                        state->SnapshotPositions.size(),
                        active->Token);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Point-cloud vertex-normal CPU",
                    *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                return MakePointCloudNormalsResult(
                    EditorCommandStatus::GeometryProcessingFailed,
                    PointNormals::RecomputeStatus::InvalidOutputProperty, command,
                    Core::ErrorCode::InvalidState,
                    "Point-cloud vertex-normal CPU job submission was rejected by the "
                    "runtime job lane.");
            }

            return MakePendingPointCloudVertexNormalsResult(
                command,
                state->SnapshotPositions.size(),
                handle);
        }

        enum class EditorMeshCpuJobKind : std::uint8_t
        {
            Curvature,
            Denoise,
            Remesh,
            Subdivide,
            Simplify,
        };

        [[nodiscard]] const char* MeshCpuJobName(
            const EditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorMeshCpuJobKind::Curvature:
                return "Sandbox.MeshCurvature.CPU";
            case EditorMeshCpuJobKind::Denoise:
                return "Sandbox.MeshDenoise.CPU";
            case EditorMeshCpuJobKind::Remesh:
                return "Sandbox.MeshRemesh.CPU";
            case EditorMeshCpuJobKind::Subdivide:
                return "Sandbox.MeshSubdivide.CPU";
            case EditorMeshCpuJobKind::Simplify:
                return "Sandbox.MeshSimplify.CPU";
            }
            return "Sandbox.MeshProcessing.CPU";
        }

        [[nodiscard]] const char* MeshCpuJobOutputName(
            const EditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorMeshCpuJobKind::Curvature:
                return "mesh_curvature_properties";
            case EditorMeshCpuJobKind::Denoise:
                return "mesh_denoise_positions";
            case EditorMeshCpuJobKind::Remesh:
                return "mesh_remesh_topology";
            case EditorMeshCpuJobKind::Subdivide:
                return "mesh_subdivide_topology";
            case EditorMeshCpuJobKind::Simplify:
                return "mesh_simplify_topology";
            }
            return "mesh_processing";
        }

        [[nodiscard]] GeometryPresentationSlotSemantic MeshCpuJobOutputSemantic(
            const EditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorMeshCpuJobKind::Curvature:
                return GeometryPresentationSlotSemantic::ScalarField;
            case EditorMeshCpuJobKind::Denoise:
            case EditorMeshCpuJobKind::Remesh:
            case EditorMeshCpuJobKind::Subdivide:
            case EditorMeshCpuJobKind::Simplify:
                return GeometryPresentationSlotSemantic::Displacement;
            }
            return GeometryPresentationSlotSemantic::Displacement;
        }

        // BUG-146 — forward the mesh's corner UVs into a scratch halfedge mesh.
        //
        // A scratch mesh is rebuilt from a triangle soup and starts with no
        // properties at all, and `ApplyMeshTopologyState` publishes its
        // halfedge properties wholesale — so anything not forwarded here is not
        // merely stale afterwards, it is removed from the entity. Vertex
        // numbering survives the round trip; halfedge numbering does not, so
        // corner UVs go through the canonical corner walk rather than a copy.
        //
        // Used by simplify, which preserves rather than resamples (an edge
        // collapse removes corners and the survivors keep their own UVs), and
        // by UV regeneration's before-state, which needs the mesh's existing
        // corner UVs to tell a genuine no-op from a change (BUG-147).
        // Operations that *create* corners (remesh, subdivide) have no source
        // UV for them and must not use this path.
        [[nodiscard]] bool CopyStoredCornerTexcoordsToScratchMesh(
            const GS::ConstSourceView& view,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (view.HalfedgeSource == nullptr || view.VertexSource == nullptr)
                return false;

            const Geometry::PropertySet& halfedgeProperties =
                view.HalfedgeSource->Properties;
            const auto storedCorners = halfedgeProperties.Get<glm::vec2>(
                Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
            if (!storedCorners ||
                storedCorners.Vector().size() != halfedgeProperties.Size())
            {
                return false;
            }
            for (const glm::vec2 uv : storedCorners.Vector())
            {
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                    return false;
            }

            std::vector<std::uint32_t> surfaceIndices{};
            std::vector<std::uint32_t> triangleFaces{};
            std::vector<std::uint32_t> cornerHalfedges{};
            if (BuildMeshSurfaceTriangleCornerTopology(
                    view,
                    surfaceIndices,
                    triangleFaces,
                    cornerHalfedges) != MeshSurfaceTopologyStatus::Success)
            {
                return false;
            }
            if (cornerHalfedges.size() != surfaceIndices.size() ||
                surfaceIndices.size() % 3u != 0u)
            {
                return false;
            }

            // The corner walk and the soup builder fan every face the same way
            // — `(ring[0], ring[i], ring[i+1])`, faces in index order, skipping
            // the same rings — so triangle `t` here is soup face `t`, which
            // `ToHalfedgeMesh` added as scratch face `t`.
            std::vector<Geometry::MeshSoup::PolygonFace> faces{};
            faces.reserve(surfaceIndices.size() / 3u);
            std::vector<glm::vec2> cornerUvs{};
            cornerUvs.reserve(surfaceIndices.size());
            for (std::size_t i = 0u; i + 2u < surfaceIndices.size(); i += 3u)
            {
                faces.push_back(Geometry::MeshSoup::PolygonFace{
                    .Indices = {surfaceIndices[i],
                                surfaceIndices[i + 1u],
                                surfaceIndices[i + 2u]},
                });
                for (std::size_t k = 0u; k < 3u; ++k)
                {
                    const std::uint32_t halfedge = cornerHalfedges[i + k];
                    if (halfedge >= storedCorners.Vector().size())
                        return false;
                    cornerUvs.push_back(storedCorners.Vector()[halfedge]);
                }
            }

            return PublishMeshCornerTexcoords(
                mesh,
                faces,
                view.VertexSource->Properties.Size(),
                cornerUvs);
        }

        // BUG-146 — does this mesh carry UVs a topology edit could destroy?
        // Follows the canonical corner-over-vertex order, and ignores a
        // property whose size does not match its domain, exactly as every
        // reader does.
        [[nodiscard]] bool MeshHasResolvableTexcoords(
            const GS::ConstSourceView& view) noexcept
        {
            if (view.HalfedgeSource != nullptr)
            {
                const Geometry::PropertySet& corners =
                    view.HalfedgeSource->Properties;
                if (const auto uvs = corners.Get<glm::vec2>(
                        Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
                    uvs && uvs.Vector().size() == corners.Size())
                {
                    return true;
                }
            }
            if (view.VertexSource != nullptr)
            {
                const Geometry::PropertySet& vertices =
                    view.VertexSource->Properties;
                if (const auto uvs = vertices.Get<glm::vec2>("v:texcoord");
                    uvs && uvs.Vector().size() == vertices.Size())
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] EditorMeshTexcoordOutcome
        CopyMeshSimplifyAuxiliaryProperties(
            const GS::ConstSourceView& view,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (view.VertexSource == nullptr)
                return EditorMeshTexcoordOutcome::None;

            const bool hadTexcoords = MeshHasResolvableTexcoords(view);
            bool carried = CopyStoredCornerTexcoordsToScratchMesh(view, mesh);

            const auto sourceTexcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (sourceTexcoords &&
                sourceTexcoords.Vector().size() == mesh.VerticesSize())
            {
                auto meshTexcoords = mesh.VertexProperties().GetOrAdd<glm::vec2>(
                    "v:texcoord",
                    glm::vec2{0.0f});
                for (std::size_t i = 0u;
                     i < sourceTexcoords.Vector().size();
                     ++i)
                {
                    meshTexcoords[i] = sourceTexcoords.Vector()[i];
                }
                carried = true;
            }

            if (!hadTexcoords)
                return EditorMeshTexcoordOutcome::None;
            return carried
                ? EditorMeshTexcoordOutcome::Preserved
                : EditorMeshTexcoordOutcome::Discarded;
        }

        struct EditorMeshCpuJobState
        {
            EditorMeshCpuJobKind Kind{
                EditorMeshCpuJobKind::Denoise};
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            std::vector<glm::vec3> SnapshotPositions{};
            std::vector<bool> DeletedVertices{};
            // Stored-topology fingerprint taken at submit. `std::nullopt` means
            // the topology could not be read, which no later reading may match.
            // See `MeshTopologyValueSignature` (BUG-138).
            std::optional<std::uint64_t> TopologySignature{};
            Geometry::HalfedgeMesh::Mesh BeforeMesh{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            MeshCurvaturePropertyState CurvatureBefore{};
            MeshCurvaturePropertyState CurvatureAfter{};
            std::vector<glm::vec3> DenoiseAfterPositions{};
            EditorMeshCurvatureCommand CurvatureCommand{};
            EditorMeshDenoiseCommand DenoiseCommand{};
            EditorMeshRemeshCommand RemeshCommand{};
            EditorMeshSubdivideCommand SubdivideCommand{};
            EditorMeshSimplifyCommand SimplifyCommand{};
            EditorMeshCurvatureResult CurvatureResult{};
            EditorMeshDenoiseResult DenoiseResult{};
            EditorMeshRemeshResult RemeshResult{};
            EditorMeshSubdivideResult SubdivideResult{};
            EditorMeshSimplifyResult SimplifyResult{};
            // Last answer this job's `ValidateBeforeApply` gave the drain.
            // `FinalizeUnpublishedOnMainThread` takes no arguments, so the
            // reason a completion was refused has to be recorded where it was
            // decided; without it an unpublished job can only say "did not
            // apply". `Current` means the gate never rejected the result, so
            // the job ended for another reason — cancellation, or a publisher
            // that refused the envelope.
            JobApplyValidation LastApplyValidation{
                JobApplyValidation::Current};
        };

        [[nodiscard]] std::string_view MeshCpuJobUnpublishedReason(
            const JobApplyValidation validation) noexcept
        {
            switch (validation)
            {
            case JobApplyValidation::Cancelled:
                return "the job was cancelled before its result could be "
                       "applied";
            case JobApplyValidation::StaleWorld:
                return "the world it was submitted against is no longer "
                       "active";
            case JobApplyValidation::StaleGeneration:
                return "the mesh changed after the job was queued, so the "
                       "result no longer matches the geometry it was computed "
                       "from";
            case JobApplyValidation::MissingTarget:
                return "the target entity no longer exists";
            case JobApplyValidation::Current:
                break;
            }
            return "it terminated without publishing a result";
        }

        [[nodiscard]] JobApplyValidation ValidateMeshCpuJobApply(
            const EditorGeometryProcessingContext& context,
            const EditorMeshCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh)
                return JobApplyValidation::StaleGeneration;

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                job.GeometryMetadataSignature)
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (view.VertexSource == nullptr)
                return JobApplyValidation::StaleGeneration;

            std::optional<std::vector<glm::vec3>> current =
                CollectFiniteGeometryPositions(view.VertexSource->Properties);
            if (!current.has_value() ||
                !SameGeometryPositions(*current, job.SnapshotPositions))
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (job.Kind == EditorMeshCpuJobKind::Remesh ||
                job.Kind == EditorMeshCpuJobKind::Subdivide ||
                job.Kind == EditorMeshCpuJobKind::Simplify)
            {
                const std::optional<std::uint64_t> current =
                    MeshTopologyValueSignature(view);
                if (!current.has_value() ||
                    !job.TopologySignature.has_value() ||
                    *current != *job.TopologySignature)
                {
                    return JobApplyValidation::StaleGeneration;
                }
            }

            if (job.Kind == EditorMeshCpuJobKind::Curvature)
            {
                GS::MutableSourceView mutableView =
                    GS::BuildMutableView(raw, *entity);
                if (!mutableView.Valid() ||
                    mutableView.VertexSource == nullptr)
                {
                    return JobApplyValidation::StaleGeneration;
                }

                MeshCurvaturePropertyState currentCurvature{};
                std::string diagnostic{};
                if (!CaptureMeshCurvaturePropertyState(
                        mutableView.VertexSource->Properties,
                        job.SnapshotPositions.size(),
                        currentCurvature,
                        diagnostic) ||
                    !SameMeshCurvaturePropertyState(
                        currentCurvature,
                        job.CurvatureBefore))
                {
                    return JobApplyValidation::StaleGeneration;
                }
            }

            return JobApplyValidation::Current;
        }

        void PublishMeshCurvatureResultSink(
            const EditorGeometryProcessingContext& context,
            EditorMeshCurvatureResult result)
        {
            if (context.MethodResultSinks.MeshCurvature)
                context.MethodResultSinks.MeshCurvature(std::move(result));
        }

        void PublishMeshDenoiseResultSink(
            const EditorGeometryProcessingContext& context,
            EditorMeshDenoiseResult result)
        {
            if (context.MethodResultSinks.MeshDenoise)
                context.MethodResultSinks.MeshDenoise(std::move(result));
        }

        void PublishMeshRemeshResultSink(
            const EditorGeometryProcessingContext& context,
            EditorMeshRemeshResult result)
        {
            if (context.MethodResultSinks.MeshRemesh)
                context.MethodResultSinks.MeshRemesh(std::move(result));
        }

        void PublishMeshSubdivideResultSink(
            const EditorGeometryProcessingContext& context,
            EditorMeshSubdivideResult result)
        {
            if (context.MethodResultSinks.MeshSubdivide)
                context.MethodResultSinks.MeshSubdivide(std::move(result));
        }

        void PublishMeshSimplifyResultSink(
            const EditorGeometryProcessingContext& context,
            EditorMeshSimplifyResult result)
        {
            if (context.MethodResultSinks.MeshSimplify)
                context.MethodResultSinks.MeshSimplify(std::move(result));
        }

        [[nodiscard]] JobResultEnvelope RunMeshCurvatureCpuWorker(
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            EditorMeshCurvatureResult& result = state->CurvatureResult;
            result.VertexSlotCount = state->SnapshotPositions.size();

            Curv::CurvatureField curvature =
                Curv::ComputeCurvature(state->Mesh);
            if (!curvature.MeanCurvatureProperty ||
                !curvature.GaussianCurvatureProperty ||
                curvature.MeanCurvatureProperty.Vector().size() !=
                    result.VertexSlotCount ||
                curvature.GaussianCurvatureProperty.Vector().size() !=
                    result.VertexSlotCount)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.Curvature produced missing or count-mismatched "
                                 "scalar properties.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            const std::vector<double>& mean =
                curvature.MeanCurvatureProperty.Vector();
            const std::vector<double>& gaussian =
                curvature.GaussianCurvatureProperty.Vector();
            result.NonFiniteScalarCount =
                CountNonFiniteScalars(
                    std::span<const double>{mean.data(), mean.size()}) +
                CountNonFiniteScalars(
                    std::span<const double>{gaussian.data(), gaussian.size()});
            if (result.NonFiniteScalarCount != 0u)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced non-finite scalar curvature values.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            state->CurvatureAfter = state->CurvatureBefore;
            state->CurvatureAfter.HadMean = true;
            state->CurvatureAfter.Mean = mean;
            state->CurvatureAfter.HadGaussian = true;
            state->CurvatureAfter.Gaussian = gaussian;
            result.ScalarPropertyCount = 2u;
            result.ScalarWrittenCount = mean.size() + gaussian.size();

            if (result.DirectionsRequested &&
                result.DirectionsAvailable)
            {
                if (!curvature.PrincipalDir1Property ||
                    !curvature.PrincipalDir2Property ||
                    curvature.PrincipalDir1Property.Vector().size() !=
                        result.VertexSlotCount ||
                    curvature.PrincipalDir2Property.Vector().size() !=
                        result.VertexSlotCount)
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.Curvature produced missing or "
                                     "count-mismatched principal-direction properties.";
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }

                const std::vector<glm::vec3>& dir1 =
                    curvature.PrincipalDir1Property.Vector();
                const std::vector<glm::vec3>& dir2 =
                    curvature.PrincipalDir2Property.Vector();
                result.NonFiniteDirectionCount =
                    CountNonFiniteVectors(
                        std::span<const glm::vec3>{dir1.data(), dir1.size()}) +
                    CountNonFiniteVectors(
                        std::span<const glm::vec3>{dir2.data(), dir2.size()});
                if (result.NonFiniteDirectionCount != 0u)
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Geometry.Curvature produced non-finite principal directions.";
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }

                state->CurvatureAfter.HadDir1 = true;
                state->CurvatureAfter.Dir1 = dir1;
                state->CurvatureAfter.HadDir2 = true;
                state->CurvatureAfter.Dir2 = dir2;
                result.DirectionPropertyCount = 2u;
                result.DirectionWrittenCount = dir1.size() + dir2.size();
            }

            result.Status = EditorCommandStatus::Applied;
            result.DirectionsPublished =
                result.DirectionPropertyCount == 2u &&
                result.DirectionWrittenCount == result.VertexSlotCount * 2u;
            result.Error = Core::ErrorCode::Success;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Mesh curvature CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope RunMeshDenoiseCpuWorker(
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            Smooth::BilateralDenoiseParams params{};
            params.NormalIterations = state->DenoiseCommand.NormalIterations;
            params.VertexIterations = state->DenoiseCommand.VertexIterations;
            params.SigmaSpatial = state->DenoiseCommand.SigmaSpatial;
            params.SigmaRange = state->DenoiseCommand.SigmaRange;
            params.PreserveBoundary = state->DenoiseCommand.PreserveBoundary;
            params.DegenerateNormalLengthEpsilon =
                state->DenoiseCommand.DegenerateNormalLengthEpsilon;

            const Smooth::BilateralDenoiseResult denoise =
                Smooth::DenoiseBilateral(state->Mesh, params);
            EditorMeshDenoiseResult& result = state->DenoiseResult;
            result.DenoiseStatus = denoise.Status;
            result.Error = ErrorForDenoiseStatus(denoise.Status);
            CopyMeshDenoiseCounters(denoise, result);
            result.VertexSlotCount = state->SnapshotPositions.size();
            result.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(state->DeletedVertices.begin(),
                               state->DeletedVertices.end(),
                               true));
            result.WrittenCount =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;

            if (denoise.Status != Smooth::DenoiseStatus::Success)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Message = "Geometry.Smoothing denoise failed with ";
                result.Message += std::string(Smooth::DebugName(denoise.Status));
                result.Message += ".";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            std::vector<glm::vec3> afterPositions =
                ExtractMeshPositions(state->Mesh);
            if (afterPositions.size() != state->SnapshotPositions.size() ||
                !AllFiniteVec3(std::span<const glm::vec3>{
                    afterPositions.data(),
                    afterPositions.size()}))
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.DenoiseStatus = Smooth::DenoiseStatus::NonFiniteInput;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message       = "Geometry.Smoothing denoise produced invalid or "
                                       "count-mismatched positions.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            std::size_t movedPublishedVertices = 0u;
            for (std::size_t i = 0u; i < afterPositions.size(); ++i)
            {
                if (i < state->DeletedVertices.size() &&
                    state->DeletedVertices[i])
                {
                    afterPositions[i] = state->SnapshotPositions[i];
                    continue;
                }
                if (afterPositions[i] != state->SnapshotPositions[i])
                    ++movedPublishedVertices;
            }
            result.MovedVertexCount = movedPublishedVertices;
            result.Status = movedPublishedVertices == 0u
                ? EditorCommandStatus::NoChange
                : EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            state->DenoiseAfterPositions = std::move(afterPositions);
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Mesh denoise CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope RunMeshRemeshCpuWorker(
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            EditorMeshRemeshResult& result = state->RemeshResult;
            result.InputVertexCount = state->Mesh.VertexCount();
            result.InputFaceCount = state->Mesh.FaceCount();

            std::optional<Geometry::RemeshingOperationResult> remeshResult{};
            if (state->RemeshCommand.Mode == EditorMeshRemeshMode::Uniform)
            {
                Remesh::RemeshingParams params{};
                params.TargetLength = state->RemeshCommand.TargetEdgeLength;
                params.Iterations = state->RemeshCommand.Iterations;
                params.Lambda = state->RemeshCommand.Lambda;
                params.PreserveBoundary =
                    state->RemeshCommand.PreserveBoundary;
                params.ProjectToSurface =
                    state->RemeshCommand.ProjectToSurface;
                params.ReferenceProjectionK =
                    state->RemeshCommand.ReferenceProjectionK;
                params.MaxReferenceProjectionDistance =
                    state->RemeshCommand.MaxReferenceProjectionDistance;
                remeshResult = Remesh::Remesh(state->Mesh, params);
            }
            else
            {
                AdaptiveRemesh::AdaptiveRemeshingParams params{};
                if (state->RemeshCommand.TargetEdgeLength > 0.0)
                {
                    params.MinEdgeLength =
                        state->RemeshCommand.TargetEdgeLength * 0.5;
                    params.MaxEdgeLength =
                        state->RemeshCommand.TargetEdgeLength * 2.0;
                }
                params.CurvatureAdaptation =
                    state->RemeshCommand.CurvatureAdaptation;
                params.Sizing =
                    ToAdaptiveSizingLaw(state->RemeshCommand.SizingLaw);
                params.ApproximationError =
                    state->RemeshCommand.ApproximationError;
                params.Iterations = state->RemeshCommand.Iterations;
                params.Lambda = state->RemeshCommand.Lambda;
                params.PreserveBoundary =
                    state->RemeshCommand.PreserveBoundary;
                params.EnableReferenceProjection =
                    state->RemeshCommand.ProjectToSurface;
                params.ReferenceProjectionK =
                    state->RemeshCommand.ReferenceProjectionK;
                params.MaxReferenceProjectionDistance =
                    state->RemeshCommand.MaxReferenceProjectionDistance;
                remeshResult = AdaptiveRemesh::AdaptiveRemesh(state->Mesh, params);
            }

            if (!remeshResult.has_value())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry remeshing failed for the selected mesh and parameters.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            if (state->Mesh.HasGarbage())
                state->Mesh.GarbageCollection();
            CopyRemeshCounters(*remeshResult, result);
            result.OutputVertexCount = state->Mesh.VertexCount();
            result.OutputFaceCount = state->Mesh.FaceCount();
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Mesh remesh CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope RunMeshSubdivideCpuWorker(
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            EditorMeshSubdivideResult& result =
                state->SubdivideResult;
            result.InputVertexCount = state->Mesh.VertexCount();
            result.InputFaceCount = state->Mesh.FaceCount();

            Geometry::HalfedgeMesh::Mesh output{};
            if (state->SubdivideCommand.Operator ==
                EditorMeshSubdivideOperator::Loop)
            {
                LoopSubdivide::SubdivisionParams params{};
                params.Iterations = state->SubdivideCommand.Iterations;
                params.MaxOutputFaces =
                    state->SubdivideCommand.MaxOutputFaces;
                params.PreserveFeatureEdges =
                    state->SubdivideCommand.PreserveLoopFeatureEdges;
                params.FeatureEdgePropertyName =
                    state->SubdivideCommand.FeatureEdgePropertyName;
                const std::optional<LoopSubdivide::SubdivisionResult>
                    subdivision =
                        LoopSubdivide::Subdivide(state->Mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.Subdivision Loop subdivision failed for the "
                                     "selected mesh and parameters.";
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }
            else if (state->SubdivideCommand.Operator ==
                     EditorMeshSubdivideOperator::CatmullClark)
            {
                CatmullClark::SubdivisionParams params{};
                params.Iterations = state->SubdivideCommand.Iterations;
                const std::optional<CatmullClark::SubdivisionResult>
                    subdivision =
                        CatmullClark::Subdivide(state->Mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.CatmullClark subdivision failed for the "
                                     "selected mesh and parameters.";
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }
            else
            {
                Sqrt3Subdivide::Sqrt3Params params{};
                params.Iterations = state->SubdivideCommand.Iterations;
                params.MaxOutputFaces =
                    state->SubdivideCommand.MaxOutputFaces;
                const std::optional<Sqrt3Subdivide::Sqrt3Result>
                    subdivision =
                        Sqrt3Subdivide::Subdivide(state->Mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.HalfedgeMesh.SubdivisionSqrt3 failed for the "
                                     "selected mesh and parameters.";
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }

            if (output.HasGarbage())
                output.GarbageCollection();
            result.OutputVertexCount = output.VertexCount();
            result.OutputFaceCount = output.FaceCount();
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            state->Mesh = std::move(output);
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Mesh subdivide CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope RunMeshSimplifyCpuWorker(
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            EditorMeshSimplifyResult& result = state->SimplifyResult;
            result.InputVertexCount = state->Mesh.VertexCount();
            result.InputFaceCount = state->Mesh.FaceCount();

            Simpl::Params params{};
            params.Metric =
                state->SimplifyCommand.Metric ==
                        EditorMeshSimplifyMetric::FA_QEM
                    ? Simpl::Metric::FA_QEM
                    : Simpl::Metric::ClassicalQEM;
            params.TargetFaces = state->SimplifyCommand.TargetFaces;
            params.MaxError = state->SimplifyCommand.MaxError > 0.0
                ? state->SimplifyCommand.MaxError
                : 1.0e30;
            params.PreserveBoundary =
                state->SimplifyCommand.PreserveBoundary;
            params.FeatureAngleThresholdDegrees =
                state->SimplifyCommand.FeatureAngleThresholdDegrees;
            params.NormalWeight = state->SimplifyCommand.NormalWeight;
            params.BoundaryWeight = state->SimplifyCommand.BoundaryWeight;
            params.CurvatureWeight = state->SimplifyCommand.CurvatureWeight;
            params.PreserveSharpFeatures =
                state->SimplifyCommand.PreserveSharpFeatures;
            params.PreserveUvSeams =
                state->SimplifyCommand.PreserveUvSeams;

            const std::optional<Simpl::Result> simplification =
                Simpl::Simplify(state->Mesh, params);
            if (!simplification.has_value())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Simplification failed for the selected mesh and parameters.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            if (state->Mesh.HasGarbage())
                state->Mesh.GarbageCollection();
            result.OutputVertexCount = state->Mesh.VertexCount();
            result.OutputFaceCount = state->Mesh.FaceCount();
            result.CollapseCount = simplification->CollapseCount;
            result.MaxCollapseError = simplification->MaxCollapseError;
            result.CollapsesRejectedTopology =
                simplification->CollapsesRejectedTopology;
            result.CollapsesRejectedQuality =
                simplification->CollapsesRejectedQuality;
            result.SharpFeatureVerticesPinned =
                simplification->SharpFeatureVerticesPinned;
            result.SeamVerticesPinned = simplification->SeamVerticesPinned;
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Mesh simplify CPU result ready",
                });
        }

        [[nodiscard]] JobResultEnvelope RunMeshCpuWorker(
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            switch (state->Kind)
            {
            case EditorMeshCpuJobKind::Curvature:
                return RunMeshCurvatureCpuWorker(state);
            case EditorMeshCpuJobKind::Denoise:
                return RunMeshDenoiseCpuWorker(state);
            case EditorMeshCpuJobKind::Remesh:
                return RunMeshRemeshCpuWorker(state);
            case EditorMeshCpuJobKind::Subdivide:
                return RunMeshSubdivideCpuWorker(state);
            case EditorMeshCpuJobKind::Simplify:
                return RunMeshSimplifyCpuWorker(state);
            }
            return JobResultEnvelope{};
        }

        [[nodiscard]] Core::Result PublishMeshDenoiseCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshDenoiseResult result = job.DenoiseResult;
            if (!result.Succeeded())
            {
                PublishMeshDenoiseResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (result.MovedVertexCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshDenoiseNoChangeMessage(result);
                PublishMeshDenoiseResultSink(context, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshDenoisePositions(
                    context,
                    job.StableEntityId,
                    job.SnapshotPositions,
                    job.DenoiseAfterPositions);
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Mesh denoise position publication failed during editor "
                                 "history commit.";
                PublishMeshDenoiseResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshDenoiseSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshDenoiseResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshCurvatureCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshCurvatureResult result = job.CurvatureResult;
            if (!result.Succeeded())
            {
                PublishMeshCurvatureResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            result.ChangedValueCount = CountChangedCurvatureValues(
                job.CurvatureBefore, job.CurvatureAfter);
            if (result.ChangedValueCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshCurvatureNoChangeMessage(result);
                PublishMeshCurvatureResultSink(context, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshCurvatureProperties(
                    context,
                    job.StableEntityId,
                    std::move(job.SnapshotPositions),
                    std::move(job.CurvatureBefore),
                    std::move(job.CurvatureAfter));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Mesh curvature property publication failed during editor "
                                 "history commit.";
                PublishMeshCurvatureResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.DirectionsPublished =
                result.DirectionPropertyCount == 2u &&
                result.DirectionWrittenCount == result.VertexSlotCount * 2u;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshCurvatureSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshCurvatureResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshRemeshCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshRemeshResult result = job.RemeshResult;
            if (!result.Succeeded())
            {
                PublishMeshRemeshResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (SameMeshTopologyAndPositions(job.BeforeMesh, job.Mesh))
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshRemeshNoChangeMessage(result);
                PublishMeshRemeshResultSink(context, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Remesh mesh",
                    job.GeometryMetadataSignature,
                    std::move(job.BeforeMesh),
                    std::move(job.Mesh));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh remesh publication failed during editor history commit.";
                PublishMeshRemeshResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshRemeshSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshRemeshResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshSubdivideCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshSubdivideResult result = job.SubdivideResult;
            if (!result.Succeeded())
            {
                PublishMeshSubdivideResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            // BUG-145: no `NoChange` gate here, unlike remesh and simplify.
            // Every implemented subdivision operator either refines — which
            // always raises the face count — or fails closed, including when
            // `MaxOutputFaces` blocks the first iteration. A changed-count gate
            // would be a branch no input can reach.
            const EditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Subdivide mesh",
                    job.GeometryMetadataSignature,
                    std::move(job.BeforeMesh),
                    std::move(job.Mesh));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh subdivide publication failed during editor history commit.";
                PublishMeshSubdivideResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSubdivideSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshSubdivideResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshSimplifyCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshSimplifyResult result = job.SimplifyResult;
            if (!result.Succeeded())
            {
                PublishMeshSimplifyResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (SameMeshTopologyAndPositions(job.BeforeMesh, job.Mesh))
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshSimplifyNoChangeMessage(result);
                PublishMeshSimplifyResultSink(context, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Simplify mesh",
                    job.GeometryMetadataSignature,
                    std::move(job.BeforeMesh),
                    std::move(job.Mesh));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh simplify publication failed during editor history commit.";
                PublishMeshSimplifyResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSimplifySuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshSimplifyResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            switch (job.Kind)
            {
            case EditorMeshCpuJobKind::Curvature:
                return PublishMeshCurvatureCpuJob(context, job);
            case EditorMeshCpuJobKind::Denoise:
                return PublishMeshDenoiseCpuJob(context, job);
            case EditorMeshCpuJobKind::Remesh:
                return PublishMeshRemeshCpuJob(context, job);
            case EditorMeshCpuJobKind::Subdivide:
                return PublishMeshSubdivideCpuJob(context, job);
            case EditorMeshCpuJobKind::Simplify:
                return PublishMeshSimplifyCpuJob(context, job);
            }
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        // A mesh CPU job that reaches a terminal state without publishing —
        // cancelled, discarded as stale, or dropped — owes the editor exactly
        // one terminal result. Without this the panel keeps the "…CPU job
        // queued" text it was given at submit time and the operation reads as
        // permanently `Pending`, which is indistinguishable from a job that
        // genuinely never ran (BUG-138). Every other queued runtime owner
        // (scene documents, asset import, clustering, point-cloud
        // consolidation) already reconciles through this hook.
        void FinalizeUnpublishedMeshCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            const JobApplyValidation validation = job.LastApplyValidation;
            const EditorCommandStatus status =
                validation == JobApplyValidation::MissingTarget ||
                        validation == JobApplyValidation::StaleGeneration ||
                        validation == JobApplyValidation::StaleWorld
                    ? EditorCommandStatus::StaleEntity
                    : EditorCommandStatus::GeometryProcessingFailed;
            const Core::ErrorCode error =
                status == EditorCommandStatus::StaleEntity
                    ? Core::ErrorCode::InvalidState
                    : Core::ErrorCode::Unknown;

            std::string message{MeshCpuJobName(job.Kind)};
            message += " did not apply: ";
            message += MeshCpuJobUnpublishedReason(validation);
            message += ".";

            switch (job.Kind)
            {
            case EditorMeshCpuJobKind::Curvature:
            {
                EditorMeshCurvatureResult result = job.CurvatureResult;
                result.Status = status;
                result.Error = error;
                result.Message = std::move(message);
                PublishMeshCurvatureResultSink(context, std::move(result));
                return;
            }
            case EditorMeshCpuJobKind::Denoise:
            {
                EditorMeshDenoiseResult result = job.DenoiseResult;
                result.Status = status;
                result.Error = error;
                result.Message = std::move(message);
                PublishMeshDenoiseResultSink(context, std::move(result));
                return;
            }
            case EditorMeshCpuJobKind::Remesh:
            {
                EditorMeshRemeshResult result = job.RemeshResult;
                result.Status = status;
                result.Error = error;
                result.Message = std::move(message);
                PublishMeshRemeshResultSink(context, std::move(result));
                return;
            }
            case EditorMeshCpuJobKind::Subdivide:
            {
                EditorMeshSubdivideResult result = job.SubdivideResult;
                result.Status = status;
                result.Error = error;
                result.Message = std::move(message);
                PublishMeshSubdivideResultSink(context, std::move(result));
                return;
            }
            case EditorMeshCpuJobKind::Simplify:
            {
                EditorMeshSimplifyResult result = job.SimplifyResult;
                result.Status = status;
                result.Error = error;
                result.Message = std::move(message);
                PublishMeshSimplifyResultSink(context, std::move(result));
                return;
            }
            }
        }

        // The retired key carried `SourcePropertyGeneration`; the dedup guard
        // never compared it, and the staleness it stood for is re-checked by
        // `ValidateMeshCpuJobApply` immediately before the apply.
        [[nodiscard]] EditorJobIdentity MakeMeshCpuJobIdentity(
            const EditorMeshCpuJobState& state)
        {
            return EditorJobIdentity{
                .EntityId = state.StableEntityId,
                .Scope = EditorJobScope::MeshSurface,
                .OutputSemantic = MeshCpuJobOutputSemantic(state.Kind),
                .OutputName = MeshCpuJobOutputName(state.Kind),
            };
        }

        [[nodiscard]] JobDesc MakeMeshCpuJobDesc(
            const EditorGeometryProcessingContext& context,
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SnapshotPositions.size(),
                                  state->BeforeMesh.FaceCount()) +
                         1023u) /
                        1024u));
            return JobDesc{
                .DebugName = MeshCpuJobName(state->Kind),
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = estimatedCost,
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunMeshCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        const JobApplyValidation validation =
                            ValidateMeshCpuJobApply(context, *state);
                        state->LastApplyValidation = validation;
                        return validation;
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorJobResult>() == nullptr)
                            return false;
                        return PublishMeshCpuJob(context, *state).has_value();
                    },
                .FinalizeUnpublishedOnMainThread =
                    [context, state]()
                    {
                        FinalizeUnpublishedMeshCpuJob(context, *state);
                    },
            };
        }

        [[nodiscard]] EditorMeshCurvatureResult
        SubmitMeshCurvatureCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorMeshCurvatureCommand& command,
            MeshCurvatureSourceResult source,
            MeshCurvaturePropertyState before,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Curvature;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions =
                std::move(source.SourcePositions);
            state->BeforeMesh = source.Mesh;
            state->Mesh = std::move(source.Mesh);
            state->CurvatureBefore = std::move(before);
            state->CurvatureAfter = state->CurvatureBefore;
            state->CurvatureCommand = command;
            state->CurvatureResult = MakeMeshCurvatureBaseResult(
                command,
                context.MeshCurvatureDirectionsAvailable);
            state->CurvatureResult.VertexSlotCount = source.VertexSlotCount;

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshCurvatureResult pending =
                    MakePendingMeshCurvatureResult(
                        command,
                        context.MeshCurvatureDirectionsAvailable,
                        source.VertexSlotCount,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh curvature CPU", *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshCurvatureResult result =
                    MakeMeshCurvatureBaseResult(
                        command,
                        context.MeshCurvatureDirectionsAvailable);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.VertexSlotCount = source.VertexSlotCount;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message         = "Mesh curvature CPU job submission was rejected by the "
                                         "runtime job lane.";
                return result;
            }

            return MakePendingMeshCurvatureResult(
                command,
                context.MeshCurvatureDirectionsAvailable,
                source.VertexSlotCount,
                handle);
        }

        [[nodiscard]] EditorMeshDenoiseResult SubmitMeshDenoiseCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorMeshDenoiseCommand& command,
            MeshDenoiseSourceResult source,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Denoise;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(source.BeforePositions);
            state->DeletedVertices = source.DeletedVertices;
            state->Mesh = std::move(source.Mesh);
            state->DenoiseCommand = command;
            state->DenoiseResult = MakeMeshDenoiseBaseResult(command);
            state->DenoiseResult.VertexSlotCount =
                state->SnapshotPositions.size();
            state->DenoiseResult.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(state->DeletedVertices.begin(),
                               state->DeletedVertices.end(),
                               true));
            state->DenoiseResult.WrittenCount =
                state->DenoiseResult.VertexSlotCount -
                state->DenoiseResult.SkippedDeletedVertexCount;

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                MeshDenoiseSourceResult pendingSource{};
                pendingSource.BeforePositions = state->SnapshotPositions;
                pendingSource.DeletedVertices = state->DeletedVertices;
                EditorMeshDenoiseResult pending =
                    MakePendingMeshDenoiseResult(
                        command,
                        pendingSource,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh denoise CPU", *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshDenoiseResult result =
                    MakeMeshDenoiseBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh denoise CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            MeshDenoiseSourceResult pendingSource{};
            pendingSource.BeforePositions = state->SnapshotPositions;
            pendingSource.DeletedVertices = state->DeletedVertices;
            return MakePendingMeshDenoiseResult(
                command,
                pendingSource,
                handle);
        }

        [[nodiscard]] EditorMeshRemeshResult SubmitMeshRemeshCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorMeshRemeshCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature,
            const EditorMeshTexcoordOutcome texcoordOutcome)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Remesh;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            // BUG-138: the apply gate must compare the stored topology
            // against the stored topology, so fingerprint it here rather than
            // re-deriving it from `BeforeMesh` later.
            state->TopologySignature =
                context.Scene != nullptr
                    ? StoredMeshTopologySignatureForEntity(
                          context.Scene->Raw(),
                          command.StableEntityId)
                    : std::nullopt;
            state->Mesh = std::move(source.Mesh);
            state->RemeshCommand = command;
            state->RemeshResult = MakeMeshRemeshBaseResult(command);
            state->RemeshResult.TexcoordOutcome = texcoordOutcome;
            state->RemeshResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->RemeshResult.InputFaceCount = state->BeforeMesh.FaceCount();

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshRemeshResult pending =
                    MakePendingMeshRemeshResult(
                        command,
                        state->BeforeMesh,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh remesh CPU", *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshRemeshResult result =
                    MakeMeshRemeshBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh remesh CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingMeshRemeshResult(command, state->BeforeMesh, handle);
        }

        [[nodiscard]] EditorMeshSubdivideResult
        SubmitMeshSubdivideCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorMeshSubdivideCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature,
            const EditorMeshTexcoordOutcome texcoordOutcome)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Subdivide;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            // BUG-138: the apply gate must compare the stored topology
            // against the stored topology, so fingerprint it here rather than
            // re-deriving it from `BeforeMesh` later.
            state->TopologySignature =
                context.Scene != nullptr
                    ? StoredMeshTopologySignatureForEntity(
                          context.Scene->Raw(),
                          command.StableEntityId)
                    : std::nullopt;
            state->Mesh = std::move(source.Mesh);
            state->SubdivideCommand = command;
            state->SubdivideResult = MakeMeshSubdivideBaseResult(command);
            state->SubdivideResult.TexcoordOutcome = texcoordOutcome;
            state->SubdivideResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->SubdivideResult.InputFaceCount =
                state->BeforeMesh.FaceCount();

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshSubdivideResult pending =
                    MakePendingMeshSubdivideResult(
                        command,
                        state->BeforeMesh,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh subdivide CPU", *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshSubdivideResult result =
                    MakeMeshSubdivideBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Mesh subdivide CPU job submission was rejected by the "
                                 "runtime job lane.";
                return result;
            }

            return MakePendingMeshSubdivideResult(
                command,
                state->BeforeMesh,
                handle);
        }

        [[nodiscard]] EditorMeshSimplifyResult SubmitMeshSimplifyCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorMeshSimplifyCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature,
            const EditorMeshTexcoordOutcome texcoordOutcome)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Simplify;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            // BUG-138: the apply gate must compare the stored topology
            // against the stored topology, so fingerprint it here rather than
            // re-deriving it from `BeforeMesh` later.
            state->TopologySignature =
                context.Scene != nullptr
                    ? StoredMeshTopologySignatureForEntity(
                          context.Scene->Raw(),
                          command.StableEntityId)
                    : std::nullopt;
            state->Mesh = std::move(source.Mesh);
            state->SimplifyCommand = command;
            state->SimplifyResult = MakeMeshSimplifyBaseResult(command);
            state->SimplifyResult.TexcoordOutcome = texcoordOutcome;
            state->SimplifyResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->SimplifyResult.InputFaceCount = state->BeforeMesh.FaceCount();

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshSimplifyResult pending =
                    MakePendingMeshSimplifyResult(
                        command,
                        state->BeforeMesh,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh simplify CPU", *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshSimplifyResult result =
                    MakeMeshSimplifyBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Mesh simplify CPU job submission was rejected by the "
                                 "runtime job lane.";
                return result;
            }

            return MakePendingMeshSimplifyResult(
                command,
                state->BeforeMesh,
                handle);
        }

        [[nodiscard]] std::string BuildRegistrationSuccessMessage(
            const EditorRegistrationResult& result)
        {
            std::string message = "ICP registration completed (variant=";
            message += DebugNameForEditorICPVariant(result.Variant);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ", step=";
            message += std::to_string(result.AppliedStep);
            message += "/";
            message += std::to_string(result.TrajectoryLength);
            message += ", converged=";
            message += result.Converged ? "yes" : "no";
            message += ").";
            return message;
        }

        // Compose an entity model matrix (translate * rotate * scale) from its
        // local Transform::Component so ICP can run in world space.
        [[nodiscard]] glm::mat4 ModelMatrixFromTransform(
            const ECSC::Transform::Component& transform) noexcept
        {
            glm::mat4 model = glm::mat4_cast(transform.Rotation);
            model[0] *= transform.Scale.x;
            model[1] *= transform.Scale.y;
            model[2] *= transform.Scale.z;
            model[3] = glm::vec4(transform.Position, 1.0f);
            return model;
        }

        // Decompose a composed model matrix back into a Transform::Component.
        // The ICP delta is rigid, so the scale carried by the source model is
        // preserved; rotation is recovered from the scale-normalized columns.
        void DecomposeModelToTransform(
            const glm::mat4& model,
            ECSC::Transform::Component& out) noexcept
        {
            out.Position = glm::vec3(model[3]);
            const glm::vec3 col0(model[0]);
            const glm::vec3 col1(model[1]);
            const glm::vec3 col2(model[2]);
            const glm::vec3 scale(
                glm::length(col0), glm::length(col1), glm::length(col2));
            const glm::mat3 rotation(
                scale.x > 0.0f ? col0 / scale.x : glm::vec3(1.0f, 0.0f, 0.0f),
                scale.y > 0.0f ? col1 / scale.y : glm::vec3(0.0f, 1.0f, 0.0f),
                scale.z > 0.0f ? col2 / scale.z : glm::vec3(0.0f, 0.0f, 1.0f));
            out.Rotation = glm::quat_cast(rotation);
            out.Scale = scale;
        }

        [[nodiscard]] glm::vec3 ComputePointCentroid(
            const std::span<const glm::vec3> points) noexcept
        {
            if (points.empty())
                return glm::vec3(0.0f);

            glm::dvec3 sum(0.0);
            for (const glm::vec3& point : points)
                sum += glm::dvec3(point);
            return glm::vec3(sum / static_cast<double>(points.size()));
        }

        [[nodiscard]] EditorRegistrationResult
        MakeRegistrationBaseResult(
            const EditorRegistrationCommand& command)
        {
            return EditorRegistrationResult{
                .Status = EditorCommandStatus::NoChange,
                .Variant = command.Variant,
                // BUG-096: a result that never reached the solver has run
                // nothing, so the effective variant stays point-to-point until
                // a validated normal span makes it point-to-plane.
                .EffectiveVariant = EditorICPVariant::PointToPoint,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorRegistrationResult
        MakePendingRegistrationResult(
            const EditorRegistrationCommand& command,
            const std::size_t sourcePointCount,
            const std::size_t targetPointCount,
            const JobToken handle)
        {
            EditorRegistrationResult result =
                MakeRegistrationBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.SourcePointCount = sourcePointCount;
            result.TargetPointCount = targetPointCount;
            result.Message = "ICP registration CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] bool SameTransformComponent(
            const ECSC::Transform::Component& lhs,
            const ECSC::Transform::Component& rhs) noexcept
        {
            return lhs.Position.x == rhs.Position.x &&
                   lhs.Position.y == rhs.Position.y &&
                   lhs.Position.z == rhs.Position.z &&
                   lhs.Rotation.w == rhs.Rotation.w &&
                   lhs.Rotation.x == rhs.Rotation.x &&
                   lhs.Rotation.y == rhs.Rotation.y &&
                   lhs.Rotation.z == rhs.Rotation.z &&
                   lhs.Scale.x == rhs.Scale.x &&
                   lhs.Scale.y == rhs.Scale.y &&
                   lhs.Scale.z == rhs.Scale.z;
        }

        struct EditorTransformMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        [[nodiscard]] EditorCommandHistoryResult ExecuteEditorTransformMutation(
            EditorCommandHistory& history,
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const ECSC::Transform::Component& before,
            const ECSC::Transform::Component& after,
            std::string label)
        {
            return Internal::ExecuteUndoableEntityMutation(
                history,
                std::move(label),
                EditorTransformMutationIdentity{
                    .Scene = scene,
                    .World = world,
                    .StableEntityId = stableEntityId,
                },
                before,
                before,
                after,
                [](
                    const EditorTransformMutationIdentity& identity,
                    const ECSC::Transform::Component& expected,
                    const ECSC::Transform::Component&)
                {
                    if (identity.Scene == nullptr || !identity.World.IsValid())
                        return EditorCommandHistoryStatus::MissingScene;

                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(raw, identity.StableEntityId);
                    if (!entity.has_value())
                        return EditorCommandHistoryStatus::StaleEntity;

                    const ECSC::Transform::Component* transform =
                        raw.try_get<ECSC::Transform::Component>(*entity);
                    if (transform == nullptr)
                        return EditorCommandHistoryStatus::MissingTransform;
                    return SameTransformComponent(*transform, expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                },
                [](
                    const EditorTransformMutationIdentity& identity,
                    const ECSC::Transform::Component& target)
                {
                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(raw, identity.StableEntityId);
                    if (!entity.has_value())
                        return EditorCommandHistoryStatus::StaleEntity;

                    ECSC::Transform::Component* transform =
                        raw.try_get<ECSC::Transform::Component>(*entity);
                    if (transform == nullptr)
                        return EditorCommandHistoryStatus::MissingTransform;
                    *transform = target;
                    return EditorCommandHistoryStatus::Applied;
                },
                [](
                    const EditorTransformMutationIdentity& identity,
                    const ECSC::Transform::Component&,
                    const ECSC::Transform::Component& target)
                {
                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(raw, identity.StableEntityId);
                    if (entity.has_value())
                    {
                        raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(
                            *entity);
                    }
                    return target;
                });
        }

        struct EditorRegistrationCpuJobState
        {
            std::uint32_t SourceStableEntityId{0u};
            std::uint32_t TargetStableEntityId{0u};
            std::uint64_t SourceGeometryMetadataSignature{0u};
            std::uint64_t TargetGeometryMetadataSignature{0u};
            EditorRegistrationCommand Command{};
            std::vector<glm::vec3> SourceLocalPoints{};
            std::vector<glm::vec3> TargetLocalPoints{};
            // BUG-096: empty for a point-to-point request. Snapshotted in local
            // space alongside the positions and converted in the worker, so a
            // normal edit between submit and apply is caught by the same
            // staleness comparison the positions get.
            std::vector<glm::vec3> TargetLocalNormals{};
            ECSC::Transform::Component SourceBeforeTransform{};
            bool TargetHadTransform{false};
            ECSC::Transform::Component TargetBeforeTransform{};
            EditorRegistrationResult Result{};
            ECSC::Transform::Component SourceAfterTransform{};
        };

        [[nodiscard]] std::vector<glm::vec3> TransformPointsToWorld(
            const std::vector<glm::vec3>& points,
            const ECSC::Transform::Component& transform)
        {
            const glm::mat4 model = ModelMatrixFromTransform(transform);
            std::vector<glm::vec3> world;
            world.reserve(points.size());
            for (const glm::vec3& point : points)
                world.push_back(glm::vec3(model * glm::vec4(point, 1.0f)));
            return world;
        }

        [[nodiscard]] JobApplyValidation
        ValidateRegistrationCpuJobApply(
            const EditorGeometryProcessingContext& context,
            const EditorRegistrationCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> sourceEntity =
                ResolveStableEntity(raw, job.SourceStableEntityId);
            const std::optional<ECS::EntityHandle> targetEntity =
                ResolveStableEntity(raw, job.TargetStableEntityId);
            if (!sourceEntity.has_value() || !targetEntity.has_value())
                return JobApplyValidation::MissingTarget;

            const GS::ConstSourceView sourceView =
                GS::BuildConstView(raw, *sourceEntity);
            const GS::ConstSourceView targetView =
                GS::BuildConstView(raw, *targetEntity);
            if (GS::BuildSourceAvailability(sourceView).ProvenanceDomain !=
                    GS::Domain::PointCloud ||
                GS::BuildSourceAvailability(targetView).ProvenanceDomain !=
                    GS::Domain::PointCloud ||
                sourceView.VertexSource == nullptr ||
                targetView.VertexSource == nullptr)
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (GeometryMetadataSignatureForEntity(raw, *sourceEntity) !=
                    job.SourceGeometryMetadataSignature ||
                GeometryMetadataSignatureForEntity(raw, *targetEntity) !=
                    job.TargetGeometryMetadataSignature)
            {
                return JobApplyValidation::StaleGeneration;
            }

            const std::optional<std::vector<glm::vec3>> sourcePoints =
                CollectFiniteGeometryPositions(
                    sourceView.VertexSource->Properties);
            const std::optional<std::vector<glm::vec3>> targetPoints =
                CollectFiniteGeometryPositions(
                    targetView.VertexSource->Properties);
            if (!sourcePoints.has_value() || !targetPoints.has_value() ||
                !SameGeometryPositions(*sourcePoints,
                                          job.SourceLocalPoints) ||
                !SameGeometryPositions(*targetPoints,
                                          job.TargetLocalPoints))
            {
                return JobApplyValidation::StaleGeneration;
            }

            // BUG-096: a point-to-plane job that snapshotted target normals is
            // stale the moment those normals change, exactly as it is when the
            // positions change.
            if (!job.TargetLocalNormals.empty())
            {
                std::vector<glm::vec3> currentNormals{};
                if (CollectTargetRegistrationNormals(
                        targetView.VertexSource->Properties,
                        currentNormals) != RegistrationNormalStatus::Ok ||
                    !SameGeometryPositions(currentNormals,
                                           job.TargetLocalNormals))
                {
                    return JobApplyValidation::StaleGeneration;
                }
            }

            const ECSC::Transform::Component* sourceTransform =
                raw.try_get<ECSC::Transform::Component>(*sourceEntity);
            if (sourceTransform == nullptr ||
                !SameTransformComponent(*sourceTransform,
                                        job.SourceBeforeTransform))
            {
                return JobApplyValidation::StaleGeneration;
            }

            const ECSC::Transform::Component* targetTransform =
                raw.try_get<ECSC::Transform::Component>(*targetEntity);
            if (targetTransform == nullptr)
                return job.TargetHadTransform
                    ? JobApplyValidation::StaleGeneration
                    : JobApplyValidation::Current;
            if (!job.TargetHadTransform ||
                !SameTransformComponent(*targetTransform,
                                        job.TargetBeforeTransform))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }

        void PublishRegistrationResultSink(
            const EditorGeometryProcessingContext& context,
            EditorRegistrationResult result)
        {
            if (context.MethodResultSinks.Registration)
                context.MethodResultSinks.Registration(std::move(result));
        }

        [[nodiscard]] JobResultEnvelope RunRegistrationCpuWorker(
            const std::shared_ptr<EditorRegistrationCpuJobState>& state)
        {
            EditorRegistrationResult& result = state->Result;
            result.SourcePointCount = state->SourceLocalPoints.size();
            result.TargetPointCount = state->TargetLocalPoints.size();

            const std::vector<glm::vec3> sourceWorld =
                TransformPointsToWorld(state->SourceLocalPoints,
                                       state->SourceBeforeTransform);
            const std::vector<glm::vec3> targetWorld =
                state->TargetHadTransform
                    ? TransformPointsToWorld(state->TargetLocalPoints,
                                             state->TargetBeforeTransform)
                    : state->TargetLocalPoints;

            const glm::vec3 prealignDelta =
                ComputePointCentroid(std::span<const glm::vec3>(targetWorld)) -
                ComputePointCentroid(std::span<const glm::vec3>(sourceWorld));
            std::vector<glm::vec3> prealignedSourceWorld = sourceWorld;
            for (glm::vec3& point : prealignedSourceWorld)
                point += prealignDelta;
            glm::mat4 prealignPose(1.0f);
            prealignPose[3] = glm::vec4(prealignDelta, 1.0f);

            std::vector<glm::vec3> targetWorldNormals{};
            if (!state->TargetLocalNormals.empty())
            {
                const glm::mat4 targetModel =
                    state->TargetHadTransform
                        ? ModelMatrixFromTransform(state->TargetBeforeTransform)
                        : glm::mat4(1.0f);
                const RegistrationNormalStatus worldStatus =
                    TransformRegistrationNormalsToWorld(
                        state->TargetLocalNormals,
                        targetModel,
                        targetWorldNormals);
                if (worldStatus != RegistrationNormalStatus::Ok)
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        BuildRegistrationNormalRejectionMessage(worldStatus);
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }
            }
            result.TargetNormalCount = targetWorldNormals.size();

            Reg::RegistrationParams params{};
            params.Variant = ToGeometryICPVariant(state->Command.Variant);
            params.MaxIterations = state->Command.MaxIterations;
            params.MaxCorrespondenceDistance =
                state->Command.MaxCorrespondenceDistance > 0.0
                    ? state->Command.MaxCorrespondenceDistance
                    : 1.0e6;
            params.InlierRatio = state->Command.InlierRatio;
            result.EffectiveVariant =
                targetWorldNormals.size() == targetWorld.size() &&
                        !targetWorldNormals.empty()
                    ? EditorICPVariant::PointToPlane
                    : EditorICPVariant::PointToPoint;

            const RegistrationAlignmentOutcome outcome =
                AlignPointClouds(
                    prealignedSourceWorld,
                    targetWorld,
                    targetWorldNormals,
                    params);
            if (!outcome.HasResult)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "ICP rejected the selected point clouds (fewer than 3 "
                                 "points or invalid parameters).";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            result.HasResult = true;
            result.IterationsPerformed = outcome.Result.IterationsPerformed;
            result.TrajectoryLength = outcome.IterationCount();
            result.FinalRMSE = outcome.Result.FinalRMSE;
            result.Converged = outcome.Result.Converged;
            result.FinalInlierCount = outcome.Result.FinalInlierCount;

            const std::size_t step =
                std::min(state->Command.TrajectoryStep,
                         outcome.IterationCount());
            result.AppliedStep = step;
            const glm::mat4 pose =
                step == 0u ? glm::mat4(1.0f)
                           : TrajectoryPose(outcome, step) * prealignPose;

            state->SourceAfterTransform = state->SourceBeforeTransform;
            DecomposeModelToTransform(
                pose * ModelMatrixFromTransform(state->SourceBeforeTransform),
                state->SourceAfterTransform);

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "ICP registration CPU result ready",
                });
        }

        [[nodiscard]] Core::Result PublishRegistrationCpuJob(
            const EditorGeometryProcessingContext& context,
            EditorRegistrationCpuJobState& job)
        {
            EditorRegistrationResult result = job.Result;
            if (!result.Succeeded())
            {
                PublishRegistrationResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "ICP registration requires an attached scene.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> sourceEntity =
                ResolveStableEntity(raw, job.SourceStableEntityId);
            if (!sourceEntity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "ICP registration source entity is stale or no longer live.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            ECSC::Transform::Component* transform =
                raw.try_get<ECSC::Transform::Component>(*sourceEntity);
            if (transform == nullptr)
            {
                result.Status = EditorCommandStatus::MissingTransform;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "ICP registration source entity has no Transform to drive.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            if (context.CommandHistory != nullptr)
            {
                const EditorCommandHistoryResult history =
                    ExecuteEditorTransformMutation(
                        *context.CommandHistory,
                        context.Scene,
                        context.World,
                        job.SourceStableEntityId,
                        job.SourceBeforeTransform,
                        job.SourceAfterTransform,
                        "Align point clouds (ICP)");
                result.Status = ToEditorCommandStatus(history.Status);
            }
            else
            {
                *transform = job.SourceAfterTransform;
                raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(
                    *sourceEntity);
                result.Status = EditorCommandStatus::Applied;
            }

            if (result.Status != EditorCommandStatus::Applied)
            {
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "ICP registration pose failed during editor history commit.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.Error = Core::ErrorCode::Success;
            result.Message = BuildRegistrationSuccessMessage(result);
            PublishRegistrationResultSink(context, result);
            return Core::Ok();
        }

        // The retired key carried the source *and* target geometry signatures;
        // neither was part of the dedup comparison, and both are re-checked by
        // `ValidateRegistrationCpuJobApply` before the apply.
        [[nodiscard]] EditorJobIdentity MakeRegistrationCpuJobIdentity(
            const EditorRegistrationCpuJobState& state)
        {
            return EditorJobIdentity{
                .EntityId = state.SourceStableEntityId,
                .Scope = EditorJobScope::PointCloudPoint,
                .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                .OutputName = "registration_transform",
            };
        }

        [[nodiscard]] JobDesc MakeRegistrationCpuJobDesc(
            const EditorGeometryProcessingContext& context,
            const std::shared_ptr<EditorRegistrationCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SourceLocalPoints.size(),
                                  state->TargetLocalPoints.size()) +
                         1023u) /
                        1024u));
            return JobDesc{
                .DebugName = "Sandbox.RegistrationICP.CPU",
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = estimatedCost,
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunRegistrationCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        return ValidateRegistrationCpuJobApply(context, *state);
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorJobResult>() == nullptr)
                            return false;
                        return PublishRegistrationCpuJob(context, *state)
                            .has_value();
                    },
            };
        }

        [[nodiscard]] EditorRegistrationResult
        SubmitRegistrationCpuJob(
            const EditorGeometryProcessingContext& context,
            const EditorRegistrationCommand& command,
            std::vector<glm::vec3> sourcePoints,
            std::vector<glm::vec3> targetPoints,
            std::vector<glm::vec3> targetNormals,
            const ECSC::Transform::Component& sourceTransform,
            const ECSC::Transform::Component* targetTransform,
            const std::uint64_t sourceGeometryMetadataSignature,
            const std::uint64_t targetGeometryMetadataSignature)
        {
            auto state =
                std::make_shared<EditorRegistrationCpuJobState>();
            state->SourceStableEntityId = command.SourceStableEntityId;
            state->TargetStableEntityId = command.TargetStableEntityId;
            state->SourceGeometryMetadataSignature =
                sourceGeometryMetadataSignature;
            state->TargetGeometryMetadataSignature =
                targetGeometryMetadataSignature;
            state->Command = command;
            state->SourceLocalPoints = std::move(sourcePoints);
            state->TargetLocalPoints = std::move(targetPoints);
            state->TargetLocalNormals = std::move(targetNormals);
            state->SourceBeforeTransform = sourceTransform;
            if (targetTransform != nullptr)
            {
                state->TargetHadTransform = true;
                state->TargetBeforeTransform = *targetTransform;
            }
            state->Result = MakeRegistrationBaseResult(command);
            state->Result.SourcePointCount = state->SourceLocalPoints.size();
            state->Result.TargetPointCount = state->TargetLocalPoints.size();

            const EditorJobIdentity identity =
                MakeRegistrationCpuJobIdentity(*state);
            JobDesc desc = MakeRegistrationCpuJobDesc(context, state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorRegistrationResult pending =
                    MakePendingRegistrationResult(
                        command,
                        state->SourceLocalPoints.size(),
                        state->TargetLocalPoints.size(),
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("ICP registration CPU", *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorRegistrationResult result =
                    MakeRegistrationBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.SourcePointCount = state->SourceLocalPoints.size();
                result.TargetPointCount = state->TargetLocalPoints.size();
                result.Error = Core::ErrorCode::InvalidState;
                result.Message          = "ICP registration CPU job submission was rejected by the "
                                          "runtime job lane.";
                return result;
            }

            return MakePendingRegistrationResult(
                command,
                state->SourceLocalPoints.size(),
                state->TargetLocalPoints.size(),
                handle);
        }
} // namespace

namespace GeometryProcessingDetail
{
    std::optional<ECS::EntityHandle> ResolveEditorStableEntity(
        const entt::registry& raw,
        const std::uint32_t stableId)
    {
        return ResolveStableEntity(raw, stableId);
    }

    std::uint64_t EditorGeometryMetadataSignatureForEntity(
        const entt::registry& raw,
        const ECS::EntityHandle entity)
    {
        return GeometryMetadataSignatureForEntity(raw, entity);
    }

    EditorMeshSourceSnapshot BuildEditorMeshSourceSnapshot(
        const ECS::Components::GeometrySources::ConstSourceView& view)
    {
        MeshDenoiseSourceResult source = BuildHalfedgeMeshForDenoise(view);
        return EditorMeshSourceSnapshot{
            .Mesh = std::move(source.Mesh),
            .BeforePositions = std::move(source.BeforePositions),
            .DeletedVertices = std::move(source.DeletedVertices),
            .SourceFaceForMeshFace =
                std::move(source.SourceFaceForMeshFace),
            .Status = source.Status,
            .Error = source.Error,
            .Diagnostic = std::move(source.Diagnostic),
        };
    }

    std::string BuildActiveEditorGeometryJobMessage(
        const std::string_view label,
        const EditorJobRecord& job)
    {
        return BuildActiveDerivedJobMessage(label, job);
    }

    EditorCommandStatus ToEditorMethodCommandStatus(
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
} // namespace GeometryProcessingDetail

    std::vector<EditorGeometryProcessingMenuItem>
GetEditorGeometryProcessingMenuItems(
        const EditorDomainWindowKind kind)
    {
        using Domain = EditorGeometryProcessingDomain;
        switch (kind)
        {
        case EditorDomainWindowKind::Mesh:
            return {
                {.Domain = Domain::MeshVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true,
                 .HasDenoiseMethod = true,
                 .HasCurvatureMethod = true,
                 .HasRemeshMethod = true,
                 .HasSubdivideMethod = true,
                 .HasSimplifyMethod = true},
                {.Domain = Domain::MeshEdges, .Label = "Edges"},
                {.Domain = Domain::MeshFaces, .Label = "Faces"},
            };
        case EditorDomainWindowKind::Graph:
            return {
                {.Domain = Domain::GraphVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
                {.Domain = Domain::GraphEdges, .Label = "Edges"},
                {.Domain = Domain::GraphHalfedges, .Label = "Halfedges"},
            };
        case EditorDomainWindowKind::PointCloud:
            return {
                {.Domain = Domain::PointCloudPoints,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
            };
        }
        return {};
    }

    EditorGeometryProcessingDomain GetEditorSupportedGeometryProcessingDomains(
        const EditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        using Domain = EditorGeometryProcessingDomain;
        switch (algorithm)
        {
        case EditorGeometryProcessingAlgorithm::KMeans:
            return Domain::MeshVertices |
                   Domain::GraphVertices |
                   Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::MeshDenoise:
        case EditorGeometryProcessingAlgorithm::Curvature:
            return Domain::MeshVertices;
        case EditorGeometryProcessingAlgorithm::Remeshing:
        case EditorGeometryProcessingAlgorithm::Simplification:
        case EditorGeometryProcessingAlgorithm::Smoothing:
        case EditorGeometryProcessingAlgorithm::Subdivision:
        case EditorGeometryProcessingAlgorithm::Repair:
            return kMeshTopologyDomains;
        case EditorGeometryProcessingAlgorithm::NormalEstimation:
            return Domain::MeshVertices |
                   Domain::GraphVertices |
                   Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::ShortestPath:
            return Domain::MeshVertices | Domain::GraphVertices;
        case EditorGeometryProcessingAlgorithm::ConvexHull:
            return Domain::MeshVertices | Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            return Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::VectorHeat:
            return Domain::MeshVertices;
        case EditorGeometryProcessingAlgorithm::Parameterization:
            return Domain::MeshVertices | Domain::MeshFaces;
        case EditorGeometryProcessingAlgorithm::BooleanCSG:
            return Domain::MeshVertices | Domain::MeshFaces;
        case EditorGeometryProcessingAlgorithm::Registration:
        case EditorGeometryProcessingAlgorithm::BilateralFilter:
        case EditorGeometryProcessingAlgorithm::OutlierEstimation:
        case EditorGeometryProcessingAlgorithm::KernelDensity:
        case EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
        case EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return Domain::MeshVertices |
                   Domain::GraphVertices |
                   Domain::PointCloudPoints;
        }
        return Domain::None;
    }

    bool SupportsEditorGeometryProcessingDomain(
        const EditorGeometryProcessingAlgorithm algorithm,
        const EditorGeometryProcessingDomain domain) noexcept
    {
        return HasAnyEditorGeometryProcessingDomain(
      GetEditorSupportedGeometryProcessingDomains(algorithm),
            domain);
    }

    EditorGeometryProcessingCapabilities
GetEditorGeometryProcessingCapabilities(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        EditorGeometryProcessingCapabilities capabilities{};
        const entt::registry& raw = registry.Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return capabilities;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        capabilities.Domains = DomainsForSourceView(view);
        capabilities.HasEditableSurfaceMesh =
            availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshVertex) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshHalfedge) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshFace);
        return capabilities;
    }

    std::vector<EditorGeometryProcessingEntry>
ResolveEditorGeometryProcessingEntries(
        const EditorGeometryProcessingCapabilities capabilities)
    {
        static constexpr std::array<EditorGeometryProcessingAlgorithm, 22>
            kAlgorithmOrder{
                EditorGeometryProcessingAlgorithm::KMeans,
                EditorGeometryProcessingAlgorithm::NormalEstimation,
                EditorGeometryProcessingAlgorithm::MeshDenoise,
                EditorGeometryProcessingAlgorithm::Curvature,
                EditorGeometryProcessingAlgorithm::Registration,
                EditorGeometryProcessingAlgorithm::BilateralFilter,
                EditorGeometryProcessingAlgorithm::OutlierEstimation,
                EditorGeometryProcessingAlgorithm::KernelDensity,
                EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling,
                EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval,
                EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval,
                EditorGeometryProcessingAlgorithm::ShortestPath,
                EditorGeometryProcessingAlgorithm::VectorHeat,
                EditorGeometryProcessingAlgorithm::Parameterization,
                EditorGeometryProcessingAlgorithm::ConvexHull,
                EditorGeometryProcessingAlgorithm::SurfaceReconstruction,
                EditorGeometryProcessingAlgorithm::BooleanCSG,
                EditorGeometryProcessingAlgorithm::Remeshing,
                EditorGeometryProcessingAlgorithm::Simplification,
                EditorGeometryProcessingAlgorithm::Smoothing,
                EditorGeometryProcessingAlgorithm::Subdivision,
                EditorGeometryProcessingAlgorithm::Repair,
            };

        std::vector<EditorGeometryProcessingEntry> entries{};
        entries.reserve(kAlgorithmOrder.size());
        for (const EditorGeometryProcessingAlgorithm algorithm :
             kAlgorithmOrder)
        {
            if (IsSurfaceTopologyAlgorithm(algorithm) &&
                !capabilities.HasEditableSurfaceMesh)
            {
                continue;
            }

            const EditorGeometryProcessingDomain domains =
                capabilities.Domains &
        GetEditorSupportedGeometryProcessingDomains(algorithm);
            if (domains == EditorGeometryProcessingDomain::None)
                continue;

            entries.push_back(EditorGeometryProcessingEntry{
                .Algorithm = algorithm,
                .Domains = domains,
            });
        }
        return entries;
    }

    std::vector<EditorGeometryProcessingEntry>
ResolveEditorGeometryProcessingEntries(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        return ResolveEditorGeometryProcessingEntries(
      GetEditorGeometryProcessingCapabilities(registry, entity));
    }


    const char*DebugNameForEditorGeometryProcessingDomain(
        const EditorGeometryProcessingDomain domain) noexcept
    {
        using Domain = EditorGeometryProcessingDomain;
        switch (domain)
        {
        case Domain::None:
            return "None";
        case Domain::MeshVertices:
            return "Mesh Vertices";
        case Domain::MeshEdges:
            return "Mesh Edges";
        case Domain::MeshHalfedges:
            return "Mesh Halfedges";
        case Domain::MeshFaces:
            return "Mesh Faces";
        case Domain::GraphVertices:
            return "Graph Nodes";
        case Domain::GraphEdges:
            return "Graph Edges";
        case Domain::GraphHalfedges:
            return "Graph Halfedges";
        case Domain::PointCloudPoints:
            return "Point Cloud Points";
        }
        return "Mixed";
    }

    const char*DebugNameForEditorGeometryProcessingAlgorithm(
        const EditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
        case EditorGeometryProcessingAlgorithm::KMeans:
            return "K-Means";
        case EditorGeometryProcessingAlgorithm::MeshDenoise:
            return "Mesh Denoise";
        case EditorGeometryProcessingAlgorithm::Curvature:
            return "Curvature";
        case EditorGeometryProcessingAlgorithm::Remeshing:
            return "Remeshing";
        case EditorGeometryProcessingAlgorithm::Simplification:
            return "Simplification";
        case EditorGeometryProcessingAlgorithm::Smoothing:
            return "Smoothing";
        case EditorGeometryProcessingAlgorithm::Subdivision:
            return "Subdivision";
        case EditorGeometryProcessingAlgorithm::Repair:
            return "Repair";
        case EditorGeometryProcessingAlgorithm::NormalEstimation:
            return "Normals";
        case EditorGeometryProcessingAlgorithm::ShortestPath:
            return "Shortest Path";
        case EditorGeometryProcessingAlgorithm::ConvexHull:
            return "Convex Hull";
        case EditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            return "Surface Reconstruction";
        case EditorGeometryProcessingAlgorithm::VectorHeat:
            return "Vector Heat Method";
        case EditorGeometryProcessingAlgorithm::Parameterization:
            return "Parameterization";
        case EditorGeometryProcessingAlgorithm::BooleanCSG:
            return "Boolean CSG";
        case EditorGeometryProcessingAlgorithm::Registration:
            return "ICP Registration";
        case EditorGeometryProcessingAlgorithm::BilateralFilter:
            return "Bilateral Filter";
        case EditorGeometryProcessingAlgorithm::OutlierEstimation:
            return "Outlier Estimation";
        case EditorGeometryProcessingAlgorithm::KernelDensity:
            return "Kernel Density";
        case EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            return "Statistical Outlier Removal";
        case EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return "Radius Outlier Removal";
        case EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return "Progressive Poisson Sampling";
        }
        return "Unknown";
    }

    const char*DebugNameForEditorMeshCurvatureOutput(
        const EditorMeshCurvatureOutput output) noexcept
    {
        switch (output)
        {
        case EditorMeshCurvatureOutput::All:
            return "All";
        case EditorMeshCurvatureOutput::Mean:
            return "Mean";
        case EditorMeshCurvatureOutput::Gaussian:
            return "Gaussian";
        case EditorMeshCurvatureOutput::PrincipalDirections:
            return "Principal";
        }
        return "Unknown";
    }

    const char*
DebugNameForEditorMeshRemeshMode(
        const EditorMeshRemeshMode mode) noexcept
    {
        switch (mode)
        {
        case EditorMeshRemeshMode::Uniform:
            return "Uniform";
        case EditorMeshRemeshMode::Adaptive:
            return "Adaptive";
        }
        return "Unknown";
    }

    const char*DebugNameForEditorMeshRemeshSizingLaw(
        const EditorMeshRemeshSizingLaw sizingLaw) noexcept
    {
        switch (sizingLaw)
        {
        case EditorMeshRemeshSizingLaw::MeanCurvature:
            return "Mean curvature";
        case EditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
            return "Error-bounded Taubin";
        }
        return "Unknown";
    }

    const char*DebugNameForEditorMeshSubdivideOperator(
        const EditorMeshSubdivideOperator op) noexcept
    {
        switch (op)
        {
        case EditorMeshSubdivideOperator::Loop:
            return "Loop";
        case EditorMeshSubdivideOperator::CatmullClark:
            return "Catmull-Clark";
        case EditorMeshSubdivideOperator::Sqrt3:
            return "Sqrt(3)";
        }
        return "Unknown";
    }

    const char*DebugNameForEditorMeshSimplifyMetric(
        const EditorMeshSimplifyMetric metric) noexcept
    {
        switch (metric)
        {
        case EditorMeshSimplifyMetric::ClassicalQEM:
            return "Classical QEM";
        case EditorMeshSimplifyMetric::FA_QEM:
            return "FA-QEM (feature-aware)";
        }
        return "Unknown";
    }

    const char* DebugNameForEditorMeshTexcoordOutcome(
        const EditorMeshTexcoordOutcome outcome) noexcept
    {
        switch (outcome)
        {
        case EditorMeshTexcoordOutcome::None:
            return "no UVs";
        case EditorMeshTexcoordOutcome::Preserved:
            return "UVs preserved";
        case EditorMeshTexcoordOutcome::Discarded:
            return "UVs discarded";
        }
        return "Unknown";
    }

    const char*
DebugNameForEditorICPVariant(
        const EditorICPVariant variant) noexcept
    {
        switch (variant)
        {
        case EditorICPVariant::PointToPoint:
            return "Point-to-point";
        case EditorICPVariant::PointToPlane:
            return "Point-to-plane";
        }
        return "Unknown";
    }

    struct EditorUvRegenerationSourceSnapshot
    {
        std::vector<glm::vec3> Positions{};
        std::vector<std::uint32_t> EdgeV0{};
        std::vector<std::uint32_t> EdgeV1{};
        std::vector<std::uint32_t> HalfedgeToVertex{};
        std::vector<std::uint32_t> HalfedgeNext{};
        std::vector<std::uint32_t> HalfedgeFace{};
        std::vector<std::uint32_t> FaceHalfedge{};
    };

    [[nodiscard]] std::optional<std::vector<std::uint32_t>> CopyU32Property(
        const Geometry::PropertySet& properties,
        const std::string_view name)
    {
        const auto property = properties.Get<std::uint32_t>(name);
        if (!property || property.Vector().size() != properties.Size())
            return std::nullopt;
        return property.Vector();
    }

    [[nodiscard]] bool CaptureUvRegenerationSourceSnapshot(
        const GS::ConstSourceView& view,
        EditorUvRegenerationSourceSnapshot& out)
    {
        if (view.VertexSource == nullptr ||
            view.EdgeSource == nullptr ||
            view.HalfedgeSource == nullptr ||
            view.FaceSource == nullptr)
        {
            return false;
        }

        std::optional<std::vector<glm::vec3>> positions =
            CollectFiniteGeometryPositions(view.VertexSource->Properties);
        std::optional<std::vector<std::uint32_t>> edgeV0 =
            CopyU32Property(view.EdgeSource->Properties,
                            GS::PropertyNames::kEdgeV0);
        std::optional<std::vector<std::uint32_t>> edgeV1 =
            CopyU32Property(view.EdgeSource->Properties,
                            GS::PropertyNames::kEdgeV1);
        std::optional<std::vector<std::uint32_t>> halfedgeToVertex =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeToVertex);
        std::optional<std::vector<std::uint32_t>> halfedgeNext =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeNext);
        std::optional<std::vector<std::uint32_t>> halfedgeFace =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeFace);
        std::optional<std::vector<std::uint32_t>> faceHalfedge =
            CopyU32Property(view.FaceSource->Properties,
                            GS::PropertyNames::kFaceHalfedge);
        if (!positions.has_value() ||
            !edgeV0.has_value() ||
            !edgeV1.has_value() ||
            !halfedgeToVertex.has_value() ||
            !halfedgeNext.has_value() ||
            !halfedgeFace.has_value() ||
            !faceHalfedge.has_value())
        {
            return false;
        }

        out.Positions = std::move(*positions);
        out.EdgeV0 = std::move(*edgeV0);
        out.EdgeV1 = std::move(*edgeV1);
        out.HalfedgeToVertex = std::move(*halfedgeToVertex);
        out.HalfedgeNext = std::move(*halfedgeNext);
        out.HalfedgeFace = std::move(*halfedgeFace);
        out.FaceHalfedge = std::move(*faceHalfedge);
        return true;
    }

    [[nodiscard]] bool SameUvRegenerationSourceSnapshot(
        const EditorUvRegenerationSourceSnapshot& lhs,
        const EditorUvRegenerationSourceSnapshot& rhs) noexcept
    {
        return SameGeometryPositions(lhs.Positions, rhs.Positions) &&
               lhs.EdgeV0 == rhs.EdgeV0 &&
               lhs.EdgeV1 == rhs.EdgeV1 &&
               lhs.HalfedgeToVertex == rhs.HalfedgeToVertex &&
               lhs.HalfedgeNext == rhs.HalfedgeNext &&
               lhs.HalfedgeFace == rhs.HalfedgeFace &&
               lhs.FaceHalfedge == rhs.FaceHalfedge;
    }

    struct UvMeshKnownPropertyState
    {
        Geometry::PropertySet VertexProperties{};
        Geometry::PropertySet FaceProperties{};
    };

    using UvMeshKnownPropertySnapshot =
        std::shared_ptr<const UvMeshKnownPropertyState>;

    [[nodiscard]] UvMeshKnownPropertySnapshot CaptureUvMeshKnownPropertyState(
        const GS::ConstSourceView& view)
    {
        if (view.VertexSource == nullptr || view.FaceSource == nullptr)
            return nullptr;
        return std::make_shared<UvMeshKnownPropertyState>(
            UvMeshKnownPropertyState{
                .VertexProperties = view.VertexSource->Properties,
                .FaceProperties = view.FaceSource->Properties,
            });
    }

    [[nodiscard]] bool SameUvMeshKnownPropertyState(
        const GS::ConstSourceView& view,
        const UvMeshKnownPropertyState& expected) noexcept
    {
        return view.VertexSource != nullptr &&
               view.FaceSource != nullptr &&
               SameKnownPropertyValues(
                   Geometry::ConstPropertySet(
                       view.VertexSource->Properties),
                   Geometry::ConstPropertySet(
                       expected.VertexProperties)) &&
               SameKnownPropertyValues(
                   Geometry::ConstPropertySet(
                       view.FaceSource->Properties),
                   Geometry::ConstPropertySet(
                       expected.FaceProperties));
    }

    struct UvMeshTopologyMutationGeneration
    {
        std::uint64_t GeometryMetadataSignature{0u};
        EditorUvRegenerationSourceSnapshot Snapshot{};
        UvMeshKnownPropertySnapshot Properties{};
    };

    [[nodiscard]] EditorCommandStatus CommitUvMeshTopologyReplacement(
        const EditorGeometryProcessingContext& context,
        const std::uint32_t stableEntityId,
        const char* label,
        const std::uint64_t expectedGeometryMetadataSignature,
        EditorUvRegenerationSourceSnapshot expectedSnapshot,
        Geometry::HalfedgeMesh::Mesh before,
        Geometry::HalfedgeMesh::Mesh after)
    {
        if (before.HasGarbage())
            before.GarbageCollection();
        if (after.HasGarbage())
            after.GarbageCollection();

        if (context.CommandHistory != nullptr)
        {
            if (context.Scene == nullptr)
            {
                return EditorCommandStatus::MissingScene;
            }
            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandStatus::StaleEntity;
            const UvMeshKnownPropertySnapshot beforeProperties =
                CaptureUvMeshKnownPropertyState(
                    GS::BuildConstView(raw, *entity));
            if (beforeProperties == nullptr)
            {
                return EditorCommandStatus::
                    UnsupportedGeometryDomain;
            }

            const MeshTopologySnapshot beforeState =
                std::make_shared<Geometry::HalfedgeMesh::Mesh>(
                    std::move(before));
            const MeshTopologySnapshot afterState =
                std::make_shared<Geometry::HalfedgeMesh::Mesh>(
                    std::move(after));
            const EditorCommandHistoryResult history =
                Internal::ExecuteUndoableEntityMutation(
                    *context.CommandHistory,
                    label,
                    MeshPropertyMutationIdentity{
                        .Scene = context.Scene,
                        .World = context.World,
                        .StableEntityId = stableEntityId,
                    },
                    UvMeshTopologyMutationGeneration{
                        .GeometryMetadataSignature =
                            expectedGeometryMetadataSignature,
                        .Snapshot = std::move(expectedSnapshot),
                        .Properties = beforeProperties,
                    },
                    beforeState,
                    afterState,
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const UvMeshTopologyMutationGeneration& expected,
                        const MeshTopologySnapshot& target)
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

                        const GS::ConstSourceView view =
                            GS::BuildConstView(raw, *entity);
                        const GS::SourceAvailability availability =
                            GS::BuildSourceAvailability(view);
                        if (availability.ProvenanceDomain != GS::Domain::Mesh)
                        {
                            return EditorCommandHistoryStatus::
                                UnsupportedOperation;
                        }
                        if (expected.Properties == nullptr || target == nullptr)
                        {
                            return EditorCommandHistoryStatus::CommandFailed;
                        }
                        if (GeometryMetadataSignatureForEntity(
                                raw,
                                *entity) !=
                            expected.GeometryMetadataSignature)
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }

                        EditorUvRegenerationSourceSnapshot current{};
                        if (!CaptureUvRegenerationSourceSnapshot(
                                view,
                                current) ||
                            !SameUvRegenerationSourceSnapshot(
                                current,
                                expected.Snapshot) ||
                            !SameUvMeshKnownPropertyState(
                                view,
                                *expected.Properties))
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }
                        return EditorCommandHistoryStatus::Applied;
                    },
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const MeshTopologySnapshot& target)
                    {
                        if (target == nullptr)
                            return EditorCommandHistoryStatus::CommandFailed;
                        return ApplyMeshTopologyState(
                            identity.Scene,
                            identity.StableEntityId,
                            *target);
                    },
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const UvMeshTopologyMutationGeneration&,
                        const MeshTopologySnapshot&)
                    {
                        entt::registry& raw = identity.Scene->Raw();
                        const std::optional<ECS::EntityHandle> entity =
                            ResolveStableEntity(
                                raw,
                                identity.StableEntityId);
                        EditorUvRegenerationSourceSnapshot current{};
                        if (entity.has_value())
                        {
                            MarkMeshTopologyReplacementDirty(raw, *entity);
                            Dirty::MarkGpuDirty(raw, *entity);
                            (void)CaptureUvRegenerationSourceSnapshot(
                                GS::BuildConstView(raw, *entity),
                                current);
                        }
                        const UvMeshKnownPropertySnapshot properties =
                            entity.has_value()
                                ? CaptureUvMeshKnownPropertyState(
                                      GS::BuildConstView(raw, *entity))
                                : nullptr;
                        return UvMeshTopologyMutationGeneration{
                            .GeometryMetadataSignature =
                                entity.has_value()
                                    ? GeometryMetadataSignatureForEntity(
                                          raw,
                                          *entity)
                                    : 0u,
                            .Snapshot = std::move(current),
                            .Properties = properties,
                        };
                    });
            return ToEditorCommandStatus(history.Status);
        }

        const EditorCommandHistoryStatus applied =
            ApplyMeshTopologyState(
                context.Scene,
                stableEntityId,
                after);
        if (applied != EditorCommandHistoryStatus::Applied)
            return ToEditorCommandStatus(applied);
        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, stableEntityId);
        if (entity.has_value())
        {
            MarkMeshTopologyReplacementDirty(raw, *entity);
            Dirty::MarkGpuDirty(raw, *entity);
        }
        return EditorCommandStatus::Applied;
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    MakeUvRegenerationResult(
        const EditorCommandStatus status,
        const Geometry::UvAtlas::UvAtlasStatus uvStatus,
        std::string diagnostic)
    {
        return EditorUvRegenerationCommandResult{
            .Status = status,
            .UvStatus = uvStatus,
            .Diagnostic = std::move(diagnostic),
        };
    }

    void CopyUvAtlasCounters(
        const Geometry::UvAtlas::UvAtlasResult& atlas,
        EditorUvRegenerationCommandResult& result)
    {
        result.UvStatus = atlas.Status;
        result.Provenance = atlas.Provenance;
        result.AtlasWidth = atlas.Diagnostics.AtlasWidth;
        result.AtlasHeight = atlas.Diagnostics.AtlasHeight;
        result.ChartCount = atlas.Diagnostics.ChartCount;
        result.SeamSplitVertexCount =
            atlas.Diagnostics.OutputVertexCount >
                    atlas.Diagnostics.InputVertexCount
                ? atlas.Diagnostics.OutputVertexCount -
                      atlas.Diagnostics.InputVertexCount
                : 0u;
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    MakePendingUvRegenerationResult(
        const JobToken handle)
    {
        EditorUvRegenerationCommandResult result{};
        result.Status = EditorCommandStatus::Pending;
        result.Diagnostic = "UV regeneration CPU job queued";
        AppendDerivedJobHandleToMessage(result.Diagnostic, handle);
        result.Diagnostic += ".";
        return result;
    }

    struct EditorUvRegenerationCpuJobState
    {
        std::uint32_t StableEntityId{0u};
        std::uint64_t GeometryMetadataSignature{0u};
        EditorUvRegenerationSourceSnapshot Snapshot{};
        MeshSoupFromGeometrySourcesResult Soup{};
        Geometry::PropertySet SourceVertexProperties{};
        bool HasSourceVertexProperties{false};
        Geometry::PropertySet SourceFaceProperties{};
        bool HasSourceFaceProperties{false};
        std::vector<glm::vec2> AuthoredTexcoords{};
        Geometry::HalfedgeMesh::Mesh BeforeMesh{};
        Geometry::HalfedgeMesh::Mesh AfterMesh{};
        EditorUvRegenerationCommand Command{};
        EditorUvRegenerationCommandResult Result{};
    };

    [[nodiscard]] JobApplyValidation
    ValidateUvRegenerationCpuJobApply(
        const EditorGeometryProcessingContext& context,
        const EditorUvRegenerationCpuJobState& job)
    {
        if (context.Scene == nullptr)
            return JobApplyValidation::MissingTarget;

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, job.StableEntityId);
        if (!entity.has_value())
            return JobApplyValidation::MissingTarget;

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::Mesh)
            return JobApplyValidation::StaleGeneration;

        if (GeometryMetadataSignatureForEntity(raw, *entity) !=
            job.GeometryMetadataSignature)
        {
            return JobApplyValidation::StaleGeneration;
        }

        EditorUvRegenerationSourceSnapshot current{};
        if (!CaptureUvRegenerationSourceSnapshot(view, current) ||
            !SameUvRegenerationSourceSnapshot(current, job.Snapshot) ||
            view.VertexSource == nullptr ||
            view.FaceSource == nullptr ||
            !SameKnownPropertyValues(
                Geometry::ConstPropertySet(
                    view.VertexSource->Properties),
                job.BeforeMesh.VertexProperties()) ||
            !SameKnownPropertyValues(
                Geometry::ConstPropertySet(
                    view.FaceSource->Properties),
                job.BeforeMesh.FaceProperties()))
        {
            return JobApplyValidation::StaleGeneration;
        }

        return JobApplyValidation::Current;
    }

    void PublishUvRegenerationResultSink(
        const EditorGeometryProcessingContext& context,
        EditorUvRegenerationCommandResult result)
    {
        if (context.MethodResultSinks.UvRegeneration)
            context.MethodResultSinks.UvRegeneration(std::move(result));
    }

    [[nodiscard]] JobResultEnvelope RunUvRegenerationCpuWorker(
        const std::shared_ptr<EditorUvRegenerationCpuJobState>& state)
    {
        Geometry::UvAtlas::UvAtlasOptions options{};
        options.PreserveValidAuthoredUvs =
            state->Command.PreserveValidAuthoredUvs;
        options.ForceRegenerate = state->Command.ForceRegenerate;
        options.Resolution = state->Command.Resolution;
        options.Padding = state->Command.Padding;
        options.TexelsPerUnit = state->Command.TexelsPerUnit;
        options.BackendName = "xatlas";

        Geometry::UvAtlas::UvAtlasInput input{};
        input.Positions = state->Soup.Mesh.Positions();
        input.Faces = state->Soup.Mesh.Faces();
        input.AuthoredTexcoords = state->AuthoredTexcoords;
        input.VertexProperties = state->HasSourceVertexProperties
            ? Geometry::ConstPropertySet(state->SourceVertexProperties)
            : Geometry::ConstPropertySet{};
        input.HasVertexProperties = state->HasSourceVertexProperties;

        Geometry::UvAtlas::UvAtlasResult atlas =
            Geometry::UvAtlas::ResolveUvAtlas(input, options, nullptr);
        CopyUvAtlasCounters(atlas, state->Result);

        if (!atlas.Succeeded())
        {
            const bool backendFailure =
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable ||
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput ||
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendFailed;
            state->Result.Status = backendFailure
                ? EditorCommandStatus::GeometryProcessingFailed
                : EditorCommandStatus::InvalidProcessingParameters;
            state->Result.Diagnostic =
                atlas.Diagnostics.BackendDetail.empty()
                    ? std::string{Geometry::UvAtlas::ToString(atlas.Status)}
                    : atlas.Diagnostics.BackendDetail;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = state->Result.Diagnostic,
                });
        }

        // BUG-147 — build the published mesh from the *source* soup, never
        // from `atlas.OutputMesh`. An unwrapper emits a fresh output vertex per
        // (chart, source vertex) pair, so publishing its output as the entity
        // mesh converts a manifold into a triangle soup: a closed icosahedron
        // used to come back as 60 vertices and 60 edges with one chart per
        // face. The seam is a UV fact, so it is carried on the corner domain
        // and the duplication happens once, at GPU upload.
        auto converted =
            Geometry::Mesh::Conversion::ToHalfedgeMesh(state->Soup.Mesh);
        if (!converted.Succeeded())
        {
            state->Result.Status =
                EditorCommandStatus::GeometryProcessingFailed;
            state->Result.Diagnostic =
                "selected mesh could not be converted back to halfedge topology";
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = state->Result.Diagnostic,
                });
        }

        CopyUvSourcePropertiesToHalfedgeMesh(
            Geometry::ConstPropertySet(state->SourceVertexProperties),
            state->HasSourceVertexProperties,
            Geometry::ConstPropertySet(state->SourceFaceProperties),
            state->HasSourceFaceProperties,
            state->Soup,
            converted.Mesh);

        if (!PublishUvRegenerationTexcoords(atlas, state->Soup, converted.Mesh))
        {
            state->Result.Status =
                EditorCommandStatus::GeometryProcessingFailed;
            state->Result.Diagnostic =
                "generated UVs could not be mapped back onto the selected "
                "mesh's own corners";
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = state->Result.Diagnostic,
                });
        }
        state->AfterMesh = std::move(converted.Mesh);
        state->Result.Status = EditorCommandStatus::Applied;
        state->Result.Diagnostic = atlas.Diagnostics.BackendDetail;
        return JobResultEnvelope::Make<EditorJobResult>(
            EditorJobResult{
                .Diagnostic = "UV regeneration CPU result ready",
            });
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    CommitUvRegenerationCpuJobResult(
        const EditorGeometryProcessingContext& context,
        EditorUvRegenerationCpuJobState& job)
    {
        EditorUvRegenerationCommandResult result = job.Result;
        if (!result.Succeeded())
            return result;

        if (SameUvRegenerationOutput(job.BeforeMesh, job.AfterMesh))
        {
            // The atlas resolved to exactly the UVs and topology already
            // stored, so there is nothing to commit; replacing the mesh with
            // itself would leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Diagnostic =
                "UV regeneration produced the texcoords and topology already "
                "stored (" +
                std::to_string(result.ChartCount) +
                " charts, " + std::to_string(result.SeamSplitVertexCount) +
                " GPU-side seam-split vertices). Nothing was published and no "
                "undo entry was created.";
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitUvMeshTopologyReplacement(
                context,
                job.StableEntityId,
                "Regenerate UVs",
                job.GeometryMetadataSignature,
                std::move(job.Snapshot),
                std::move(job.BeforeMesh),
                std::move(job.AfterMesh));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Diagnostic =
                "UV regeneration publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        InvalidateSelectedModelCache(context);
        return result;
    }

    [[nodiscard]] Core::Result PublishUvRegenerationCpuJob(
        const EditorGeometryProcessingContext& context,
        EditorUvRegenerationCpuJobState& job)
    {
        EditorUvRegenerationCommandResult result =
            CommitUvRegenerationCpuJobResult(context, job);
        const bool succeeded = result.Succeeded();
        PublishUvRegenerationResultSink(context, std::move(result));
        return succeeded ? Core::Ok() : Core::Err(Core::ErrorCode::Unknown);
    }

    // The retired key carried `SourcePropertyGeneration`; the dedup guard never
    // compared it, and the source/metadata staleness it stood for is re-checked
    // by `ValidateUvRegenerationCpuJobApply` immediately before the apply.
    [[nodiscard]] EditorJobIdentity MakeUvRegenerationCpuJobIdentity(
        const EditorUvRegenerationCpuJobState& state)
    {
        return EditorJobIdentity{
            .EntityId = state.StableEntityId,
            .Scope = EditorJobScope::MeshSurface,
            .OutputSemantic = GeometryPresentationSlotSemantic::Albedo,
            .OutputName = std::string{kUvRegenerationJobOutputName},
        };
    }

    [[nodiscard]] JobDesc MakeUvRegenerationCpuJobDesc(
        const EditorGeometryProcessingContext& context,
        const std::shared_ptr<EditorUvRegenerationCpuJobState>& state)
    {
        return JobDesc{
            .DebugName = "Sandbox.UvRegeneration.CPU",
            .Scope = context.World,
            .Priority = Core::Dag::TaskPriority::Normal,
            .Kind = RuntimeTaskKinds::GeometryProcess,
            .EstimatedCost = std::max<std::uint32_t>(
                1u,
                static_cast<std::uint32_t>(
                    (std::max(state->Soup.Mesh.VertexCount(),
                              state->Soup.Mesh.FaceCount()) +
                     1023u) /
                    1024u)),
            .Work =
                [state](const JobCancellation&) -> JobResultEnvelope
                {
                    return RunUvRegenerationCpuWorker(state);
                },
            .ValidateBeforeApply =
                [context, state]()
                {
                    return ValidateUvRegenerationCpuJobApply(
                        context,
                        *state);
                },
            .PublishCompletion =
                [context, state](KernelEventBus&,
                                 const JobResultEnvelope& result) -> bool
                {
                    if (result.TryGet<EditorJobResult>() == nullptr)
                        return false;
                    return PublishUvRegenerationCpuJob(context, *state)
                        .has_value();
                },
        };
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    SubmitUvRegenerationCpuJob(
        const EditorGeometryProcessingContext& context,
        const std::shared_ptr<EditorUvRegenerationCpuJobState>& state)
    {
        const EditorJobIdentity identity =
            MakeUvRegenerationCpuJobIdentity(*state);
        JobDesc desc = MakeUvRegenerationCpuJobDesc(context, state);
        if (const std::optional<EditorJobRecord> active =
                FindActiveEditorJob(context, identity))
        {
            EditorUvRegenerationCommandResult pending =
                MakePendingUvRegenerationResult(active->Token);
            pending.Diagnostic =
                BuildActiveDerivedJobMessage("UV regeneration CPU", *active);
            return pending;
        }

        const JobToken handle = context.JobCommands.Submit(
            std::move(desc),
            identity);
        if (!handle.IsValid())
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::GeometryProcessingFailed,
                Geometry::UvAtlas::UvAtlasStatus::BackendFailed,
                "UV regeneration CPU job submission was rejected by the runtime job "
                "lane.");
        }

        return MakePendingUvRegenerationResult(handle);
    }

    EditorUvRegenerationCommandResult ApplyEditorUvRegenerationCommand(
        const EditorGeometryProcessingContext& context,
        const EditorUvRegenerationCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::MissingScene,
                Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                "Scene registry is unavailable.");
        }
        if (command.Resolution == 0u || command.Padding >= command.Resolution)
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration requires a positive resolution and padding smaller "
                "than the atlas.");
        }
        if (!command.BackendName.empty() && command.BackendName != "xatlas")
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable,
                "Only the promoted xatlas UV backend is available.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::StaleEntity,
                Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                "UV regeneration target entity is stale or no longer live.");
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshSoupFromGeometrySourcesResult soup =
            BuildMeshSoupFromGeometrySources(view);
        if (!soup.Succeeded())
        {
            return MakeUvRegenerationResult(
                soup.Status,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                soup.Diagnostic);
        }

        MeshTopologySourceResult topology =
            BuildHalfedgeMeshForTopologyEdit(view, "UV regeneration");
        if (!topology.Succeeded())
        {
            return MakeUvRegenerationResult(
                topology.Status,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                topology.Diagnostic);
        }

        EditorUvRegenerationSourceSnapshot snapshot{};
        if (!CaptureUvRegenerationSourceSnapshot(view, snapshot))
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration requires count-matched mesh position, edge, "
                "halfedge, and face topology properties.");
        }

        std::vector<glm::vec2> authoredTexcoords;
        if (view.VertexSource != nullptr)
        {
            const auto texcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (texcoords && texcoords.Vector().size() == soup.Mesh.VertexCount())
                authoredTexcoords = texcoords.Vector();
        }

        auto state =
            std::make_shared<EditorUvRegenerationCpuJobState>();
        state->StableEntityId = command.StableEntityId;
        state->GeometryMetadataSignature =
            GeometryMetadataSignatureForEntity(raw, *entity);
        state->Snapshot = std::move(snapshot);
        state->Soup = std::move(soup);
        state->BeforeMesh = std::move(topology.Mesh);
        state->Command = command;
        state->AuthoredTexcoords = std::move(authoredTexcoords);
        if (view.VertexSource != nullptr)
        {
            state->SourceVertexProperties = view.VertexSource->Properties;
            state->HasSourceVertexProperties = true;
        }
        if (view.FaceSource != nullptr)
        {
            state->SourceFaceProperties = view.FaceSource->Properties;
            state->HasSourceFaceProperties = true;
        }
        CopyUvSourcePropertiesToHalfedgeMesh(
            Geometry::ConstPropertySet(state->SourceVertexProperties),
            state->HasSourceVertexProperties,
            Geometry::ConstPropertySet(state->SourceFaceProperties),
            state->HasSourceFaceProperties,
            state->Soup,
            state->BeforeMesh);
        // BUG-147 — the before-state needs the mesh's existing corner UVs too,
        // both so a genuine no-op is recognised as one and so undo restores
        // them. Vertex-domain properties came across above; halfedge numbering
        // does not survive the round trip, so these go through the corner walk.
        (void)CopyStoredCornerTexcoordsToScratchMesh(view, state->BeforeMesh);

        if (context.JobCommands.Available())
            return SubmitUvRegenerationCpuJob(context, state);

        const JobResultEnvelope worker =
            RunUvRegenerationCpuWorker(state);
        if (worker.TryGet<EditorJobResult>() == nullptr)
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::GeometryProcessingFailed,
                Geometry::UvAtlas::UvAtlasStatus::BackendFailed,
                "UV regeneration CPU worker failed.");
        }

        return CommitUvRegenerationCpuJobResult(context, *state);
    }

    EditorMeshDenoiseResult
ApplyEditorMeshDenoiseCommand(
        const EditorGeometryProcessingContext& context,
        const EditorMeshDenoiseCommand& command)
    {
        EditorMeshDenoiseResult result =
            MakeMeshDenoiseBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh denoise.";
            return result;
        }
        if (!context.MeshDenoiseKernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message       = "Geometry.Smoothing mesh denoiser is unavailable in this "
                                   "runtime configuration.";
            return result;
        }

        const bool validStage =
            std::find(kMeshDenoiseStages.begin(),
                      kMeshDenoiseStages.end(),
                      command.Stage) != kMeshDenoiseStages.end();
        if (!validStage ||
            command.NormalIterations == 0u ||
            command.VertexIterations == 0u ||
            !std::isfinite(command.SigmaSpatial) ||
            !std::isfinite(command.SigmaRange) ||
            command.SigmaSpatial < 0.0 ||
            command.SigmaRange < 0.0 ||
            !IsPositiveFinite(command.DegenerateNormalLengthEpsilon))
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message       = "Mesh denoise requires a valid stage, positive iteration "
                                   "counts, non-negative finite sigma values, and a positive "
                                   "finite degeneracy epsilon.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh denoise target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshDenoiseSourceResult source =
            BuildHalfedgeMeshForDenoise(view);
        result.VertexSlotCount = source.BeforePositions.size();
        result.SkippedDeletedVertexCount =
            static_cast<std::size_t>(
                std::count(source.DeletedVertices.begin(),
                           source.DeletedVertices.end(),
                           true));
        result.WrittenCount =
            result.VertexSlotCount - result.SkippedDeletedVertexCount;
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.DenoiseStatus =
                source.Status ==
                        EditorCommandStatus::UnsupportedGeometryDomain
                    ? Smooth::DenoiseStatus::EmptyMesh
                    : Smooth::DenoiseStatus::InvalidParams;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        if (context.JobCommands.Available())
        {
            return SubmitMeshDenoiseCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Smooth::BilateralDenoiseParams params{};
        params.NormalIterations = command.NormalIterations;
        params.VertexIterations = command.VertexIterations;
        params.SigmaSpatial = command.SigmaSpatial;
        params.SigmaRange = command.SigmaRange;
        params.PreserveBoundary = command.PreserveBoundary;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;

        const Smooth::BilateralDenoiseResult denoise =
            Smooth::DenoiseBilateral(source.Mesh, params);
        result.DenoiseStatus = denoise.Status;
        result.Error = ErrorForDenoiseStatus(denoise.Status);
        CopyMeshDenoiseCounters(denoise, result);
        result.VertexSlotCount = source.BeforePositions.size();
        result.WrittenCount =
            result.VertexSlotCount - result.SkippedDeletedVertexCount;

        if (denoise.Status != Smooth::DenoiseStatus::Success)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Message = "Geometry.Smoothing denoise failed with ";
            result.Message += std::string(Smooth::DebugName(denoise.Status));
            result.Message += ".";
            return result;
        }

        std::vector<glm::vec3> afterPositions =
            ExtractMeshPositions(source.Mesh);
        if (afterPositions.size() != source.BeforePositions.size() ||
            !AllFiniteVec3(std::span<const glm::vec3>{
                afterPositions.data(),
                afterPositions.size()}))
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.DenoiseStatus = Smooth::DenoiseStatus::NonFiniteInput;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message       = "Geometry.Smoothing denoise produced invalid or "
                                   "count-mismatched positions.";
            return result;
        }

        std::size_t movedPublishedVertices = 0u;
        for (std::size_t i = 0u; i < afterPositions.size(); ++i)
        {
            if (i < source.DeletedVertices.size() && source.DeletedVertices[i])
            {
                afterPositions[i] = source.BeforePositions[i];
                continue;
            }
            if (afterPositions[i] != source.BeforePositions[i])
                ++movedPublishedVertices;
        }
        result.MovedVertexCount = movedPublishedVertices;

        if (movedPublishedVertices == 0u)
        {
            // Nothing changed, so there is nothing to commit; publishing an
            // identity edit would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshDenoiseNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshDenoisePositions(
                context,
                command.StableEntityId,
                std::move(source.BeforePositions),
                std::move(afterPositions));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message = "Mesh denoise position publication failed during editor "
                             "history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshDenoiseSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshCurvatureResult
ApplyEditorMeshCurvatureCommand(
        const EditorGeometryProcessingContext& context,
        const EditorMeshCurvatureCommand& command)
    {
        EditorMeshCurvatureResult result =
            MakeMeshCurvatureBaseResult(
                command,
                context.MeshCurvatureDirectionsAvailable);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh curvature.";
            return result;
        }
        if (!context.MeshCurvatureKernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Geometry.Curvature mesh curvature is unavailable in this "
                             "runtime configuration.";
            return result;
        }

        const bool validOutput =
            std::find(kMeshCurvatureOutputs.begin(),
                      kMeshCurvatureOutputs.end(),
                      command.Output) != kMeshCurvatureOutputs.end();
        if (!validOutput)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Mesh curvature requires a valid output mode.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh curvature target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView constView = GS::BuildConstView(raw, *entity);
        MeshCurvatureSourceResult source =
            BuildHalfedgeMeshForCurvature(constView);
        result.VertexSlotCount = source.VertexSlotCount;
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        GS::MutableSourceView publishView = GS::BuildMutableView(raw, *entity);
        if (!publishView.Valid() || publishView.VertexSource == nullptr)
        {
            result.Status = EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh curvature target has no writable vertex GeometrySources.";
            return result;
        }

        MeshCurvaturePropertyState before{};
        std::string captureDiagnostic{};
        if (!CaptureMeshCurvaturePropertyState(
                publishView.VertexSource->Properties,
                result.VertexSlotCount,
                before,
                captureDiagnostic))
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message = captureDiagnostic;
            return result;
        }

        if (context.JobCommands.Available())
        {
            return SubmitMeshCurvatureCpuJob(
                context,
                command,
                std::move(source),
                std::move(before),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Curv::CurvatureField curvature = Curv::ComputeCurvature(source.Mesh);
        if (!curvature.MeanCurvatureProperty ||
            !curvature.GaussianCurvatureProperty ||
            curvature.MeanCurvatureProperty.Vector().size() !=
                result.VertexSlotCount ||
            curvature.GaussianCurvatureProperty.Vector().size() !=
                result.VertexSlotCount)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Geometry.Curvature produced missing or count-mismatched "
                             "scalar properties.";
            return result;
        }

        const std::vector<double>& mean =
            curvature.MeanCurvatureProperty.Vector();
        const std::vector<double>& gaussian =
            curvature.GaussianCurvatureProperty.Vector();
        result.NonFiniteScalarCount =
            CountNonFiniteScalars(
                std::span<const double>{mean.data(), mean.size()}) +
            CountNonFiniteScalars(
                std::span<const double>{gaussian.data(), gaussian.size()});
        if (result.NonFiniteScalarCount != 0u)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Curvature produced non-finite scalar curvature values.";
            return result;
        }

        MeshCurvaturePropertyState after = before;
        after.HadMean = true;
        after.Mean = mean;
        after.HadGaussian = true;
        after.Gaussian = gaussian;
        result.ScalarPropertyCount = 2u;
        result.ScalarWrittenCount = mean.size() + gaussian.size();

        if (result.DirectionsRequested &&
            result.DirectionsAvailable)
        {
            if (!curvature.PrincipalDir1Property ||
                !curvature.PrincipalDir2Property ||
                curvature.PrincipalDir1Property.Vector().size() !=
                    result.VertexSlotCount ||
                curvature.PrincipalDir2Property.Vector().size() !=
                    result.VertexSlotCount)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.Curvature produced missing or "
                                 "count-mismatched principal-direction properties.";
                return result;
            }

            const std::vector<glm::vec3>& dir1 =
                curvature.PrincipalDir1Property.Vector();
            const std::vector<glm::vec3>& dir2 =
                curvature.PrincipalDir2Property.Vector();
            result.NonFiniteDirectionCount =
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir1.data(), dir1.size()}) +
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir2.data(), dir2.size()});
            if (result.NonFiniteDirectionCount != 0u)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced non-finite principal directions.";
                return result;
            }

            after.HadDir1 = true;
            after.Dir1 = dir1;
            after.HadDir2 = true;
            after.Dir2 = dir2;
            result.DirectionPropertyCount = 2u;
            result.DirectionWrittenCount = dir1.size() + dir2.size();
        }

        result.ChangedValueCount = CountChangedCurvatureValues(before, after);
        if (result.ChangedValueCount == 0u)
        {
            // Nothing differs, so there is nothing to commit; publishing an
            // identity edit would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshCurvatureNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshCurvatureProperties(
                context,
                command.StableEntityId,
                std::move(source.SourcePositions),
                std::move(before),
                std::move(after));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message = "Mesh curvature property publication failed during editor "
                             "history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.DirectionsPublished =
            result.DirectionPropertyCount == 2u &&
            result.DirectionWrittenCount == result.VertexSlotCount * 2u;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshCurvatureSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshRemeshResult
ApplyEditorMeshRemeshCommand(
        const EditorGeometryProcessingContext& context,
        const EditorMeshRemeshCommand& command)
    {
        EditorMeshRemeshResult result =
            MakeMeshRemeshBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh remesh.";
            return result;
        }
        if (!ValidMeshRemeshMode(command.Mode) ||
            !ValidMeshRemeshSizingLaw(command.SizingLaw) ||
            command.Iterations == 0u ||
            !std::isfinite(command.TargetEdgeLength) ||
            command.TargetEdgeLength < 0.0 ||
            !IsPositiveFinite(command.Lambda) ||
            !std::isfinite(command.CurvatureAdaptation) ||
            command.CurvatureAdaptation < 0.0 ||
            !std::isfinite(command.MaxReferenceProjectionDistance) ||
            command.MaxReferenceProjectionDistance < 0.0 ||
            (command.ProjectToSurface && command.ReferenceProjectionK == 0u) ||
            (command.SizingLaw ==
                 EditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
             !IsPositiveFinite(command.ApproximationError)))
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Mesh remesh requires a valid mode, sizing law, positive "
                             "iteration count, finite non-negative target length, "
                             "positive lambda, and valid projection/sizing parameters.";
            return result;
        }
        if (command.Mode == EditorMeshRemeshMode::Uniform &&
            !context.MeshRemeshUniformKernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Geometry.Remeshing uniform remesher is unavailable in "
                             "this runtime configuration.";
            return result;
        }
        if (command.Mode == EditorMeshRemeshMode::Adaptive &&
            !context.MeshRemeshAdaptiveKernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Geometry.HalfedgeMesh.AdaptiveRemeshing is unavailable "
                             "in this runtime configuration.";
            return result;
        }
        if (command.ProjectToSurface &&
            !context.MeshRemeshProjectToSurfaceAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Mesh remesh project-to-surface is unavailable in this "
                             "runtime configuration.";
            return result;
        }
        if (command.SizingLaw ==
                EditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
            !context.MeshRemeshErrorBoundedSizingAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Mesh remesh error-bounded Taubin sizing is unavailable "
                             "in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh remesh target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh remesh");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        // BUG-146 — remesh and subdivide replace the topology with one whose
        // corners have no source UV, so they cannot carry the parameterization
        // and the publish step removes it. Report that instead of leaving the
        // loss silent; resampling UVs onto a re-tessellated surface is a
        // separate capability, not a side effect of this command.
        const EditorMeshTexcoordOutcome texcoordOutcome =
            MeshHasResolvableTexcoords(view)
                ? EditorMeshTexcoordOutcome::Discarded
                : EditorMeshTexcoordOutcome::None;

        if (context.JobCommands.Available())
        {
            return SubmitMeshRemeshCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity),
                texcoordOutcome);
        }
        result.TexcoordOutcome = texcoordOutcome;

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;

        std::optional<Geometry::RemeshingOperationResult> remeshResult{};
        if (command.Mode == EditorMeshRemeshMode::Uniform)
        {
            Remesh::RemeshingParams params{};
            params.TargetLength = command.TargetEdgeLength;
            params.Iterations = command.Iterations;
            params.Lambda = command.Lambda;
            params.PreserveBoundary = command.PreserveBoundary;
            params.ProjectToSurface = command.ProjectToSurface;
            params.ReferenceProjectionK = command.ReferenceProjectionK;
            params.MaxReferenceProjectionDistance =
                command.MaxReferenceProjectionDistance;
            remeshResult = Remesh::Remesh(source.Mesh, params);
        }
        else
        {
            AdaptiveRemesh::AdaptiveRemeshingParams params{};
            if (command.TargetEdgeLength > 0.0)
            {
                params.MinEdgeLength = command.TargetEdgeLength * 0.5;
                params.MaxEdgeLength = command.TargetEdgeLength * 2.0;
            }
            params.CurvatureAdaptation = command.CurvatureAdaptation;
            params.Sizing = ToAdaptiveSizingLaw(command.SizingLaw);
            params.ApproximationError = command.ApproximationError;
            params.Iterations = command.Iterations;
            params.Lambda = command.Lambda;
            params.PreserveBoundary = command.PreserveBoundary;
            params.EnableReferenceProjection = command.ProjectToSurface;
            params.ReferenceProjectionK = command.ReferenceProjectionK;
            params.MaxReferenceProjectionDistance =
                command.MaxReferenceProjectionDistance;
            remeshResult = AdaptiveRemesh::AdaptiveRemesh(source.Mesh, params);
        }

        if (!remeshResult.has_value())
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry remeshing failed for the selected mesh and parameters.";
            return result;
        }

        if (source.Mesh.HasGarbage())
            source.Mesh.GarbageCollection();
        CopyRemeshCounters(*remeshResult, result);
        result.OutputVertexCount = source.Mesh.VertexCount();
        result.OutputFaceCount = source.Mesh.FaceCount();

        if (SameMeshTopologyAndPositions(before, source.Mesh))
        {
            // Nothing differs, so there is nothing to commit; replacing the
            // mesh with itself would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshRemeshNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Remesh mesh",
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh remesh publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshRemeshSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshSubdivideResult
ApplyEditorMeshSubdivideCommand(
        const EditorGeometryProcessingContext& context,
        const EditorMeshSubdivideCommand& command)
    {
        EditorMeshSubdivideResult result =
            MakeMeshSubdivideBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh subdivide.";
            return result;
        }
        if (!ValidMeshSubdivideOperator(command.Operator) ||
            command.Iterations == 0u ||
            (command.PreserveLoopFeatureEdges &&
             command.FeatureEdgePropertyName.empty()))
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Mesh subdivide requires a valid operator, positive "
                             "iteration count, and a feature-edge property name when "
                             "feature preservation is enabled.";
            return result;
        }
        if (command.Operator == EditorMeshSubdivideOperator::Loop &&
            !context.MeshSubdivideLoopKernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Geometry.Subdivision Loop subdivision is unavailable in "
                             "this runtime configuration.";
            return result;
        }
        if (command.Operator ==
                EditorMeshSubdivideOperator::CatmullClark &&
            !context.MeshSubdivideCatmullClarkKernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Geometry.CatmullClark subdivision is unavailable in this "
                             "runtime configuration.";
            return result;
        }
        if (command.Operator == EditorMeshSubdivideOperator::Sqrt3 &&
            !context.MeshSubdivideSqrt3KernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Geometry.HalfedgeMesh.SubdivisionSqrt3 is unavailable in "
                             "this runtime configuration.";
            return result;
        }
        if (command.PreserveLoopFeatureEdges &&
            command.Operator != EditorMeshSubdivideOperator::Loop)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Loop feature-edge preservation can only be used with the "
                             "Loop subdivision operator.";
            return result;
        }
        if (command.PreserveLoopFeatureEdges &&
            !context.MeshSubdivideLoopFeatureEdgesAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Loop subdivision feature-edge preservation is "
                             "unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh subdivide target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh subdivide");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        // BUG-146 — remesh and subdivide replace the topology with one whose
        // corners have no source UV, so they cannot carry the parameterization
        // and the publish step removes it. Report that instead of leaving the
        // loss silent; resampling UVs onto a re-tessellated surface is a
        // separate capability, not a side effect of this command.
        const EditorMeshTexcoordOutcome texcoordOutcome =
            MeshHasResolvableTexcoords(view)
                ? EditorMeshTexcoordOutcome::Discarded
                : EditorMeshTexcoordOutcome::None;

        if (context.JobCommands.Available())
        {
            return SubmitMeshSubdivideCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity),
                texcoordOutcome);
        }
        result.TexcoordOutcome = texcoordOutcome;

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;
        Geometry::HalfedgeMesh::Mesh output{};

        if (command.Operator == EditorMeshSubdivideOperator::Loop)
        {
            LoopSubdivide::SubdivisionParams params{};
            params.Iterations = command.Iterations;
            params.MaxOutputFaces = command.MaxOutputFaces;
            params.PreserveFeatureEdges = command.PreserveLoopFeatureEdges;
            params.FeatureEdgePropertyName = command.FeatureEdgePropertyName;
            const std::optional<LoopSubdivide::SubdivisionResult>
                subdivision = LoopSubdivide::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.Subdivision Loop subdivision failed for the "
                                 "selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }
        else if (command.Operator ==
                 EditorMeshSubdivideOperator::CatmullClark)
        {
            CatmullClark::SubdivisionParams params{};
            params.Iterations = command.Iterations;
            const std::optional<CatmullClark::SubdivisionResult>
                subdivision = CatmullClark::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.CatmullClark subdivision failed for the "
                                 "selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }
        else
        {
            Sqrt3Subdivide::Sqrt3Params params{};
            params.Iterations = command.Iterations;
            params.MaxOutputFaces = command.MaxOutputFaces;
            const std::optional<Sqrt3Subdivide::Sqrt3Result>
                subdivision = Sqrt3Subdivide::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.HalfedgeMesh.SubdivisionSqrt3 failed for the "
                                 "selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }

        if (output.HasGarbage())
            output.GarbageCollection();
        result.OutputVertexCount = output.VertexCount();
        result.OutputFaceCount = output.FaceCount();

        // BUG-145: see the subdivide job publisher — subdivision cannot both
        // run and leave the mesh unchanged, so there is no gate here.
        const EditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Subdivide mesh",
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(before),
                std::move(output));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh subdivide publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSubdivideSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshSimplifyResult
ApplyEditorMeshSimplifyCommand(
        const EditorGeometryProcessingContext& context,
        const EditorMeshSimplifyCommand& command)
    {
        EditorMeshSimplifyResult result =
            MakeMeshSimplifyBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh simplify.";
            return result;
        }
        const bool hasStopCriterion =
            command.TargetFaces > 0u || command.MaxError > 0.0;
        if (!ValidMeshSimplifyMetric(command.Metric) ||
            !hasStopCriterion ||
            command.NormalWeight < 0.0 ||
            command.BoundaryWeight < 0.0 ||
            command.CurvatureWeight < 0.0 ||
            command.FeatureAngleThresholdDegrees < 0.0 ||
            command.FeatureAngleThresholdDegrees > 180.0)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Mesh simplify requires a valid metric, a positive target "
                             "face count or maximum error, non-negative weights, and a "
                             "feature angle within [0, 180].";
            return result;
        }
        if (!context.MeshSimplifyKernelAvailable)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Simplification is unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh simplify target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh simplify");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        // BuildHalfedgeMeshForTopologyEdit carries only positions + topology, so
        // the scratch halfedge mesh has neither v:texcoord nor h:texcoord even
        // when the selected mesh has them. Copy them in so FA_QEM's
        // PreserveUvSeams can actually see a seam, and -- since the publish step
        // replaces the entity's properties wholesale -- so the mesh keeps the
        // UVs it came in with (BUG-146).
        const EditorMeshTexcoordOutcome texcoordOutcome =
            CopyMeshSimplifyAuxiliaryProperties(view, source.Mesh);

        if (context.JobCommands.Available())
        {
            return SubmitMeshSimplifyCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity),
                texcoordOutcome);
        }
        result.TexcoordOutcome = texcoordOutcome;

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;

        Simpl::Params params{};
        params.Metric =
            command.Metric == EditorMeshSimplifyMetric::FA_QEM
                ? Simpl::Metric::FA_QEM
                : Simpl::Metric::ClassicalQEM;
        params.TargetFaces = command.TargetFaces;
        params.MaxError = command.MaxError > 0.0 ? command.MaxError : 1.0e30;
        params.PreserveBoundary = command.PreserveBoundary;
        params.FeatureAngleThresholdDegrees =
            command.FeatureAngleThresholdDegrees;
        params.NormalWeight = command.NormalWeight;
        params.BoundaryWeight = command.BoundaryWeight;
        params.CurvatureWeight = command.CurvatureWeight;
        params.PreserveSharpFeatures = command.PreserveSharpFeatures;
        params.PreserveUvSeams = command.PreserveUvSeams;

        const std::optional<Simpl::Result> simplification =
            Simpl::Simplify(source.Mesh, params);
        if (!simplification.has_value())
        {
            result.Status =
                EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Simplification failed for the selected mesh and parameters.";
            return result;
        }

        if (source.Mesh.HasGarbage())
            source.Mesh.GarbageCollection();
        result.OutputVertexCount = source.Mesh.VertexCount();
        result.OutputFaceCount = source.Mesh.FaceCount();
        result.CollapseCount = simplification->CollapseCount;
        result.MaxCollapseError = simplification->MaxCollapseError;
        result.CollapsesRejectedTopology =
            simplification->CollapsesRejectedTopology;
        result.CollapsesRejectedQuality =
            simplification->CollapsesRejectedQuality;
        result.SharpFeatureVerticesPinned =
            simplification->SharpFeatureVerticesPinned;
        result.SeamVerticesPinned = simplification->SeamVerticesPinned;

        if (SameMeshTopologyAndPositions(before, source.Mesh))
        {
            // Nothing differs, so there is nothing to commit; replacing the
            // mesh with itself would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSimplifyNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Simplify mesh",
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh simplify publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSimplifySuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshVertexNormalsResult ApplyEditorMeshVertexNormalsCommand(
        const EditorGeometryProcessingContext& context,
        const EditorMeshVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeMeshNormalsResult(
                EditorCommandStatus::MissingScene,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for mesh vertex-normal recompute.");
        }
        if (!std::isfinite(command.DegenerateNormalLengthEpsilon) ||
            command.DegenerateNormalLengthEpsilon <= 0.0)
        {
            return MakeMeshNormalsResult(
                EditorCommandStatus::InvalidProcessingParameters,
                GN::RecomputeStatus::InvalidOutputProperty, command.Weighting,
                Core::ErrorCode::InvalidArgument,
                "Mesh vertex-normal recompute requires a positive finite degeneracy "
                "epsilon.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeMeshNormalsResult(
                EditorCommandStatus::StaleEntity,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                Core::ErrorCode::ResourceNotFound,
                "Mesh vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const MeshForVertexNormalsResult source =
            BuildHalfedgeMeshForVertexNormalRecompute(view);
        if (!source.Succeeded())
        {
            return MakeMeshNormalsResult(
                source.Status,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                source.Error,
                source.Diagnostic);
        }

        const std::uint64_t geometryMetadataSignature =
            GeometryMetadataSignatureForEntity(raw, *entity);
        const GS::ConstSourceView sourceView =
            GS::BuildConstView(raw, *entity);
        const Geometry::PropertySet* normalProperties =
            VertexNormalsSourceProperties(
                sourceView,
                EditorVertexNormalsCpuJobKind::Mesh);
        VertexNormalsSourceState sourceState{};
        VertexNormalPropertyState beforeNormal{};
        if (normalProperties == nullptr ||
            !CaptureVertexNormalsSourceState(
                sourceView,
                EditorVertexNormalsCpuJobKind::Mesh,
                sourceState) ||
            !CaptureVertexNormalPropertyState(
                *normalProperties,
                beforeNormal))
        {
            return MakeMeshNormalsResult(
                EditorCommandStatus::GeometryProcessingFailed,
                GN::RecomputeStatus::PropertyTypeConflict,
                command.Weighting,
                Core::ErrorCode::TypeMismatch,
                "Mesh vertex-normal publication requires an absent or "
                "count-matched vec3 v:normal property.");
        }

        if (context.JobCommands.Available())
        {
            return SubmitMeshVertexNormalsCpuJob(
                context,
                command,
                source.Mesh,
                std::move(sourceState),
                std::move(beforeNormal),
                geometryMetadataSignature);
        }

        Geometry::HalfedgeMesh::Mesh mesh = source.Mesh;
        GN::Params params{};
        params.Weighting = command.Weighting;
        params.OutputProperty = GN::kDefaultOutputProperty;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.SkipDeleted = true;

        const GN::Result normalResult = GN::Recompute(mesh, params);
        EditorMeshVertexNormalsResult result{
            .Status = normalResult.Status == GN::RecomputeStatus::Success
                ? EditorCommandStatus::Applied
                : EditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .Weighting = normalResult.Weighting,
            .Error = normalResult.Status == GN::RecomputeStatus::Success
                ? Core::ErrorCode::Success
                : Core::ErrorCode::Unknown,
        };
        CopyMeshNormalCounters(normalResult, result);
        if (normalResult.Status != GN::RecomputeStatus::Success)
        {
            result.Message = "Geometry.HalfedgeMesh.Vertices.Normals failed with ";
            result.Message += std::string(GN::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        result.ChangedNormalCount = CountChangedVertexNormals(
            beforeNormal.HadNormal,
            beforeNormal.Normals,
            normalResult.Normals.Vector());
        if (result.ChangedNormalCount == 0u)
        {
            // Nothing differs, so there is nothing to commit; publishing an
            // identity edit would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildVertexNormalsNoChangeMessage(
                "Mesh", result.WrittenCount);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitVertexNormalProperty(
                context,
                command.StableEntityId,
                EditorVertexNormalsCpuJobKind::Mesh,
                "Recompute mesh vertex normals",
                geometryMetadataSignature,
                std::move(sourceState),
                std::move(beforeNormal),
                normalResult.Normals.Vector());
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message = "Mesh vertex-normal publication failed during "
                             "editor history commit.";
            return result;
        }

        result.Message = BuildMeshNormalsSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorGraphVertexNormalsResult ApplyEditorGraphVertexNormalsCommand(
        const EditorGeometryProcessingContext& context,
        const EditorGraphVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeGraphNormalsResult(
                EditorCommandStatus::MissingScene,
                GraphNormals::RecomputeStatus::EmptyGraph,
                command.OrientTowardFallback,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for graph vertex-normal recompute.");
        }
        if (!IsPositiveFinite(command.DegenerateNormalLengthEpsilon) ||
            !IsPositiveFinite(command.CollinearEigenvalueRatioEpsilon))
        {
            return MakeGraphNormalsResult(
                EditorCommandStatus::InvalidProcessingParameters,
                GraphNormals::RecomputeStatus::InvalidOutputProperty, command.OrientTowardFallback,
                Core::ErrorCode::InvalidArgument,
                "Graph vertex-normal recompute requires positive finite degeneracy and "
                "collinearity epsilons.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeGraphNormalsResult(
                EditorCommandStatus::StaleEntity,
                GraphNormals::RecomputeStatus::EmptyGraph,
                command.OrientTowardFallback,
                Core::ErrorCode::ResourceNotFound,
                "Graph vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GraphForVertexNormalsResult source =
            BuildGraphConnectivityForVertexNormalRecompute(view);
        if (!source.Succeeded())
        {
            return MakeGraphNormalsResult(
                source.Status,
                GraphNormals::RecomputeStatus::InvalidTopologyProperty,
                command.OrientTowardFallback,
                source.Error,
                source.Diagnostic);
        }

        const std::uint64_t geometryMetadataSignature =
            GeometryMetadataSignatureForEntity(raw, *entity);
        const GS::ConstSourceView sourceView =
            GS::BuildConstView(raw, *entity);
        const Geometry::PropertySet* normalProperties =
            VertexNormalsSourceProperties(
                sourceView,
                EditorVertexNormalsCpuJobKind::Graph);
        VertexNormalsSourceState sourceState{};
        VertexNormalPropertyState beforeNormal{};
        if (normalProperties == nullptr ||
            !CaptureVertexNormalsSourceState(
                sourceView,
                EditorVertexNormalsCpuJobKind::Graph,
                sourceState) ||
            !CaptureVertexNormalPropertyState(
                *normalProperties,
                beforeNormal))
        {
            return MakeGraphNormalsResult(
                EditorCommandStatus::GeometryProcessingFailed,
                GraphNormals::RecomputeStatus::PropertyTypeConflict,
                command.OrientTowardFallback,
                Core::ErrorCode::TypeMismatch,
                "Graph vertex-normal publication requires an absent or "
                "count-matched vec3 v:normal property.");
        }

        if (context.JobCommands.Available())
        {
            return SubmitGraphVertexNormalsCpuJob(
                context,
                command,
                view.VertexSource->Properties,
                view.EdgeSource->Properties,
                source.Halfedges,
                source.EdgeSlotCount,
                std::move(sourceState),
                std::move(beforeNormal),
                geometryMetadataSignature);
        }

        Geometry::Vertices scratchNodes = view.VertexSource->Properties;
        GraphNormals::Params params{};
        params.PositionProperty = GS::PropertyNames::kPosition;
        params.OutputProperty = GraphNormals::kDefaultOutputProperty;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.CollinearEigenvalueRatioEpsilon =
            command.CollinearEigenvalueRatioEpsilon;
        params.SkipDeleted = true;
        params.OrientTowardFallback = command.OrientTowardFallback;

        const GraphNormals::PropertySetResult normalResult =
            GraphNormals::Recompute(
                scratchNodes,
                Geometry::ConstPropertySet(scratchNodes)
                    .Get<glm::vec3>(GS::PropertyNames::kPosition),
                Geometry::ConstPropertySet(source.Halfedges)
                    .Get<Geometry::Graph::HalfedgeConnectivity>(
                        GS::PropertyNames::kHalfedgeConnectivity),
                source.EdgeSlotCount,
                params,
                Geometry::ConstPropertySet(scratchNodes).Get<bool>(
                    "v:deleted"),
                Geometry::ConstPropertySet(view.EdgeSource->Properties)
                    .Get<bool>("e:deleted"));

        EditorGraphVertexNormalsResult result{
            .Status = normalResult.Status ==
                    GraphNormals::RecomputeStatus::Success
                ? EditorCommandStatus::Applied
                : EditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .OrientTowardFallback = command.OrientTowardFallback,
            .Error = ErrorForGraphNormalStatus(normalResult.Status),
        };
        CopyGraphNormalCounters(normalResult.Diagnostics, result);
        if (normalResult.Status != GraphNormals::RecomputeStatus::Success)
        {
            result.Message = "Geometry.Graph.Vertex.Normals failed with ";
            result.Message +=
                std::string(GraphNormals::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!normalResult.Normals.IsValid())
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus =
                GraphNormals::RecomputeStatus::InvalidOutputProperty;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Graph vertex-normal recompute produced no normal property.";
            return result;
        }

        result.ChangedNormalCount = CountChangedVertexNormals(
            beforeNormal.HadNormal,
            beforeNormal.Normals,
            normalResult.Normals.Vector());
        if (result.ChangedNormalCount == 0u)
        {
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildVertexNormalsNoChangeMessage(
                "Graph", result.WrittenCount);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitVertexNormalProperty(
                context,
                command.StableEntityId,
                EditorVertexNormalsCpuJobKind::Graph,
                "Recompute graph vertex normals",
                geometryMetadataSignature,
                std::move(sourceState),
                std::move(beforeNormal),
                normalResult.Normals.Vector());
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message = "Graph vertex-normal publication failed during "
                             "editor history commit.";
            return result;
        }

        result.Message = BuildGraphNormalsSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorPointCloudVertexNormalsResult
ApplyEditorPointCloudVertexNormalsCommand(
        const EditorGeometryProcessingContext& context,
        const EditorPointCloudVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakePointCloudNormalsResult(
                EditorCommandStatus::MissingScene, PointNormals::RecomputeStatus::EmptyInput,
                command, Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for point-cloud vertex-normal "
                "recompute.");
        }
        if (command.KNeighbors == 0u ||
            command.MinimumNeighbors == 0u ||
            !IsPositiveFinite(command.DegenerateNormalLengthEpsilon) ||
            !IsPositiveFinite(command.CollinearEigenvalueRatioEpsilon) ||
            (command.UseRadiusSearch &&
             (!std::isfinite(command.Radius) || command.Radius <= 0.0f)))
        {
            return MakePointCloudNormalsResult(
                EditorCommandStatus::InvalidProcessingParameters,
                PointNormals::RecomputeStatus::InvalidOutputProperty, command,
                Core::ErrorCode::InvalidArgument,
                "Point-cloud vertex-normal recompute requires positive finite "
                "neighborhood settings.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakePointCloudNormalsResult(
                EditorCommandStatus::StaleEntity,
                PointNormals::RecomputeStatus::EmptyInput,
                command,
                Core::ErrorCode::ResourceNotFound,
                "Point-cloud vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
            view.VertexSource == nullptr)
        {
            return MakePointCloudNormalsResult(
                EditorCommandStatus::UnsupportedGeometryDomain,
                PointNormals::RecomputeStatus::EmptyInput, command,
                Core::ErrorCode::InvalidArgument,
                "Point-cloud vertex normals require selected point-cloud "
                "GeometrySources.");
        }

        const std::uint64_t geometryMetadataSignature =
            GeometryMetadataSignatureForEntity(raw, *entity);
        const GS::ConstSourceView sourceView =
            GS::BuildConstView(raw, *entity);
        const Geometry::PropertySet* normalProperties =
            VertexNormalsSourceProperties(
                sourceView,
                EditorVertexNormalsCpuJobKind::PointCloud);
        VertexNormalsSourceState sourceState{};
        VertexNormalPropertyState beforeNormal{};
        if (normalProperties == nullptr ||
            !CaptureVertexNormalsSourceState(
                sourceView,
                EditorVertexNormalsCpuJobKind::PointCloud,
                sourceState) ||
            !CaptureVertexNormalPropertyState(
                *normalProperties,
                beforeNormal))
        {
            return MakePointCloudNormalsResult(
                EditorCommandStatus::GeometryProcessingFailed,
                PointNormals::RecomputeStatus::PropertyTypeConflict,
                command,
                Core::ErrorCode::TypeMismatch,
                "Point-cloud vertex-normal publication requires an absent or "
                "count-matched vec3 v:normal property.");
        }

        if (context.JobCommands.Available())
        {
            return SubmitPointCloudVertexNormalsCpuJob(
                context,
                command,
                view.VertexSource->Properties,
                std::move(sourceState),
                std::move(beforeNormal),
                geometryMetadataSignature);
        }

        Geometry::Vertices scratchPoints = view.VertexSource->Properties;
        Geometry::PointCloud::Cloud scratchCloud{scratchPoints};
        PointNormals::Params params{};
        params.PositionProperty = GS::PropertyNames::kPosition;
        params.OutputProperty = PointNormals::kDefaultOutputProperty;
        params.KNeighbors = command.KNeighbors;
        params.MinimumNeighbors = command.MinimumNeighbors;
        params.UseRadiusSearch = command.UseRadiusSearch;
        params.Radius = command.Radius;
        params.Orientation = command.Orientation;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.CollinearEigenvalueRatioEpsilon =
            command.CollinearEigenvalueRatioEpsilon;
        params.SkipDeleted = true;

        const PointNormals::Result normalResult =
            PointNormals::Recompute(scratchCloud, params);

        EditorPointCloudVertexNormalsResult result{
            .Status = normalResult.Status ==
                    PointNormals::RecomputeStatus::Success
                ? EditorCommandStatus::Applied
                : EditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .Backend = normalResult.Backend,
            .Orientation = command.Orientation,
            .KNeighbors = command.KNeighbors,
            .MinimumNeighbors = command.MinimumNeighbors,
            .UseRadiusSearch = command.UseRadiusSearch,
            .Radius = command.Radius,
            .Error = ErrorForPointCloudNormalStatus(normalResult.Status),
        };
        CopyPointCloudNormalCounters(normalResult.Diagnostics, result);
        if (normalResult.Status != PointNormals::RecomputeStatus::Success)
        {
            result.Message = "Geometry.PointCloud.Normals failed with ";
            result.Message +=
                std::string(PointNormals::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!normalResult.Normals.IsValid())
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus =
                PointNormals::RecomputeStatus::InvalidOutputProperty;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud vertex-normal recompute produced no normal property.";
            return result;
        }

        result.ChangedNormalCount = CountChangedVertexNormals(
            beforeNormal.HadNormal,
            beforeNormal.Normals,
            normalResult.Normals.Vector());
        if (result.ChangedNormalCount == 0u)
        {
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildVertexNormalsNoChangeMessage(
                "Point-cloud", result.WrittenCount);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitVertexNormalProperty(
                context,
                command.StableEntityId,
                EditorVertexNormalsCpuJobKind::PointCloud,
                "Recompute point-cloud vertex normals",
                geometryMetadataSignature,
                std::move(sourceState),
                std::move(beforeNormal),
                normalResult.Normals.Vector());
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message = "Point-cloud vertex-normal publication failed during "
                             "editor history commit.";
            return result;
        }

        result.Message = BuildPointCloudNormalsSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorPointCloudOutlierRemovalResult
ApplyEditorPointCloudOutlierRemovalCommand(
        const EditorGeometryProcessingContext& context,
        const EditorPointCloudOutlierRemovalCommand& command)
    {
        EditorPointCloudOutlierRemovalResult result =
            MakePointCloudOutlierRemovalBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires an attached scene.";
            return result;
        }

        const bool statistical =
            command.Method ==
            EditorPointCloudOutlierMethod::Statistical;
        if (statistical)
        {
            if (command.KNeighbors == 0u ||
                !std::isfinite(command.StdDevMultiplier))
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.GeometryStatus =
                    Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Statistical outlier removal requires KNeighbors > 0 "
                                 "and a finite std-dev multiplier.";
                return result;
            }
        }
        else if (!std::isfinite(command.SearchRadius) ||
                 command.SearchRadius <= 0.0f)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Radius outlier removal requires a positive finite search radius.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Point-cloud outlier-removal target entity is stale or no longer live.";
            return result;
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
            view.VertexSource == nullptr)
        {
            result.Status =
                EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Point-cloud outlier removal requires selected "
                             "point-cloud GeometrySources.";
            return result;
        }
        const PointCloudSourceSnapshot sourceSnapshot =
            CapturePointCloudSourceState(
                GS::BuildConstView(raw, *entity));
        if (sourceSnapshot == nullptr)
        {
            result.Status =
                EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Point-cloud outlier removal requires selected "
                             "point-cloud GeometrySources.";
            return result;
        }
        const std::uint64_t geometryMetadataSignature =
            GeometryMetadataSignatureForEntity(raw, *entity);

        // Snapshot the original point property set (including any deleted slots
        // and the live deletion counter) so undo restores the entity exactly.
        Geometry::Vertices originalPoints = view.VertexSource->Properties;
        std::size_t originalNumDeleted = view.VertexSource->NumDeleted;
        if (!MirrorGeometrySourcePositionsToPointCloudStorage(originalPoints))
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Point-cloud outlier removal requires a count-matched "
                             "v:position property.";
            return result;
        }
        Geometry::PointCloud::Cloud beforeCloud{
            originalPoints,
            originalNumDeleted};

        // Build a separate live-only working cloud: bind the deletion counter and
        // garbage-collect so the GEOM-016 operators (which iterate every slot via
        // VerticesSize()) run over the live points only, and so kept/rejected
        // indices and result counts match the live point set rather than dead
        // slots. The full property set is carried so user attributes survive.
        Geometry::Vertices workPoints = view.VertexSource->Properties;
        std::size_t workNumDeleted = view.VertexSource->NumDeleted;
        if (!MirrorGeometrySourcePositionsToPointCloudStorage(workPoints))
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Point-cloud outlier removal requires a count-matched "
                             "v:position property.";
            return result;
        }
        Geometry::PointCloud::Cloud workCloud{workPoints, workNumDeleted};
        if (workCloud.HasGarbage())
            workCloud.GarbageCollection();

        if (!CollectFiniteGeometryPositions(
                 view.VertexSource->Properties)
                 .has_value())
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Point-cloud outlier removal requires a count-matched "
                             "v:position property.";
            return result;
        }

        if (context.JobCommands.Available())
        {
            return SubmitPointCloudOutlierRemovalCpuJob(
                context,
                command,
                std::move(beforeCloud),
                std::move(workCloud),
                sourceSnapshot,
                geometryMetadataSignature);
        }

        Geometry::PointCloud::OutlierRemovalResult removal{};
        if (statistical)
        {
            Geometry::PointCloud::StatisticalOutlierRemovalParams params{};
            params.KNeighbors = command.KNeighbors;
            params.StdDevMultiplier = command.StdDevMultiplier;
            removal =
                Geometry::PointCloud::RemoveStatisticalOutliers(
                    workCloud,
                    params);
        }
        else
        {
            Geometry::PointCloud::RadiusOutlierRemovalParams params{};
            params.SearchRadius = command.SearchRadius;
            params.MinNeighbors = command.MinNeighbors;
            removal =
                Geometry::PointCloud::RemoveRadiusOutliers(
                    workCloud,
                    params);
        }

        CopyPointCloudOutlierRemovalCounters(removal, result);

        if (removal.Status !=
            Geometry::PointCloud::OutlierRemovalStatus::Success)
        {
            result.Status =
                removal.Status ==
                        Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters
                    ? EditorCommandStatus::InvalidProcessingParameters
                    : EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Geometry.PointCloud outlier removal failed with ";
            result.Message += DebugNameForOutlierRemovalStatus(removal.Status);
            result.Message += ".";
            return result;
        }

        // Compact the full-property working cloud down to the kept points by
        // deleting the rejected slots and garbage-collecting. This preserves
        // every surviving per-point property (normals, K-Means labels,
        // visualization scalars, ...), unlike `removal.Filtered`, which only
        // carries the Cloud built-ins (position/normal/color/radius).
        Geometry::PointCloud::Cloud afterCloud = workCloud;
        for (const std::size_t rejected : removal.RejectedIndices)
            afterCloud.DeletePoint(
                Geometry::VertexHandle{static_cast<std::uint32_t>(rejected)});
        afterCloud.GarbageCollection();

        if (removal.RejectedIndices.empty())
        {
            // Nothing was rejected, so the compacted cloud equals the input;
            // committing it would replace the cloud with itself and leave a
            // useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message =
                BuildPointCloudOutlierRemovalNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus status =
            CommitPointCloudReplacement(
                context,
                command.StableEntityId,
                statistical
                    ? "Remove statistical point-cloud outliers"
                    : "Remove radius point-cloud outliers",
                geometryMetadataSignature,
                sourceSnapshot,
                std::move(beforeCloud),
                std::move(afterCloud));
        result.Status = status;
        if (status != EditorCommandStatus::Applied)
        {
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message = "Point-cloud outlier-removal publication failed; the "
                             "target entity may no longer be live.";
            return result;
        }

        result.Message = BuildPointCloudOutlierRemovalSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorRegistrationResult
ApplyEditorRegistrationCommand(
        const EditorGeometryProcessingContext& context,
        const EditorRegistrationCommand& command)
    {
        EditorRegistrationResult result =
            MakeRegistrationBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "ICP registration requires an attached scene.";
            return result;
        }
        if (command.SourceStableEntityId == command.TargetStableEntityId)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration requires two distinct source and target entities.";
            return result;
        }
        if (!ValidEditorICPVariant(command.Variant) ||
            command.MaxIterations == 0u ||
            !(command.InlierRatio > 0.0 && command.InlierRatio <= 1.0) ||
            !std::isfinite(command.MaxCorrespondenceDistance))
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "ICP registration requires a valid variant, a positive "
                             "iteration count, an inlier ratio in (0, 1], and a finite "
                             "correspondence distance.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> sourceEntity =
            ResolveStableEntity(raw, command.SourceStableEntityId);
        if (!sourceEntity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "ICP registration source entity is stale or no longer live.";
            return result;
        }
        const std::optional<ECS::EntityHandle> targetEntity =
            ResolveStableEntity(raw, command.TargetStableEntityId);
        if (!targetEntity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "ICP registration target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView sourceView =
            GS::BuildConstView(raw, *sourceEntity);
        if (GS::BuildSourceAvailability(sourceView).ProvenanceDomain !=
                GS::Domain::PointCloud ||
            sourceView.VertexSource == nullptr)
        {
            result.Status =
                EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration source must be a point-cloud entity.";
            return result;
        }
        const GS::ConstSourceView targetView =
            GS::BuildConstView(raw, *targetEntity);
        if (GS::BuildSourceAvailability(targetView).ProvenanceDomain !=
                GS::Domain::PointCloud ||
            targetView.VertexSource == nullptr)
        {
            result.Status =
                EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration target must be a point-cloud entity.";
            return result;
        }

        const std::optional<std::vector<glm::vec3>> sourcePoints =
            CollectFiniteGeometryPositions(
                sourceView.VertexSource->Properties);
        const std::optional<std::vector<glm::vec3>> targetPoints =
            CollectFiniteGeometryPositions(
                targetView.VertexSource->Properties);
        if (!sourcePoints.has_value() || !targetPoints.has_value())
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "ICP registration requires both point clouds to expose a "
                             "count-matched, finite v:position property.";
            return result;
        }
        result.SourcePointCount = sourcePoints->size();
        result.TargetPointCount = targetPoints->size();

        // BUG-096: resolve and validate the target normals before anything is
        // dispatched or mutated, so a point-to-plane request with unusable
        // normals fails closed instead of degrading to point-to-point behind a
        // point-to-plane label.
        const bool pointToPlane =
            command.Variant == EditorICPVariant::PointToPlane;
        std::vector<glm::vec3> targetLocalNormals{};
        if (pointToPlane)
        {
            const RegistrationNormalStatus normalStatus =
                CollectTargetRegistrationNormals(
                    targetView.VertexSource->Properties,
                    targetLocalNormals);
            if (normalStatus != RegistrationNormalStatus::Ok)
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    BuildRegistrationNormalRejectionMessage(normalStatus);
                return result;
            }
            if (targetLocalNormals.size() != targetPoints->size())
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = BuildRegistrationNormalRejectionMessage(
                    RegistrationNormalStatus::CountMismatch);
                return result;
            }
        }

        ECSC::Transform::Component* transform =
            raw.try_get<ECSC::Transform::Component>(*sourceEntity);
        if (transform == nullptr)
        {
            result.Status = EditorCommandStatus::MissingTransform;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "ICP registration source entity has no Transform to drive.";
            return result;
        }

        const ECSC::Transform::Component* targetTransform =
            raw.try_get<ECSC::Transform::Component>(*targetEntity);

        if (context.JobCommands.Available())
        {
            return SubmitRegistrationCpuJob(
                context,
                command,
                *sourcePoints,
                *targetPoints,
                targetLocalNormals,
                *transform,
                targetTransform,
                GeometryMetadataSignatureForEntity(raw, *sourceEntity),
                GeometryMetadataSignatureForEntity(raw, *targetEntity));
        }

        // Register in world space: transform each cloud's local v:position by its
        // entity model matrix so a non-identity source/target Transform is
        // respected (identical local clouds with a translated target must still
        // converge onto the target). The ICP delta is composed with the existing
        // source model matrix before being written back as the source Transform.
        const glm::mat4 sourceModel = ModelMatrixFromTransform(*transform);
        glm::mat4 targetModel(1.0f);
        if (targetTransform != nullptr)
            targetModel = ModelMatrixFromTransform(*targetTransform);

        std::vector<glm::vec3> sourceWorld;
        sourceWorld.reserve(sourcePoints->size());
        for (const glm::vec3& p : *sourcePoints)
            sourceWorld.push_back(glm::vec3(sourceModel * glm::vec4(p, 1.0f)));
        std::vector<glm::vec3> targetWorld;
        targetWorld.reserve(targetPoints->size());
        for (const glm::vec3& p : *targetPoints)
            targetWorld.push_back(glm::vec3(targetModel * glm::vec4(p, 1.0f)));

        const glm::vec3 prealignDelta =
            ComputePointCentroid(std::span<const glm::vec3>(targetWorld)) -
            ComputePointCentroid(std::span<const glm::vec3>(sourceWorld));
        std::vector<glm::vec3> prealignedSourceWorld = sourceWorld;
        for (glm::vec3& point : prealignedSourceWorld)
            point += prealignDelta;
        glm::mat4 prealignPose(1.0f);
        prealignPose[3] = glm::vec4(prealignDelta, 1.0f);

        std::vector<glm::vec3> targetWorldNormals{};
        if (pointToPlane)
        {
            const RegistrationNormalStatus worldStatus =
                TransformRegistrationNormalsToWorld(
                    targetLocalNormals, targetModel, targetWorldNormals);
            if (worldStatus != RegistrationNormalStatus::Ok)
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    BuildRegistrationNormalRejectionMessage(worldStatus);
                return result;
            }
        }
        result.TargetNormalCount = targetWorldNormals.size();

        Reg::RegistrationParams params{};
        params.Variant = ToGeometryICPVariant(command.Variant);
        params.MaxIterations = command.MaxIterations;
        params.MaxCorrespondenceDistance =
            command.MaxCorrespondenceDistance > 0.0
                ? command.MaxCorrespondenceDistance
                : 1.0e6;
        params.InlierRatio = command.InlierRatio;
        // The solver degrades to point-to-point when the span is empty, and
        // the span is non-empty here exactly when point-to-plane was requested
        // and validated, so the two agree by construction.
        result.EffectiveVariant =
            pointToPlane && targetWorldNormals.size() == targetWorld.size()
                ? EditorICPVariant::PointToPlane
                : EditorICPVariant::PointToPoint;

        const RegistrationAlignmentOutcome outcome =
            AlignPointClouds(
                prealignedSourceWorld, targetWorld, targetWorldNormals, params);
        if (!outcome.HasResult)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "ICP rejected the selected point clouds (fewer than 3 "
                             "points or invalid parameters).";
            return result;
        }

        result.HasResult = true;
        result.IterationsPerformed = outcome.Result.IterationsPerformed;
        result.TrajectoryLength = outcome.IterationCount();
        result.FinalRMSE = outcome.Result.FinalRMSE;
        result.Converged = outcome.Result.Converged;
        result.FinalInlierCount = outcome.Result.FinalInlierCount;

        const std::size_t step =
            std::min(command.TrajectoryStep, outcome.IterationCount());
        result.AppliedStep = step;
        const glm::mat4 pose =
            step == 0u ? glm::mat4(1.0f)
                       : TrajectoryPose(outcome, step) * prealignPose;

        // The pose is the world-space source->target delta; compose it with the
        // source's current model matrix and decompose the result back into the
        // local Transform (position/rotation, preserving the existing scale).
        ECSC::Transform::Component next = *transform;
        DecomposeModelToTransform(pose * sourceModel, next);

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult history =
                ExecuteEditorTransformMutation(
                    *context.CommandHistory,
                    context.Scene,
                    context.World,
                    command.SourceStableEntityId,
                    *transform,
                    next,
                    "Align point clouds (ICP)");
            result.Status = ToEditorCommandStatus(history.Status);
        }
        else
        {
            *transform = next;
            raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(*sourceEntity);
            result.Status = EditorCommandStatus::Applied;
        }

        if (result.Status != EditorCommandStatus::Applied)
        {
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "ICP registration pose failed during editor history commit.";
            return result;
        }

        result.Error = Core::ErrorCode::Success;
        result.Message = BuildRegistrationSuccessMessage(result);
        return result;
    }

} // namespace Extrinsic::Runtime
