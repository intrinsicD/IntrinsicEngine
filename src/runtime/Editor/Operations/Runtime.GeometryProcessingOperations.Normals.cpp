module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <string_view>
#include <functional>
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
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.VertexNormals;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.TransferQueue;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"

namespace Extrinsic::Runtime
{
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
            GeometryProcessingDetail::GpuRowPages Pages{};
            std::vector<std::uint32_t> NeighborOffsets{0}, NeighborIndices{};
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
            if (c.Backend == NormalEstimationBackend::Vulkan)
            {
                using Weighting = Geometry::HalfedgeMesh::VertexNormals::AveragingMode;
                if (c.Method != NormalEstimationMethod::MeshFaceWeighted)
                    return fail("Backend vulkan runs mesh_face_weighted normals on the GPU property residency; "
                                "point-set PCA uses vulkan_lbvh and the other methods run on the CPU.");
                if (c.Weighting == Weighting::AngleWeighted || c.Weighting == Weighting::AreaAngleWeighted)
                    return fail("Vulkan vertex normals support uniform, area and max weighting; the angle weightings run on the CPU.");
                if (c.Output.Name == "v:position")
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
            if (c.Backend == NormalEstimationBackend::Vulkan)
            {
                // A device without float32 denorm preservation would flush a subnormal
                // coordinate on the float -> double conversion and diverge from the reference.
                if (input.HasSubnormalCoordinates)
                    return fail("Vulkan vertex normals require normal or zero coordinate components; subnormal coordinates are unsupported.");
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
                    if (!context.JobCommands.Available() || !context.SpatialIndices ||
                        !context.SpatialIndices->GpuQueriesAvailable())
                        return fail("Vulkan normal neighborhoods require the framed spatial cache and job service.");
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
                std::optional<PN::EstimateResult> estimate;
                if (c.Backend == NormalEstimationBackend::VulkanLBVH)
                {
                    r.ActualBackend = "vulkan_lbvh";
                    // Readback IDs are source rows. Conversion and fitting stay on the CPU worker.
                    for (auto& id : w.NeighborIndices)
                    {
                        const auto found = std::lower_bound(w.Slots.begin(), w.Slots.end(), id);
                        if (found == w.Slots.end() || *found != id)
                        {
                            r.Message = "Vulkan normal query returned an invalid source row.";
                            return;
                        }
                        id = std::uint32_t(found - w.Slots.begin());
                    }
                    estimate = PN::Estimate(w.Points, PN::Neighborhoods{w.NeighborOffsets, w.NeighborIndices}, p);
                }
                else
                {
                    estimate = w.Index ? PN::Estimate(w.Points, w.Index->Index, p) : PN::Estimate(w.Points, p);
                    r.ActualBackend = w.Index ? "cpu_lbvh" : "cpu_kdtree";
                }
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
                const glm::dvec3 fallback(c.FallbackNormal);
                const double fallbackLength = glm::length(fallback);
                const glm::vec3 fallbackNormal = fallbackLength > c.DegenerateNormalLengthEpsilon
                    ? glm::vec3(fallback / fallbackLength) : glm::vec3{0, 0, 1};
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
            r.Message = c.Method == NormalEstimationMethod::PointSetPCA &&
                        c.Backend == NormalEstimationBackend::VulkanLBVH
                ? "Normals computed using Vulkan LBVH neighborhoods and CPU PCA/orientation."
                : "Normals computed using " + r.ActualBackend + ".";
        }
        // Rows become CSR neighborhoods of raw source IDs; Compute decodes them.
        bool AdvanceNormalGpu(const EditorProcessingContext& context, NormalWork& w)
        {
            if (w.Abandoned || !CurrentNormalInput(context, w, true))
            {
                w.Result.Status = EditorCommandStatus::StaleEntity;
                w.Result.Message = "Normal inputs changed or the job was cancelled before GPU completion.";
                w.Pages.Batch.reset();
                return true;
            }
            const auto state = GeometryProcessingDetail::AdvanceGpuRowPages(
                w.Pages, w.Points.size(), w.Config.GpuQueryBatchSize, w.Result.Message,
                [&](const SpatialNearestBatch& batch, std::string& why)
                {
                    for (auto count : batch.Counts)
                        if (count > batch.Capacity)
                        {
                            why = "Vulkan radius neighborhood exceeds 1024 candidates; use a CPU backend or a smaller radius. Previous normals retained.";
                            return false;
                        }
                    for (std::size_t row = 0; row < batch.Counts.size(); ++row)
                    {
                        for (std::uint32_t j = 0; j < batch.Counts[row]; ++j)
                            w.NeighborIndices.push_back(batch.Neighbors[row * batch.Capacity + j].Index);
                        w.NeighborOffsets.push_back(std::uint32_t(w.NeighborIndices.size()));
                    }
                    return true;
                },
                [&](std::size_t first, std::size_t count, std::shared_ptr<SpatialNearestBatch> reuse)
                {
                    const auto queries = std::span(w.Points).subspan(first, count);
                    if (w.Config.UseRadiusSearch)
                        return context.SpatialIndices->QueueGpuRadius(w.GpuIndex, queries, w.Config.Radius,
                            std::uint32_t(std::min<std::size_t>(1024, w.Points.size())), {}, std::move(reuse));
                    return context.SpatialIndices->QueueGpuKNearest(w.GpuIndex, queries,
                        std::uint32_t(std::min<std::uint64_t>(w.Points.size(),
                            std::uint64_t(std::max(w.Config.KNeighbors, w.Config.MinimumNeighbors)) + 1)),
                        {}, std::move(reuse));
                });
            w.Result.GpuQueryBatches = w.Pages.QueryBatches;
            if (state == GeometryProcessingDetail::RowsState::Ready)
            {
                w.Result.ActualBackend = "vulkan_lbvh";
                w.Result.GpuNeighborhoodMilliseconds = w.Pages.Milliseconds;
            }
            if (state == GeometryProcessingDetail::RowsState::Failed)
                w.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
            return state != GeometryProcessingDetail::RowsState::Pending;
        }
        EditorNormalEstimationResult PublishNormals(const EditorProcessingContext &context,
                                                    const std::shared_ptr<NormalWork> &w)
        {
            auto &r = w->Result;
            if (!CurrentNormalInput(context, *w, true))
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
            auto revision = std::make_shared<Watch>(w->OutputWatch);
            const auto mutate = [context, entity = w->Entity, inputs = w->Inputs, output = w->Config.Output,
                                 revision](const OutputState &expected, const OutputState &target) {
                if (!Detail::GeometryPropertiesCurrent(context, entity, inputs))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto *props = Detail::MutableGeometryProperties(context.Scene->Raw(), entity, output.Domain);
                const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
                if (!props || Detail::ObserveGeometryProperty(a, revision->Domain, revision->Name) != *revision)
                    return EditorCommandHistoryStatus::StaleEntity;
                const auto &read = std::as_const(*props);
                const auto current = read.Get<glm::vec3>(output.Name);
                // Deleted output rows may contain NaNs; compare their stored bits.
                if (expected.Exists && (!current || !GeometryValueComparison::BitEqual(current.Vector(), expected.Values)))
                    return EditorCommandHistoryStatus::StaleEntity;
                if (target.Exists)
                    props->GetOrAdd<glm::vec3>(output.Name).Vector() = target.Values;
                else if (auto p = props->Get<glm::vec3>(output.Name))
                    props->Remove(p);
                *revision = Detail::ObserveGeometryProperty(BuildGeometryAvailability(context.Scene->Raw(), entity), revision->Domain,
                                    revision->Name);
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

    // The Vulkan run's job state (RUNTIME-296, ADR 0030 decision 1): the capture, the residency
    // slots it reads and writes, the device workspace and the transaction phase the panel drives.
    struct EditorNormalTransaction
    {
        EditorProcessingContext Context{};
        std::shared_ptr<NormalWork> Work{}; // its Result is the transaction's result
        EditorJobIdentity Identity{};
        Graphics::GpuPropertyResidency* Residency{};
        Graphics::GpuPropertyKey Key{};   // the output ring
        std::uint64_t Ring{};             // the ring generation this run acquired: the only one it discards
        std::uint32_t Count{};            // vertex rows
        // Input: the canonical positions slot; Base: the output's canonical slot when the
        // property exists; Topology: the resident bundle; Back: the ring write slot.
        std::optional<Graphics::GpuPropertyView> Input{}, Base{}, Topology{}, Back{};
        Graphics::VertexNormalsTopologyLayout Layout{};
        std::vector<std::uint32_t> Bundle{};
        std::shared_ptr<Graphics::VertexNormalsWorkspace> Workspace{};
        std::shared_ptr<SpatialGpuResult> Gpu{};
        bool StoreRecorded{};
        std::uint32_t Deferrals{}, Previews{};
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        bool AutoAccept{}, Abandoned{}, Delivered{};
        std::function<void(EditorNormalEstimationResult)> Sink{};
        std::shared_ptr<GeometryProcessingDetail::GpuFrontReadback> Readback{};
        std::optional<std::vector<glm::vec3>> TestFront{};
    };

    namespace NormalTransactionDetail
    {
        using Run = EditorNormalTransactionHandle;
        namespace GP = GeometryProcessingDetail;
        constexpr std::uint32_t kRingDepth = 2u;    // terminal-only output (ADR 0030 decision 4)
        constexpr std::uint32_t kMaxDeferrals = 600u; // frames the residency may refuse before the run fails
        constexpr std::string_view kBackend = "vulkan_mesh_face_weighted";

        // The topology bundle's residency key: derived (no CPU property of that name), one per
        // entity, so `Prune` drops it with the entity.
        Graphics::GpuPropertyKey TopologyKeyFor(const EditorProcessingContext& ctx, const entt::entity entity)
        {
            return MakeGpuPropertyKey(ctx.World, entity,
                {.Domain = D::MeshFace, .Name = "#vertex_normal_topology", .ValueKind = Geometry::PropertyValueKind::UInt32});
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
            const auto table = MN::GatherFaceCornerTable(built.Mesh);
            std::vector<std::uint32_t> faceOffsets(w.FaceCount + 1u, 0u), corners;
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
            layout = Graphics::PackVertexNormalsTopology(faceOffsets, corners, std::uint32_t(w.Deleted.size()), w.Slots, bundle);
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
            const auto key = TopologyKeyFor(ctx, w.Entity);
            const auto signature = SignatureOf(w);
            const auto vertices = std::uint32_t(w.Deleted.size()), liveRows = std::uint32_t(w.Slots.size());
            const auto faces = std::uint32_t(w.FaceCount);
            if (layout.Words == 0u)
            {
                if (const auto resident = r.HasRing(key) ? std::nullopt : r.Front(key);
                    resident && resident->Revision == signature && resident->Layout.Scalar == Graphics::GpuScalarType::UInt32 &&
                    resident->Layout.Channels == 1u && resident->Layout.Stride == 0u)
                    layout = Graphics::UnpackVertexNormalsTopologyLayout(faces, vertices, liveRows, resident->Layout.Count);
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

        bool Current(const Run& w)
        {
            return !w->Abandoned && CurrentNormalInput(w->Context, *w->Work, true) && GP::EditorProcessingContextWorldCurrent(w->Context);
        }
        void Deliver(const Run& w, EditorNormalEstimationResult result)
        {
            w->Work->Result = result;
            if (w->Delivered) return;
            w->Delivered = true;
            if (w->Sink) w->Sink(std::move(result));
        }
        void ReleaseRings(const Run& w)
        {
            w->Input.reset();
            w->Base.reset();
            w->Topology.reset();
            w->Back.reset();
            if (w->Readback) w->Readback->Lease.reset();
            if (w->Residency) (void)w->Residency->Discard(w->Key, w->Ring);
        }
        void Finish(const Run& w, const EditorGpuTransactionPhase phase, const EditorCommandStatus status, std::string message)
        {
            if (w->Readback) w->Readback->Abandoned = true; // a framed readback still queued records nothing
            ReleaseRings(w);
            w->Phase = phase;
            auto result = w->Work->Result;
            result.Status = status;
            result.Message = std::move(message);
            Deliver(w, std::move(result));
        }
        Graphics::VertexNormalsResidentView View(const std::optional<Graphics::GpuPropertyView>& v)
        {
            if (!v) return {};
            return {.Buffer = v->Buffer, .Address = v->Address};
        }
        // The frame whose commands touch the run's slots (their reuse waits for it).
        void NoteUses(const Run& w)
        {
            if (!w->Residency || !w->Context.Device) return;
            const auto frame = w->Context.Device->GetGlobalFrameNumber();
            for (const auto* view : {&w->Input, &w->Base, &w->Topology, &w->Back})
                if (*view) w->Residency->NoteUse((*view)->Buffer, frame);
        }
        Graphics::VertexNormalsGpuParams Params(const NormalEstimationConfig& c)
        {
            using Weighting = Geometry::HalfedgeMesh::VertexNormals::AveragingMode;
            Graphics::VertexNormalsGpuParams params{};
            params.Weighting = c.Weighting == Weighting::UniformFace ? Graphics::VertexNormalGpuWeighting::UniformFace
                             : c.Weighting == Weighting::MaxWeighted ? Graphics::VertexNormalGpuWeighting::MaxWeighted
                             : Graphics::VertexNormalGpuWeighting::AreaWeighted;
            params.Epsilon = c.DegenerateNormalLengthEpsilon;
            bool repaired = false;
            const auto fallback = MN::ResolveFallbackNormal(
                {.FallbackNormal = c.FallbackNormal, .DegenerateNormalLengthEpsilon = c.DegenerateNormalLengthEpsilon}, repaired);
            params.Fallback = {fallback.x, fallback.y, fallback.z};
            return params;
        }
        // The slots a submission needs. Deferred while the residency refuses (a refused upload or
        // an exhausted ring); Failed when the topology cannot be built.
        TopologyState AcquireSlots(const Run& w, std::string& why)
        {
            auto& r = *w->Residency;
            const auto& ctx = w->Context;
            auto& work = *w->Work;
            const auto& c = work.Config;
            auto& io = work.Result;
            if (!w->Input)
            {
                const auto before = r.Stats().UploadBytes;
                w->Input = ResolveGpuPropertyInput(r, *ctx.Scene, ctx.World, work.Entity, c.Positions);
                if (!w->Input || w->Input->Layout.Count != w->Count) { w->Input.reset(); return TopologyState::Deferred; }
                io.GpuInputUploadBytes += r.Stats().UploadBytes - before;
            }
            // An existing output keeps its bytes outside the live rows: the store copies its
            // canonical slot first.
            if (work.OutputWatch.Revision.has_value() && !w->Base)
            {
                const auto before = r.Stats().UploadBytes;
                w->Base = ResolveGpuPropertyInput(r, *ctx.Scene, ctx.World, work.Entity, c.Output);
                if (!w->Base || w->Base->Layout.Count != w->Count) { w->Base.reset(); return TopologyState::Deferred; }
                io.GpuInputUploadBytes += r.Stats().UploadBytes - before;
            }
            if (!w->Topology)
            {
                bool uploaded = false;
                const auto state = ResolveTopology(ctx, r, work, w->Bundle, w->Layout, w->Topology, uploaded, why);
                if (state != TopologyState::Ready) return state;
                io.GpuTopologyBytes = w->Layout.Bytes();
                io.GpuTopologyReused = !uploaded;
            }
            if (!w->Back)
            {
                w->Back = AcquireGpuPropertyOutput(r, ctx.World, work.Entity, c.Output, w->Count, kRingDepth);
                if (!w->Back) return TopologyState::Deferred;
                w->Ring = r.RingGeneration(w->Key);
            }
            return TopologyState::Ready;
        }
        std::shared_ptr<SpatialGpuResult> FailedResult(std::string why)
        {
            auto result = std::make_shared<SpatialGpuResult>();
            result->State = SpatialQueryState::Failed;
            result->Diagnostic = std::move(why);
            return result;
        }
        bool Defer(const Run& w)
        {
            if (++w->Deferrals < kMaxDeferrals) return false;
            w->Gpu = FailedResult("The GPU property residency refused the run's input, topology or output slot; previous normals retained.");
            return true;
        }
        void PublishPreview(const Run& w)
        {
            const bool recorded = std::exchange(w->StoreRecorded, false);
            w->Back.reset();
            if (recorded && w->Residency && w->Residency->Publish(w->Key)) ++w->Previews;
        }
        // One submission: both passes; the readback is the stats word triple.
        void Queue(const Run& w)
        {
            const auto& ctx = w->Context;
            w->Workspace = std::make_shared<Graphics::VertexNormalsWorkspace>(*ctx.Device);
            w->StoreRecorded = true;
            w->Gpu = ctx.SpatialIndices->QueueGpuCompute(std::size_t(Graphics::VertexNormalsWorkspace::StatsReadbackBytes),
                [w](RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle {
                    if (w->Abandoned || !w->Back) return {};
                    NoteUses(w);
                    const Graphics::VertexNormalsResidentIo io{.Positions = View(w->Input), .Topology = View(w->Topology),
                                                               .Output = View(w->Back), .Base = View(w->Base),
                                                               .OutputBytes = w->Back->Bytes, .Layout = w->Layout};
                    return w->Workspace->Record(commands, Params(w->Work->Config), io);
                });
        }
        // Main-thread readiness poll of the compute job.
        bool Poll(const Run& w)
        {
            if (!Current(w)) return true;
            if (!w->Gpu)
            {
                std::string why;
                switch (AcquireSlots(w, why))
                {
                case TopologyState::Failed: w->Gpu = FailedResult(std::move(why)); return true;
                case TopologyState::Deferred: return Defer(w);
                case TopologyState::Ready: break;
                }
                Queue(w);
                return false;
            }
            if (w->Gpu->State == SpatialQueryState::Ready) PublishPreview(w);
            return w->Gpu->State == SpatialQueryState::Ready || w->Gpu->State == SpatialQueryState::Failed;
        }
        // The compute job ended: the run either waits for Accept or failed.
        void CompleteRun(const Run& w)
        {
            w->Input.reset();
            w->Base.reset();
            w->Topology.reset();
            auto& r = w->Work->Result;
            if (!w->Gpu || w->Gpu->State != SpatialQueryState::Ready)
            {
                Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                       w->Gpu && !w->Gpu->Diagnostic.empty() ? w->Gpu->Diagnostic
                                                            : "Vulkan vertex normals did not return a result; previous normals retained.");
                return;
            }
            if (w->Previews == 0u)
            {
                Finish(w, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                       "Vulkan vertex normals published no preview; previous normals retained.");
                return;
            }
            if (w->Gpu->Data.size() == sizeof(std::uint32_t) * 3u)
            {
                std::array<std::uint32_t, 3> stats{};
                std::memcpy(stats.data(), w->Gpu->Data.data(), sizeof(stats));
                r.ProcessedFaces = stats[0];
                r.ValidCount = stats[1];
                r.FallbackCount = stats[2];
            }
            w->Phase = EditorGpuTransactionPhase::ReadyToAccept;
            r.Status = EditorCommandStatus::Pending;
            r.Message = "The GPU normals wait for Accept or Discard.";
        }
        std::string IoSummary(const EditorNormalEstimationResult& r)
        {
            return "positions upload: " + std::to_string(r.GpuInputUploadBytes) + " bytes; topology bundle: " +
                   std::to_string(r.GpuTopologyBytes) + " bytes (" + (r.GpuTopologyReused ? "resident" : "uploaded") + ")";
        }
        // Accept landed: the front's rows become the published property and the canonical slot.
        void CompleteAccept(const Run& w)
        {
            const auto& ctx = w->Context;
            auto& work = *w->Work;
            const std::uint64_t accepted = w->Readback && w->Readback->Lease ? w->Readback->Lease->Publication : 0u;
            if (w->Readback) w->Readback->Lease.reset();
            if (w->Readback && w->Readback->Failed)
            {
                Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                       "Vulkan vertex normals readback failed; previous normals retained.");
                return;
            }
            std::vector<glm::vec3> after(w->Count);
            if (w->TestFront) after = *w->TestFront;
            else
            {
                if (!w->Readback || w->Readback->Bytes.size() != std::size_t(w->Count) * sizeof(glm::vec3))
                {
                    Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                           "Vulkan vertex normals readback has the wrong size; previous normals retained.");
                    return;
                }
                std::memcpy(after.data(), w->Readback->Bytes.data(), after.size() * sizeof(glm::vec3));
            }
            work.After = std::move(after);
            auto& r = work.Result;
            r.ActualBackend = std::string{kBackend};
            r.WrittenCount = work.Slots.size();
            r.ChangedCount = 0;
            for (const auto i : work.Slots)
                r.ChangedCount += !work.OutputWatch.Revision.has_value() || work.Before[i] != work.After[i];
            r.Status = r.ChangedCount ? EditorCommandStatus::Applied : EditorCommandStatus::NoChange;
            r.Message = "Normals computed using " + r.ActualBackend + "; " + IoSummary(r) + ".";
            auto result = PublishNormals(ctx, w->Work);
            if (!result.Succeeded())
            {
                ReleaseRings(w);
                w->Phase = result.Status == EditorCommandStatus::StaleEntity ? EditorGpuTransactionPhase::Discarded
                                                                             : EditorGpuTransactionPhase::Failed;
                Deliver(w, std::move(result));
                return;
            }
            // The front becomes the canonical slot of the new CPU revision (ADR 0030 decision 6).
            if (w->Residency)
            {
                const auto watch = GP::ObserveGeometryProperty(BuildGeometryAvailability(ctx.Scene->Raw(), work.Entity),
                                                               work.Config.Output.Domain, work.Config.Output.Name);
                if (!watch.Revision || !w->Residency->BindRevision(w->Key, *watch.Revision, accepted))
                    (void)w->Residency->Discard(w->Key, w->Ring);
            }
            w->Phase = EditorGpuTransactionPhase::Applied;
            Deliver(w, std::move(result));
        }
        // Queues the front's readback and the job that publishes it.
        EditorNormalEstimationResult BeginAccept(const Run& w, std::function<void(EditorNormalEstimationResult)> onComplete)
        {
            auto result = w->Work->Result;
            const auto refuse = [&](const EditorCommandStatus status, std::string message) {
                result.Status = status;
                result.Message = std::move(message);
                return result;
            };
            if (w->Phase != EditorGpuTransactionPhase::ReadyToAccept)
                return refuse(EditorCommandStatus::InvalidProcessingParameters, "No GPU result waits for Accept.");
            if (!Current(w))
                return refuse(EditorCommandStatus::StaleEntity, "The inputs changed since the run; discard the result and run again.");
            const auto& ctx = w->Context;
            if (onComplete) w->Sink = GuardEditorProcessingResult(ctx, std::move(onComplete));
            w->Readback.reset();
            if (!w->TestFront)
            {
                w->Readback = std::make_shared<GP::GpuFrontReadback>();
                if (!w->Residency || !GP::BeginGpuFrontReadback(ctx, *w->Residency, w->Key, w->Readback))
                {
                    w->Readback.reset();
                    Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                           "The GPU result is no longer resident; previous normals retained.");
                    return w->Work->Result;
                }
            }
            w->Phase = EditorGpuTransactionPhase::Accepting;
            JobDesc accept{
                .DebugName = "Vulkan vertex normals accept", .Scope = ctx.World, .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                .IsReadyToApply = [w] { return !Current(w) || !w->Readback || GP::PollGpuFrontReadback(*w->Readback); },
                .ValidateBeforeApply = [w] { return Current(w) ? JobApplyValidation::Current : JobApplyValidation::StaleGeneration; },
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) {
                    CompleteAccept(w);
                    return w->Phase == EditorGpuTransactionPhase::Applied;
                },
                .FinalizeUnpublishedOnMainThread = [w] {
                    w->Abandoned = true;
                    if (w->Delivered) return;
                    Finish(w, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                           "Vulkan vertex normals cancelled or stale; previous normals retained.");
                }};
            if (!ctx.JobCommands.Submit(std::move(accept), w->Identity).IsValid())
            {
                Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                       "Vulkan vertex normals accept submission rejected.");
                return w->Work->Result;
            }
            result.Status = EditorCommandStatus::Pending;
            result.Message = "Reading the GPU normals back.";
            w->Work->Result = result;
            return result;
        }
        // Builds the run from the capture and queues its compute job. Null with `result` filled
        // (a rejection) when the request cannot run.
        Run StartVulkan(const EditorProcessingContext& context, const std::shared_ptr<NormalWork>& work,
                        EditorNormalEstimationResult& result, std::function<void(EditorNormalEstimationResult)> onComplete,
                        const bool autoAccept)
        {
            const auto fail = [&](const EditorCommandStatus status, std::string message) {
                result.Status = status;
                result.Message = std::move(message);
                return Run{};
            };
            const auto& c = work->Config;
            const EditorJobIdentity identity{.EntityId = c.StableEntityId, .Scope = ToEditorJobScope(c.Output.Domain),
                                             .OutputSemantic = GeometryPresentationSlotSemantic::Normal, .OutputName = c.Output.Name};
            if (auto active = Detail::MeshSupport::FindActiveEditorJob(context, identity); active && IsActiveEditorJobState(active->State))
                return fail(EditorCommandStatus::Pending, "A normal job for this output is already active.");
            auto* residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
            if (!residency) return fail(EditorCommandStatus::InvalidProcessingParameters, "Vulkan vertex normals need the GPU property residency.");
            auto w = std::make_shared<EditorNormalTransaction>();
            w->Context = context;
            w->Work = work;
            w->Identity = identity;
            w->Residency = residency;
            w->Count = std::uint32_t(work->Deleted.size());
            w->Key = MakeGpuPropertyKey(context.World, work->Entity, c.Output);
            if (residency->HasRing(w->Key))
                return fail(EditorCommandStatus::InvalidProcessingParameters, "A GPU result for this output awaits Accept or Discard.");
            w->AutoAccept = autoAccept;
            w->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            result = work->Result;
            result.Status = EditorCommandStatus::Pending;
            result.Message = "Vulkan vertex normals queued.";
            work->Result = result;
            JobDesc gpu{
                .DebugName = "Vulkan vertex normals", .Scope = context.World, .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                .IsReadyToApply = [w] { return Poll(w); },
                .ValidateBeforeApply = [w] { return Current(w) ? JobApplyValidation::Current : JobApplyValidation::StaleGeneration; },
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) {
                    CompleteRun(w);
                    if (w->Phase == EditorGpuTransactionPhase::ReadyToAccept && w->AutoAccept)
                    {
                        // A refused automatic Accept (e.g. stale) ends the transaction: nothing
                        // waits for a user here.
                        const auto accepted = BeginAccept(w, {});
                        if (accepted.Status != EditorCommandStatus::Pending)
                            Finish(w, accepted.Status == EditorCommandStatus::StaleEntity ? EditorGpuTransactionPhase::Discarded
                                                                                          : EditorGpuTransactionPhase::Failed,
                                   accepted.Status, accepted.Message);
                    }
                    return w->Phase != EditorGpuTransactionPhase::Failed;
                },
                .FinalizeUnpublishedOnMainThread = [w] {
                    w->Abandoned = true;
                    if (w->Delivered) return;
                    Finish(w, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                           "Vulkan vertex normals cancelled or stale; previous normals retained.");
                }};
            if (!context.JobCommands.Submit(std::move(gpu), w->Identity).IsValid())
            {
                w->Abandoned = true;
                return fail(EditorCommandStatus::GeometryProcessingFailed, "Vulkan vertex normals submission rejected.");
            }
            return w;
        }
    } // namespace NormalTransactionDetail

    EditorNormalTransactionHandle StartEditorNormalEstimationTransaction(
        const EditorProcessingCommands& commands, const NormalEstimationConfig& config, EditorNormalEstimationResult& failure)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        failure = {.Method = config.Method, .RequestedBackend = config.Backend, .Output = config.Output};
        failure.Status = EditorCommandStatus::InvalidProcessingParameters;
        if (config.Backend != NormalEstimationBackend::Vulkan)
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
        return NormalTransactionDetail::StartVulkan(context, work, failure, {}, false);
    }

    EditorNormalTransactionSnapshot SnapshotEditorNormalEstimation(const EditorProcessingCommands&,
                                                                   const EditorNormalTransactionHandle& run)
    {
        EditorNormalTransactionSnapshot snapshot;
        if (!run) return snapshot;
        snapshot.Phase = run->Phase;
        snapshot.Previews = run->Previews;
        snapshot.DeviceWorkQueued = run->Gpu != nullptr;
        snapshot.Result = run->Work->Result;
        if (run->Phase == EditorGpuTransactionPhase::ReadyToAccept)
        {
            snapshot.Stale = !NormalTransactionDetail::Current(run);
            const bool resident = run->TestFront.has_value() || (run->Residency && run->Residency->HasRing(run->Key));
            snapshot.CanAccept = !snapshot.Stale && resident;
            if (snapshot.Stale) snapshot.AcceptDisabledReason = "The inputs changed since the run; discard the result and run again.";
            else if (!resident) snapshot.AcceptDisabledReason = "The GPU result is no longer resident; discard it.";
        }
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
        if (!run) return;
        namespace NT = NormalTransactionDetail;
        switch (run->Phase)
        {
        case EditorGpuTransactionPhase::Applied:
        case EditorGpuTransactionPhase::Discarded:
        case EditorGpuTransactionPhase::Failed:
            return;
        case EditorGpuTransactionPhase::Running:
        case EditorGpuTransactionPhase::Accepting:
            // The job finalizes as cancelled on its next drain; the ring goes now (freed after
            // its completions).
            run->Abandoned = true;
            NT::Finish(run, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                       "Vulkan vertex normals discarded; previous normals retained.");
            return;
        case EditorGpuTransactionPhase::ReadyToAccept:
            NT::Finish(run, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                       "Vulkan vertex normals discarded; previous normals retained.");
            return;
        }
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
        if (!work || work->Config.Method != NormalEstimationMethod::MeshFaceWeighted || front.size() != work->Deleted.size())
            return {};
        work->Config.Backend = c.Backend;
        work->Result.RequestedBackend = c.Backend;
        auto w = std::make_shared<EditorNormalTransaction>();
        w->Context = context;
        w->Work = work;
        w->Identity = {.EntityId = c.StableEntityId, .Scope = ToEditorJobScope(work->Config.Output.Domain),
                       .OutputSemantic = GeometryPresentationSlotSemantic::Normal, .OutputName = work->Config.Output.Name};
        w->Count = std::uint32_t(work->Deleted.size());
        w->Key = MakeGpuPropertyKey(context.World, work->Entity, work->Config.Output);
        w->Residency = residency;
        if (residency)
        {
            if (const auto back = AcquireGpuPropertyOutput(*residency, context.World, work->Entity, work->Config.Output, w->Count, NT::kRingDepth);
                back && residency->Publish(w->Key))
                ++w->Previews;
            w->Ring = residency->RingGeneration(w->Key);
        }
        else ++w->Previews;
        w->TestFront = std::move(front);
        auto& r = work->Result;
        r.Status = EditorCommandStatus::Pending;
        r.Message = "The GPU normals wait for Accept or Discard.";
        r.ActualBackend = std::string{NT::kBackend};
        w->Phase = EditorGpuTransactionPhase::ReadyToAccept;
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
        if (work->Config.Method != NormalEstimationMethod::MeshFaceWeighted)
        {
            diagnostic = "The topology bundle serves mesh_face_weighted normals.";
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
    EditorNormalEstimationResult ApplyEditorNormalEstimationCommand(
        const EditorProcessingCommands &commands, const NormalEstimationConfig &config,
        std::function<void(EditorNormalEstimationResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        auto w = CaptureNormalWork(context, config, diagnostic,
                                   config.Backend == NormalEstimationBackend::Vulkan ? CapturePurpose::ExecuteResident
                                                                                      : CapturePurpose::Execute);
        const auto report = [&config, &w](EditorCommandStatus status, std::string message) {
            auto result = w ? w->Result
                            : EditorNormalEstimationResult{.Method = config.Method,
                                                           .RequestedBackend = config.Backend,
                                                           .Output = config.Output};
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!w)
            return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
        if (w->Config.Backend == NormalEstimationBackend::Vulkan)
        {
            // Batch and agent callers accept automatically when the device finishes.
            auto pending = w->Result;
            (void)NormalTransactionDetail::StartVulkan(context, w, pending, std::move(onComplete), true);
            return pending;
        }
        if (w->Config.Method == NormalEstimationMethod::PointSetPCA &&
            w->Config.Backend != NormalEstimationBackend::CpuKDTree)
        {
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(
                *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                return report(EditorCommandStatus::StaleEntity,
                              "Normal index snapshot does not match the selected samples.");
        }
        if (!context.JobCommands.Available())
        {
            ComputeNormals(*w);
            return PublishNormals(context, w);
        }
        const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                                         .Scope = ToEditorJobScope(w->Config.Output.Domain),
                                         .OutputSemantic = GeometryPresentationSlotSemantic::Normal,
                                         .OutputName = w->Config.Output.Name};
        if (auto active = GeometryProcessingDetail::MeshSupport::FindActiveEditorJob(context, identity);
            active && IsActiveEditorJobState(active->State))
            return report(EditorCommandStatus::Pending,
                          "A normal job for this output is already active.");
        auto sink = GuardEditorProcessingResult(context, std::move(onComplete));
        auto delivered = std::make_shared<bool>(false);
        auto pending = w->Result;
        pending.Status = EditorCommandStatus::Pending;
        pending.Message = "Normal estimation queued.";
        JobDesc desc{
            .DebugName = "Normal estimation",
            .Scope = context.World,
            .Kind = RuntimeTaskKinds::GeometryProcess,
            .Work = [w](const JobCancellation &) -> JobResultEnvelope {
                ComputeNormals(*w);
                return JobResultEnvelope::Make(true);
            },
            .ValidateBeforeApply =
                [context, w] {
                    return CurrentNormalInput(context, *w, true) ? JobApplyValidation::Current
                                                                 : JobApplyValidation::StaleGeneration;
                },
            .PublishCompletion =
                [context, w, sink, delivered](KernelEventBus &, const JobResultEnvelope &) {
                    auto result = PublishNormals(context, w);
                    *delivered = true;
                    if (sink)
                        sink(result);
                    return result.Succeeded();
                },
            .FinalizeUnpublishedOnMainThread =
                [sink, delivered, w, pending]() mutable {
                    w->Abandoned = true;
                    if (sink && !*delivered)
                    {
                        if (w->Result.Status == EditorCommandStatus::GeometryProcessingFailed)
                            pending = w->Result;
                        else
                        {
                            pending.Status = EditorCommandStatus::StaleEntity;
                            pending.Message = "Normal job was cancelled or its source became stale; previous output retained.";
                        }
                        sink(std::move(pending));
                    }
                }};
        if (w->Config.Method == NormalEstimationMethod::PointSetPCA &&
            w->Config.Backend == NormalEstimationBackend::VulkanLBVH)
        {
            JobDesc gpu{
                .DebugName = "Normal neighborhoods (Vulkan)", .Scope = context.World,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                .IsReadyToApply = [context, w] { return AdvanceNormalGpu(context, *w); },
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->Pages.Finished; },
                .FinalizeUnpublishedOnMainThread = [w] { w->Abandoned = true; }};
            const auto prerequisite = context.JobCommands.Submit(std::move(gpu), identity);
            if (!prerequisite.IsValid())
                return report(EditorCommandStatus::GeometryProcessingFailed, "GPU normal job submission was rejected.");
            desc.DependsOn.push_back({prerequisite, "Complete Vulkan normal neighborhoods before CPU PCA"});
        }
        const auto token = context.JobCommands.Submit(std::move(desc), identity);
        if (!token.IsValid())
        {
            w->Abandoned = true;
            pending.Status = EditorCommandStatus::GeometryProcessingFailed;
            pending.Message = "Normal job submission was rejected.";
        }
        return pending;
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
