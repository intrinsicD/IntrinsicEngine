module;
#include <algorithm>
#include <entt/entity/registry.hpp>
#include <limits>
#include <string>
#include <unordered_set>
module Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Components.GeometrySources;
namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        using D = GeometryElementDomain;
        bool Alive(const GeometryEntityAvailability& source, D domain, std::uint32_t index)
        {
            const auto* properties = ResolveGeometryPropertySet(source, domain);
            if (!properties || index >= properties->Size())
                return false;
            std::string_view name = "v:deleted";
            if (domain == D::MeshFace)
                name = "f:deleted";
            else if (domain == D::MeshEdge || domain == D::GraphEdge)
                name = "e:deleted";
            else if (domain == D::MeshHalfedge || domain == D::GraphHalfedge)
            {
                if (!source.SourceView.EdgeSource)
                    return false;
                properties = &source.SourceView.EdgeSource->Properties;
                index /= 2;
                if (index >= properties->Size())
                    return false;
                name = "e:deleted";
            }
            const auto deleted = properties->Get<bool>(name);
            return !deleted || (index < deleted.Size() && !deleted[index]);
        }
        PrimitiveSelectionSnapshot Base(const ECS::Scene::Registry& scene, std::uint32_t id,
                                        D domain)
        {
            PrimitiveSelectionSnapshot result{.EntityId = id, .Domain = domain};
            const auto entity = SelectionController::ToEntityHandle(id);
            if (!id || !scene.IsValid(entity))
            {
                result.Message = "Selection entity is unavailable.";
                return result;
            }
            const auto source = BuildGeometryAvailability(scene.Raw(), entity);
            const auto* properties = ResolveGeometryPropertySet(source, domain);
            if (!SupportsGeometryElementDomain(source, domain) || !properties)
            {
                result.Status = PrimitiveSelectionStatus::UnsupportedDomain;
                result.Message = "This geometry does not expose the requested element domain.";
                return result;
            }
            result.ElementCount = properties->Size();
            result.Status = PrimitiveSelectionStatus::Empty;
            return result;
        }
    } // namespace
    GeometryElementDomain ResolveSelectionTargetDomain(const GeometryEntityAvailability& source,
                                                       SelectionTarget target) noexcept
    {
        const bool mesh = source.Sources.HasMeshProvenance();
        const bool graph = source.Sources.HasGraphProvenance();
        switch (target)
        {
        case SelectionTarget::Vertex:
            return mesh ? D::MeshVertex : graph ? D::GraphNode : D::PointCloudPoint;
        case SelectionTarget::Edge:
            return mesh ? D::MeshEdge : graph ? D::GraphEdge : D::Unknown;
        case SelectionTarget::Face:
            return mesh ? D::MeshFace : D::Unknown;
        default:
            return D::Unknown;
        }
    }
    std::vector<std::uint64_t> BuildSelectionTopologyStamp(const ECS::Scene::Registry& scene,
                                                           std::uint32_t id)
    {
        const auto entity = SelectionController::ToEntityHandle(id);
        if (!id || !scene.IsValid(entity))
            return {};
        const auto view = GS::BuildConstView(scene.Raw(), entity);
        std::vector<std::uint64_t> stamp{static_cast<std::uint64_t>(view.ActiveDomain)};
        const auto append = [&](const auto* source, std::initializer_list<std::string_view> names) {
            stamp.push_back(source ? source->Properties.Size() + 1u : 0u);
            for (const auto name : names)
                stamp.push_back(source ? source->Properties.FindPropertyRevision(name).value_or(0)
                                       : 0);
        };
        append(view.VertexSource, {"v:deleted", "v:connectivity"});
        append(view.EdgeSource, {"e:deleted", "e:v0", "e:v1"});
        append(view.HalfedgeSource, {"h:connectivity", "h:to_vertex", "h:next", "h:face"});
        append(view.FaceSource, {"f:deleted", "f:halfedge"});
        // Minimal custom point sources without deletion/topology channels have no
        // separate row-identity token. Conservatively invalidate on position edits.
        if (view.VertexSource && !view.VertexSource->Properties.Exists("v:deleted") &&
            !view.HalfedgeSource)
            stamp.push_back(
                view.VertexSource->Properties.FindPropertyRevision("v:position").value_or(0));
        return stamp;
    }
    PrimitiveSelectionSnapshot SelectionController::ReadPrimitives(const Registry& registry,
                                                                   std::uint32_t id, D domain) const
    {
        auto result = Base(registry, id, domain);
        if (!result.Usable())
            return result;
        const auto record = std::ranges::find_if(m_Primitives, [&](const auto& row) {
            return row.EntityId == id && row.Domain == domain;
        });
        if (record == m_Primitives.end())
            return result;
        const auto source = BuildGeometryAvailability(registry.Raw(), ToEntityHandle(id));
        if (record->TopologyStamp != BuildSelectionTopologyStamp(registry, id) ||
            !std::ranges::all_of(record->Indices, [&](auto i) { return Alive(source, domain, i); }))
        {
            result.Status = PrimitiveSelectionStatus::StaleTopology;
            result.Message =
                "Selection expired after a topology or element-identity change; select again.";
            return result;
        }
        result.Indices = record->Indices;
        result.Status = result.Indices.empty() ? PrimitiveSelectionStatus::Empty
                                               : PrimitiveSelectionStatus::Ready;
        return result;
    }
    PrimitiveSelectionSnapshot SelectionController::EditPrimitives(
        Registry& registry, std::uint32_t id, D domain, PrimitiveSelectionEdit edit,
        std::span<const std::uint32_t> indices)
    {
        auto result = Base(registry, id, domain);
        if (!result.Usable())
            return result;
        const auto source = BuildGeometryAvailability(registry.Raw(), ToEntityHandle(id));
        if (edit != PrimitiveSelectionEdit::Clear && edit != PrimitiveSelectionEdit::All &&
            edit != PrimitiveSelectionEdit::Invert &&
            !std::ranges::all_of(indices, [&](auto i) { return Alive(source, domain, i); }))
        {
            result.Status = PrimitiveSelectionStatus::InvalidIndex;
            result.Message =
                "Selection contains an out-of-range or deleted element; no changes applied.";
            return result;
        }
        auto current = ReadPrimitives(registry, id, domain);
        if (!current.Usable() && edit != PrimitiveSelectionEdit::Replace &&
            edit != PrimitiveSelectionEdit::Clear && edit != PrimitiveSelectionEdit::All)
            return current;
        std::vector<std::uint32_t> next = current.Indices;
        if (edit == PrimitiveSelectionEdit::Replace || edit == PrimitiveSelectionEdit::Clear ||
            edit == PrimitiveSelectionEdit::All || edit == PrimitiveSelectionEdit::Invert)
            next.clear();
        if (edit == PrimitiveSelectionEdit::All || edit == PrimitiveSelectionEdit::Invert)
        {
            if (result.ElementCount > std::numeric_limits<std::uint32_t>::max())
            {
                result.Status = PrimitiveSelectionStatus::InvalidIndex;
                return result;
            }
            std::vector<bool> selected(result.ElementCount, false);
            for (auto i : current.Indices)
                selected[i] = true;
            for (std::uint32_t i = 0; i < result.ElementCount; ++i)
                if (Alive(source, domain, i) &&
                    (edit == PrimitiveSelectionEdit::All || !selected[i]))
                    next.push_back(i);
        }
        else if (edit != PrimitiveSelectionEdit::Clear)
        {
            std::unordered_set<std::uint32_t> visited;
            std::unordered_set<std::uint32_t> members(next.begin(), next.end());
            for (auto i : indices)
            {
                // Duplicate toggle inputs in one batch toggle only once.
                if (!visited.insert(i).second)
                    continue;
                const bool present = members.contains(i);
                if (present && (edit == PrimitiveSelectionEdit::Remove ||
                                edit == PrimitiveSelectionEdit::Toggle))
                    members.erase(i);
                else if (!present && edit != PrimitiveSelectionEdit::Remove)
                {
                    members.insert(i);
                    next.push_back(i);
                }
            }
            std::erase_if(next, [&](auto i) { return !members.contains(i); });
        }
        if (!current.Usable() || current.Indices != next)
        {
            std::erase_if(m_Primitives, [&](const auto& row) {
                return row.EntityId == id && row.Domain == domain;
            });
            if (!next.empty())
                m_Primitives.push_back(
                    {id, domain, BuildSelectionTopologyStamp(registry, id), std::move(next)});
            ++m_SelectionGeneration;
        }
        return ReadPrimitives(registry, id, domain);
    }
    void SelectionController::ClearPrimitives() noexcept
    {
        if (m_Primitives.empty())
            return;
        m_Primitives.clear();
        ++m_SelectionGeneration;
    }
    void SelectionController::PrunePrimitives(const Registry& registry)
    {
        const auto count = std::erase_if(m_Primitives, [&](const auto& row) {
            const auto base = Base(registry, row.EntityId, row.Domain);
            if (!base.Usable() ||
                row.TopologyStamp != BuildSelectionTopologyStamp(registry, row.EntityId))
                return true;
            const auto source =
                BuildGeometryAvailability(registry.Raw(), ToEntityHandle(row.EntityId));
            return !std::ranges::all_of(row.Indices,
                                        [&](auto i) { return Alive(source, row.Domain, i); });
        });
        if (count)
            ++m_SelectionGeneration;
    }
    std::vector<PrimitiveSelectionSnapshot> SelectionController::PrimitiveSnapshots(
        const Registry& registry) const
    {
        std::vector<PrimitiveSelectionSnapshot> result;
        for (const auto& record : m_Primitives)
        {
            auto row = ReadPrimitives(registry, record.EntityId, record.Domain);
            if (row.Usable())
                result.push_back(std::move(row));
        }
        return result;
    }
} // namespace Extrinsic::Runtime
