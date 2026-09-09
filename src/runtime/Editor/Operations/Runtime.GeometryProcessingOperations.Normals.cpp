module;
#include <algorithm>
#include <bit>
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
module Extrinsic.Runtime.GeometryProcessingOperations;
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
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.PointCloud.Normals;
import Geometry.Properties;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace PN = Geometry::PointCloud::Normals;
        namespace GN = Geometry::Graph::VertexNormals;
        namespace MN = Geometry::HalfedgeMesh::VertexNormals;
        using D = GeometryElementDomain;
        enum class WatchKind
        {
            Vec3,
            Bool,
            UInt
        };
        struct Watch
        {
            D Domain{};
            std::string Name{};
            WatchKind Kind{};
            std::size_t Count{};
            std::uint64_t Revision{};
            bool Exists{};
            bool operator==(const Watch &) const = default;
        };
        Watch Observe(const GeometryEntityAvailability &a, D domain, std::string name, WatchKind kind)
        {
            Watch w{domain, std::move(name), kind};
            const auto *props = ResolveGeometryPropertySet(a, domain);
            if (!props)
                return w;
            w.Count = props->Size();
            w.Exists = props->Exists(w.Name);
            switch (kind)
            {
            case WatchKind::Vec3:
                if (auto p = props->Get<glm::vec3>(w.Name))
                    w.Revision = p.Revision();
                break;
            case WatchKind::Bool:
                if (auto p = props->Get<bool>(w.Name))
                    w.Revision = p.Revision();
                break;
            case WatchKind::UInt:
                if (auto p = props->Get<std::uint32_t>(w.Name))
                    w.Revision = p.Revision();
                break;
            }
            return w;
        }
        Geometry::PropertySet *MutableProperties(entt::registry &raw, entt::entity entity, D domain)
        {
            auto view = GS::BuildMutableView(raw, entity);
            switch (domain)
            {
            case D::MeshVertex:
            case D::GraphNode:
            case D::PointCloudPoint:
                return view.VertexSource ? &view.VertexSource->Properties : nullptr;
            case D::MeshEdge:
            case D::GraphEdge:
                return view.EdgeSource ? &view.EdgeSource->Properties : nullptr;
            case D::MeshHalfedge:
            case D::GraphHalfedge:
                return view.HalfedgeSource ? &view.HalfedgeSource->Properties : nullptr;
            case D::MeshFace:
                return view.FaceSource ? &view.FaceSource->Properties : nullptr;
            default:
                return nullptr;
            }
        }
        D DefaultDomain(const GeometryEntityAvailability &a)
        {
            for (auto d : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
                if (SupportsGeometryElementDomain(a, d))
                    return d;
            return D::Unknown;
        }
        bool Finite(glm::vec3 p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }
        bool SameOutput(std::span<const glm::vec3> a, std::span<const glm::vec3> b)
        {
            // Deleted output rows may contain NaNs; preserve their exact stored values.
            return std::ranges::equal(a, b, [](glm::vec3 x, glm::vec3 y) {
                for (unsigned component = 0; component < 3; ++component)
                    if (std::bit_cast<std::uint32_t>(x[component]) !=
                        std::bit_cast<std::uint32_t>(y[component]))
                        return false;
                return true;
            });
        }
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
            EditorNormalEstimationResult Result{};
        };
        bool ReadMask(const GeometryEntityAvailability &a, D domain, std::string name, std::size_t count,
                      std::vector<bool> &mask, std::vector<Watch> &watches)
        {
            const auto *props = ResolveGeometryPropertySet(a, domain);
            if (!props || props->Size() != count)
                return false;
            watches.push_back(Observe(a, domain, name, WatchKind::Bool));
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
            Readiness,
            Catalog
        };
        std::shared_ptr<NormalWork> CaptureNormalWork(const EditorGeometryProcessingContext &context,
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
            if (!context.Scene)
                return fail("Scene is unavailable.");
            const auto entity =
                GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity)
                return fail("Normal target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown)
                c.Positions.Domain = DefaultDomain(a);
            if (c.Output.Domain == D::Unknown)
                c.Output.Domain = c.Positions.Domain;
            const auto *props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved element domain.");
            if (c.Output.Domain != c.Positions.Domain || c.Output.Name == c.Positions.Name)
                return fail("Normals require a distinct output on the input domain.");
            if (props->Exists(c.Output.Name) &&
                !ResolveGeometryProperty(a, c.Output, props->Size(), false).Resolved())
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
            work->Result.SlotCount = props->Size();
            work->Inputs.push_back(Observe(a, c.Positions.Domain, c.Positions.Name, WatchKind::Vec3));
            work->OutputWatch = Observe(a, c.Output.Domain, c.Output.Name, WatchKind::Vec3);
            if (purpose == CapturePurpose::Execute)
            {
                if (work->OutputWatch.Exists)
                    work->Before = props->Get<glm::vec3>(c.Output.Name).Vector();
                work->After = work->OutputWatch.Exists ? work->Before
                                                       : std::vector<glm::vec3>(props->Size(), glm::vec3(0));
            }
            auto maskDomain = c.Positions.Domain;
            std::string maskName = "v:deleted";
            std::size_t divisor = 1;
            if (maskDomain == D::MeshFace)
                maskName = "f:deleted";
            if (maskDomain == D::MeshEdge || maskDomain == D::GraphEdge)
                maskName = "e:deleted";
            if (maskDomain == D::MeshHalfedge || maskDomain == D::GraphHalfedge)
            {
                maskDomain = maskDomain == D::MeshHalfedge ? D::MeshEdge : D::GraphEdge;
                maskName = "e:deleted";
                divisor = 2;
            }
            std::vector<bool> mask;
            if (props->Size() % divisor ||
                !ReadMask(a, maskDomain, maskName, props->Size() / divisor, mask, work->Inputs))
                return fail("Deletion mask is missing its domain or has an invalid type/cardinality.");
            const auto points = props->Get<glm::vec3>(c.Positions.Name);
            bool lbvhCoordinatesValid = true;
            for (std::uint32_t i = 0; i < props->Size(); ++i)
            {
                if (purpose == CapturePurpose::Execute)
                    work->Deleted.push_back(mask[i / divisor]);
                if (mask[i / divisor])
                    continue;
                if (!Finite(points[i]))
                    return fail("Live position samples must be finite.");
                ++work->Result.LiveCount;
                lbvhCoordinatesValid &= Geometry::PointLBVH::ValidPoint(points[i]);
                if (purpose == CapturePurpose::Execute)
                {
                    work->Points.push_back(points[i]);
                    work->Slots.push_back(i);
                }
            }
            if (!work->Result.LiveCount)
                return fail("Normal estimation requires live input samples.");
            if (purpose == CapturePurpose::Catalog)
                return work;
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
                return work;
            }
            if (c.Positions.Domain != D::MeshVertex && c.Positions.Domain != D::GraphNode)
                return fail("Topology normals require a vertex/node position domain and its adjacency.");
            if (c.Method == NormalEstimationMethod::MeshFaceWeighted)
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
                work->Inputs.push_back(Observe(a, D::MeshFace, "f:halfedge", WatchKind::UInt));
                for (auto name : {"h:to_vertex", "h:next", "h:face"})
                {
                    const auto values = halves->Get<std::uint32_t>(name);
                    if (!values || values.Size() != halves->Size())
                        return fail("Mesh normals require count-matched halfedge topology.");
                    work->Inputs.push_back(Observe(a, D::MeshHalfedge, name, WatchKind::UInt));
                }
                std::vector<bool> deletedFaces;
                if (!ReadMask(a, D::MeshFace, "f:deleted", faces->Size(), deletedFaces, work->Inputs))
                    return fail("Invalid face deletion mask.");
                const auto *edges = ResolveGeometryPropertySet(a, D::MeshEdge);
                std::vector<bool> deletedEdges;
                if (!edges || halves->Size() != 2 * edges->Size() ||
                    !ReadMask(a, D::MeshEdge, "e:deleted", edges->Size(), deletedEdges, work->Inputs))
                    return fail("Invalid mesh edge deletion mask or halfedge cardinality.");
                if (purpose == CapturePurpose::Readiness)
                    return work;
                // Snapshot reconstruction is submission work, never a per-frame UI readiness operation.
                auto built =
                    GeometryProcessingDetail::BuildEditorNormalMeshSnapshot(a.SourceView, c.Positions.Name);
                if (built.Status != EditorCommandStatus::Applied)
                    return fail(built.Diagnostic);
                built.Mesh.VertexProperties().GetOrAdd<bool>("v:deleted").Vector() = work->Deleted;
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
                work->Inputs.push_back(Observe(a, edgeDomain, name, WatchKind::UInt));
            std::vector<bool> deletedEdges;
            if (!ReadMask(a, edgeDomain, "e:deleted", edges->Size(), deletedEdges, work->Inputs))
                return fail("Invalid edge deletion mask.");
            if (purpose == CapturePurpose::Readiness)
                return work;
            work->GraphVertices.Resize(props->Size());
            work->GraphEdges.Resize(edges->Size());
            work->GraphHalfedges.Resize(edges->Size() * 2);
            work->GraphVertices.GetOrAdd<glm::vec3>(c.Positions.Name).Vector() = points.Vector();
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
        bool CurrentNormalSource(const EditorGeometryProcessingContext &context, entt::entity entity,
                                 std::span<const Watch> inputs)
        {
            if (!context.Scene || !context.Scene->Raw().valid(entity))
                return false;
            const auto availability = BuildGeometryAvailability(context.Scene->Raw(), entity);
            for (const auto &watch : inputs)
                if (Observe(availability, watch.Domain, watch.Name, watch.Kind) != watch)
                    return false;
            return true;
        }
        bool CurrentNormalInput(const EditorGeometryProcessingContext &context, const NormalWork &work,
                                bool output)
        {
            if (!CurrentNormalSource(context, work.Entity, work.Inputs))
                return false;
            const auto &watch = work.OutputWatch;
            return !output || Observe(BuildGeometryAvailability(context.Scene->Raw(), work.Entity),
                                      watch.Domain, watch.Name, watch.Kind) == watch;
        }
        void ComputeNormals(NormalWork &w)
        {
            auto &r = w.Result;
            const auto &c = w.Config;
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
                const auto estimate =
                    w.Index ? PN::Estimate(w.Points, w.Index->Index, p) : PN::Estimate(w.Points, p);
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
                r.ChangedCount += !w.OutputWatch.Exists || w.Before[i] != w.After[i];
            r.Status = r.ChangedCount ? EditorCommandStatus::Applied : EditorCommandStatus::NoChange;
            r.Message = "Normals computed using " + r.ActualBackend + ".";
        }
        EditorNormalEstimationResult PublishNormals(const EditorGeometryProcessingContext &context,
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
            auto before = std::make_shared<OutputState>(OutputState{w->OutputWatch.Exists, w->Before});
            auto after = std::make_shared<OutputState>(OutputState{true, w->After});
            auto revision = std::make_shared<Watch>(w->OutputWatch);
            const auto mutate = [context, entity = w->Entity, inputs = w->Inputs, output = w->Config.Output,
                                 revision](const OutputState &expected, const OutputState &target) {
                if (!CurrentNormalSource(context, entity, inputs))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto *props = MutableProperties(context.Scene->Raw(), entity, output.Domain);
                const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
                if (!props || Observe(a, revision->Domain, revision->Name, revision->Kind) != *revision)
                    return EditorCommandHistoryStatus::StaleEntity;
                const auto &read = std::as_const(*props);
                const auto current = read.Get<glm::vec3>(output.Name);
                if (expected.Exists && (!current || !SameOutput(current.Vector(), expected.Values)))
                    return EditorCommandHistoryStatus::StaleEntity;
                if (target.Exists)
                    props->GetOrAdd<glm::vec3>(output.Name).Vector() = target.Values;
                else if (auto p = props->Get<glm::vec3>(output.Name))
                    props->Remove(p);
                *revision = Observe(BuildGeometryAvailability(context.Scene->Raw(), entity), revision->Domain,
                                    revision->Name, revision->Kind);
                ECS::Components::DirtyTags::MarkVertexNormalsDirty(context.Scene->Raw(), entity);
                ECS::Components::DirtyTags::MarkVertexAttributesDirty(context.Scene->Raw(), entity);
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
            r.Status = GeometryProcessingDetail::ToEditorMethodCommandStatus(status);
            if (r.Status != EditorCommandStatus::Applied)
                r.Message = "Normal publication rejected by source/output history checks.";
            return r;
        }
    } // namespace
    EditorNormalEstimationReadiness PreviewEditorNormalEstimationCommand(
        const EditorGeometryProcessingContext &context, const NormalEstimationConfig &config)
    {
        EditorNormalEstimationReadiness result;
        auto work = CaptureNormalWork(context, config, result.Diagnostic, CapturePurpose::Readiness);
        result.Ready = bool(work);
        if (work)
            result.Resolved = work->Config;
        return result;
    }
    GeometryPropertyCatalogSnapshot GetEditorNormalEstimationInputCatalog(
        const EditorGeometryProcessingContext &context, std::uint32_t id)
    {
        if (!context.Scene)
            return {};
        const auto entity = GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(), id);
        if (!entity)
            return {};
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        std::uint64_t generation = 1469598103934665603ull;
        for (unsigned d = 1; d <= unsigned(D::PointCloudPoint); ++d)
        {
            const auto *properties = ResolveGeometryPropertySet(a, D(d));
            generation = (generation ^ (properties ? properties->Revision() : 0)) * 1099511628211ull;
        }
        auto catalog = BuildGeometryPropertyCatalogSnapshot(a, id, generation);
        for (auto &entry : catalog.Entries)
            entry.PropertyGeneration = ResolveGeometryPropertySet(a, entry.Ref.Domain)
                                           ->FindPropertyRevision(entry.Ref.Name)
                                           .value_or(0);
        std::erase_if(catalog.Entries, [&](const auto &entry) {
            if (entry.Ref.ValueKind != Geometry::PropertyValueKind::Vec3)
                return true;
            NormalEstimationConfig c;
            c.StableEntityId = id;
            c.Positions = entry.Ref;
            c.Output = {.Domain = entry.Ref.Domain,
                        .Name = entry.Ref.Name + ".estimated_normal",
                        .ValueKind = Geometry::PropertyValueKind::Vec3};
            const auto *properties = ResolveGeometryPropertySet(a, entry.Ref.Domain);
            while (properties->Exists(c.Output.Name))
                c.Output.Name += "_";
            std::string diagnostic;
            return !CaptureNormalWork(context, c, diagnostic, CapturePurpose::Catalog);
        });
        return catalog;
    }
    EditorNormalEstimationResult ApplyEditorNormalEstimationCommand(
        const EditorGeometryProcessingContext &context, const NormalEstimationConfig &config)
    {
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
            w->Config.Backend == NormalEstimationBackend::CpuLBVH)
        {
            auto acquired = context.SpatialIndices->Acquire(context.World, w->Entity, w->Config.Positions);
            if (!acquired.Ready())
                return report(EditorCommandStatus::InvalidProcessingParameters, acquired.Diagnostic);
            w->Index = context.SpatialIndices->Snapshot(acquired.Handle);
            w->Result.IndexReused = acquired.Reused;
            if (!w->Index || w->Index->Slots != w->Slots ||
                w->Index->Index.Points().size() != w->Points.size() ||
                !std::equal(w->Points.begin(), w->Points.end(), w->Index->Index.Points().begin()))
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
        if (context.JobCommands.FindActive)
            if (auto active = context.JobCommands.FindActive(identity);
                active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,
                              "A normal job for this output is already active.");
        auto sink = context.MethodResultSinks.NormalEstimation;
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
                [sink, delivered, pending]() mutable {
                    if (sink && !*delivered)
                    {
                        pending.Status = EditorCommandStatus::StaleEntity;
                        pending.Message =
                            "Normal job was cancelled or its source became stale; previous output retained.";
                        sink(std::move(pending));
                    }
                }};
        const auto token = context.JobCommands.Submit(std::move(desc), identity);
        if (!token.IsValid())
        {
            pending.Status = EditorCommandStatus::GeometryProcessingFailed;
            pending.Message = "Normal job submission was rejected.";
        }
        return pending;
    }
    EditorNormalEstimationResult ApplyEditorConfiguredNormalEstimation(
        const EditorGeometryProcessingContext &context)
    {
        const auto config = GetEditorNormalEstimationConfig(context);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Normal estimation config is unavailable."};
        return ApplyEditorNormalEstimationCommand(context, *config);
    }
} // namespace Extrinsic::Runtime
