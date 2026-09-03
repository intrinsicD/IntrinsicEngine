#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "EditorFeatureTestContext.hpp"

import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.SelectionController;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.Properties;
import Geometry.Subdivision;

namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeTwoRegimeMesh()
    {
        const Geometry::HalfedgeMesh::Mesh coarse =
            Geometry::HalfedgeMesh::MakeMeshIcosahedron();
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto subdivision = Geometry::Subdivision::Subdivide(
            coarse,
            mesh,
            Geometry::Subdivision::SubdivisionParams{.Iterations = 2u});
        EXPECT_TRUE(subdivision.has_value());
        for (const Geometry::VertexHandle vertex : mesh.LiveVertices())
        {
            glm::vec3& position = mesh.Position(vertex);
            if (position.z >= 0.0f)
            {
                position.x *= 0.55f;
                position.y *= 0.55f;
                position.z *= 1.65f;
            }
            else
            {
                position.x *= 1.25f;
                position.y *= 1.25f;
                position.z *= 0.8f;
            }
        }
        return mesh;
    }

    struct SegmentationHarness
    {
        ECS::Scene::Registry Scene{};
        Runtime::SelectionController Selection{};
        Runtime::EditorCommandHistory History{};
        Runtime::EditorSelectedModelCache ModelCache{};
        Geometry::HalfedgeMesh::Mesh SourceMesh{MakeTwoRegimeMesh()};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        Intrinsic::Tests::EditorFeatureTestContext Context{};

        SegmentationHarness()
        {
            Entity = Scene.Create();
            GS::PopulateFromMesh(Scene.Raw(), Entity, SourceMesh);
            EXPECT_TRUE(Selection.SetSelectedEntity(Scene, Entity));
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);
            Context.Scene = &Scene;
            Context.Selection = &Selection;
            Context.CommandHistory = &History;
            Context.SelectedModelCache = &ModelCache;
        }

        [[nodiscard]] GS::Vertices& Vertices()
        {
            return Scene.Raw().get<GS::Vertices>(Entity);
        }

        [[nodiscard]] GS::Edges& Edges()
        {
            return Scene.Raw().get<GS::Edges>(Entity);
        }

        [[nodiscard]] GS::Halfedges& Halfedges()
        {
            return Scene.Raw().get<GS::Halfedges>(Entity);
        }

        [[nodiscard]] GS::Faces& Faces()
        {
            return Scene.Raw().get<GS::Faces>(Entity);
        }
    };

    [[nodiscard]] Runtime::CurvatureSegmentationConfig MakeFixedConfig()
    {
        Runtime::CurvatureSegmentationConfig config{};
        config.Method =
            Runtime::CurvatureSegmentationMethod::CurvatureGmm;
        config.SelectionMode =
            Runtime::CurvatureSegmentationSelectionMode::FixedCount;
        config.FixedComponentCount = 2u;
        config.SpatialWeight = 0.25;
        config.FeatureSensitivity = 6.0;
        config.MinimumRegionFaces = 1u;
        config.Seed = 19u;
        return config;
    }

    [[nodiscard]] Runtime::EditorCurvatureSegmentationResult Apply(
        SegmentationHarness& harness)
    {
        return Runtime::ApplyEditorCurvatureSegmentationCommand(
            harness.Context,
            Runtime::EditorCurvatureSegmentationCommand{
                .StableEntityId = harness.StableEntityId,
                .Config = MakeFixedConfig(),
            });
    }
}

TEST(CurvatureSegmentationOperations,
     FeatureAlignedMethodPublishesEvidenceBoundaryRolesAndActualIdentity)
{
    SegmentationHarness harness{};
    Runtime::CurvatureSegmentationConfig config{};
    config.Method =
        Runtime::CurvatureSegmentationMethod::FeatureAlignedPatches;
    config.SelectionMode =
        Runtime::CurvatureSegmentationSelectionMode::FixedCount;
    config.FixedComponentCount = 2u;
    config.PatchComplexityCost = 0.5;

    const Runtime::EditorCurvatureSegmentationResult result =
        Runtime::ApplyEditorCurvatureSegmentationCommand(
            harness.Context,
            Runtime::EditorCurvatureSegmentationCommand{
                .StableEntityId = harness.StableEntityId,
                .Config = config,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.RequestedMethod,
              Runtime::CurvatureSegmentationMethod::FeatureAlignedPatches);
    EXPECT_EQ(result.ActualMethod, result.RequestedMethod);
    ASSERT_TRUE(result.FeatureDiagnostics.has_value());
    ASSERT_TRUE(result.PatchDiagnostics.has_value());
    EXPECT_EQ(result.Diagnostics.ConnectedRegionCount,
              result.PatchDiagnostics->FinalRegionCount);
    EXPECT_EQ(result.Diagnostics.BoundaryEdgeCount,
              result.PatchDiagnostics->FinalBoundaryEdgeCount);

    GS::Edges& edges = harness.Edges();
    const auto hard = edges.Properties.Get<bool>(
        GS::PropertyNames::kCurvatureHardFeature);
    const auto soft = edges.Properties.Get<double>(
        GS::PropertyNames::kCurvatureSoftFeatureConfidence);
    const auto boundary = edges.Properties.Get<bool>(
        GS::PropertyNames::kCurvatureRegionBoundary);
    const auto roles = edges.Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvaturePatchBoundaryRole);
    const auto colors = edges.Properties.Get<glm::vec4>(
        GS::PropertyNames::kCurvatureFeaturePatchColor);
    ASSERT_TRUE(hard);
    ASSERT_TRUE(soft);
    ASSERT_TRUE(boundary);
    ASSERT_TRUE(roles);
    ASSERT_TRUE(colors);
    ASSERT_EQ(hard.Vector().size(), edges.Properties.Size());
    ASSERT_EQ(soft.Vector().size(), edges.Properties.Size());
    ASSERT_EQ(roles.Vector().size(), edges.Properties.Size());
    ASSERT_EQ(colors.Vector().size(), edges.Properties.Size());

    std::size_t boundaryCount = 0u;
    for (std::size_t edge = 0u; edge < boundary.Vector().size(); ++edge)
    {
        if (boundary[edge])
        {
            ++boundaryCount;
            EXPECT_NE(roles[edge], 0u);
            EXPECT_GT(colors[edge].a, 0.0f);
        }
        else
        {
            EXPECT_FLOAT_EQ(colors[edge].a, 0.0f);
        }
        if (hard[edge])
        {
            EXPECT_TRUE(boundary[edge]);
            EXPECT_EQ(
                roles[edge],
                static_cast<std::uint32_t>(
                    Geometry::CurvatureSegmentation::
                        PatchBoundaryRole::HardFeature));
        }
        if (roles[edge] == static_cast<std::uint32_t>(
                Geometry::CurvatureSegmentation::
                    PatchBoundaryRole::SoftFeatureSupported))
        {
            EXPECT_GT(soft[edge], 0.0);
        }
    }
    EXPECT_EQ(boundaryCount,
              result.PatchDiagnostics->FinalBoundaryEdgeCount);
}

TEST(CurvatureSegmentationOperations,
     PublishesSemanticPropertiesPreservesTopologyAndSupportsHistory)
{
    SegmentationHarness harness{};
    GS::Vertices& vertices = harness.Vertices();
    GS::Edges& edges = harness.Edges();
    GS::Halfedges& halfedges = harness.Halfedges();
    GS::Faces& faces = harness.Faces();

    const std::vector<glm::vec3> positions =
        vertices.Properties.Get<glm::vec3>(
            GS::PropertyNames::kPosition).Vector();
    const std::vector<std::uint32_t> edgeV0 =
        edges.Properties.Get<std::uint32_t>(
            GS::PropertyNames::kEdgeV0).Vector();
    const std::vector<std::uint32_t> edgeV1 =
        edges.Properties.Get<std::uint32_t>(
            GS::PropertyNames::kEdgeV1).Vector();
    const std::vector<std::uint32_t> halfedgeNext =
        halfedges.Properties.Get<std::uint32_t>(
            GS::PropertyNames::kHalfedgeNext).Vector();
    const std::vector<std::uint32_t> halfedgeFaces =
        halfedges.Properties.Get<std::uint32_t>(
            GS::PropertyNames::kHalfedgeFace).Vector();
    const std::vector<std::uint32_t> faceHalfedges =
        faces.Properties.Get<std::uint32_t>(
            GS::PropertyNames::kFaceHalfedge).Vector();

    const std::vector<float> authoredFaceValues(
        faces.Properties.Size(), 3.25f);
    faces.Properties.GetOrAdd<float>("f:user_value", 0.0f).Vector() =
        authoredFaceValues;
    const std::vector<glm::vec2> authoredEdgeValues(
        edges.Properties.Size(), glm::vec2{0.25f, 0.75f});
    edges.Properties.GetOrAdd<glm::vec2>(
        "e:user_value", glm::vec2{0.0f}).Vector() = authoredEdgeValues;

    ASSERT_FALSE(faces.Properties.Exists(
        GS::PropertyNames::kCurvatureComponent));
    ASSERT_FALSE(edges.Properties.Exists(
        GS::PropertyNames::kCurvatureRegionBoundary));
    ASSERT_FALSE(edges.Properties.Exists(
        GS::PropertyNames::kCurvatureHardFeature));
    const Geometry::PropertyRevision faceRevisionBefore =
        faces.Properties.Revision();
    const Geometry::PropertyRevision edgeRevisionBefore =
        edges.Properties.Revision();

    const Runtime::EditorCurvatureSegmentationResult first = Apply(harness);
    ASSERT_TRUE(first.Succeeded()) << first.Message;
    ASSERT_EQ(first.Status, Runtime::EditorCommandStatus::Applied);
    EXPECT_EQ(first.Diagnostics.SelectedComponentCount, 2u);
    EXPECT_GT(first.ChangedValueCount, 0u);
    EXPECT_EQ(harness.History.UndoCount(), 1u);
    EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::GpuDirty>(
        harness.Entity));
    EXPECT_GT(faces.Properties.Revision(), faceRevisionBefore);
    EXPECT_GT(edges.Properties.Revision(), edgeRevisionBefore);

    const auto components = faces.Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureComponent);
    const auto regions = faces.Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureRegion);
    const auto regionColors = faces.Properties.Get<glm::vec4>(
        GS::PropertyNames::kCurvatureRegionColor);
    const auto boundaries = edges.Properties.Get<bool>(
        GS::PropertyNames::kCurvatureRegionBoundary);
    const auto boundaryColors = edges.Properties.Get<glm::vec4>(
        GS::PropertyNames::kCurvatureRegionBoundaryColor);
    const auto hardFeatures = edges.Properties.Get<bool>(
        GS::PropertyNames::kCurvatureHardFeature);
    ASSERT_TRUE(components);
    ASSERT_TRUE(regions);
    ASSERT_TRUE(regionColors);
    ASSERT_TRUE(boundaries);
    ASSERT_TRUE(boundaryColors);
    ASSERT_TRUE(hardFeatures);
    ASSERT_EQ(components.Vector().size(), faces.Properties.Size());
    ASSERT_EQ(regions.Vector().size(), faces.Properties.Size());
    ASSERT_EQ(regionColors.Vector().size(), faces.Properties.Size());
    ASSERT_EQ(boundaries.Vector().size(), edges.Properties.Size());
    ASSERT_EQ(boundaryColors.Vector().size(), edges.Properties.Size());
    EXPECT_TRUE(std::all_of(
        components.Vector().begin(),
        components.Vector().end(),
        [](const std::uint32_t label) { return label < 2u; }));
    EXPECT_TRUE(std::all_of(
        regions.Vector().begin(),
        regions.Vector().end(),
        [](const std::uint32_t label)
        {
            return label != std::numeric_limits<std::uint32_t>::max();
        }));
    EXPECT_TRUE(std::all_of(
        regionColors.Vector().begin(),
        regionColors.Vector().end(),
        [](const glm::vec4 color) { return color.a == 1.0f; }));

    std::size_t semanticBoundaryCount = 0u;
    for (const Geometry::EdgeHandle edge : harness.SourceMesh.LiveEdges())
    {
        bool expected = false;
        if (!harness.SourceMesh.IsBoundary(edge))
        {
            const Geometry::FaceHandle face0 = harness.SourceMesh.Face(
                harness.SourceMesh.Halfedge(edge, 0u));
            const Geometry::FaceHandle face1 = harness.SourceMesh.Face(
                harness.SourceMesh.Halfedge(edge, 1u));
            ASSERT_TRUE(face0.IsValid());
            ASSERT_TRUE(face1.IsValid());
            expected = regions[face0.Index] != regions[face1.Index];
        }
        EXPECT_EQ(boundaries[edge.Index], expected);
        EXPECT_FLOAT_EQ(
            boundaryColors[edge.Index].a,
            expected ? 1.0f : 0.0f);
        semanticBoundaryCount += expected ? 1u : 0u;
    }
    EXPECT_EQ(semanticBoundaryCount,
              first.Diagnostics.BoundaryEdgeCount);

    EXPECT_EQ(vertices.Properties.Get<glm::vec3>(
                  GS::PropertyNames::kPosition).Vector(),
              positions);
    EXPECT_EQ(edges.Properties.Get<std::uint32_t>(
                  GS::PropertyNames::kEdgeV0).Vector(),
              edgeV0);
    EXPECT_EQ(edges.Properties.Get<std::uint32_t>(
                  GS::PropertyNames::kEdgeV1).Vector(),
              edgeV1);
    EXPECT_EQ(halfedges.Properties.Get<std::uint32_t>(
                  GS::PropertyNames::kHalfedgeNext).Vector(),
              halfedgeNext);
    EXPECT_EQ(halfedges.Properties.Get<std::uint32_t>(
                  GS::PropertyNames::kHalfedgeFace).Vector(),
              halfedgeFaces);
    EXPECT_EQ(faces.Properties.Get<std::uint32_t>(
                  GS::PropertyNames::kFaceHalfedge).Vector(),
              faceHalfedges);
    EXPECT_EQ(faces.Properties.Get<float>("f:user_value").Vector(),
              authoredFaceValues);
    EXPECT_EQ(edges.Properties.Get<glm::vec2>("e:user_value").Vector(),
              authoredEdgeValues);

    const std::vector<std::uint32_t> publishedComponents =
        components.Vector();
    const std::vector<std::uint32_t> publishedRegions = regions.Vector();
    const std::vector<glm::vec4> publishedRegionColors =
        regionColors.Vector();
    const std::vector<bool> publishedBoundaries = boundaries.Vector();
    const std::vector<glm::vec4> publishedBoundaryColors =
        boundaryColors.Vector();

    harness.Scene.Raw().remove<Dirty::GpuDirty>(harness.Entity);
    const Runtime::EditorCurvatureSegmentationResult second = Apply(harness);
    EXPECT_EQ(second.Status, Runtime::EditorCommandStatus::NoChange)
        << second.Message;
    EXPECT_EQ(second.ChangedValueCount, 0u);
    EXPECT_EQ(harness.History.UndoCount(), 1u);
    EXPECT_FALSE(harness.Scene.Raw().all_of<Dirty::GpuDirty>(
        harness.Entity));

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_FALSE(faces.Properties.Exists(
        GS::PropertyNames::kCurvatureComponent));
    EXPECT_FALSE(faces.Properties.Exists(
        GS::PropertyNames::kCurvatureRegion));
    EXPECT_FALSE(faces.Properties.Exists(
        GS::PropertyNames::kCurvatureRegionColor));
    EXPECT_FALSE(edges.Properties.Exists(
        GS::PropertyNames::kCurvatureRegionBoundary));
    EXPECT_FALSE(edges.Properties.Exists(
        GS::PropertyNames::kCurvatureRegionBoundaryColor));
    EXPECT_FALSE(edges.Properties.Exists(
        GS::PropertyNames::kCurvatureHardFeature));
    EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::GpuDirty>(
        harness.Entity));
    EXPECT_EQ(faces.Properties.Get<float>("f:user_value").Vector(),
              authoredFaceValues);
    EXPECT_EQ(edges.Properties.Get<glm::vec2>("e:user_value").Vector(),
              authoredEdgeValues);

    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_EQ(faces.Properties.Get<std::uint32_t>(
                  GS::PropertyNames::kCurvatureComponent).Vector(),
              publishedComponents);
    EXPECT_EQ(faces.Properties.Get<std::uint32_t>(
                  GS::PropertyNames::kCurvatureRegion).Vector(),
              publishedRegions);
    EXPECT_EQ(faces.Properties.Get<glm::vec4>(
                  GS::PropertyNames::kCurvatureRegionColor).Vector(),
              publishedRegionColors);
    EXPECT_EQ(edges.Properties.Get<bool>(
                  GS::PropertyNames::kCurvatureRegionBoundary).Vector(),
              publishedBoundaries);
    EXPECT_EQ(edges.Properties.Get<glm::vec4>(
                  GS::PropertyNames::kCurvatureRegionBoundaryColor).Vector(),
              publishedBoundaryColors);
    EXPECT_TRUE(edges.Properties.Exists(
        GS::PropertyNames::kCurvatureHardFeature));

    auto currentPositions = vertices.Properties.Get<glm::vec3>(
        GS::PropertyNames::kPosition);
    ASSERT_TRUE(currentPositions);
    currentPositions[0].x += 0.125f;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        harness.History.Snapshot();
    EXPECT_EQ(harness.History.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(harness.History.UndoCount(), 1u);
    EXPECT_EQ(harness.History.Snapshot().Revision,
              beforeRejectedUndo.Revision);
    currentPositions[0].x = positions[0].x;
    EXPECT_TRUE(harness.History.Undo().Succeeded());
}

TEST(CurvatureSegmentationOperations,
     RejectsInvalidConfigurationAndUnavailableKernelWithoutMutation)
{
    SegmentationHarness harness{};
    Runtime::CurvatureSegmentationConfig invalid = MakeFixedConfig();
    invalid.FixedComponentCount = 0u;
    const auto rejected = Runtime::ApplyEditorCurvatureSegmentationCommand(
        harness.Context,
        Runtime::EditorCurvatureSegmentationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = invalid,
        });
    EXPECT_EQ(rejected.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(harness.Faces().Properties.Exists(
        GS::PropertyNames::kCurvatureComponent));
    EXPECT_EQ(harness.History.UndoCount(), 0u);

    harness.Context.CurvatureSegmentationKernelAvailable = false;
    const auto unavailable = Apply(harness);
    EXPECT_EQ(unavailable.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_FALSE(harness.Faces().Properties.Exists(
        GS::PropertyNames::kCurvatureComponent));
    EXPECT_EQ(harness.History.UndoCount(), 0u);
}
