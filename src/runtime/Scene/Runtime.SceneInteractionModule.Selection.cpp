module;
#include <cmath>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.Graphics.RenderWorld;
namespace Extrinsic::Runtime
{
    RuntimeSceneInteractionRenderSnapshot BuildPrimitiveSelectionRenderSnapshot(
        const ECS::Scene::Registry& scene, const SelectionController& selection, WorldHandle world)
    {
        RuntimeSceneInteractionRenderSnapshot result{.World = world};
        const auto& config = selection.GetConfig().Interaction;
        if (!config.Highlight)
            return result;
        namespace GS = ECS::Components::GeometrySources;
        using D = GeometryElementDomain;
        constexpr glm::vec4 color{1.f, 0.55f, 0.05f, 1.f};
        for (const auto& selected : selection.PrimitiveSnapshots(scene))
        {
            const auto entity = SelectionController::ToEntityHandle(selected.EntityId);
            const auto source = GS::BuildConstView(scene.Raw(), entity);
            if (!source.VertexSource)
                continue;
            const auto positions = source.VertexSource->Properties.Get<glm::vec3>("v:position");
            if (!positions)
                continue;
            const auto* worldMatrix =
                scene.Raw().try_get<ECS::Components::Transform::WorldMatrix>(entity);
            const auto transform = worldMatrix ? worldMatrix->Matrix : glm::mat4{1.f};
            auto point = [&](std::uint32_t index) -> std::optional<glm::vec3> {
                if (index >= positions.Size())
                    return std::nullopt;
                const auto p = glm::vec3(transform * glm::vec4(positions[index], 1.f));
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    return std::nullopt;
                return p;
            };
            auto line = [&](std::uint32_t a, std::uint32_t b) {
                const auto start = point(a), end = point(b);
                if (start && end)
                    result.DebugLines.push_back({*start, *end, color, 2.f, false});
            };
            for (auto index : selected.Indices)
            {
                if (selected.Domain == D::MeshVertex || selected.Domain == D::GraphNode ||
                    selected.Domain == D::PointCloudPoint)
                {
                    if (const auto p = point(index))
                        result.DebugPoints.push_back({*p, color, config.PointRadius, false});
                }
                else if (selected.Domain == D::MeshEdge || selected.Domain == D::GraphEdge ||
                         selected.Domain == D::MeshHalfedge || selected.Domain == D::GraphHalfedge)
                {
                    if (!source.EdgeSource)
                        continue;
                    if (selected.Domain == D::MeshHalfedge || selected.Domain == D::GraphHalfedge)
                        index /= 2;
                    const auto a = source.EdgeSource->Properties.Get<std::uint32_t>("e:v0");
                    const auto b = source.EdgeSource->Properties.Get<std::uint32_t>("e:v1");
                    if (a && b && index < a.Size() && index < b.Size())
                        line(a[index], b[index]);
                }
                else if (selected.Domain == D::MeshFace && source.HalfedgeSource &&
                         source.FaceSource)
                {
                    const auto first =
                        source.FaceSource->Properties.Get<std::uint32_t>("f:halfedge");
                    const auto next =
                        source.HalfedgeSource->Properties.Get<std::uint32_t>("h:next");
                    const auto to =
                        source.HalfedgeSource->Properties.Get<std::uint32_t>("h:to_vertex");
                    if (!first || !next || !to || index >= first.Size())
                        continue;
                    auto h = first[index];
                    for (std::size_t steps = 0; steps < next.Size(); ++steps)
                    {
                        if (h >= next.Size() || h >= to.Size())
                            break;
                        const auto n = next[h];
                        if (n >= to.Size())
                            break;
                        line(to[h], to[n]);
                        h = n;
                        if (h == first[index])
                            break;
                    }
                }
            }
        }
        return result;
    }
} // namespace Extrinsic::Runtime
