#include <functional>
#include <algorithm>
#include <array>
#include <bit>
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
import Extrinsic.Core.Error;
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
import Geometry.PointLBVH;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"

namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    namespace GS = ECS::Components::GeometrySources;
    using D = GeometryElementDomain;
    GeometryPropertyCatalogSnapshot BuildPointInputCandidateCatalog(
        const GeometryEntityAvailability& availability, std::uint32_t id)
    {
        std::uint64_t generation = 1469598103934665603ull;
        for (unsigned d = 1; d <= unsigned(D::PointCloudPoint); ++d)
        {
            const auto* props = ResolveGeometryPropertySet(availability, D(d));
            generation = (generation ^ (props ? props->Revision() : 0)) * 1099511628211ull;
        }
        auto catalog = BuildGeometryPropertyCatalogSnapshot(availability, id, generation);
        std::erase_if(catalog.Entries, [](const auto& entry) {
            return entry.Ref.ValueKind != Geometry::PropertyValueKind::Vec3;
        });
        for (auto& entry : catalog.Entries)
        {
            const auto* props = ResolveGeometryPropertySet(availability, entry.Ref.Domain);
            entry.PropertyGeneration = props->FindPropertyRevision(entry.Ref.Name).value_or(0);
        }
        return catalog;
    }
    GeometryPropertyCatalogSnapshot BuildPointInputCatalog(const EditorProcessingContext& context, std::uint32_t id)
    {
        if ((context.AttachmentActive && !context.AttachmentActive()) || !context.Scene) return {};
        const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
        if (!entity) return {};
        const auto availability = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        auto catalog = BuildPointInputCandidateCatalog(availability, id);
        std::erase_if(catalog.Entries, [&](auto& entry)
        {
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

    PointDeletionSource ResolvePointDeletionSource(D domain)
    {
        PointDeletionSource source{.Domain = domain};
        if (domain == D::MeshFace) source.Name = "f:deleted";
        if (domain == D::MeshEdge || domain == D::GraphEdge) source.Name = "e:deleted";
        if (domain == D::MeshHalfedge || domain == D::GraphHalfedge)
        {
            source.Domain = domain == D::MeshHalfedge ? D::MeshEdge : D::GraphEdge;
            source.Name = "e:deleted";
            source.Divisor = 2;
        }
        return source;
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
        const auto [deletionDomain, deletionName, divisor] = ResolvePointDeletionSource(positions.Domain);
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


}

namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
        [[nodiscard]] bool IsFiniteGeometryPosition(
            const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>>
        CollectFiniteGeometryPositions(
            const Geometry::PropertySet& properties,
            const std::string_view positionProperty)
        {
            const auto positions =
                properties.Get<glm::vec3>(positionProperty);
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

}
