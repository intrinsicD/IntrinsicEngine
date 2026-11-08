module;
#include <atomic>
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <string_view>
#include <atomic>
#include <functional>
#include <mutex>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.NormalOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Private.WorkspaceLease;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Geometry.Graph.Fwd;
import Geometry.Graph.Vertex.Normals;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.PointCloud.Normals;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.StepRuns; // the worker-step hooks of Runtime.GpuTransactionLifecycle.hpp
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.VertexNormals;
import Extrinsic.Graphics.PointNormals;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.TransferQueue;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.OneShotCpuRun.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
#include "Services/Runtime.WorkerGpuPages.hpp"

namespace Extrinsic::Runtime
{
    template std::shared_ptr<Graphics::PointNormalsWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::PointNormalsWorkspace>();
    template std::shared_ptr<Graphics::VertexNormalsWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::VertexNormalsWorkspace>();

    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace PN = Geometry::PointCloud::Normals;
        namespace GN = Geometry::Graph::VertexNormals;
        namespace MN = Geometry::HalfedgeMesh::VertexNormals;
        using D = GeometryElementDomain;
        namespace Detail = GeometryProcessingDetail;
        using Watch = Detail::PointPropertyWatch;
        struct NormalWork
        {
            NormalEstimationConfig Config{};
            entt::entity Entity{};
            std::vector<Watch> Inputs{};
            Watch OutputWatch{};
            std::vector<glm::vec3> Points{}, Before{}, After{};
            std::vector<bool> Deleted{};
            std::vector<std::uint32_t> Slots{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            Geometry::PropertySet GraphVertices{}, GraphHalfedges{}, GraphEdges{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            std::size_t FaceCount{}; // source faces (mesh methods)
            bool Abandoned{};
            EditorNormalEstimationResult Result{};
        };
        bool CaptureDeletionMetadata(const GeometryEntityAvailability &a, D domain, std::string name,
                                     std::size_t count, std::vector<Watch> &watches)
        {
            const auto *props = ResolveGeometryPropertySet(a, domain);
            if (!props || props->Size() != count)
                return false;
            watches.push_back(Detail::ObserveGeometryProperty(a, domain, name));
            if (!props->Exists(name))
                return true;
            const auto p = props->Get<bool>(name);
            return p && p.Size() == count;
        }
        // The source faces `BuildHalfedgeMeshForVertexNormalRecompute` reconstructs, in order:
        // every face that is not deleted and whose ring touches no deleted edge. False with a
        // diagnostic on a ring the builder would reject.
        bool ProcessedSourceFaces(const GS::ConstSourceView& view, std::vector<std::uint32_t>& faces, std::string& diagnostic)
        {
            faces.clear();
            const auto& faceProps = view.FaceSource->Properties;
            const auto& halfProps = view.HalfedgeSource->Properties;
            const auto faceHalfedges = faceProps.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge);
            const auto halfedgeFaces = halfProps.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeFace);
            const auto nextHalfedges = halfProps.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext);
            const auto toVertices = halfProps.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeToVertex);
            const auto deletedFaces = faceProps.Get<bool>("f:deleted");
            const auto deletedEdges = view.EdgeSource ? view.EdgeSource->Properties.Get<bool>("e:deleted")
                                                      : decltype(faceProps.Get<bool>("f:deleted")){};
            const auto vertexCount = std::uint32_t(view.VertexSource->Properties.Size());
            std::vector<std::uint32_t> ring;
            for (std::size_t face = 0; face < faceHalfedges.Size(); ++face)
            {
                if (deletedFaces && face < deletedFaces.Size() && deletedFaces[face]) continue;
                const auto status = Detail::MeshSupport::BuildMeshFaceRing(faceHalfedges.Vector(), halfedgeFaces.Vector(),
                    nextHalfedges.Vector(), toVertices.Vector(), face, vertexCount, ring);
                if (status != Detail::MeshSupport::MeshFaceRingStatus::Triangulate)
                {
                    diagnostic = "selected mesh topology is not valid for normal recompute";
                    return false;
                }
                bool touchesDeletedEdge = false;
                if (deletedEdges)
                {
                    auto h = faceHalfedges[face];
                    for (std::size_t corner = 0; corner < ring.size(); ++corner)
                    {
                        if (h / 2 < deletedEdges.Size() && deletedEdges[h / 2]) touchesDeletedEdge = true;
                        h = nextHalfedges[h];
                    }
                }
                if (!touchesDeletedEdge) faces.push_back(std::uint32_t(face));
            }
            return true;
        }
        enum class CapturePurpose
        {
            Execute,
            // Execute without the mesh snapshot: the Vulkan transaction reconstructs it only
            // when its topology bundle is not resident (RUNTIME-296).
            ExecuteResident,
            Readiness
        };
        std::shared_ptr<NormalWork> CaptureNormalWork(const EditorProcessingContext &context,
                                                      NormalEstimationConfig c, std::string &diagnostic,
                                                      CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<NormalWork> {
                diagnostic = std::move(why);
                return {};
            };
            const auto validation = ValidateNormalEstimationConfigSection(
                SerializeNormalEstimationConfig(c), {}, kNormalEstimationConfigSectionName);
            if (!validation.Usable())
                return fail(validation.Diagnostics.front().Message);
            if ((context.AttachmentActive && !context.AttachmentActive()) || !context.Scene)
                return fail("Scene is unavailable.");
            const auto entity =
                EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity)
                return fail("Normal target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown)
                c.Positions.Domain = Detail::PrimaryPointDomain(a);
            const bool faceNormals = c.Method == NormalEstimationMethod::MeshFaceNormals;
            if (c.Output.Domain == D::Unknown)
                c.Output.Domain = faceNormals ? D::MeshFace : c.Positions.Domain;
            const auto *props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved element domain.");
            if (faceNormals ? (c.Positions.Domain != D::MeshVertex || c.Output.Domain != D::MeshFace)
                            : (c.Output.Domain != c.Positions.Domain || c.Output.Name == c.Positions.Name))
                return fail(faceNormals ? "Face normals require mesh vertex positions and a mesh face output."
                                        : "Normals require a distinct output on the input domain.");
            const auto *outputProps = ResolveGeometryPropertySet(a, c.Output.Domain);
            if (!outputProps)
                return fail("Normal output domain is unavailable.");
            if (outputProps->Exists(c.Output.Name) &&
                !ResolveGeometryProperty(a, c.Output, outputProps->Size(), false).Resolved())
                return fail("Normal output must be absent or a count-matched vec3 property.");
            if (c.Output.Name == "v:deleted" || c.Output.Name == "e:deleted" ||
                c.Output.Name == "f:deleted" || c.Output.Name == "h:deleted")
                return fail("Normal output cannot replace a topology/deletion property.");
            if (props->Size() > std::numeric_limits<std::uint32_t>::max())
                return fail("Normal input exceeds the supported slot range.");
            if (c.Backend == NormalEstimationBackend::VulkanLBVH && c.Method != NormalEstimationMethod::PointSetPCA)
                return fail("Backend vulkan_lbvh requires point_set_pca.");
            if (c.Backend == NormalEstimationBackend::Vulkan)
            {
                using Weighting = Geometry::HalfedgeMesh::VertexNormals::AveragingMode;
                if (c.Method != NormalEstimationMethod::MeshFaceWeighted && !faceNormals)
                    return fail("Backend vulkan runs mesh_face_weighted and mesh_face_normals on the GPU property residency; "
                                "point-set PCA uses vulkan_lbvh and graph normals run on the CPU.");
                if (!faceNormals && (c.Weighting == Weighting::AngleWeighted || c.Weighting == Weighting::AreaAngleWeighted))
                    return fail("Vulkan vertex normals support uniform, area and max weighting; the angle weightings run on the CPU.");
                if (c.Output.Name == GS::PropertyNames::kPosition)
                    return fail("Vulkan vertex normals cannot write v:position (its ring would be shown as positions).");
                if (props->Size() > Graphics::VertexNormalsMaxVertices)
                    return fail("Vulkan vertex normals support at most 2^24 vertices; use the CPU backend.");
            }
            auto work = std::make_shared<NormalWork>();
            work->Config = c;
            work->Entity = *entity;
            work->Result.Method = c.Method;
            work->Result.RequestedBackend = c.Backend;
            work->Result.Output = c.Output;
            work->Result.SlotCount = outputProps->Size();
            work->OutputWatch = Detail::ObserveGeometryProperty(a, c.Output.Domain, c.Output.Name);
            if (purpose != CapturePurpose::Readiness)
            {
                if (work->OutputWatch.Revision.has_value())
                    work->Before = outputProps->Get<glm::vec3>(c.Output.Name).Vector();
                work->After = work->OutputWatch.Revision.has_value() ? work->Before
                                                       : std::vector<glm::vec3>(outputProps->Size(), glm::vec3(0));
            }
            Detail::PointInputCapture input;
            const bool captured = purpose != CapturePurpose::Readiness
                ? Detail::CapturePointInput(a, c.Positions, true, input, diagnostic)
                : Detail::PreparePointInput(context, *entity, a, c.Positions, input, diagnostic);
            if (!captured)
                return {};
            work->Inputs = std::move(input.Inputs);
            work->Result.LiveCount = input.LiveCount;
            const bool lbvhCoordinatesValid = input.ValidLbvh;
            if (c.Backend == NormalEstimationBackend::VulkanLBVH)
            {
                if (c.Orientation != PN::OrientationMode::None)
                    return fail("Vulkan PCA normals support unoriented output; MST orientation requires a CPU backend.");
                if (c.Output.Name == GS::PropertyNames::kPosition)
                    return fail("Vulkan PCA normals cannot write the observed position property.");
                if (std::fpclassify(c.DegenerateNormalLengthEpsilon) == FP_SUBNORMAL ||
                    std::fpclassify(c.CollinearEigenvalueRatioEpsilon) == FP_SUBNORMAL)
                    return fail("Vulkan PCA normals do not support subnormal numerical tolerances; use zero or a normal floating-point value.");
                if ((c.UseRadiusSearch && std::fpclassify(c.Radius) == FP_SUBNORMAL) ||
                    std::fpclassify(c.FallbackNormal.x) == FP_SUBNORMAL ||
                    std::fpclassify(c.FallbackNormal.y) == FP_SUBNORMAL ||
                    std::fpclassify(c.FallbackNormal.z) == FP_SUBNORMAL)
                    return fail("Vulkan PCA normals require normal or zero radius/fallback components; subnormal inputs are unsupported.");
            }
            if (c.Backend == NormalEstimationBackend::Vulkan || c.Backend == NormalEstimationBackend::VulkanLBVH)
            {
                // A device without float32 denorm preservation would flush a subnormal
                // coordinate on the float -> double conversion and diverge from the reference.
                if (input.HasSubnormalCoordinates)
                    return fail("Vulkan vertex normals require normal or zero coordinate components; subnormal coordinates are unsupported.");

                if (c.Backend == NormalEstimationBackend::VulkanLBVH &&
                    (!context.JobCommands.Available() || !context.SpatialIndices || !context.SpatialIndices->GpuQueriesAvailable()))
                    return fail("Vulkan normal neighborhoods require the framed spatial cache and job service.");
                if (!context.Device || !context.Device->IsOperational() || !context.Device->SupportsShaderFloat64() ||
                    !context.SpatialIndices || !context.SpatialIndices->GpuQueriesAvailable() || !context.JobCommands.Available())
                    return fail("Vulkan vertex normals need an operational device with shader double precision and framed GPU jobs.");
            }
            if (purpose != CapturePurpose::Readiness)
            {
                work->Points = std::move(input.Points);
                work->Slots = std::move(input.Slots);
                work->Deleted.assign(input.SlotCount, true);
                for (const auto slot : work->Slots)
                    work->Deleted[slot] = false;
            }
            if (!work->Result.LiveCount)
                return fail("Normal estimation requires live input samples.");
            if (c.Method == NormalEstimationMethod::PointSetPCA)
            {
                if (work->Result.LiveCount < 3)
                    return fail("Point-set PCA requires at least three live finite samples.");
                if (c.Backend == NormalEstimationBackend::CpuLBVH &&
                    (!context.SpatialIndices || !lbvhCoordinatesValid ||
                     work->Result.LiveCount > (1u << 24) ||
                     (c.UseRadiusSearch && c.Radius > Geometry::PointLBVH::CoordinateLimit)))
                    return fail("CPU LBVH requires the spatial cache, at most 2^24 samples and "
                                "coordinates/radius within 1e18.");
                if (c.Backend == NormalEstimationBackend::VulkanLBVH)
                {
                    if (!lbvhCoordinatesValid || work->Result.LiveCount > (1u << 20) ||
                        (c.UseRadiusSearch && c.Radius > Geometry::PointLBVH::CoordinateLimit))
                        return fail("Vulkan normal neighborhoods support at most 2^20 live samples and coordinates/radius within 1e18.");
                    const auto candidates = std::min<std::uint64_t>(work->Result.LiveCount,
                        std::uint64_t(std::max(c.KNeighbors, c.MinimumNeighbors)) + 1);
                    if (!c.UseRadiusSearch && candidates > 64)
                        return fail("Vulkan normal kNN supports at most 64 candidates including the extra self candidate; use k/minimum <=63 or a CPU backend.");
                }
                return work;
            }
            if (c.Positions.Domain != D::MeshVertex && c.Positions.Domain != D::GraphNode)
                return fail("Topology normals require a vertex/node position domain and its adjacency.");
            if (c.Method == NormalEstimationMethod::MeshFaceWeighted || faceNormals)
            {
                if (!a.SourceView.FaceSource || !a.SourceView.HalfedgeSource)
                    return fail("Mesh normals require face rings and halfedge topology.");
                const auto *faces = ResolveGeometryPropertySet(a, D::MeshFace);
                const auto *halves = ResolveGeometryPropertySet(a, D::MeshHalfedge);
                if (!faces || !halves)
                    return fail("Mesh normals require valid nonempty source face rings.");
                const auto faceHalfedges = faces->Get<std::uint32_t>("f:halfedge");
                if (!faceHalfedges || faceHalfedges.Size() != faces->Size())
                    return fail("Mesh normals require count-matched f:halfedge rings.");
                work->Inputs.push_back(Detail::ObserveGeometryProperty(a, D::MeshFace, "f:halfedge"));
                for (auto name : {"h:to_vertex", "h:next", "h:face"})
                {
                    const auto values = halves->Get<std::uint32_t>(name);
                    if (!values || values.Size() != halves->Size())
                        return fail("Mesh normals require count-matched halfedge topology.");
                    work->Inputs.push_back(Detail::ObserveGeometryProperty(a, D::MeshHalfedge, name));
                }
                if (!CaptureDeletionMetadata(a, D::MeshFace, "f:deleted", faces->Size(), work->Inputs))
                    return fail("Invalid face deletion mask.");
                const auto *edges = ResolveGeometryPropertySet(a, D::MeshEdge);
                if (!edges || halves->Size() != 2 * edges->Size() ||
                    !CaptureDeletionMetadata(a, D::MeshEdge, "e:deleted", edges->Size(), work->Inputs))
                    return fail("Invalid mesh edge deletion mask or halfedge cardinality.");
                work->FaceCount = faces->Size();
                if (c.Backend == NormalEstimationBackend::Vulkan &&
                    (faces->Size() > Graphics::VertexNormalsMaxFaces || halves->Size() > Graphics::VertexNormalsMaxCorners))
                    return fail("Vulkan vertex normals support at most 2^24 faces and 2^26 corners; use the CPU backend.");
                if (purpose == CapturePurpose::ExecuteResident && faceNormals)
                {
                    // The rows the face-normals kernel writes: the faces the snapshot would hold
                    // (not deleted, no deleted edge on the ring), without reconstructing it.
                    if (!ProcessedSourceFaces(a.SourceView, work->Slots, diagnostic)) return {};
                    work->Result.LiveCount = work->Slots.size();
                }
                if (purpose != CapturePurpose::Execute)
                    return work;
                // Snapshot reconstruction is submission work, never a per-frame UI readiness operation.
                auto built =
                    Detail::MeshSupport::BuildHalfedgeMeshForVertexNormalRecompute(a.SourceView, c.Positions.Name);
                if (built.Status != EditorCommandStatus::Applied)
                    return fail(built.Diagnostic);
                built.Mesh.VertexProperties().GetOrAdd<bool>("v:deleted").Vector() = work->Deleted;
                if (faceNormals)
                {
                    work->Slots = std::move(built.SourceFaceForMeshFace);
                    work->Result.LiveCount = work->Slots.size();
                }
                work->Mesh = std::move(built.Mesh);
                return work;
            }
            const auto edgeDomain = c.Positions.Domain == D::MeshVertex ? D::MeshEdge : D::GraphEdge;
            const auto *edges = ResolveGeometryPropertySet(a, edgeDomain);
            if (!edges)
                return fail("Graph-neighborhood normals require edge endpoints.");
            auto v0 = edges->Get<std::uint32_t>("e:v0"), v1 = edges->Get<std::uint32_t>("e:v1");
            if (!v0 || !v1 || v0.Size() != edges->Size() || v1.Size() != edges->Size())
                return fail("Graph-neighborhood normals require count-matched e:v0/e:v1 endpoints.");
            for (auto name : {"e:v0", "e:v1"})
                work->Inputs.push_back(Detail::ObserveGeometryProperty(a, edgeDomain, name));
            if (!CaptureDeletionMetadata(a, edgeDomain, "e:deleted", edges->Size(), work->Inputs))
                return fail("Invalid edge deletion mask.");
            if (purpose == CapturePurpose::Readiness)
                return work;
            work->GraphVertices.Resize(props->Size());
            work->GraphEdges.Resize(edges->Size());
            work->GraphHalfedges.Resize(edges->Size() * 2);
            work->GraphVertices.GetOrAdd<glm::vec3>(c.Positions.Name).Vector() =
                props->Get<glm::vec3>(c.Positions.Name).Vector();
            work->GraphVertices.GetOrAdd<bool>("v:deleted").Vector() = work->Deleted;
            const auto deletedEdges = edges->Get<bool>("e:deleted");
            auto graphDeletedEdges = work->GraphEdges.GetOrAdd<bool>("e:deleted");
            if (deletedEdges)
                graphDeletedEdges.Vector() = deletedEdges.Vector();
            auto connectivity =
                work->GraphHalfedges.GetOrAdd<Geometry::Graph::HalfedgeConnectivity>("h:connectivity");
            for (std::uint32_t e = 0; e < edges->Size(); ++e)
            {
                connectivity[2 * e].Vertex = Geometry::VertexHandle{v1[e]};
                connectivity[2 * e + 1].Vertex = Geometry::VertexHandle{v0[e]};
            }
            return work;
        }

        // The fallback mesh_face_normals writes: the configured normal normalized in double, or
        // +Z when it is not longer than the epsilon (shared by the CPU and Vulkan backends).
        glm::vec3 FaceNormalFallback(const NormalEstimationConfig& c)
        {
            const glm::dvec3 fallback(c.FallbackNormal);
            const double fallbackLength = glm::length(fallback);
            return fallbackLength > c.DegenerateNormalLengthEpsilon ? glm::vec3(fallback / fallbackLength) : glm::vec3{0, 0, 1};
        }
        bool CurrentNormalInput(const EditorProcessingContext &context, const NormalWork &work,
                                bool output)
        {
            if (!Detail::GeometryPropertiesCurrent(context, work.Entity, work.Inputs))
                return false;
            const auto &watch = work.OutputWatch;
            return !output || Detail::ObserveGeometryProperty(BuildGeometryAvailability(context.Scene->Raw(), work.Entity),
                                      watch.Domain, watch.Name) == watch;
        }
        void ComputeNormals(NormalWork &w)
        {
            auto &r = w.Result;
            const auto &c = w.Config;
            const auto started = std::chrono::steady_clock::now();
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            if (c.Method == NormalEstimationMethod::PointSetPCA)
            {
                PN::Params p;
                p.KNeighbors = c.KNeighbors;
                p.MinimumNeighbors = c.MinimumNeighbors;
                p.UseRadiusSearch = c.UseRadiusSearch;
                p.Radius = c.Radius;
                p.Orientation = c.Orientation;
                p.FallbackNormal = c.FallbackNormal;
                p.DegenerateNormalLengthEpsilon = c.DegenerateNormalLengthEpsilon;
                p.CollinearEigenvalueRatioEpsilon = c.CollinearEigenvalueRatioEpsilon;
                const auto estimate = w.Index ? PN::Estimate(w.Points, w.Index->Index, p) : PN::Estimate(w.Points, p);
                r.ActualBackend = w.Index ? "cpu_lbvh" : "cpu_kdtree";
                if (!estimate || estimate->Status != PN::RecomputeStatus::Success)
                {
                    r.Message = "PCA normal estimation failed.";
                    return;
                }
                r.PointDiagnostics = estimate->Diagnostics;
                r.ValidCount = estimate->Diagnostics.ValidNormalPointCount;
                r.FallbackCount = estimate->Diagnostics.FallbackPointCount;
                for (std::size_t i = 0; i < w.Slots.size(); ++i)
                    w.After[w.Slots[i]] = estimate->Normals[i];
            }
            else if (c.Method == NormalEstimationMethod::MeshFaceNormals)
            {
                r.ActualBackend = "cpu_mesh_face_normals";
                const glm::vec3 fallbackNormal = FaceNormalFallback(c);
                for (std::uint32_t face = 0; face < w.Slots.size(); ++face)
                {
                    const auto handle = Geometry::FaceHandle{face};
                    bool deletedCorner = false;
                    for (const auto vertex : w.Mesh.VerticesAroundFace(handle))
                        deletedCorner |= w.Mesh.IsDeleted(vertex);
                    const auto area = deletedCorner ? glm::dvec3(0)
                        : Geometry::MeshUtils::FaceAreaVector(w.Mesh, handle);
                    const double length = glm::length(area);
                    const bool valid = std::isfinite(length) && length > c.DegenerateNormalLengthEpsilon;
                    w.After[w.Slots[face]] = valid ? glm::vec3(area / length) : fallbackNormal;
                    r.ValidCount += valid;
                    r.FallbackCount += !valid;
                    ++r.ProcessedFaces;
                }
            }
            else if (c.Method == NormalEstimationMethod::MeshFaceWeighted)
            {
                const auto estimate =
                    MN::Recompute(w.Mesh, {.Weighting = c.Weighting,
                                           .FallbackNormal = c.FallbackNormal,
                                           .DegenerateNormalLengthEpsilon = c.DegenerateNormalLengthEpsilon});
                r.ActualBackend = "cpu_mesh_face_weighted";
                if (estimate.Status != MN::RecomputeStatus::Success)
                {
                    r.Message = std::string(MN::DebugName(estimate.Status));
                    return;
                }
                r.ValidCount = estimate.ValidNormalVertexCount;
                r.FallbackCount = estimate.FallbackVertexCount;
                r.ProcessedFaces = estimate.ProcessedFaceCount;
                for (auto i : w.Slots)
                    w.After[i] = estimate.Normals[Geometry::VertexHandle{i}];
            }
            else
            {
                const auto &vertices = std::as_const(w.GraphVertices);
                const auto &halves = std::as_const(w.GraphHalfedges);
                const auto &edges = std::as_const(w.GraphEdges);
                const auto estimate = GN::Recompute(
                    w.GraphVertices, vertices.Get<glm::vec3>(c.Positions.Name),
                    halves.Get<Geometry::Graph::HalfedgeConnectivity>("h:connectivity"), edges.Size(),
                    {.PositionProperty = c.Positions.Name,
                     .OutputProperty = c.Output.Name,
                     .FallbackNormal = c.FallbackNormal,
                     .DegenerateNormalLengthEpsilon = c.DegenerateNormalLengthEpsilon,
                     .CollinearEigenvalueRatioEpsilon = c.CollinearEigenvalueRatioEpsilon,
                     .OrientTowardFallback = c.OrientTowardFallback},
                    vertices.Get<bool>("v:deleted"), edges.Get<bool>("e:deleted"));
                r.ActualBackend = "cpu_graph_neighborhood";
                if (estimate.Status != GN::RecomputeStatus::Success)
                {
                    r.Message = std::string(GN::DebugName(estimate.Status));
                    return;
                }
                r.ValidCount = estimate.Diagnostics.ValidNormalVertexCount;
                r.FallbackCount = estimate.Diagnostics.FallbackVertexCount;
                r.InvalidEdges = estimate.Diagnostics.InvalidEdgeCount;
                for (auto i : w.Slots)
                    w.After[i] = estimate.Normals[i];
            }
            r.WrittenCount = w.Slots.size();
            for (auto i : w.Slots)
                r.ChangedCount += !w.OutputWatch.Revision.has_value() || w.Before[i] != w.After[i];
            r.Status = r.ChangedCount ? EditorCommandStatus::Applied : EditorCommandStatus::NoChange;
            r.CpuComputeMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            r.Message = "Normals computed using " + r.ActualBackend + ".";
        }
        EditorNormalEstimationResult PublishNormals(const EditorProcessingContext &context,
                                                    const std::shared_ptr<NormalWork> &w)
        {
            auto &r = w->Result;
            if (!Detail::EditorProcessingContextWorldCurrent(context) || !CurrentNormalInput(context, *w, true))
            {
                r.Status = EditorCommandStatus::StaleEntity;
                r.Message = "Normal input or output changed before publication.";
                return r;
            }
            if (r.Status != EditorCommandStatus::Applied)
                return r;
            struct OutputState
            {
                bool Exists{};
                std::vector<glm::vec3> Values{};
            };
            auto before = std::make_shared<OutputState>(OutputState{w->OutputWatch.Revision.has_value(), w->Before});
            auto after = std::make_shared<OutputState>(OutputState{true, w->After});
            auto outputWatches = Detail::CapturePointPropertyHistory(context, w->Entity, std::span(&w->Config.Output, 1));
            const auto mutate = [context, entity = w->Entity, inputs = w->Inputs, output = w->Config.Output,
                                 outputWatches=std::move(outputWatches)]
                                (const OutputState &expected, const OutputState &target) {
                if (!Detail::EditorProcessingContextWorldCurrent(context) ||
                    !Detail::GeometryPropertiesCurrent(context, entity, inputs) ||
                    !Detail::PointPropertyHistoryCurrent(context, entity, outputWatches))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto *props = Detail::MutableGeometryProperties(context.Scene->Raw(), entity, output.Domain);
                if (!props || props->Exists(output.Name) != expected.Exists)
                    return EditorCommandHistoryStatus::StaleEntity;
                const auto &read = std::as_const(*props);
                const auto current = read.Get<glm::vec3>(output.Name);
                // Deleted output rows may contain NaNs; compare their stored bits.
                if (expected.Exists && (!current || !GeometryValueComparison::BitEqual(current.Vector(), expected.Values)))
                    return EditorCommandHistoryStatus::StaleEntity;
                if (target.Exists && target.Values.size() != props->Size())
                    return EditorCommandHistoryStatus::InvalidCommand;
                if (target.Exists)
                    props->GetOrAdd<glm::vec3>(output.Name).Vector() = target.Values;
                else if (auto p = props->Get<glm::vec3>(output.Name))
                    props->Remove(p);
                Detail::StampPointPropertyHistory(context, entity, outputWatches);
                if (output.Domain == D::MeshFace)
                    ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(), entity);
                else
                {
                    ECS::Components::DirtyTags::MarkVertexNormalsDirty(context.Scene->Raw(), entity);
                    ECS::Components::DirtyTags::MarkVertexAttributesDirty(context.Scene->Raw(), entity);
                }
                if (context.InvalidateWorkspaceSnapshotCache)
                    context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status =
                context.CommandHistory
                    ? context.CommandHistory
                          ->Execute({.Label = "Estimate normals",
                                     .Redo = [mutate, before, after] { return mutate(*before, *after); },
                                     .Undo = [mutate, before, after] { return mutate(*after, *before); }})
                          .Status
                    : mutate(*before, *after);
            r.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
            if (r.Status != EditorCommandStatus::Applied)
                r.Message = "Normal publication rejected by source/output history checks.";
            return r;
        }
    } // namespace

    // The Vulkan run (RUNTIME-296, ADR 0030 decision 1) on the shared Run/Accept lifecycle: the
    // capture, the slots it reads, the device workspaces and its one output ring (read back on
    // Accept; ring 0 of the core).
    struct EditorNormalTransaction : std::enable_shared_from_this<EditorNormalTransaction>
    {
        GeometryProcessingDetail::GpuTransactionCore Core{};
        std::shared_ptr<NormalWork> Work{}; // its Result is the transaction's result
        std::uint32_t Count{};            // output rows (vertices, or faces for mesh_face_normals)
        std::uint32_t VertexCount{};      // rows of the positions input
        // Input: the canonical positions slot; Base: the output's canonical slot when the
        // property exists; Topology: the resident bundle.
        std::optional<Graphics::GpuPropertyView> Input{}, Base{}, Topology{};
        Graphics::VertexNormalsTopologyLayout Layout{};
        std::vector<std::uint32_t> Bundle{};
        // Leased from the spatial cache; the recorder closures keep them while their work may run.
        std::shared_ptr<Graphics::VertexNormalsWorkspace> Workspace{};
        std::shared_ptr<Graphics::PointNormalsWorkspace> PointWorkspace{};
        std::uint32_t PointFirst{};
        bool StoreRecorded{};
        std::uint32_t Previews{};
        EditorNormalEstimationResult Initial{}; // the result as queued: a restart's new generation begins here
        std::function<void(EditorNormalEstimationResult)> Sink{};
        std::optional<std::vector<glm::vec3>> TestFront{};
        // ARCH-023: the step runs on the step run's worker. Its Init (owner) retains the index view (point normals)
        // and captures what the pages record with (its own workspace references, the views without their leases);
        // the worker writes only the last page's statistics and its readback bytes, read once the step ended.
        std::shared_ptr<SpatialGpuIndexLease> IndexView{};
        struct WorkerView
        {
            std::shared_ptr<Graphics::VertexNormalsWorkspace> Workspace{};
            std::shared_ptr<Graphics::PointNormalsWorkspace> PointWorkspace{};
            std::shared_ptr<SpatialGpuIndexLease> Index{};
            Graphics::PointNormalsResidentIo PointIo{};
            Graphics::VertexNormalsResidentIo MeshIo{};
            std::vector<std::byte> Stats{};
            std::uint64_t ReadbackBytes{};
            std::size_t Batches{};
        } Worker{};
    };

    namespace NormalTransactionDetail
    {
        using Run = EditorNormalTransactionHandle;
        namespace GP = GeometryProcessingDetail;
        constexpr std::uint32_t kRingDepth = 2u;    // terminal-only output (ADR 0030 decision 4)
        constexpr std::string_view kBackend = "vulkan_mesh_face_weighted", kFaceBackend = "vulkan_mesh_face_normals";
        bool FaceNormals(const NormalWork& w) { return w.Config.Method == NormalEstimationMethod::MeshFaceNormals; }
        Graphics::VertexNormalsBundleKind KindOf(const NormalWork& w)
        {
            return FaceNormals(w) ? Graphics::VertexNormalsBundleKind::FaceNormals : Graphics::VertexNormalsBundleKind::VertexNormals;
        }
        bool PointNormals(const NormalWork& w) { return w.Config.Method == NormalEstimationMethod::PointSetPCA; }
        std::string_view BackendOf(const NormalWork& w) { return PointNormals(w) ? "vulkan_lbvh" : FaceNormals(w) ? kFaceBackend : kBackend; }

        // The topology bundle's residency key: derived (no CPU property of that name), one per
        // entity and bundle kind, so `Prune` drops it with the entity.
        Graphics::GpuPropertyKey TopologyKeyFor(const EditorProcessingContext& ctx, const NormalWork& w)
        {
            return MakeGpuPropertyKey(ctx.World, w.Entity,
                {.Domain = D::MeshFace, .Name = FaceNormals(w) ? "#face_normal_topology" : "#vertex_normal_topology",
                 .ValueKind = Geometry::PropertyValueKind::UInt32});
        }
        // The bundle's revision: FNV-1a over the captured topology and deletion watches (every
        // watch but the positions), so a topology or deletion edit is a new revision.
        std::uint64_t SignatureOf(const NormalWork& w)
        {
            std::uint64_t h = 1469598103934665603ull;
            const auto mix = [&](const void* data, const std::size_t bytes) {
                for (std::size_t i = 0; i < bytes; ++i)
                {
                    h ^= std::uint64_t(static_cast<const unsigned char*>(data)[i]);
                    h *= 1099511628211ull;
                }
            };
            for (const auto& watch : w.Inputs)
            {
                if (watch.Domain == w.Config.Positions.Domain && watch.Name == w.Config.Positions.Name) continue;
                const auto domain = std::uint32_t(watch.Domain);
                mix(&domain, sizeof(domain));
                mix(watch.Name.data(), watch.Name.size());
                const std::uint64_t count = watch.Count, revision = watch.Revision.value_or(0u);
                const std::uint8_t present = watch.Revision.has_value() ? 1u : 0u;
                mix(&count, sizeof(count));
                mix(&revision, sizeof(revision));
                mix(&present, sizeof(present));
            }
            return h == 0u ? 1u : h;
        }
        // Builds the bundle from the reference's scratch mesh (the reference's ring walk and
        // skip rules), indexed by source face so the layout follows the source counts.
        bool BuildBundle(const EditorProcessingContext& ctx, NormalWork& w, std::vector<std::uint32_t>& bundle,
                         Graphics::VertexNormalsTopologyLayout& layout, std::string& why)
        {
            const auto a = BuildGeometryAvailability(ctx.Scene->Raw(), w.Entity);
            auto built = Detail::MeshSupport::BuildHalfedgeMeshForVertexNormalRecompute(a.SourceView, w.Config.Positions.Name);
            if (built.Status != EditorCommandStatus::Applied)
            {
                why = built.Diagnostic;
                return false;
            }
            built.Mesh.VertexProperties().GetOrAdd<bool>("v:deleted").Vector() = w.Deleted;
            std::vector<std::uint32_t> faceOffsets(w.FaceCount + 1u, 0u), corners;
            if (FaceNormals(w))
            {
                // mesh_face_normals walks every snapshot face's ring as MeshUtils::FaceAreaVector
                // does (deleted corners become the fallback sentinel); the rows are the snapshot
                // faces themselves.
                if (built.SourceFaceForMeshFace != w.Slots)
                {
                    why = "The processed face rows do not match the mesh snapshot.";
                    return false;
                }
                for (std::size_t meshFace = 0, face = 0; face < w.FaceCount; ++face)
                {
                    if (meshFace < built.SourceFaceForMeshFace.size() && built.SourceFaceForMeshFace[meshFace] == face)
                    {
                        for (const auto vertex : built.Mesh.VerticesAroundFace(Geometry::FaceHandle{std::uint32_t(meshFace)}))
                            corners.push_back(built.Mesh.IsDeleted(vertex) ? Graphics::VertexNormalsDeletedCorner
                                                                           : std::uint32_t(vertex.Index));
                        ++meshFace;
                    }
                    faceOffsets[face + 1u] = std::uint32_t(corners.size());
                }
            }
            else
            {
                const auto table = MN::GatherFaceCornerTable(built.Mesh);
                corners.reserve(table.Corners.size());
                std::size_t meshFace = 0;
                for (std::size_t face = 0; face < w.FaceCount; ++face)
                {
                    if (meshFace < built.SourceFaceForMeshFace.size() && built.SourceFaceForMeshFace[meshFace] == face)
                    {
                        corners.insert(corners.end(), table.Corners.begin() + table.FaceOffsets[meshFace],
                                       table.Corners.begin() + table.FaceOffsets[meshFace + 1u]);
                        ++meshFace;
                    }
                    faceOffsets[face + 1u] = std::uint32_t(corners.size());
                }
            }
            layout = Graphics::PackVertexNormalsTopology(faceOffsets, corners, std::uint32_t(w.Deleted.size()), w.Slots, bundle, KindOf(w));
            if (layout.Words == 0u)
            {
                why = "The mesh topology could not be packed for the device.";
                return false;
            }
            return true;
        }
        enum class TopologyState : std::uint8_t { Ready, Deferred, Failed };
        // The bundle's canonical slot for this topology revision. A resident bundle describes
        // itself from the source counts (no mesh reconstruction); otherwise the bundle is built
        // and uploaded once. Deferred on a refused upload (the caller polls again).
        TopologyState ResolveTopology(const EditorProcessingContext& ctx, Graphics::GpuPropertyResidency& r, NormalWork& w,
                                      std::vector<std::uint32_t>& bundle, Graphics::VertexNormalsTopologyLayout& layout,
                                      std::optional<Graphics::GpuPropertyView>& view, bool& uploaded, std::string& why)
        {
            const auto key = TopologyKeyFor(ctx, w);
            const auto signature = SignatureOf(w);
            const auto vertices = std::uint32_t(w.Deleted.size()), liveRows = std::uint32_t(w.Slots.size());
            const auto faces = std::uint32_t(w.FaceCount);
            if (layout.Words == 0u)
            {
                if (const auto resident = r.HasRing(key) ? std::nullopt : r.Front(key);
                    resident && resident->Revision == signature && resident->Layout.Scalar == Graphics::GpuScalarType::UInt32 &&
                    resident->Layout.Channels == 1u && resident->Layout.Stride == 0u)
                    layout = Graphics::UnpackVertexNormalsTopologyLayout(faces, vertices, liveRows, resident->Layout.Count, KindOf(w));
                if (layout.Words == 0u && !BuildBundle(ctx, w, bundle, layout, why)) return TopologyState::Failed;
            }
            const Graphics::GpuPropertyLayout slot{.Scalar = Graphics::GpuScalarType::UInt32, .Channels = 1u, .Count = layout.Words};
            const auto before = r.Stats().Uploads;
            bool consistent = true;
            view = r.AcquireInput(key, signature, slot, [&](std::span<std::byte> out) {
                if (bundle.empty())
                {
                    // Only when the slot went between the check and the acquire: rebuild (the
                    // same revision packs the same words).
                    Graphics::VertexNormalsTopologyLayout rebuilt{};
                    std::string ignored;
                    consistent = BuildBundle(ctx, w, bundle, rebuilt, ignored) && rebuilt.Words == layout.Words;
                    if (!consistent) bundle.assign(layout.Words, 0u);
                }
                std::memcpy(out.data(), bundle.data(), std::min<std::size_t>(out.size(), bundle.size() * sizeof(std::uint32_t)));
            });
            uploaded = r.Stats().Uploads > before;
            if (!consistent)
            {
                why = "The mesh topology bundle could not be rebuilt consistently.";
                return TopologyState::Failed;
            }
            if (!view) return TopologyState::Deferred;
            return TopologyState::Ready;
        }

        bool Current(const Run& w) { return GP::GpuTransactionCurrent(w->Core); }
        auto& Ring(const Run& w) { return w->Core.Rings[0]; }
        // Recorders own their workspace leases (the cache keeps them until the readback is
        // safe), so the run returns its own at once.
        void ReleaseWorkspaces(const Run& w)
        {
            w->Workspace.reset();
            w->PointWorkspace.reset();
        }
        void Finish(const Run& w, const EditorGpuTransactionPhase phase, const EditorCommandStatus status, std::string message)
        {
            GP::FinishGpuTransaction(w->Core, phase, status, std::move(message));
        }
        Graphics::VertexNormalsResidentView View(const std::optional<Graphics::GpuPropertyView>& v)
        {
            if (!v) return {};
            return {.Buffer = v->Buffer, .Address = v->Address};
        }
        Graphics::VertexNormalsGpuParams Params(const NormalEstimationConfig& c)
        {
            using Weighting = Geometry::HalfedgeMesh::VertexNormals::AveragingMode;
            Graphics::VertexNormalsGpuParams params{};
            params.Weighting = c.Weighting == Weighting::UniformFace ? Graphics::VertexNormalGpuWeighting::UniformFace
                             : c.Weighting == Weighting::MaxWeighted ? Graphics::VertexNormalGpuWeighting::MaxWeighted
                             : Graphics::VertexNormalGpuWeighting::AreaWeighted;
            params.Epsilon = c.DegenerateNormalLengthEpsilon;
            glm::vec3 fallback{};
            if (c.Method == NormalEstimationMethod::MeshFaceNormals)
                fallback = FaceNormalFallback(c); // the face-normals reference normalizes in double, +Z when degenerate
            else
            {
                bool repaired = false;
                fallback = MN::ResolveFallbackNormal(
                    {.FallbackNormal = c.FallbackNormal, .DegenerateNormalLengthEpsilon = c.DegenerateNormalLengthEpsilon}, repaired);
            }
            params.Fallback = {fallback.x, fallback.y, fallback.z};
            return params;
        }
        // The slots a submission needs. Deferred while the residency refuses (a refused upload or
        // an exhausted ring); Failed when the topology cannot be built.
        TopologyState AcquireSlots(const Run& w, std::string& why)
        {
            auto& r = *w->Core.Residency;
            const auto& ctx = w->Core.Context;
            auto& work = *w->Work;
            const auto& c = work.Config;
            auto& io = work.Result;
            if (!w->Input)
            {
                const auto before = r.Stats().UploadBytes;
                w->Input = ResolveGpuPropertyInput(r, *ctx.Scene, ctx.World, work.Entity, c.Positions);
                if (!w->Input || w->Input->Layout.Count != w->VertexCount) { w->Input.reset(); return TopologyState::Deferred; }
                io.GpuInputUploadBytes += r.Stats().UploadBytes - before;
                if (r.Stats().UploadBytes == before) ++io.GpuInputCacheHits;
            }
            // An existing output keeps its bytes outside the live rows: the store copies its
            // canonical slot first.
            if (work.OutputWatch.Revision.has_value() && !w->Base)
            {
                const auto before = r.Stats().UploadBytes;
                w->Base = ResolveGpuPropertyInput(r, *ctx.Scene, ctx.World, work.Entity, c.Output);
                if (!w->Base || w->Base->Layout.Count != w->Count) { w->Base.reset(); return TopologyState::Deferred; }
                io.GpuInputUploadBytes += r.Stats().UploadBytes - before;
                if (r.Stats().UploadBytes == before) ++io.GpuInputCacheHits;
            }
            if (!PointNormals(work) && !w->Topology)
            {
                bool uploaded = false;
                const auto state = ResolveTopology(ctx, r, work, w->Bundle, w->Layout, w->Topology, uploaded, why);
                if (state != TopologyState::Ready) return state;
                io.GpuTopologyBytes = w->Layout.Bytes();
                io.GpuTopologyReused = !uploaded;
            }
            switch (GP::AcquireGpuTransactionBack(w->Core, 0, work.Entity, c.Output, w->Count, kRingDepth))
            {
            case GP::GpuRingAcquisition::Foreign:
                why = "Another normal result acquired this output ring; previous normals retained.";
                return TopologyState::Failed;
            case GP::GpuRingAcquisition::Deferred: return TopologyState::Deferred;
            case GP::GpuRingAcquisition::Ready: break;
            }
            return TopologyState::Ready;
        }
        void PublishPreview(const Run& w)
        {
            const bool recorded = std::exchange(w->StoreRecorded, false);
            Ring(w).Back.reset();
            if (recorded && w->Core.Residency && w->Core.Residency->Publish(Ring(w).Key)) ++w->Previews;
        }
        Graphics::PointNormalsGpuParams PointParams(const NormalEstimationConfig& c)
        {
            return {.K = std::max(c.KNeighbors, c.MinimumNeighbors),
                .MinimumNeighbors = c.MinimumNeighbors,
                .BatchSize = c.GpuQueryBatchSize,
                .RadiusSearch = c.UseRadiusSearch,
                .Radius = c.Radius,
                .Epsilon = c.DegenerateNormalLengthEpsilon,
                .CollinearRatio = c.CollinearEigenvalueRatioEpsilon,
                .Fallback = {c.FallbackNormal.x, c.FallbackNormal.y, c.FallbackNormal.z}};
        }
        Graphics::GpuPropertyView Stripped(Graphics::GpuPropertyView view)
        {
            view.Lease.reset(); // the owner keeps the lease
            return view;
        }
        // The Init (main thread, once the run computes): the canonical inputs, the topology bundle and the output's
        // write slot, the workspace prepared for this capture and (point normals) the retained index view; then the
        // immutable recording view.
        GP::GpuTransactionPrepare Prepare(const Run& w, std::string& why)
        {
            using P = GP::GpuTransactionPrepare;
            const auto& ctx = w->Core.Context;
            switch (AcquireSlots(w, why))
            {
            case TopologyState::Failed: return P::Failed;
            case TopologyState::Deferred:
                if (!GP::GpuTransactionDeferralsExhausted(w->Core))
                    return P::Pending;
                why =
                    "The GPU property residency refused the run's input, topology or output slot; previous normals retained.";
                return P::Failed;
            case TopologyState::Ready: break;
            }
            auto& worker = w->Worker;
            const auto& back = Ring(w).Back;
            if (PointNormals(*w->Work))
            {
                if (!w->PointWorkspace)
                    w->PointWorkspace = ctx.SpatialIndices->LeaseGpuWorkspace<Graphics::PointNormalsWorkspace>();
                if (!w->IndexView)
                    w->IndexView = ctx.SpatialIndices->PrepareGpuIndex(w->Work->GpuIndex, why);
                if (!w->IndexView)
                    return P::Failed;
                const auto& index = w->IndexView->View();
                const auto refusals = ctx.SpatialIndices->Stats().ScratchRefusals;
                if (!w->PointWorkspace || !w->PointWorkspace->Prepare(PointParams(w->Work->Config), index.Count))
                {
                    why = ctx.SpatialIndices->MethodScratchDiagnostic(
                        refusals, "PCA normal device workspace unavailable.");
                    return P::Failed;
                }
                worker.PointWorkspace = w->PointWorkspace;
                worker.Index = w->IndexView;
                worker.PointIo = {.Positions = Stripped(*w->Input),
                    .Output = Stripped(*back),
                    .Base = w->Base ? Stripped(*w->Base) : Graphics::GpuPropertyView{},
                    .Nodes = index.NodesBDA,
                    .LiveSlots = index.OriginalSlotsBDA,
                    .LiveCount = index.Count};
                worker.Stats.assign(sizeof(Graphics::PointNormalsGpuStats), std::byte{0});
            }
            else
            {
                if (!w->Workspace)
                    w->Workspace = ctx.SpatialIndices->LeaseGpuWorkspace<Graphics::VertexNormalsWorkspace>();
                const auto refusals = ctx.SpatialIndices->Stats().ScratchRefusals;
                if (!w->Workspace || !w->Workspace->Prepare(w->Layout))
                {
                    why = ctx.SpatialIndices->MethodScratchDiagnostic(
                        refusals, "Vertex normal device workspace unavailable.");
                    return P::Failed;
                }
                worker.Workspace = w->Workspace;
                worker.MeshIo = {.Positions = View(w->Input),
                    .Topology = View(w->Topology),
                    .Output = View(back),
                    .Base = View(w->Base),
                    .OutputBytes = back->Bytes,
                    .Layout = w->Layout};
                worker.Stats.assign(std::size_t(Graphics::VertexNormalsWorkspace::StatsReadbackBytes), std::byte{0});
            }
            worker.ReadbackBytes = 0u;
            worker.Batches = 0u;
            w->StoreRecorded = true;
            return P::Ready;
        }
        // One page of the worker step; empty on success, else why it failed (nothing for an abandoned page).
        std::string RunPage(
            EditorNormalTransaction& w, const std::function<RHI::BufferHandle(RHI::ICommandContext&)>& record)
        {
            switch (w.Core.Pages->Run(record, w.Worker.Stats))
            {
            case WorkerGpuPageStatus::Complete:
                w.Worker.ReadbackBytes += w.Worker.Stats.size();
                ++w.Worker.Batches;
                return {};
            case WorkerGpuPageStatus::Aborted:  return " ";
            case WorkerGpuPageStatus::Refused:
                return "Vulkan normals compute submission refused; previous normals retained.";
            case WorkerGpuPageStatus::Failed:
            case WorkerGpuPageStatus::TimedOut: break;
            }
            return "Vulkan normals page did not complete: the device was lost.";
        }
        // The step run's Step, on a worker: the mesh passes in one page, or every completion-gated PCA page.
        StepOutcome Step(EditorNormalTransaction& w)
        {
            auto& worker = w.Worker;
            const auto fail = [&w](std::string why)
            {
                if (why != " ")
                    w.Core.WorkerFailure = std::move(why);
                return StepOutcome::Failed;
            };
            if (!PointNormals(*w.Work))
            {
                const auto params = Params(w.Work->Config);
                if (auto why = RunPage(w,
                        [&](RHI::ICommandContext& commands)
                        {
                            return worker.Workspace->Record(commands, params, worker.MeshIo);
                        });
                    !why.empty())
                    return fail(std::move(why));
                return StepOutcome::Converged;
            }
            const auto& c = w.Work->Config;
            const auto params = PointParams(c);
            const auto rows =
                Graphics::PointNormalsWorkspace::RowsPerSubmission(c.UseRadiusSearch, c.GpuQueryBatchSize);
            for (std::uint32_t first = 0u; first < worker.PointIo.LiveCount; first += rows)
            {
                if (auto why = RunPage(w,
                        [&](RHI::ICommandContext& commands)
                        {
                            for (const auto buffer : worker.Index->Barriers())
                                commands.BufferBarrier(buffer,
                                    RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferWrite,
                                    RHI::MemoryAccess::ShaderRead);
                            return worker.PointWorkspace->Record(commands, params, worker.PointIo, first);
                        });
                    !why.empty())
                    return fail(std::move(why));
                Graphics::PointNormalsGpuStats stats{};
                std::memcpy(&stats, worker.Stats.data(), sizeof(stats));
                if (stats.Overflow)
                    return fail("Vulkan PCA radius neighborhoods exceed 1024 candidates; use a CPU backend.");
            }
            return StepOutcome::Converged;
        }
        // The owner's side: once the step completed, its store becomes the preview front.
        bool Poll(const Run& w)
        {
            if (!GP::GpuTransactionStepCompleted(w->Core))
                return false;
            PublishPreview(w);
            return true;
        }
        // The compute job ended: the run either waits for Accept or failed.
        // The input, base and topology views and the workspaces stay until Accept, Discard or the run's end
        // (Release), so a restart records again without resolving, uploading or leasing anything.
        void CompleteRun(const Run& w)
        {
            auto& r = w->Work->Result;
            // The step's pulled statistics (its last page's): no step runs now.
            const auto& data = w->Worker.Stats;
            if (w->Previews == 0u)
            {
                Finish(w, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                       "Vulkan normals published no preview; previous normals retained.");
                return;
            }
            if (!PointNormals(*w->Work) && data.size() == sizeof(std::uint32_t) * 3u)
            {
                std::array<std::uint32_t, 3> stats{};
                std::memcpy(stats.data(), data.data(), sizeof(stats));
                r.ProcessedFaces = stats[0];
                r.ValidCount = stats[1];
                r.FallbackCount = stats[2];
            }
            if (PointNormals(*w->Work) && data.size() == sizeof(Graphics::PointNormalsGpuStats))
            {
                r.GpuQueryBatches = std::exchange(w->Worker.Batches, 0u);
                Graphics::PointNormalsGpuStats stats{};
                std::memcpy(&stats, data.data(), sizeof(stats));
                r.ValidCount = stats.Valid;
                r.FallbackCount = stats.Fallback;
                r.PointDiagnostics.ValidNormalPointCount = stats.Valid;
                r.PointDiagnostics.FallbackPointCount = stats.Fallback;
                r.PointDiagnostics.TooFewNeighborCount = stats.TooFew;
                r.PointDiagnostics.CollinearNeighborhoodCount = stats.Collinear;
                r.PointDiagnostics.WrittenCount = w->Work->Slots.size();
                r.PointDiagnostics.PointSlotCount = w->Work->Slots.size();
                r.PointDiagnostics.FinitePointCount = w->Work->Slots.size();
                r.PointDiagnostics.DegenerateNeighborhoodCount = stats.Degenerate;
                r.PointDiagnostics.DuplicatePositionCount = stats.Duplicates;
                r.PointDiagnostics.FallbackNormalWasRepaired = stats.FallbackRepaired != 0;
            }
            r.ActualBackend = std::string{BackendOf(*w->Work)};
            r.CpuStageReadbackBytes += std::exchange(w->Worker.ReadbackBytes, 0u);
            GP::ReadyGpuTransaction(w->Core);
            r.Status = EditorCommandStatus::Pending;
            r.Message = "The GPU normals wait for Accept or Discard.";
        }
        std::string IoSummary(const EditorNormalEstimationResult& r)
        {
            return "input cache hits: " + std::to_string(r.GpuInputCacheHits) + "; CPU readback: " +
                   std::to_string(r.CpuStageReadbackBytes) + " bytes; input upload: " + std::to_string(r.GpuInputUploadBytes) + " bytes; topology bundle: " +
                   std::to_string(r.GpuTopologyBytes) + " bytes (" + (r.GpuTopologyReused ? "resident" : "uploaded") + ")";
        }
        // Accept landed: the front's rows become the published property and the canonical slot.
        void CompleteAccept(const Run& w)
        {
            const auto& ctx = w->Core.Context;
            auto& work = *w->Work;
            auto* residency = w->Core.Residency;
            const auto& readback = Ring(w).Readback;
            const auto testFront = w->TestFront && residency ? residency->Front(Ring(w).Key) : std::nullopt;
            const std::uint64_t accepted = readback && readback->Lease ? readback->Lease->Publication
                                           : testFront ? testFront->Publication : 0u;
            if (readback) readback->Lease.reset();
            if (readback && readback->Failed)
            {
                Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                       "Vulkan normals readback failed; previous normals retained.");
                return;
            }
            std::vector<glm::vec3> after(w->Count);
            if (w->TestFront) after = *w->TestFront;
            else
            {
                if (!readback || readback->Bytes.size() != std::size_t(w->Count) * sizeof(glm::vec3))
                {
                    Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                           "Vulkan normals readback has the wrong size; previous normals retained.");
                    return;
                }
                std::memcpy(after.data(), readback->Bytes.data(), after.size() * sizeof(glm::vec3));
            }
            work.Result.CpuStageReadbackBytes += after.size() * sizeof(glm::vec3);
            work.After = std::move(after);
            auto& r = work.Result;
            r.ActualBackend = std::string{BackendOf(work)};
            r.WrittenCount = work.Slots.size();
            r.ChangedCount = 0;
            for (const auto i : work.Slots)
                r.ChangedCount += !work.OutputWatch.Revision.has_value() || work.Before[i] != work.After[i];
            r.Status = r.ChangedCount ? EditorCommandStatus::Applied : EditorCommandStatus::NoChange;
            r.Message = "Normals computed using " + r.ActualBackend + "; " + IoSummary(r) + ".";
            auto result = PublishNormals(ctx, w->Work);
            work.Result = result;
            if (!result.Succeeded())
            {
                Finish(w, result.Status == EditorCommandStatus::StaleEntity ? EditorGpuTransactionPhase::Discarded
                                                                            : EditorGpuTransactionPhase::Failed,
                       result.Status, std::move(result.Message));
                return;
            }
            // The front becomes the canonical slot of the new CPU revision (ADR 0030 decision 6).
            if (residency)
            {
                const auto watch = GP::ObserveGeometryProperty(BuildGeometryAvailability(ctx.Scene->Raw(), work.Entity),
                                                               work.Config.Output.Domain, work.Config.Output.Name);
                if (!watch.Revision || !residency->BindRevision(Ring(w).Key, *watch.Revision, accepted))
                    (void)residency->Discard(Ring(w).Key, Ring(w).Generation);
            }
            Finish(w, EditorGpuTransactionPhase::Applied, result.Status, std::move(result.Message));
        }
        // Builds the transaction on the shared lifecycle; hooks reach it by raw pointer and take
        // ownership (shared_from_this) for recorders.
        Run Make(const EditorProcessingContext& context, const std::shared_ptr<NormalWork>& work,
                 Graphics::GpuPropertyResidency* residency)
        {
            const auto& c = work->Config;
            auto w = std::make_shared<EditorNormalTransaction>();
            w->Work = work;
            w->Count = std::uint32_t(work->Result.SlotCount);
            w->VertexCount = std::uint32_t(work->Deleted.size());
            auto& t = w->Core;
            t.Context = context;
            t.Residency = residency;
            t.Label = "Vulkan normals";
            t.AcceptJobName = "Vulkan normals accept";
            t.JobLabel = "Normal estimation";
            t.Identity = {.EntityId = c.StableEntityId, .Scope = ToEditorJobScope(c.Output.Domain),
                          .OutputSemantic = GeometryPresentationSlotSemantic::Normal, .OutputName = c.Output.Name,
                          .RequestedDomain = EditorJobDomain::GpuCompute};
            t.Rings[0] = {.Key = MakeGpuPropertyKey(context.World, work->Entity, c.Output), .ReadBack = true};
            t.RingCount = 1;
            auto* raw = w.get();
            const auto self = [raw] { return raw->shared_from_this(); };
            t.Hooks = {.Current =
                           [raw]
                {
                    return CurrentNormalInput(raw->Core.Context, *raw->Work, true);
                },
                .Poll =
                    [self]
                {
                    return Poll(self());
                },
                .CompleteRun =
                    [self]
                {
                    CompleteRun(self());
                },
                .CompleteAccept =
                    [self]
                {
                    CompleteAccept(self());
                },
                .Accepting =
                    [raw]
                {
                    raw->Work->Result.Status = EditorCommandStatus::Pending;
                    raw->Work->Result.Message = "Reading the GPU normals back.";
                },
                // A restart records the passes again from the capture: the inputs, topology, workspaces and index view
                // stay (the capture is current); the Init acquires a fresh write slot; no preview yet, counters as queued.
                .Restart =
                    [self](std::string&)
                {
                    const auto w = self();
                    w->PointFirst = 0u;
                    w->StoreRecorded = false;
                    w->Previews = 0u;
                    w->Worker.ReadbackBytes = 0u;
                    w->Work->Result = w->Initial;
                    if (w->Topology) // the held bundle: resident, nothing uploaded again
                    {
                        w->Work->Result.GpuTopologyBytes = w->Layout.Bytes();
                        w->Work->Result.GpuTopologyReused = true;
                    }
                    return true;
                },
                .Prepare =
                    [self](std::string& diagnostic)
                {
                    return Prepare(self(), diagnostic);
                },
                .Step =
                    [raw](StepRunMetric&)
                {
                    return Step(*raw);
                },
                .Uses =
                    [raw](std::vector<RHI::BufferHandle>& buffers)
                {
                    for (const auto* view : {&raw->Input, &raw->Base, &raw->Topology})
                        if (*view)
                            buffers.push_back((*view)->Buffer);
                },
                .Release =
                    [self]
                {
                    const auto w = self();
                    w->Input.reset();
                    w->Base.reset();
                    w->Topology.reset();
                    ReleaseWorkspaces(w);
                    w->IndexView.reset();
                    w->Worker = {}; // no step runs any more (the core waits for that)
                },
                .Deliver =
                    [raw](const EditorCommandStatus status, std::string message)
                {
                    auto& r = raw->Work->Result;
                    r.Status = status;
                    r.Message = std::move(message);
                    r.StepRun = raw->Core.StepRun;
                    if (auto sink = std::move(raw->Sink))
                        sink(r);
                }};
            return w;
        }
        // Queues the front's readback and the job that publishes it.
        EditorNormalEstimationResult BeginAccept(const Run& w, std::function<void(EditorNormalEstimationResult)> onComplete)
        {
            auto result = w->Work->Result;
            if (auto refused = GP::GpuTransactionAcceptRefusal(w->Core, bool(onComplete)))
            {
                result.Status = refused->Status;
                result.Message = std::move(refused->Message);
                return result;
            }
            if (onComplete) w->Sink = GuardEditorProcessingResult(w->Core.Context, std::move(onComplete));
            (void)GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(w));
            return w->Work->Result;
        }
        // Builds the run from the capture and queues its compute job. Null with `result` filled
        // (a rejection) when the request cannot run.
        Run StartVulkan(const EditorProcessingContext& context,
            const std::shared_ptr<NormalWork>& work,
            EditorNormalEstimationResult& result,
            std::function<void(EditorNormalEstimationResult)> onComplete,
            const StepRunExecution& execution)
        {
            const auto fail = [&](const EditorCommandStatus status, std::string message) {
                result.Status = status;
                result.Message = std::move(message);
                return Run{};
            };
            const auto& c = work->Config;
            auto w = Make(context, work, context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr);
            if (auto refused = GP::GpuTransactionStartRefusal(w->Core))
                return fail(refused->Status, std::move(refused->Message));
            if (PointNormals(*work))
            {
                std::string why;
                if (GP::AcquirePointIndex(*context.SpatialIndices,
                        context.World,
                        work->Entity,
                        c.Positions,
                        work->Slots,
                        work->Points,
                        work->GpuIndex,
                        work->Index,
                        work->Result.IndexReused,
                        why,
                        false) != GP::PointIndexState::Ready)
                    return fail(EditorCommandStatus::InvalidProcessingParameters, why);
            }
            w->Core.Execution = OneShotExecution(execution);
            w->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            result = work->Result;
            result.Status = EditorCommandStatus::Pending;
            result.Message = "Vulkan normals queued.";
            work->Result = result;
            w->Initial = result;
            w->Core.Pages = std::make_shared<WorkerGpuPages>(*context.Device); // the passes run on the step's worker
            if (!GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(w), "Vulkan normals").IsValid())
                return fail(EditorCommandStatus::GeometryProcessingFailed, GP::MeshSupport::QueuedJobRejectedMessage(w->Core.JobLabel));
            return w;
        }
    } // namespace NormalTransactionDetail

    EditorNormalTransactionHandle StartEditorNormalEstimationTransaction(const EditorProcessingCommands& commands,
        const NormalEstimationConfig& config,
        EditorNormalEstimationResult& failure)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        const auto& execution = config.Execution;
        failure = {.Method = config.Method, .RequestedBackend = config.Backend, .Output = config.Output};
        failure.Status = EditorCommandStatus::InvalidProcessingParameters;
        if (config.Backend != NormalEstimationBackend::Vulkan && config.Backend != NormalEstimationBackend::VulkanLBVH)
        {
            failure.Message = "The normals transaction runs the Vulkan backend; CPU runs publish at once.";
            return {};
        }
        std::string diagnostic;
        auto work = CaptureNormalWork(context, config, diagnostic, CapturePurpose::ExecuteResident);
        if (!work)
        {
            failure.Message = diagnostic;
            return {};
        }
        return NormalTransactionDetail::StartVulkan(context, work, failure, {}, execution);
    }

    EditorNormalTransactionSnapshot SnapshotEditorNormalEstimation(const EditorProcessingCommands&,
                                                                   const EditorNormalTransactionHandle& run)
    {
        EditorNormalTransactionSnapshot snapshot;
        if (!run) return snapshot;
        snapshot.Phase = run->Core.Phase;
        snapshot.Previews = run->Previews;
        snapshot.DeviceWorkQueued = run->Core.Gpu != nullptr || run->Core.Prepared; // a worker step's Init done
        snapshot.Result = run->Work->Result;
        snapshot.Result.StepRun = run->Core.StepRun;
        if (run->Core.Phase == EditorGpuTransactionPhase::ReadyToAccept)
        {
            snapshot.Stale = !NormalTransactionDetail::Current(run);
            const bool resident = run->TestFront.has_value() || (run->Core.Residency && run->Core.Residency->HasRing(run->Core.Rings[0].Key));
            snapshot.CanAccept = !snapshot.Stale && resident;
            if (snapshot.Stale) snapshot.AcceptDisabledReason = "The inputs changed since the run; discard the result and run again.";
            else if (!resident) snapshot.AcceptDisabledReason = "The GPU result is no longer resident; discard it.";
        }
        // Running, accepting or finished: the lifecycle's own refusal says why Accept is not available now.
        else if (const auto refused = GeometryProcessingDetail::GpuTransactionAcceptRefusal(run->Core)) snapshot.AcceptDisabledReason = refused->Message;
        return snapshot;
    }

    EditorNormalEstimationResult AcceptEditorNormalEstimation(const EditorProcessingCommands&, const EditorNormalTransactionHandle& run,
                                                              std::function<void(EditorNormalEstimationResult)> onComplete)
    {
        if (!run)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters, .Message = "No normals transaction."};
        return NormalTransactionDetail::BeginAccept(run, std::move(onComplete));
    }

    void DiscardEditorNormalEstimation(const EditorProcessingCommands&, const EditorNormalTransactionHandle& run)
    {
        // A queued job finalizes as cancelled on its next drain; the ring goes now (freed after
        // its completions).
        if (run)
            GeometryProcessingDetail::DiscardGpuTransaction(run->Core, EditorCommandStatus::StaleEntity,
                                                            "Vulkan normals discarded; previous normals retained.");
    }

    EditorNormalTransactionHandle MakeEditorNormalTransactionForTest(
        const EditorProcessingCommands& commands, const NormalEstimationConfig& c, std::vector<glm::vec3> front,
        Graphics::GpuPropertyResidency* residency)
    {
        namespace NT = NormalTransactionDetail;
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        // Like the transaction's capture without the backend admission: the seam runs on a
        // null device.
        auto config = c;
        config.Backend = NormalEstimationBackend::CpuKDTree;
        std::string diagnostic;
        auto work = CaptureNormalWork(context, config, diagnostic, CapturePurpose::ExecuteResident);
        if (!work || (work->Config.Method != NormalEstimationMethod::MeshFaceWeighted &&
                      work->Config.Method != NormalEstimationMethod::MeshFaceNormals &&
                      work->Config.Method != NormalEstimationMethod::PointSetPCA) ||
            front.size() != work->Result.SlotCount)
            return {};
        work->Config.Backend = c.Backend;
        work->Result.RequestedBackend = c.Backend;
        auto w = NT::Make(context, work, residency);
        if (residency)
        {
            auto& ring = w->Core.Rings[0];
            if (const auto back = AcquireGpuPropertyOutput(*residency, context.World, work->Entity, work->Config.Output, w->Count, NT::kRingDepth);
                back && residency->Publish(ring.Key))
                ++w->Previews;
            ring.Generation = residency->RingGeneration(ring.Key);
        }
        else ++w->Previews;
        w->TestFront = std::move(front);
        w->Core.TestFront = true;
        auto& r = work->Result;
        r.Status = EditorCommandStatus::Pending;
        r.Message = "The GPU normals wait for Accept or Discard.";
        r.ActualBackend = std::string{NT::BackendOf(*work)};
        GeometryProcessingDetail::ReadyGpuTransaction(w->Core);
        return w;
    }

    std::optional<EditorNormalTopologyResidency> ResolveEditorNormalTopology(
        const EditorProcessingCommands& commands, const NormalEstimationConfig& c, Graphics::GpuPropertyResidency& residency,
        std::string& diagnostic)
    {
        namespace NT = NormalTransactionDetail;
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        auto config = c;
        config.Backend = NormalEstimationBackend::CpuKDTree;
        auto work = CaptureNormalWork(context, config, diagnostic, CapturePurpose::ExecuteResident);
        if (!work) return std::nullopt;
        if (work->Config.Method != NormalEstimationMethod::MeshFaceWeighted &&
            work->Config.Method != NormalEstimationMethod::MeshFaceNormals)
        {
            diagnostic = "The topology bundle serves mesh_face_weighted and mesh_face_normals.";
            return std::nullopt;
        }
        std::vector<std::uint32_t> bundle;
        Graphics::VertexNormalsTopologyLayout layout{};
        std::optional<Graphics::GpuPropertyView> view;
        bool uploaded = false;
        switch (NT::ResolveTopology(context, residency, *work, bundle, layout, view, uploaded, diagnostic))
        {
        case NT::TopologyState::Failed: return std::nullopt;
        case NT::TopologyState::Deferred: diagnostic = "The GPU residency refused the topology bundle."; return std::nullopt;
        case NT::TopologyState::Ready: break;
        }
        return EditorNormalTopologyResidency{.Bytes = layout.Bytes(), .Uploaded = uploaded, .Faces = layout.Faces, .LiveRows = layout.LiveRows};
    }

    ActionReadiness PreviewEditorNormalEstimationCommand(
        const EditorProcessingCommands &commands, const NormalEstimationConfig &config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = CaptureNormalWork(context, config, diagnostic, CapturePurpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    namespace
    {
        // Apply's run (a batch) or a start's (`config.Execution`): Vulkan as its transaction (whose step run keeps it
        // until it ends), the CPU backends as a one-shot worker step run of the session's service; without that
        // service (or without a job lane) only a batch runs, as one queued job (or inline).
        EditorNormalEstimationResult RunNormalEstimation(const EditorProcessingCommands& commands,
            const NormalEstimationConfig& config,
            std::function<void(EditorNormalEstimationResult)> onComplete,
            const StepRunExecution& execution)
        {
            const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
            std::string diagnostic;
            auto w = CaptureNormalWork(context,
                config,
                diagnostic,
                config.Backend == NormalEstimationBackend::Vulkan ? CapturePurpose::ExecuteResident
                                                                  : CapturePurpose::Execute);
            const auto report = [&config, &w](EditorCommandStatus status, std::string message)
            {
                auto result = w
                    ? w->Result
                    : EditorNormalEstimationResult{
                          .Method = config.Method, .RequestedBackend = config.Backend, .Output = config.Output};
                result.Status = status;
                result.Message = std::move(message);
                return result;
            };
            if (!w)
                return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
            if (w->Config.Backend == NormalEstimationBackend::Vulkan ||
                w->Config.Backend == NormalEstimationBackend::VulkanLBVH)
            {
                auto pending = w->Result;
                const auto run =
                    NormalTransactionDetail::StartVulkan(context, w, pending, std::move(onComplete), execution);
                if (run)
                    pending.StepRun = run->Core.StepRun;
                return pending;
            }
            const bool batch = execution.Start == StepRunStart::Run && execution.AutoAccept != StepRunAutoAccept::Never;
            if (!batch && (!context.StepRuns || !context.JobCommands.Available()))
                return report(EditorCommandStatus::InvalidProcessingParameters,
                    "Normal estimation needs the step-run service and a job lane for its run controls; none is "
                    "composed.");
            if (w->Config.Method == NormalEstimationMethod::PointSetPCA &&
                w->Config.Backend != NormalEstimationBackend::CpuKDTree)
            {
                const auto indexState = GeometryProcessingDetail::AcquirePointIndex(*context.SpatialIndices,
                    context.World,
                    w->Entity,
                    w->Config.Positions,
                    w->Slots,
                    w->Points,
                    w->GpuIndex,
                    w->Index,
                    w->Result.IndexReused,
                    diagnostic);
                if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                    return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
                if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                    return report(
                        EditorCommandStatus::StaleEntity, "Normal index snapshot does not match the selected samples.");
            }
            if (!context.JobCommands.Available())
            {
                ComputeNormals(*w);
                return PublishNormals(context, w);
            }
            const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                .Scope = ToEditorJobScope(w->Config.Output.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::Normal,
                .OutputName = w->Config.Output.Name,
                .RequestedDomain = EditorJobDomainOfBackend(ToString(config.Backend))};
            namespace MS = GeometryProcessingDetail::MeshSupport;
            if (auto busy = MS::ActiveOutputJobRefusal(context, identity, "Normal estimation"))
                return report(EditorCommandStatus::Pending, std::move(busy->Message));
            auto queued = w->Result;
            queued.Message = "Normal estimation queued.";
            const MS::QueuedJobDelivery<EditorNormalEstimationResult> delivery{
                context, std::move(onComplete), std::move(queued), "Normal estimation"};
            if (context.StepRuns)
                return MS::StartOneShotCpuRun<NormalWork, EditorNormalEstimationResult>(
                    context,
                    {.Label = "Normal estimation",
                        .Owner = "Runtime.NormalEstimation",
                        .Identity = identity,
                        .Execution = execution},
                    w,
                    [](NormalWork& work)
                    {
                        ComputeNormals(work);
                    },
                    [context](const NormalWork& work)
                    {
                        return CurrentNormalInput(context, work, true);
                    },
                    [context](const std::shared_ptr<NormalWork>& work)
                    {
                        return PublishNormals(context, work);
                    },
                    delivery);
            JobDesc desc{.DebugName = "Normal estimation",
                .Scope = context.World,
                .Work = [w](const JobCancellation&) -> JobResultEnvelope
                {
                    ComputeNormals(*w);
                    return JobResultEnvelope::Make(true);
                },
                .ValidateBeforeApply =
                    [context, w]
                {
                    return MS::ValidateQueuedJob(w->Abandoned, CurrentNormalInput(context, *w, true));
                },
                .PublishCompletion =
                    [context, w, delivery](KernelEventBus&, const JobResultEnvelope&)
                {
                    return delivery.Publish(PublishNormals(context, w));
                },
                .FinalizeUnpublishedOnMainThread =
                    [w, delivery]
                {
                    w->Abandoned = true;
                    delivery.FinalizeAfterWorker(w->Result);
                }};
            if (!context.JobCommands.Submit(std::move(desc), identity).IsValid())
            {
                w->Abandoned = true;
                return delivery.Rejected();
            }
            return delivery.Pending();
        }
    }
    EditorNormalEstimationResult ApplyEditorNormalEstimationCommand(const EditorProcessingCommands& commands,
        const NormalEstimationConfig& config,
        std::function<void(EditorNormalEstimationResult)> onComplete)
    {
        // A batch: the device or the worker computes at once and the result is accepted at its end.
        return RunNormalEstimation(commands, config, std::move(onComplete), kOneShotBatchExecution);
    }
    EditorNormalEstimationResult StartEditorNormalEstimationRun(const EditorProcessingCommands& commands,
        const NormalEstimationConfig& config,
        std::function<void(EditorNormalEstimationResult)> onComplete)
    {
        return RunNormalEstimation(commands, config, std::move(onComplete), config.Execution);
    }
    EditorNormalEstimationResult StartEditorConfiguredNormalEstimation(
        const EditorProcessingCommands& commands, std::function<void(EditorNormalEstimationResult)> onComplete)
    {
        const auto config = GetEditorNormalEstimationConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                .Message = "Normal estimation config is unavailable."};
        return StartEditorNormalEstimationRun(commands, *config, std::move(onComplete));
    }
    EditorNormalEstimationResult ApplyEditorConfiguredNormalEstimation(
        const EditorProcessingCommands &commands, std::function<void(EditorNormalEstimationResult)> onComplete)
    {
        const auto config = GetEditorNormalEstimationConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Normal estimation config is unavailable."};
        return ApplyEditorNormalEstimationCommand(commands, *config, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime
