#include <functional>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
// Named only so the shared job declarations in the point-field header resolve;
// both modules are already in this unit's closure through EditorProcessing.
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.ECS.Component.Transform;
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"

namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    namespace GS = ECS::Components::GeometrySources;
    using D = GeometryElementDomain;
    GeometryPropertyCatalogSnapshot BuildPointInputCatalog(const EditorProcessingContext& context, std::uint32_t id)
    {
        if ((context.AttachmentActive && !context.AttachmentActive()) || !context.Scene) return {};
        const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
        if (!entity) return {};
        const auto availability = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        std::uint64_t generation = 1469598103934665603ull;
        for (unsigned d = 1; d <= unsigned(D::PointCloudPoint); ++d)
        {
            const auto* props = ResolveGeometryPropertySet(availability, D(d));
            generation = (generation ^ (props ? props->Revision() : 0)) * 1099511628211ull;
        }
        auto catalog = BuildGeometryPropertyCatalogSnapshot(availability, id, generation);
        std::erase_if(catalog.Entries, [&](auto& entry)
        {
            if (entry.Ref.ValueKind != Geometry::PropertyValueKind::Vec3) return true;
            const auto* props = ResolveGeometryPropertySet(availability, entry.Ref.Domain);
            entry.PropertyGeneration = props->FindPropertyRevision(entry.Ref.Name).value_or(0);
            auto positions = entry.Ref;
            PointInputCapture capture;
            std::string diagnostic;
            return !CapturePointInput(availability, positions, false, capture, diagnostic) || !capture.LiveCount;
        });
        return catalog;
    }
    PointPropertyWatch ObserveGeometryProperty(const GeometryEntityAvailability& a, D domain, std::string name)
    {
        const auto* props = ResolveGeometryPropertySet(a, domain);
        return {domain, name, props ? props->Size() : 0,
                props ? props->FindPropertyRevision(name) : std::nullopt};
    }
    Geometry::PropertySet *MutableGeometryProperties(entt::registry &raw, entt::entity entity, D domain)
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
    D PrimaryPointDomain(const GeometryEntityAvailability &a)
    {
        for (auto d : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
            if (SupportsGeometryElementDomain(a, d))
                return d;
        return D::Unknown;
    }
    bool FinitePosition(glm::vec3 p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    bool GeometryPropertiesCurrent(const EditorProcessingContext& context, entt::entity entity,
                       std::span<const PointPropertyWatch> inputs)
    {
        if ((context.AttachmentActive && !context.AttachmentActive()) ||
            !context.Scene || !context.Scene->Raw().valid(entity)) return false;
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
        for (const auto& w : inputs)
            if (ObserveGeometryProperty(a, w.Domain, w.Name) != w) return false;
        return true;
    }
    bool ValidatePointOutputs(
        const GeometryEntityAvailability& a, const GeometryPropertyRef& positions,
        std::span<const GeometryPropertyRef> outputs, std::string_view outputLabel,
        std::string& diagnostic)
    {
        const auto fail = [&](std::string why) { diagnostic = std::move(why); return false; };
        const auto* props = ResolveGeometryPropertySet(a, positions.Domain);
        if (!props) return fail("Choose a resolved input domain.");
        for (const auto& output : outputs)
        {
            if (output.Domain != positions.Domain || output.Name == positions.Name)
                return fail(std::string(outputLabel) + " outputs must be distinct properties on the input domain.");
            for (const auto* reserved : {"v:deleted", "e:deleted", "h:deleted", "f:deleted", "v:halfedge",
                 "e:v0", "e:v1", "h:to_vertex", "h:next", "h:prev", "h:opposite", "h:face", "f:halfedge", "h:connectivity"})
                if (output.Name == reserved) return fail(std::string(outputLabel) + " outputs cannot replace topology/deletion properties.");
            if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                return fail(std::string(outputLabel) + " outputs must be absent or count-matched properties of the configured type.");
        }
        return true;
    }

    bool CapturePointInput(
        const GeometryEntityAvailability& a, GeometryPropertyRef& positions, bool copyValues,
        PointInputCapture& w, std::string& diagnostic)
    {
        const auto fail = [&](std::string why) { diagnostic = std::move(why); return false; };
        if (positions.Domain == D::Unknown) positions.Domain = PrimaryPointDomain(a);
        const auto* props = ResolveGeometryPropertySet(a, positions.Domain);
        if (!props || !ResolveGeometryProperty(a, positions, props->Size(), false).Resolved())
            return fail("Choose a count-matched vec3 position property on a resolved element domain.");
        if (props->Size() > std::numeric_limits<std::uint32_t>::max()) return fail("Input exceeds the supported slot range.");
        w.SlotCount = props->Size();
        w.Inputs.push_back(ObserveGeometryProperty(a, positions.Domain, positions.Name));
        auto deletionDomain = positions.Domain;
        const char* deletionName = "v:deleted";
        std::size_t divisor = 1;
        if (deletionDomain == D::MeshFace) deletionName = "f:deleted";
        if (deletionDomain == D::MeshEdge || deletionDomain == D::GraphEdge) deletionName = "e:deleted";
        if (deletionDomain == D::MeshHalfedge || deletionDomain == D::GraphHalfedge)
        {
            deletionDomain = deletionDomain == D::MeshHalfedge ? D::MeshEdge : D::GraphEdge;
            deletionName = "e:deleted"; divisor = 2;
        }
        const auto* deletionProps = ResolveGeometryPropertySet(a, deletionDomain);
        if (!deletionProps || props->Size() % divisor || deletionProps->Size() != props->Size() / divisor)
            return fail("Invalid deletion domain/cardinality.");
        const auto deleted = deletionProps->Get<bool>(deletionName);
        if (deletionProps->Exists(deletionName) && (!deleted || deleted.Size() != deletionProps->Size()))
            return fail("Deletion mask must be a count-matched bool property.");
        w.Inputs.push_back(ObserveGeometryProperty(a, deletionDomain, deletionName));
        const auto points = props->Get<glm::vec3>(positions.Name);
        for (std::uint32_t i = 0; i < props->Size(); ++i)
        {
            if (deleted && deleted[i / divisor]) continue;
            if (!FinitePosition(points[i])) return fail("Live position samples must be finite.");
            w.ValidLbvh &= Geometry::PointLBVH::ValidPoint(points[i]);
            // Bit inspection remains valid when a GPU flushes subnormal floats to zero.
            for (unsigned component = 0; component < 3; ++component)
            {
                const auto magnitude = std::bit_cast<std::uint32_t>(points[i][component]) & 0x7fffffffu;
                w.HasSubnormalCoordinates |= magnitude != 0 && magnitude < 0x00800000u;
            }
            ++w.LiveCount;
            if (copyValues) { w.Points.push_back(points[i]); w.Slots.push_back(i); }
        }
        return true;
    }

    bool CapturePointScalarField(
        const GeometryEntityAvailability& a, GeometryPropertyRef& positions,
        GeometryPropertyRef& output, std::string_view outputLabel, bool copyValues,
        PointScalarCapture& w, std::string& diagnostic)
    {
        if (!CapturePointInput(a, positions, copyValues, w, diagnostic)) return false;
        if (output.Domain == D::Unknown) output.Domain = positions.Domain;
        if (!ValidatePointOutputs(a, positions, std::span(&output, 1), outputLabel, diagnostic)) return false;
        w.OutputWatch = ObserveGeometryProperty(a, output.Domain, output.Name);
        if (copyValues)
        {
            const auto* props = ResolveGeometryPropertySet(a, output.Domain);
            if (w.OutputWatch.Revision) w.BeforeValues = props->Get<float>(output.Name).Vector();
            w.AfterValues = w.OutputWatch.Revision ? w.BeforeValues : std::vector<float>(w.SlotCount);
        }
        return true;
    }

    bool PointScalarFieldCurrent(const EditorProcessingContext& context,
                                 entt::entity entity, const PointScalarCapture& w)
    {
        const std::array outputs{w.OutputWatch};
        return GeometryPropertiesCurrent(context, entity, w.Inputs) &&
               GeometryPropertiesCurrent(context, entity, outputs);
    }

    EditorCommandHistoryStatus PublishPointScalarField(
        const EditorProcessingContext& context, entt::entity entity,
        const PointScalarCapture& w, std::string label)
    {
        struct State { bool Exists{}; std::vector<float> Values{}; };
        auto before = std::make_shared<State>(State{bool(w.OutputWatch.Revision), w.BeforeValues});
        auto after = std::make_shared<State>(State{true, w.AfterValues});
        auto revisions = std::make_shared<std::array<PointPropertyWatch, 1>>(std::array{w.OutputWatch});
        const auto mutate = [context, entity, inputs=w.Inputs, revisions](const State& target)
        {
            if (!GeometryPropertiesCurrent(context, entity, inputs) ||
                !GeometryPropertiesCurrent(context, entity, *revisions))
                return EditorCommandHistoryStatus::StaleEntity;
            const auto& output = revisions->front();
            auto* props = MutableGeometryProperties(context.Scene->Raw(), entity, output.Domain);
            if (target.Exists) props->GetOrAdd<float>(output.Name).Vector() = target.Values;
            else if (auto p = props->Get<float>(output.Name)) props->Remove(p);
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
            *revisions = {ObserveGeometryProperty(a, output.Domain, output.Name)};
            // Scalar buffers follow their property revision. They do not alter
            // resident positions, topology or vertex channels.
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        return context.CommandHistory ? context.CommandHistory->Execute({.Label=std::move(label),
            .Redo=[mutate,after]{return mutate(*after);},
            .Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
    }

    KnnRowsState AdvancePointKnnRows(
        SpatialIndexCache& cache, SpatialIndexHandle index, std::span<const glm::vec3> points,
        std::span<const std::uint32_t> slots, std::uint32_t width,
        std::uint32_t batchSize, PointKnnRows& rows, std::string& diagnostic)
    {
        const auto fail = [&](std::string why) {
            diagnostic = std::move(why); rows.Batch.reset(); return KnnRowsState::Failed;
        };
        if (rows.Finished) return KnnRowsState::Ready;
        if (rows.Started == std::chrono::steady_clock::time_point{})
            rows.Started = std::chrono::steady_clock::now();
        if (rows.Batch)
        {
            if (rows.Batch->State == SpatialQueryState::Failed) return fail(rows.Batch->Diagnostic);
            if (rows.Batch->State != SpatialQueryState::Ready) return KnnRowsState::Pending;
            for (std::size_t row = 0; row < rows.Batch->Counts.size(); ++row)
            {
                if (rows.Batch->Counts[row] != width) return fail("Incomplete Vulkan kNN neighborhood.");
                for (std::size_t j = 0; j < width; ++j)
                {
                    const auto id = rows.Batch->Neighbors[row * rows.Batch->Capacity + j].Index;
                    const auto found = std::lower_bound(slots.begin(), slots.end(), id);
                    if (found == slots.end() || *found != id) return fail("Invalid Vulkan neighbor source row.");
                    rows.Indices.push_back(std::uint32_t(found - slots.begin()));
                }
            }
            rows.NextQuery += rows.Batch->Counts.size();
            if (rows.NextQuery == points.size())
            {
                rows.Batch.reset(); rows.Finished = true;
                rows.Milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - rows.Started).count();
                return KnnRowsState::Ready;
            }
        }
        const auto count = std::min<std::size_t>(batchSize, points.size() - rows.NextQuery);
        if (rows.Batch && rows.Batch->Counts.size() != count) rows.Batch.reset();
        rows.Batch = cache.QueueGpuKNearest(index, points.subspan(rows.NextQuery, count),
                                            width, {}, std::move(rows.Batch));
        ++rows.QueryBatches;
        if (rows.Batch->State == SpatialQueryState::Failed) return fail(rows.Batch->Diagnostic);
        return KnnRowsState::Pending;
    }
}
