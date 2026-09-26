// Sample capture, weighted sample graphs and typed snapshots shared by property smoothing and
// harmonic fields.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Geometry.HalfedgeMesh;
import Geometry.Smoothing;
import Geometry.DEC;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.MeshFieldOperations.PropertyGraph.hpp"

// The helpers are declared extern "C++" in the shared header; define them in the global module.
extern "C++"
{
namespace Extrinsic::Runtime::PropertyGraphDetail
{
    namespace
    {
        namespace S = Geometry::Smoothing;
        namespace GP = GeometryProcessingDetail;
        namespace GS = ECS::Components::GeometrySources;
        using D = GeometryElementDomain;
        using K = Geometry::PropertyValueKind;
    }

    Storage EmptyStorage(K k)
    {
        switch (k) {
        case K::Double: return std::vector<double>{};
        case K::Vec2: return std::vector<glm::vec2>{};
        case K::Vec3: return std::vector<glm::vec3>{};
        case K::Vec4: return std::vector<glm::vec4>{};
        case K::Int32: return std::vector<std::int32_t>{};
        default: return std::vector<float>{}; }
    }
    bool CountMatches(const Geometry::PropertySet& props, const GeometryPropertyRef& ref)
    {
        if (ref.ValueKind == K::Bool) // masks are read directly, never snapshotted
        {
            const auto property = props.Get<bool>(ref.Name);
            return property && property.Size() == props.Size();
        }
        return std::visit([&](const auto& values) {
            using T = typename std::decay_t<decltype(values)>::value_type;
            const auto property = props.Get<T>(ref.Name);
            return property && property.Size() == props.Size();
        }, EmptyStorage(ref.ValueKind));
    }
    Snapshot Capture(const Geometry::PropertySet& props, const GeometryPropertyRef& ref)
    {
        Snapshot s{props.Exists(ref.Name), EmptyStorage(ref.ValueKind)};
        std::visit([&](auto& values) {
            using T = typename std::decay_t<decltype(values)>::value_type;
            if (const auto property = props.Get<T>(ref.Name)) values = property.Vector();
        }, s.Values);
        return s;
    }
    bool Same(const Snapshot& a, const Snapshot& b)
    {
        if (a.Exists != b.Exists || a.Values.index() != b.Values.index()) return false;
        return std::visit([&](const auto& values) {
            return GeometryValueComparison::BitEqual(values, std::get<std::decay_t<decltype(values)>>(b.Values));
        }, a.Values);
    }

    bool CaptureSamples(const GeometryEntityAvailability& a, D inputDomain, const GeometryPropertyRef& positions,
                        GP::PointInputCapture& capture, std::string& diagnostic)
    {
        auto positionRef = positions;
        if (positions.Domain == inputDomain)
            return GP::CapturePointInput(a, positionRef, true, capture, diagnostic);
        const auto* props = ResolveGeometryPropertySet(a, inputDomain);
        const auto* vertices = ResolveGeometryPropertySet(a, positions.Domain);
        GP::PointInputCapture vertexCapture;
        if (!GP::CapturePointInput(a, positionRef, true, vertexCapture, diagnostic)) return false;
        capture.Inputs = std::move(vertexCapture.Inputs);
        capture.SlotCount = props->Size();
        const auto source = vertices->Get<glm::vec3>(positions.Name);
        std::vector<glm::vec3> anchors;
        if (inputDomain == D::MeshFace)
        {
            if (BuildMeshFaceCenters(a.SourceView, source.Span(), anchors, capture.Slots) != MeshSurfaceTopologyStatus::Success)
            { diagnostic = "Unable to derive face centers from mesh topology."; return false; }
            const auto deleted = props->Get<bool>("f:deleted");
            if (anchors.size() != props->Size() || (props->Exists("f:deleted") && (!deleted || deleted.Size() != props->Size())))
            { diagnostic = "Invalid face or deletion cardinality."; return false; }
            std::size_t live = 0;
            for (std::size_t row = 0; row < props->Size(); ++row)
                if (!deleted || !deleted[row]) ++live;
            if (capture.Slots.size() != live)
            { diagnostic = "A live face has no valid finite center."; return false; }
        }
        else
        {
            const bool halfedge = inputDomain == D::MeshHalfedge || inputDomain == D::GraphHalfedge;
            const auto edgeDomain = inputDomain == D::MeshHalfedge ? D::MeshEdge : inputDomain == D::GraphHalfedge ? D::GraphEdge : inputDomain;
            const auto* edges = ResolveGeometryPropertySet(a, edgeDomain);
            if (!edges) { diagnostic = "Missing edge topology."; return false; }
            const auto v0 = edges->Get<std::uint32_t>(GS::PropertyNames::kEdgeV0);
            const auto v1 = edges->Get<std::uint32_t>(GS::PropertyNames::kEdgeV1);
            const auto deleted = edges->Get<bool>("e:deleted");
            if (!v0 || !v1 || v0.Size() != edges->Size() || v1.Size() != edges->Size() ||
                (edges->Exists("e:deleted") && (!deleted || deleted.Size() != edges->Size())) ||
                capture.SlotCount != edges->Size() * (halfedge ? 2 : 1))
            { diagnostic = "Invalid edge endpoint or deletion storage."; return false; }
            std::vector<bool> liveVertex(vertices->Size(), false);
            for (auto slot : vertexCapture.Slots) liveVertex[slot] = true;
            anchors.resize(capture.SlotCount);
            for (std::size_t row = 0; row < capture.SlotCount; ++row)
            {
                const auto e = halfedge ? row / 2 : row;
                if (deleted && deleted[e]) continue;
                if (v0[e] >= source.Size() || v1[e] >= source.Size() || !liveVertex[v0[e]] || !liveVertex[v1[e]])
                { diagnostic = "Live edge references an invalid or deleted vertex."; return false; }
                anchors[row] = glm::vec3(0.5 * (glm::dvec3(source[v0[e]]) + glm::dvec3(source[v1[e]])));
                capture.Slots.push_back(static_cast<std::uint32_t>(row));
            }
        }
        // Topology and deletion revisions guard derived anchors through undo/redo.
        for (unsigned domain = unsigned(D::MeshVertex); domain <= unsigned(D::PointCloudPoint); ++domain)
            if (const auto* set = ResolveGeometryPropertySet(a, D(domain)))
                for (const auto& name : set->Properties())
                    if (IsTopologyProperty(D(domain), name)) capture.Inputs.push_back(GP::ObserveGeometryProperty(a, D(domain), name));
        for (auto slot : capture.Slots) capture.Points.push_back(anchors[slot]);
        capture.LiveCount = capture.Slots.size();
        if (!capture.LiveCount) { diagnostic = "No live property rows."; return false; }
        return true;
    }

    bool UsesMeshTopology(const GraphRequest& r)
    {
        return r.Weight == S::PropertyWeight::Cotangent || r.Weight == S::PropertyWeight::MeshUniform || r.LumpedMass || r.BoundaryRows;
    }

    bool BuildGraph(const GeometryEntityAvailability& a, const GraphRequest& r, GP::PointInputCapture& samples,
                    Graph& graph, std::string& diagnostic)
    {
        const bool meshWeights = r.Weight == S::PropertyWeight::Cotangent || r.Weight == S::PropertyWeight::MeshUniform;
        graph = {};
        if (UsesMeshTopology(r))
        {
            auto mesh = GP::MeshSupport::BuildHalfedgeMeshForProcessing(a.SourceView, r.Operation, r.Positions.Name);
            if (!mesh.Succeeded()) { diagnostic = mesh.Diagnostic; return false; }
            if (mesh.Mesh.VerticesSize() != samples.SlotCount) { diagnostic = "Mesh vertex correspondence mismatch."; return false; }
            if (r.LumpedMass)
            {
                const auto areas = Geometry::DEC::BuildHodgeStar0(mesh.Mesh);
                for (auto slot : samples.Slots)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(slot)};
                    graph.Mass.push_back(mesh.Mesh.IsIsolated(v) ? 1.0 : areas.Diagonal[slot]);
                }
            }
            if (r.BoundaryRows)
                for (std::size_t i = 0; i < samples.Slots.size(); ++i)
                    if (mesh.Mesh.IsBoundary(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(samples.Slots[i])}))
                        graph.BoundaryRows.push_back(i);
            std::vector<std::size_t> inverse(samples.SlotCount, samples.SlotCount);
            for (std::size_t i = 0; i < samples.Slots.size(); ++i) inverse[samples.Slots[i]] = i;
            if (r.Weight == S::PropertyWeight::MeshUniform)
            {
                for (std::size_t e = 0; e < mesh.Mesh.EdgesSize(); ++e)
                {
                    if (mesh.Mesh.IsDeleted(Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(e)})) continue;
                    const Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(2 * e)};
                    const auto i = mesh.Mesh.FromVertex(h).Index, j = mesh.Mesh.ToVertex(h).Index;
                    if (inverse[i] != samples.SlotCount && inverse[j] != samples.SlotCount)
                        graph.Edges.push_back({inverse[i], inverse[j], 1.0});
                }
            }
            else if (r.Weight == S::PropertyWeight::Cotangent)
            {
                // Cotangent weights are only assembled when selected; kNN weights ignore them.
                const auto laplacian = Geometry::DEC::BuildLaplacian(mesh.Mesh);
                for (std::size_t i = 0; i < laplacian.Rows; ++i)
                    for (auto entry = laplacian.RowOffsets[i]; entry < laplacian.RowOffsets[i+1]; ++entry)
                    {
                        const auto j = laplacian.ColIndices[entry];
                        if (j > i && inverse[i] != samples.SlotCount && inverse[j] != samples.SlotCount)
                            graph.Edges.push_back({inverse[i], inverse[j], std::max(0.0, -laplacian.Values[entry])});
                    }
            }
            for (unsigned d = unsigned(D::MeshVertex); d <= unsigned(D::MeshFace); ++d)
                if (const auto* set = ResolveGeometryPropertySet(a, D(d)))
                    for (const auto& name : set->Properties())
                        if (IsTopologyProperty(D(d), name)) samples.Inputs.push_back(GP::ObserveGeometryProperty(a, D(d), name));
        }
        if (!meshWeights)
        {
            auto edges = S::BuildPropertyNeighborhood(samples.Points, r.Neighbors, r.Weight, r.SpatialSigma);
            if (!edges) { diagnostic = "Invalid spatial neighborhood; check finite positions, coordinate bounds and sigma."; return false; }
            graph.Edges = std::move(*edges);
        }
        return true;
    }
}
}
