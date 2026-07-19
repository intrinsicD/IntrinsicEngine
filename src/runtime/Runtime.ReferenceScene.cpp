module;

#include <exception>
#include <utility>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.ReferenceScene;

import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr glm::vec3 kReferenceCameraPosition{
            0.0f, 0.0f, 3.0f};
        constexpr glm::vec3 kReferenceCameraForward{
            0.0f, 0.0f, -1.0f};
        constexpr glm::vec3 kReferenceCameraUp{
            0.0f, 1.0f, 0.0f};
        constexpr float kReferenceCameraNear = 0.1f;
        constexpr float kReferenceCameraFar = 100.0f;
        constexpr ECS::Components::StableId
            kReferenceTriangleStableId{
                0x52554E54494D4530ull,
                0x545249414E474C45ull,
            };

        [[nodiscard]] Graphics::CameraViewInput
        MakeReferenceCameraSeed() noexcept
        {
            Graphics::CameraViewInput seed{};
            seed.Position = kReferenceCameraPosition;
            seed.Forward = kReferenceCameraForward;
            seed.Up = kReferenceCameraUp;
            seed.NearPlane = kReferenceCameraNear;
            seed.FarPlane = kReferenceCameraFar;
            seed.Valid = true;
            return seed;
        }

        [[nodiscard]] Geometry::HalfedgeMesh::Mesh
        MakeReferenceTriangleMesh()
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            const auto v0 =
                mesh.AddVertex({-0.5f, -0.5f, 0.0f});
            const auto v1 =
                mesh.AddVertex({0.5f, -0.5f, 0.0f});
            const auto v2 =
                mesh.AddVertex({0.0f, 0.5f, 0.0f});
            if (!mesh.AddTriangle(v0, v1, v2).has_value())
                std::terminate();
            auto uv = Geometry::VertexProperty<glm::vec2>(
                mesh.VertexProperties().GetOrAdd<glm::vec2>(
                    "v:texcoord", glm::vec2{0.0f}));
            uv[v0] = glm::vec2{0.0f, 0.0f};
            uv[v1] = glm::vec2{1.0f, 0.0f};
            uv[v2] = glm::vec2{0.5f, 1.0f};
            return mesh;
        }

        [[nodiscard]] ReferenceScenePopulation
        BootstrapTriangle(ECS::Scene::Registry& scene)
        {
            const ECS::EntityHandle entity =
                ECS::Scene::CreateDefault(
                    scene, "ReferenceTriangle");

            auto& raw = scene.Raw();
            raw.emplace<ECS::Components::StableId>(
                entity, kReferenceTriangleStableId);
            raw.emplace<
                ECS::Components::Selection::SelectableTag>(entity);
            raw.emplace<Graphics::Components::RenderSurface>(
                entity,
                Graphics::Components::RenderSurface{
                    .Domain =
                        Graphics::Components::RenderSurface::
                            SourceDomain::Vertex,
                });
            Graphics::Components::VisualizationConfig
                visualization{};
            visualization.Source =
                Graphics::Components::VisualizationConfig::
                    ColorSource::UniformColor;
            visualization.Color =
                glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
            raw.emplace<
                Graphics::Components::VisualizationConfig>(
                entity, std::move(visualization));

            Geometry::HalfedgeMesh::Mesh mesh =
                MakeReferenceTriangleMesh();
            ECS::Components::GeometrySources::PopulateFromMesh(
                raw, entity, mesh);

            ReferenceScenePopulation population{};
            population.Entities.push_back(
                ReferenceSceneEntity{entity});
            population.Camera = MakeReferenceCameraSeed();
            return population;
        }
    }

    ReferenceScenePopulation BootstrapReferenceScene(
        const Core::Config::ReferenceSceneSelector selector,
        ECS::Scene::Registry& scene)
    {
        switch (selector)
        {
        case Core::Config::ReferenceSceneSelector::Triangle:
            return BootstrapTriangle(scene);
        }
        std::terminate();
    }

    void TeardownReferenceScene(
        ECS::Scene::Registry& scene,
        const ReferenceScenePopulation& population) noexcept
    {
        for (const ReferenceSceneEntity& owned :
             population.Entities)
        {
            if (scene.IsValid(owned.Entity))
                scene.Destroy(owned.Entity);
        }
    }
}
