// Graph publication, source preservation, undo and input rejection for
// scalar-property ridge extraction.
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.ScalarRidgeOperations;
import Extrinsic.Runtime.SelectionController;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
namespace Runtime = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace
{
    // Flat 32x32 grid over [-1, 1]^2 carrying a Gaussian bump field in x: one
    // straight ridge along x = 0.
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        Runtime::EditorCommandHistory History;
        Extrinsic::ECS::EntityHandle Entity;
        Runtime::EditorProcessingContext Context;
        Runtime::EditorScalarRidgeCommand Command;
        Harness()
        {
            constexpr int n = 32;
            Geometry::HalfedgeMesh::Mesh mesh;
            std::vector<Geometry::VertexHandle> v;
            for (int j = 0; j <= n; ++j)
                for (int i = 0; i <= n; ++i)
                    v.push_back(mesh.AddVertex({-1 + 2.f * i / n, -1 + 2.f * j / n, 0}));
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                {
                    const auto a = v[j * (n + 1) + i], b = v[j * (n + 1) + i + 1],
                               c = v[(j + 1) * (n + 1) + i + 1], d = v[(j + 1) * (n + 1) + i];
                    EXPECT_TRUE(mesh.AddTriangle(a, b, c));
                    EXPECT_TRUE(mesh.AddTriangle(a, c, d));
                }
            Entity = Extrinsic::ECS::Scene::CreateDefault(Scene, "Plate");
            GS::PopulateFromMesh(Scene.Raw(), Entity, mesh);
            auto positions = Properties().Get<glm::vec3>("v:position");
            auto bump = Properties().GetOrAdd<double>("v:bump", 0.0);
            auto bumpFloat = Properties().GetOrAdd<float>("v:bump_float", 0.f);
            for (std::size_t i = 0; i < Properties().Size(); ++i)
            {
                const double x = positions.Vector()[i].x;
                bump[i] = std::exp(-x * x / 0.08);
                bumpFloat[i] = static_cast<float>(bump[i]);
            }
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
            Command.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity);
            Command.Property = {Runtime::GeometryElementDomain::MeshVertex, "v:bump",
                                Geometry::PropertyValueKind::Double};
            Command.RadiusRatio = 0.08;
        }
        Geometry::PropertySet& Properties()
        {
            return Scene.Raw().get<GS::Vertices>(Entity).Properties;
        }
        [[nodiscard]] Runtime::EditorProcessingCommands Commands() const
        {
            return Runtime::BindEditorProcessingCommands(Context);
        }
        [[nodiscard]] std::size_t EntityCount() const
        {
            std::size_t count = 0;
            for ([[maybe_unused]] auto e : Scene.Raw().view<GS::Vertices>())
                ++count;
            return count;
        }
    };
} // namespace

TEST(ScalarRidgeOperations, PublishesRidgeGraphWithoutChangingSourceAndSupportsUndoRedo)
{
    Harness h;
    const auto names = h.Properties().Size();
    const auto positions = h.Properties().Get<glm::vec3>("v:position").Vector();
    const auto result = Runtime::ApplyEditorScalarRidgeCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_GT(result.RidgeSegmentCount, 10u);
    EXPECT_EQ(result.ValleySegmentCount, 0u);
    ASSERT_NE(result.OutputEntityId, 0u);
    EXPECT_EQ(h.Properties().Size(), names);
    EXPECT_EQ(h.Properties().Get<glm::vec3>("v:position").Vector(), positions);
    EXPECT_FALSE(h.Properties().Exists("v:scalar_ridge_field"));

    const auto graph = Runtime::SelectionController::ToEntityHandle(result.OutputEntityId);
    ASSERT_TRUE(h.Scene.Raw().valid(graph));
    ASSERT_TRUE(h.Scene.Raw().all_of<GS::Edges>(graph));
    // Const reads: a mutable property handle counts as an edit and would make
    // undo refuse to destroy the generated entity.
    const auto& raw = std::as_const(h.Scene.Raw());
    const auto kind = raw.get<GS::Edges>(graph).Properties.Get<float>("e:scalar_extremum");
    ASSERT_TRUE(kind);
    for (float k : kind.Vector())
        EXPECT_EQ(k, 1.f);
    for (auto p : raw.get<GS::Vertices>(graph).Properties.Get<glm::vec3>("v:position").Vector())
        EXPECT_LT(std::abs(p.x), 0.05f);

    EXPECT_EQ(h.EntityCount(), 2u);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(h.EntityCount(), 1u);
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_EQ(h.EntityCount(), 2u);
}

TEST(ScalarRidgeOperations, FloatPropertyAndValleyOnlyRequests)
{
    Harness h;
    h.Command.Property = {Runtime::GeometryElementDomain::MeshVertex, "v:bump_float",
                          Geometry::PropertyValueKind::Float};
    EXPECT_TRUE(Runtime::ApplyEditorScalarRidgeCommand(h.Commands(), h.Command).Succeeded());
    h.Command.Ridges = false;
    const auto valleys = Runtime::ApplyEditorScalarRidgeCommand(h.Commands(), h.Command);
    EXPECT_EQ(valleys.Status, Runtime::EditorCommandStatus::NoChange) << valleys.Message;
    EXPECT_EQ(h.History.UndoCount(), 1);
}

TEST(ScalarRidgeOperations, RejectsMissingNonScalarOrNonVertexPropertiesWithoutHistory)
{
    Harness h;
    const auto expectRejected = [&](Runtime::GeometryPropertyRef property) {
        h.Command.Property = std::move(property);
        const auto result = Runtime::ApplyEditorScalarRidgeCommand(h.Commands(), h.Command);
        EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters)
            << h.Command.Property.Name;
        EXPECT_FALSE(result.Message.empty());
    };
    expectRejected({Runtime::GeometryElementDomain::MeshVertex, "v:absent",
                    Geometry::PropertyValueKind::Double});
    expectRejected({Runtime::GeometryElementDomain::MeshVertex, "v:position",
                    Geometry::PropertyValueKind::Vec3});
    expectRejected({Runtime::GeometryElementDomain::MeshFace, "v:bump",
                    Geometry::PropertyValueKind::Double});
    h.Command.Property.Domain = Runtime::GeometryElementDomain::MeshVertex;
    h.Command.Property.Name = "v:bump";
    h.Command.Ridges = h.Command.Valleys = false;
    EXPECT_EQ(Runtime::ApplyEditorScalarRidgeCommand(h.Commands(), h.Command).Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    h.Command.Ridges = true;
    h.Command.StableEntityId = 0u;
    EXPECT_EQ(Runtime::ApplyEditorScalarRidgeCommand(h.Commands(), h.Command).Status,
              Runtime::EditorCommandStatus::StaleEntity);
    EXPECT_EQ(h.History.UndoCount(), 0);
    EXPECT_EQ(h.EntityCount(), 1u);
}
