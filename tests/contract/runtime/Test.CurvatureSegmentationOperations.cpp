#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <utility>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "EditorFeatureTestContext.hpp"

import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.SelectionController;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;
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
            EXPECT_GT(colors[edge].w, 0.0f);
        }
        else
        {
            EXPECT_FLOAT_EQ(colors[edge].w, 0.0f);
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
        [](const glm::vec4 color) { return color.w == 1.0f; }));

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
            boundaryColors[edge.Index].w,
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

TEST(CurvatureSegmentationOperations,
     BoundaryCurvesPublishRegionsWithoutFittedComponentsAndUndoRemoval)
{
    SegmentationHarness harness{};
    Runtime::CurvatureSegmentationConfig config{};
    config.Method = Runtime::CurvatureSegmentationMethod::FeatureBoundaryCurves;
    const auto command = Runtime::EditorCurvatureSegmentationCommand{
        .StableEntityId = harness.StableEntityId, .Config = config};
    const auto positions = harness.Vertices().Properties.Get<glm::vec3>(
        GS::PropertyNames::kPosition).Vector();
    const auto topology = harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kFaceHalfedge).Vector();
    (void)harness.Faces().Properties.GetOrAdd<float>("f:user_value", 3.25f);
    const auto result = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.ActualMethod, config.Method);
    EXPECT_EQ(result.RequestedMethod, config.Method);
    ASSERT_TRUE(result.BoundaryDiagnostics.has_value());
    EXPECT_FALSE(result.PatchDiagnostics.has_value());
    EXPECT_FALSE(result.Diagnostics.Succeeded());
    EXPECT_EQ(result.Message.find("GMM components="), std::string::npos);
    EXPECT_NE(result.Message.find("curves_v1"), std::string::npos);
    EXPECT_FALSE(harness.Faces().Properties.Exists(GS::PropertyNames::kCurvatureComponent));
    const auto regions = harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureRegion).Vector();
    const auto boundaries = harness.Edges().Properties.Get<bool>(
        GS::PropertyNames::kCurvatureRegionBoundary).Vector();
    const auto hard = harness.Edges().Properties.Get<bool>(GS::PropertyNames::kCurvatureHardFeature);
    const auto colors = harness.Edges().Properties.Get<glm::vec4>(GS::PropertyNames::kCurvatureFeaturePatchColor);
    ASSERT_EQ(regions.size(), harness.Faces().Properties.Size());
    ASSERT_EQ(boundaries.size(), harness.Edges().Properties.Size());
    for (const auto region : regions)
        EXPECT_LT(region, result.BoundaryDiagnostics->RegionCount);
    for (const auto edge : harness.SourceMesh.LiveEdges())
    {
        const bool expected = !harness.SourceMesh.IsBoundary(edge) &&
            regions[harness.SourceMesh.Face(harness.SourceMesh.Halfedge(edge, 0)).Index] !=
            regions[harness.SourceMesh.Face(harness.SourceMesh.Halfedge(edge, 1)).Index];
        EXPECT_EQ(boundaries[edge.Index], expected);
        if (hard[edge.Index]) EXPECT_TRUE(expected);
        EXPECT_FLOAT_EQ(colors[edge.Index].w, expected ? 1.0f : 0.0f);
    }
    EXPECT_EQ(harness.History.UndoCount(), 1u);
    // Adding only stale fitted labels must still create a removal transaction
    // when every partition output is already identical.
    const std::vector<std::uint32_t> staleComponents(regions.size(), 7u);
    harness.Faces().Properties.GetOrAdd<std::uint32_t>(
        std::string{GS::PropertyNames::kCurvatureComponent}, 0u).Vector() = staleComponents;
    const auto repeated = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command);
    ASSERT_TRUE(repeated.Succeeded()) << repeated.Message;
    EXPECT_EQ(repeated.Status, Runtime::EditorCommandStatus::Applied);
    EXPECT_EQ(repeated.ChangedValueCount, staleComponents.size());
    EXPECT_FALSE(harness.Faces().Properties.Exists(GS::PropertyNames::kCurvatureComponent));
    EXPECT_EQ(harness.History.UndoCount(), 2u);
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureComponent).Vector(), staleComponents);
    EXPECT_EQ(harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureRegion).Vector(), regions);
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_FALSE(harness.Faces().Properties.Exists(GS::PropertyNames::kCurvatureComponent));
    EXPECT_EQ(harness.Edges().Properties.Get<bool>(
        GS::PropertyNames::kCurvatureRegionBoundary).Vector(), boundaries);
    EXPECT_EQ(harness.Vertices().Properties.Get<glm::vec3>(GS::PropertyNames::kPosition).Vector(), positions);
    EXPECT_EQ(harness.Faces().Properties.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge).Vector(), topology);
    EXPECT_FLOAT_EQ(harness.Faces().Properties.Get<float>("f:user_value")[0], 3.25f);
    const auto unchanged = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command);
    EXPECT_TRUE(unchanged.Succeeded());
    EXPECT_EQ(unchanged.Status, Runtime::EditorCommandStatus::NoChange);
    EXPECT_EQ(harness.History.UndoCount(), 2u);
}

TEST(CurvatureSegmentationOperations, BoundaryCurvesFailurePreservesExistingPublication)
{
    SegmentationHarness harness{};
    ASSERT_TRUE(Apply(harness).Succeeded());
    const auto components = harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureComponent).Vector();
    const auto regions = harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureRegion).Vector();
    const auto history = harness.History.UndoCount();
    // A collapsed surface must fail before publication or history changes.
    auto position = harness.Vertices().Properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
    std::fill(position.Vector().begin(), position.Vector().end(), glm::vec3{0.0f});
    Runtime::CurvatureSegmentationConfig config{};
    config.Method = Runtime::CurvatureSegmentationMethod::FeatureBoundaryCurves;
    const auto failed = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context,
        {.StableEntityId = harness.StableEntityId, .Config = config});
    EXPECT_FALSE(failed.Succeeded());
    EXPECT_EQ(failed.ActualMethod, config.Method);
    EXPECT_EQ(harness.History.UndoCount(), history);
    EXPECT_EQ(harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureComponent).Vector(), components);
    EXPECT_EQ(harness.Faces().Properties.Get<std::uint32_t>(
        GS::PropertyNames::kCurvatureRegion).Vector(), regions);
}

TEST(CurvatureSegmentationOperations, CustomBindingsPreserveCanonicalOutputsAndUndo)
{
    SegmentationHarness h;
    auto config = MakeFixedConfig();
    auto alternate = h.Vertices().Properties.GetOrAdd<glm::vec3>("v:rest", {});
    alternate.Vector() = h.Vertices().Properties.Get<glm::vec3>("v:position").Vector();
    config.Positions.Name = "v:rest";
    for (auto* output : {&config.Components, &config.Regions, &config.RegionColors,
                         &config.Boundaries, &config.BoundaryColors, &config.HardFeatures,
                         &config.FeatureConfidence, &config.BoundaryRoles, &config.FeatureColors})
        output->Name += "_custom";
    auto sentinel = h.Faces().Properties.GetOrAdd<glm::vec4>("f:curvature_region_color", {1, 0, 0, 1});
    const auto before = sentinel.Vector();
    const auto result = Runtime::ApplyEditorCurvatureSegmentationCommand(h.Context,
        {.StableEntityId=h.StableEntityId, .Config=config});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(h.Faces().Properties.Exists(config.Regions.Name));
    EXPECT_TRUE(h.Edges().Properties.Exists(config.FeatureColors.Name));
    EXPECT_FALSE(h.Faces().Properties.Exists("f:curvature_region"));
    EXPECT_EQ(sentinel.Vector(), before);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(h.Faces().Properties.Exists(config.Regions.Name));
    EXPECT_FALSE(h.Edges().Properties.Exists(config.FeatureColors.Name));
    EXPECT_EQ(sentinel.Vector(), before);
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_TRUE(h.Faces().Properties.Exists(config.Regions.Name));
}

TEST(CurvatureSegmentationOperations, ColorHistoryUsesNumericComponentEquality)
{
    SegmentationHarness harness;
    const auto config = MakeFixedConfig();
    auto colors = harness.Faces().Properties.GetOrAdd<glm::vec4>(
        config.RegionColors.Name, glm::vec4{0.0f});
    ASSERT_TRUE(Apply(harness).Succeeded());
    ASSERT_TRUE(harness.History.Undo().Succeeded());

    // A sign-only zero edit remains equivalent to the captured authored colors.
    colors[0] = glm::vec4{-0.0f};
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    const glm::vec4 published = colors[0];
    const auto revision = harness.History.Snapshot().Revision;
    for (int component = 0; component < 4; ++component)
    {
        colors[0][component] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(harness.History.Undo().Status,
                  Runtime::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_EQ(harness.History.Snapshot().Revision, revision);
        colors[0] = published;
    }
    EXPECT_TRUE(harness.History.Undo().Succeeded());
}

TEST(CurvatureSegmentationOperations, SuppliedFeatureShapesAndNamesAreEquivalent)
{
    SegmentationHarness harness;
    auto& faces = harness.Faces().Properties;
    const auto count = faces.Size();
    auto scalar = faces.GetOrAdd<float>("temperature");
    auto wide = faces.GetOrAdd<double>("unrelated_label");
    auto second = faces.GetOrAdd<float>("second");
    auto vector = faces.GetOrAdd<glm::vec2>("two_channels");
    for (std::size_t i = 0; i < count; ++i)
    {
        const float value = (i % 2u) == 0u ? -2.0f : 2.0f;
        scalar[i] = value;
        wide[i] = value;
        second[i] = value * 3.0f;
        vector[i] = {value, value * 3.0f};
    }
    auto config = MakeFixedConfig();
    config.SpatialWeight = 0.0;
    config.MinimumRegionFaces = 1u;
    const auto run = [&](std::vector<Runtime::GeometryPropertyRef> features)
    {
        config.Features = std::move(features);
        const Runtime::EditorCurvatureSegmentationCommand command{.StableEntityId = harness.StableEntityId, .Config = config};
        EXPECT_TRUE(Runtime::PreviewEditorCurvatureSegmentationCommand(harness.Context, command).Enabled);
        const auto result = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command);
        EXPECT_TRUE(result.Succeeded()) << result.Message;
        return faces.Get<std::uint32_t>(config.Components.Name).Vector();
    };
    using D = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    const auto baseline = run({{D::MeshFace, "temperature", K::Float}});
    EXPECT_EQ(baseline, run({{D::MeshFace, "unrelated_label", K::Double}}));
    ASSERT_EQ(baseline.size(), count);
    ASSERT_NE(baseline[0], baseline[1]);
    for (std::size_t i = 0; i < count; ++i) EXPECT_EQ(baseline[i], baseline[i % 2u]);
    EXPECT_EQ(run({{D::MeshFace, "two_channels", K::Vec2}}),
              run({{D::MeshFace, "temperature", K::Float}, {D::MeshFace, "second", K::Float}}));

    // A different field on the same unchanged surface must change the partition.
    for (std::size_t i = 0; i < count; ++i) scalar[i] = i % 3u == 0u ? -2.0f : 2.0f;
    const auto changed = run({{D::MeshFace, "temperature", K::Float}});
    ASSERT_NE(changed[0], changed[1]);
    EXPECT_EQ(changed[1], changed[2]);
    EXPECT_NE(baseline[1], baseline[2]);
    EXPECT_FALSE(harness.Vertices().Properties.Get<double>("v:k1"));
    EXPECT_EQ(wide[0], -2.0);
}

TEST(CurvatureSegmentationOperations, VertexFeaturesMatchTheirExplicitFaceAverages)
{
    SegmentationHarness harness;
    auto values = harness.Vertices().Properties.GetOrAdd<float>("samples");
    for (std::size_t i = 0; i < values.Vector().size(); ++i)
        values[i] = static_cast<float>(i % 7u);
    auto faceValues = harness.Faces().Properties.GetOrAdd<double>("averaged");
    for (const auto face : harness.SourceMesh.LiveFaces())
    {
        double sum = 0.0;
        for (const auto vertex : harness.SourceMesh.VerticesAroundFace(face)) sum += values[vertex.Index];
        faceValues[face.Index] = sum / 3.0;
    }
    auto config = MakeFixedConfig();
    config.Features = {{Runtime::GeometryElementDomain::MeshVertex, "samples", Geometry::PropertyValueKind::Float}};
    auto result = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context,
        {.StableEntityId = harness.StableEntityId, .Config = config});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const auto expected = harness.Faces().Properties.Get<std::uint32_t>(config.Components.Name).Vector();
    config.Features = {{Runtime::GeometryElementDomain::MeshFace, "averaged", Geometry::PropertyValueKind::Double}};
    result = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context,
        {.StableEntityId = harness.StableEntityId, .Config = config});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(expected, harness.Faces().Properties.Get<std::uint32_t>(config.Components.Name).Vector());
}

TEST(CurvatureSegmentationOperations, InvalidFeaturesNeverFallBackToComputedCurvature)
{
    SegmentationHarness harness;
    auto config = MakeFixedConfig();
    using D = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    config.Features = {{D::MeshFace, "missing", K::Float}};
    auto command = Runtime::EditorCurvatureSegmentationCommand{.StableEntityId = harness.StableEntityId, .Config = config};
    EXPECT_FALSE(Runtime::PreviewEditorCurvatureSegmentationCommand(harness.Context, command).Enabled);
    EXPECT_FALSE(Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command).Succeeded());
    auto wide = harness.Faces().Properties.GetOrAdd<std::uint64_t>("wide");
    wide[0] = (std::uint64_t{1} << 53u) + 1u;
    command.Config.Features = {{D::MeshFace, "wide", K::UInt64}};
    EXPECT_TRUE(Runtime::PreviewEditorCurvatureSegmentationCommand(harness.Context, command).Enabled);
    const auto invalid = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command);
    EXPECT_FALSE(invalid.Succeeded());
    EXPECT_NE(invalid.Message.find("precision loss"), std::string::npos);
    EXPECT_EQ(harness.History.UndoCount(), 0u);
    EXPECT_FALSE(harness.Faces().Properties.Get<std::uint32_t>(config.Components.Name));
    command.Config.Features = {{D::MeshFace, "wide", K::Vec4}};
    EXPECT_FALSE(Runtime::IsValidCurvatureSegmentationConfig(command.Config));
    command.Config.Features = {{D::MeshFace, "wide", K::UInt64}};
    command.Config.Method = Runtime::CurvatureSegmentationMethod::FeatureBoundaryCurves;
    EXPECT_FALSE(Runtime::IsValidCurvatureSegmentationConfig(command.Config));
}


TEST(CurvatureSegmentationOperations, InputPreservationFiniteChecksAndStructuralOutputs)
{
    SegmentationHarness harness;
    auto& faces = harness.Faces().Properties;
    auto values = faces.GetOrAdd<double>("field");
    for (std::size_t i = 0; i < faces.Size(); ++i) values[i] = i % 2u ? 1.0 : -1.0;
    const auto before = values.Vector();
    auto config = MakeFixedConfig();
    using D = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    config.Features = {{D::MeshFace, "field", K::Double}};
    auto command = Runtime::EditorCurvatureSegmentationCommand{.StableEntityId = harness.StableEntityId, .Config = config};
    const auto result = Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(values.Vector(), before);
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(values.Vector(), before);
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_EQ(values.Vector(), before);
    const auto history = harness.History.UndoCount();
    values[0] = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(Runtime::ApplyEditorCurvatureSegmentationCommand(harness.Context, command).Succeeded());
    EXPECT_EQ(harness.History.UndoCount(), history);
    for (const auto* name : {"f:halfedge", "f:connectivity", "f:deleted"})
    {
        command.Config.Components.Name = name;
        EXPECT_FALSE(Runtime::IsValidCurvatureSegmentationConfig(command.Config));
    }
    command.Config = config;
    command.Config.Components.Name = "field";
    EXPECT_FALSE(Runtime::IsValidCurvatureSegmentationConfig(command.Config));
    EXPECT_TRUE(Runtime::IsSegmentationFeatureBinding({D::MeshFace, "any", K::Vec3}));
    EXPECT_FALSE(Runtime::IsSegmentationFeatureBinding({D::MeshHalfedge, "any", K::Vec3}));
    EXPECT_FALSE(Runtime::IsSegmentationFeatureBinding({D::MeshFace, "any", K::Vec4}));
}
