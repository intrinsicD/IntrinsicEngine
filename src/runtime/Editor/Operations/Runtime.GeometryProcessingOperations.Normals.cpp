module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <string_view>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.NormalOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Geometry.Graph;
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
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"

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
            std::shared_ptr<SpatialNearestBatch> Batch{};
            std::vector<std::uint32_t> NeighborOffsets{0}, NeighborIndices{};
            std::size_t NextQuery{};
            bool GpuFinished{}, Abandoned{};
            std::chrono::steady_clock::time_point GpuStarted{};
            EditorNormalEstimationResult Result{};
        };
        bool ReadMask(const GeometryEntityAvailability &a, D domain, std::string name, std::size_t count,
                      std::vector<bool> &mask, std::vector<Watch> &watches)
        {
            const auto *props = ResolveGeometryPropertySet(a, domain);
            if (!props || props->Size() != count)
                return false;
            watches.push_back(Detail::ObserveGeometryProperty(a, domain, name));
            mask.assign(count, false);
            if (!props->Exists(name))
                return true;
            const auto p = props->Get<bool>(name);
            if (!p || p.Size() != count)
                return false;
            mask = p.Vector();
            return true;
        }
        enum class CapturePurpose
        {
            Execute,
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
            auto work = std::make_shared<NormalWork>();
            work->Config = c;
            work->Entity = *entity;
            work->Result.Method = c.Method;
            work->Result.RequestedBackend = c.Backend;
            work->Result.Output = c.Output;
            work->Result.SlotCount = outputProps->Size();
            work->OutputWatch = Detail::ObserveGeometryProperty(a, c.Output.Domain, c.Output.Name);
            if (purpose == CapturePurpose::Execute)
            {
                if (work->OutputWatch.Revision.has_value())
                    work->Before = outputProps->Get<glm::vec3>(c.Output.Name).Vector();
                work->After = work->OutputWatch.Revision.has_value() ? work->Before
                                                       : std::vector<glm::vec3>(outputProps->Size(), glm::vec3(0));
            }
            Detail::PointInputCapture input;
            const bool captured = purpose == CapturePurpose::Execute
                ? Detail::CapturePointInput(a, c.Positions, true, input, diagnostic)
                : Detail::PreparePointInput(context, *entity, a, c.Positions, input, diagnostic);
            if (!captured)
                return {};
            work->Inputs = std::move(input.Inputs);
            work->Result.LiveCount = input.LiveCount;
            const bool lbvhCoordinatesValid = input.ValidLbvh;
            if (purpose == CapturePurpose::Execute)
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
                std::vector<bool> deletedFaces;
                if (!ReadMask(a, D::MeshFace, "f:deleted", faces->Size(), deletedFaces, work->Inputs))
                    return fail("Invalid face deletion mask.");
                const auto *edges = ResolveGeometryPropertySet(a, D::MeshEdge);
                std::vector<bool> deletedEdges;
                if (!edges || halves->Size() != 2 * edges->Size() ||
                    !ReadMask(a, D::MeshEdge, "e:deleted", edges->Size(), deletedEdges, work->Inputs))
                    return fail("Invalid mesh edge deletion mask or halfedge cardinality.");
                if (faceNormals)
                    work->Result.LiveCount = std::ranges::count(deletedFaces, false);
                if (purpose == CapturePurpose::Readiness)
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
            std::vector<bool> deletedEdges;
            if (!ReadMask(a, edgeDomain, "e:deleted", edges->Size(), deletedEdges, work->Inputs))
                return fail("Invalid edge deletion mask.");
            if (purpose == CapturePurpose::Readiness)
                return work;
            work->GraphVertices.Resize(props->Size());
            work->GraphEdges.Resize(edges->Size());
            work->GraphHalfedges.Resize(edges->Size() * 2);
            work->GraphVertices.GetOrAdd<glm::vec3>(c.Positions.Name).Vector() =
                props->Get<glm::vec3>(c.Positions.Name).Vector();
            work->GraphVertices.GetOrAdd<bool>("v:deleted").Vector() = work->Deleted;
            work->GraphEdges.GetOrAdd<bool>("e:deleted").Vector() = deletedEdges;
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
        bool AdvanceNormalGpu(const EditorProcessingContext& context, NormalWork& w)
        {
            auto fail = [&](std::string message, EditorCommandStatus status = EditorCommandStatus::GeometryProcessingFailed) {
                w.Result.Status = status;
                w.Result.Message = std::move(message);
                w.Batch.reset();
                return true;
            };
            if (w.Abandoned || !CurrentNormalInput(context, w, true))
                return fail("Normal inputs changed or the job was cancelled before GPU completion.", EditorCommandStatus::StaleEntity);
            if (w.GpuFinished) return true;
            if (w.GpuStarted == std::chrono::steady_clock::time_point{})
                w.GpuStarted = std::chrono::steady_clock::now();
            if (w.Batch)
            {
                if (w.Batch->State == SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
                if (w.Batch->State != SpatialQueryState::Ready) return false;
                const auto& batch = *w.Batch;
                for (auto count : batch.Counts)
                    if (count > batch.Capacity)
                        return fail("Vulkan radius neighborhood exceeds 1024 candidates; use a CPU backend or a smaller radius. Previous normals retained.");
                for (std::size_t row = 0; row < batch.Counts.size(); ++row)
                {
                    for (std::uint32_t j = 0; j < batch.Counts[row]; ++j)
                        w.NeighborIndices.push_back(batch.Neighbors[row * batch.Capacity + j].Index);
                    w.NeighborOffsets.push_back(std::uint32_t(w.NeighborIndices.size()));
                }
                w.NextQuery += batch.Counts.size();
                if (w.NextQuery == w.Points.size())
                {
                    w.Batch.reset();
                    w.GpuFinished = true;
                    w.Result.ActualBackend = "vulkan_lbvh";
                    w.Result.GpuNeighborhoodMilliseconds = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - w.GpuStarted).count();
                    return true;
                }
            }
            const auto count = std::min<std::size_t>(w.Config.GpuQueryBatchSize, w.Points.size() - w.NextQuery);
            if (w.Batch && w.Batch->Counts.size() != count) w.Batch.reset();
            const auto queries = std::span(w.Points).subspan(w.NextQuery, count);
            if (w.Config.UseRadiusSearch)
                w.Batch = context.SpatialIndices->QueueGpuRadius(w.GpuIndex, queries, w.Config.Radius,
                    std::uint32_t(std::min<std::size_t>(1024, w.Points.size())), {}, std::move(w.Batch));
            else
                w.Batch = context.SpatialIndices->QueueGpuKNearest(w.GpuIndex, queries,
                    std::uint32_t(std::min<std::uint64_t>(w.Points.size(),
                        std::uint64_t(std::max(w.Config.KNeighbors, w.Config.MinimumNeighbors)) + 1)),
                    {}, std::move(w.Batch));
            ++w.Result.GpuQueryBatches;
            if (w.Batch->State == SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
            return false;
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
        auto w = CaptureNormalWork(context, config, diagnostic);
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
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->GpuFinished; },
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
