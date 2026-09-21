#include <algorithm>
#include <memory>
#include <optional>
#include <functional>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <variant>
#include <gtest/gtest.h>
#include "EditorFeatureTestContext.hpp"
#include "SandboxEditorJobHarness.hpp"
#include "PointDomainFixture.hpp"

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
using Intrinsic::Tests::PointDomainProperties;
namespace
{
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    entt::entity Make(Extrinsic::ECS::Scene::Registry &scene, D domain)
    {
        auto entity = Intrinsic::Tests::MakePointDomainSource(scene, domain);
        auto &props = PointDomainProperties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i)
            samples[i] = plane[i % 4];
        auto keep = props.GetOrAdd<float>("keep");
        std::ranges::fill(keep.Vector(), 42.f);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }
    R::NormalEstimationConfig Config(entt::entity entity, D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain,
                              .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Output = {
                    .Domain = domain, .Name = "estimated", .ValueKind = Geometry::PropertyValueKind::Vec3}};
    }
} // namespace

TEST(NormalEstimation, EveryCanonicalDomainPublishesNamedNormalsAndSupportsUndoRedo)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (auto backend : {R::NormalEstimationBackend::CpuKDTree, R::NormalEstimationBackend::CpuLBVH})
        {
            SCOPED_TRACE(std::to_string(d) + R::ToString(backend));
            R::WorldRegistry worlds;
            auto world = worlds.CreateWorld("normals");
            auto &scene = *worlds.Get(world);
            R::SpatialIndexCache cache(worlds);
            const auto entity = Make(scene, D(d));
            auto config = Config(entity, D(d));
            config.Backend = backend;
            auto &props = PointDomainProperties(scene, entity, D(d));
            const auto slots = props.Size();
            R::EditorCommandHistory history;
            R::EditorProcessingContext context{
                .Scene = &scene, .World = world, .CommandHistory = &history, .SpatialIndices = &cache};
            auto catalog = R::GetEditorPointInputCatalog(R::BindEditorProcessingCommands(context), config.StableEntityId);
            ASSERT_TRUE(std::ranges::any_of(catalog.Entries,
                                            [&](const auto &e) { return e.Ref == config.Positions; }));
            const auto inputRevision = std::as_const(props).Get<glm::vec3>("samples").Revision();
            ASSERT_TRUE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
            EXPECT_FALSE(props.Exists("estimated"));
            const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
            ASSERT_EQ(result.Status, R::EditorCommandStatus::Applied) << result.Message;
            EXPECT_EQ(result.ActualBackend, R::ToString(backend));
            EXPECT_EQ(result.Output, config.Output);
            EXPECT_EQ(result.ValidCount, result.LiveCount);
            EXPECT_EQ(result.FallbackCount, 0);
            EXPECT_EQ(result.WrittenCount, result.LiveCount);
            EXPECT_EQ(props.Size(), slots);
            const auto normal = std::as_const(props).Get<glm::vec3>("estimated");
            for (std::size_t i = 0; i < result.LiveCount; ++i)
            {
                EXPECT_NEAR(normal[i].z, 1.f, 1e-5);
                EXPECT_NEAR(glm::length(normal[i]), 1.f, 1e-5);
            }
            if (D(d) == D::PointCloudPoint)
                EXPECT_EQ(normal[4], glm::vec3(0));
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(), inputRevision);
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 42.f);
            ASSERT_TRUE(history.Undo().Succeeded());
            EXPECT_FALSE(props.Exists("estimated"));
            ASSERT_TRUE(history.Redo().Succeeded());
            EXPECT_TRUE(props.Exists("estimated"));
            auto repeated = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
            EXPECT_EQ(repeated.Status, R::EditorCommandStatus::NoChange);
            EXPECT_EQ(repeated.IndexReused, backend == R::NormalEstimationBackend::CpuLBVH);
        }
}

TEST(NormalEstimation, PreservesDeletedRowsAndUnrelatedEditsButGuardsOutputHistory)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::PointCloudPoint);
    auto &props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    std::ranges::fill(props.GetOrAdd<glm::vec3>("estimated").Vector(), glm::vec3(7, 8, 9));
    props.Get<glm::vec3>("estimated")[4].x = std::numeric_limits<float>::quiet_NaN();
    R::EditorCommandHistory history;
    R::EditorProcessingContext context{.Scene = &scene, .CommandHistory = &history};
    ASSERT_TRUE(
        R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), Config(entity, D::PointCloudPoint)).Succeeded());
    EXPECT_TRUE(std::isnan(std::as_const(props).Get<glm::vec3>("estimated")[4].x));
    props.Get<float>("keep")[0] = 99.f;
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("estimated")[0], glm::vec3(7, 8, 9));
    ASSERT_TRUE(history.Redo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99.f);
    props.Get<glm::vec3>("estimated")[0] = glm::vec3(1, 0, 0);
    EXPECT_FALSE(history.Undo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("estimated")[0], glm::vec3(1, 0, 0));
}

TEST(NormalEstimation, SharedCapturePreservesDeletedVertexAndPairedHalfedgeOutputs)
{
    for (const auto [domain, method] : std::array{
             std::pair{D::MeshVertex, R::NormalEstimationMethod::MeshFaceWeighted},
             std::pair{D::MeshVertex, R::NormalEstimationMethod::GraphNeighborhood},
             std::pair{D::GraphNode, R::NormalEstimationMethod::GraphNeighborhood},
             std::pair{D::GraphHalfedge, R::NormalEstimationMethod::PointSetPCA},
             std::pair{D::MeshHalfedge, R::NormalEstimationMethod::PointSetPCA}})
    {
        SCOPED_TRACE(std::to_string(unsigned(domain)) + R::ToString(method));
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, domain);
        auto config = Config(entity, domain);
        config.Method = method;
        auto& props = PointDomainProperties(scene, entity, domain);
        const bool halfedges = domain == D::GraphHalfedge || domain == D::MeshHalfedge;
        auto& deletion = halfedges ? scene.Raw().get<GS::Edges>(entity).Properties : props;
        const auto maskName = halfedges ? "e:deleted" : "v:deleted";
        deletion.GetOrAdd<bool>(maskName)[0] = true;
        const glm::vec3 sentinel{7,8,9};
        std::ranges::fill(props.GetOrAdd<glm::vec3>("estimated").Vector(), sentinel);
        const auto deletionRevision = std::as_const(deletion).Get<bool>(maskName).Revision();
        R::EditorProcessingContext context{.Scene = &scene};
        const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        const std::size_t deletedCount = halfedges ? 2 : 1;
        EXPECT_EQ(result.LiveCount, props.Size() - deletedCount);
        EXPECT_EQ(result.WrittenCount, result.LiveCount);
        const auto normals = std::as_const(props).Get<glm::vec3>("estimated");
        for (std::size_t i = 0; i < normals.Size(); ++i)
            if (i < deletedCount)
                EXPECT_EQ(normals[i], sentinel);
            else
                EXPECT_NEAR(glm::length(normals[i]), 1.f, 1e-5);
        EXPECT_EQ(std::as_const(deletion).Get<bool>(maskName).Revision(), deletionRevision);
        EXPECT_TRUE(std::as_const(deletion).Get<bool>(maskName)[0]);
    }
}

TEST(NormalEstimation, TopologyVariantsConsumeCustomPositionsAndGraphMethodAcceptsMesh)
{
    for (auto domain : {D::MeshVertex, D::GraphNode})
        for (auto method :
             {R::NormalEstimationMethod::MeshFaceWeighted, R::NormalEstimationMethod::GraphNeighborhood})
        {
            Extrinsic::ECS::Scene::Registry scene;
            const auto entity = Make(scene, domain);
            auto config = Config(entity, domain);
            config.Method = method;
            R::EditorProcessingContext context{.Scene = &scene};
            if (domain == D::GraphNode && method == R::NormalEstimationMethod::MeshFaceWeighted)
            {
                EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
                continue;
            }
            auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
            ASSERT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_EQ(result.ValidCount, 4);
            EXPECT_EQ(result.FallbackCount, 0);
            auto normal = std::as_const(PointDomainProperties(scene, entity, domain)).Get<glm::vec3>("estimated");
            for (std::size_t i = 0; i < normal.Size(); ++i)
                EXPECT_NEAR(std::abs(normal[i].z), 1.f, 1e-5);
            if (method == R::NormalEstimationMethod::MeshFaceWeighted)
                EXPECT_EQ(result.ProcessedFaces, 2);
        }
}

TEST(NormalEstimation, MeshDeletionMasksExcludeFacesAndEdgesWithoutRenumberingOutput)
{
    for (bool edgeDeletion : {false, true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D::MeshVertex);
        auto config = Config(entity, D::MeshVertex);
        config.Method = R::NormalEstimationMethod::MeshFaceWeighted;
        if (edgeDeletion)
            scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[0] = true;
        else
        {
            auto &faces = scene.Raw().get<GS::Faces>(entity).Properties;
            faces.GetOrAdd<bool>("f:deleted")[0] = true;
            faces.Get<std::uint32_t>("f:halfedge")[0] = std::numeric_limits<std::uint32_t>::max();
        }
        R::EditorProcessingContext context{.Scene = &scene};
        const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.SlotCount, 4);
        EXPECT_EQ(result.ProcessedFaces, 1);
        EXPECT_EQ(result.FallbackCount, 1);
    }
}

TEST(NormalEstimation, TopologyMaskMetadataAgreesWithApply)
{
    for (const auto domain : {D::MeshVertex, D::GraphNode})
        for (const auto method : {R::NormalEstimationMethod::MeshFaceWeighted,
                                 R::NormalEstimationMethod::MeshFaceNormals,
                                 R::NormalEstimationMethod::GraphNeighborhood})
        {
            if (domain == D::GraphNode && method != R::NormalEstimationMethod::GraphNeighborhood)
                continue;
            for (const bool faceMask : {false, true})
            {
                if (faceMask && method == R::NormalEstimationMethod::GraphNeighborhood)
                    continue;
                for (unsigned state = 0; state < 5; ++state)
                {
                    if (state == 4 && (faceMask || method == R::NormalEstimationMethod::GraphNeighborhood)) continue;
                    SCOPED_TRACE(std::to_string(unsigned(domain)) + ":" + R::ToString(method) +
                                 ":" + std::to_string(faceMask) + ":" + std::to_string(state));
                    Extrinsic::ECS::Scene::Registry scene;
                    const auto entity = Make(scene, domain);
                    auto config = Config(entity, domain);
                    config.Method = method;
                    if (method == R::NormalEstimationMethod::MeshFaceNormals)
                        config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
                    const auto maskDomain = faceMask ? D::MeshFace :
                        (domain == D::MeshVertex ? D::MeshEdge : D::GraphEdge);
                    auto& props = PointDomainProperties(scene, entity, maskDomain);
                    const char* name = faceMask ? "f:deleted" : "e:deleted";
                    if (auto mask = props.Get<bool>(name)) props.Remove(mask);
                    if (state == 1) (void)props.GetOrAdd<bool>(name);
                    if (state == 2) (void)props.GetOrAdd<float>(name);
                    if (state == 3) props.GetOrAdd<bool>(name).Vector().pop_back();
                    if (state == 4)
                    {
                        auto& halves = PointDomainProperties(scene, entity, D::MeshHalfedge);
                        halves.Resize(halves.Size() - 1);
                    }
                    const bool valid = state < 2;
                    R::EditorProcessingContext context{.Scene = &scene};
                    const auto commands = R::BindEditorProcessingCommands(context);
                    const auto readiness = R::PreviewEditorNormalEstimationCommand(commands, config);
                    EXPECT_EQ(readiness.Enabled, valid) << readiness.DisabledReason;
                    const auto result = R::ApplyEditorNormalEstimationCommand(commands, config);
                    EXPECT_EQ(result.Succeeded(), valid) << result.Message;
                    if (!valid)
                    {
                        EXPECT_FALSE(readiness.DisabledReason.empty());
                        EXPECT_EQ(result.Message, readiness.DisabledReason);
                        EXPECT_FALSE(PointDomainProperties(scene, entity, config.Output.Domain).Exists(config.Output.Name));
                    }
                }
            }
        }
}

TEST(NormalEstimation, EmptyFaceTopologyPreservesNoOpAndVertexFallbackSemantics)
{
    for (const auto method : {R::NormalEstimationMethod::MeshFaceNormals,
                             R::NormalEstimationMethod::MeshFaceWeighted})
        for (unsigned empty = 0; empty < 3; ++empty)
            for (const bool existingOutput : {false, true})
            {
                SCOPED_TRACE(std::string(R::ToString(method)) + ":" + std::to_string(empty) +
                             ":" + std::to_string(existingOutput));
                Extrinsic::ECS::Scene::Registry scene;
                const auto entity = Make(scene, D::MeshVertex);
                auto config = Config(entity, D::MeshVertex);
                config.Method = method;
                config.FallbackNormal = {0, 2, 0};
                const bool faceNormals = method == R::NormalEstimationMethod::MeshFaceNormals;
                if (faceNormals) config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
                PointDomainProperties(scene, entity, D::MeshVertex).GetOrAdd<bool>("v:deleted")[3] = true;
                auto& faces = PointDomainProperties(scene, entity, D::MeshFace);
                auto& edges = PointDomainProperties(scene, entity, D::MeshEdge);
                if (empty == 0) std::ranges::fill(faces.GetOrAdd<bool>("f:deleted").Vector(), true);
                if (empty == 1) std::ranges::fill(edges.GetOrAdd<bool>("e:deleted").Vector(), true);
                if (empty == 2) faces.Resize(0);
                auto& output = PointDomainProperties(scene, entity, config.Output.Domain);
                const glm::vec3 previous{7, 8, 9};
                if (existingOutput) (void)output.GetOrAdd<glm::vec3>(config.Output.Name, previous);
                const auto beforeRevision = existingOutput ? std::as_const(output).Get<glm::vec3>(config.Output.Name).Revision()
                                                          : Geometry::PropertyRevision{};
                R::EditorCommandHistory history;
                R::EditorProcessingContext context{.Scene = &scene, .CommandHistory = &history};
                const auto commands = R::BindEditorProcessingCommands(context);
                ASSERT_TRUE(R::PreviewEditorNormalEstimationCommand(commands, config).Enabled);
                const auto result = R::ApplyEditorNormalEstimationCommand(commands, config);
                ASSERT_TRUE(result.Succeeded()) << result.Message;
                EXPECT_EQ(result.ProcessedFaces, 0u);
                EXPECT_EQ(result.WrittenCount, faceNormals ? 0u : 3u);
                EXPECT_EQ(result.LiveCount, faceNormals ? 0u : 3u);
                EXPECT_EQ(result.FallbackCount, faceNormals ? 0u : 3u);
                EXPECT_EQ(result.Status, faceNormals ? R::EditorCommandStatus::NoChange : R::EditorCommandStatus::Applied);
                EXPECT_EQ(history.CanUndo(), !faceNormals);
                EXPECT_EQ(output.Exists(config.Output.Name), existingOutput || !faceNormals);
                if (auto values = std::as_const(output).Get<glm::vec3>(config.Output.Name))
                {
                    if (faceNormals) EXPECT_EQ(values.Revision(), beforeRevision);
                    for (std::size_t row = 0; row < values.Size(); ++row)
                        EXPECT_EQ(values[row], faceNormals ? previous :
                            (row == 3 ? (existingOutput ? previous : glm::vec3(0)) : glm::vec3(0, 1, 0)));
                }
            }
}

TEST(NormalEstimation, GraphNeighborhoodPreservesEdgeDeletionRows)
{
    for (const auto domain : {D::MeshVertex, D::GraphNode})
        for (const bool partial : {false, true})
    {
        SCOPED_TRACE(std::to_string(unsigned(domain)) + ":" + std::to_string(partial));
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, domain);
        auto config = Config(entity, domain);
        config.Method = R::NormalEstimationMethod::GraphNeighborhood;
        config.FallbackNormal = {0, 2, 0};
        auto& edges = PointDomainProperties(scene, entity, domain == D::MeshVertex ? D::MeshEdge : D::GraphEdge);
        auto mask = edges.GetOrAdd<bool>("e:deleted");
        const auto v0 = std::as_const(edges).Get<std::uint32_t>("e:v0");
        const auto v1 = std::as_const(edges).Get<std::uint32_t>("e:v1");
        for (std::size_t row = 0; row < mask.Size(); ++row)
            mask[row] = !partial || v0[row] == 0 || v1[row] == 0;
        R::EditorProcessingContext context{.Scene = &scene};
        const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.ValidCount, partial ? 3u : 0u);
        EXPECT_EQ(result.FallbackCount, partial ? 1u : 4u);
        EXPECT_EQ(result.InvalidEdges, 0u);
        const auto output = std::as_const(PointDomainProperties(scene, entity, domain)).Get<glm::vec3>(config.Output.Name);
        for (std::size_t row = 0; row < output.Size(); ++row)
            if (!partial || row == 0) EXPECT_EQ(output[row], glm::vec3(0, 1, 0));
            else EXPECT_NEAR(std::abs(output[row].z), 1.f, 1e-5);
    }
}

TEST(NormalEstimation, QueuedEmptyFaceNormalsDoNotPublishOrCreateHistory)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::MeshVertex);
    auto config = Config(entity, D::MeshVertex);
    config.Method = R::NormalEstimationMethod::MeshFaceNormals;
    config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
    auto& faces = PointDomainProperties(scene, entity, D::MeshFace);
    std::ranges::fill(faces.GetOrAdd<bool>("f:deleted").Vector(), true);
    Intrinsic::Tests::EditorFeatureTestContext context;
    context.Scene = &scene;
    R::EditorCommandHistory history;
    context.CommandHistory = &history;
    std::optional<R::EditorNormalEstimationResult> delivered;
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(context);
    ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config,
        [&](auto result) { delivered = std::move(result); }).Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::NoChange) << delivered->Message;
    EXPECT_EQ(delivered->LiveCount, 0u);
    EXPECT_EQ(delivered->WrittenCount, 0u);
    EXPECT_FALSE(faces.Exists(config.Output.Name));
    EXPECT_FALSE(history.CanUndo());
}

TEST(NormalEstimation, RejectsInvalidBindingsAndUnavailableBackendsWithoutMutation)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::PointCloudPoint);
    R::EditorProcessingContext context{.Scene = &scene};
    const auto base = Config(entity, D::PointCloudPoint);
    auto &props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    for (unsigned invalid = 0; invalid < 7; ++invalid)
    {
        auto config = base;
        switch (invalid)
        {
        case 0:
            config.Output.Domain = D::MeshFace;
            break;
        case 1:
            config.Output.Name = "samples";
            break;
        case 2:
            config.Output.Name = "keep";
            break;
        case 3:
            config.Backend = R::NormalEstimationBackend::CpuLBVH;
            break;
        case 4:
            config.Method = R::NormalEstimationMethod::GraphNeighborhood;
            break;
        case 5:
            config.Output.Name = "v:deleted";
            break;
        case 6:
            config.Positions.Name = "missing";
            break;
        }
        EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
        const auto rejected = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
        EXPECT_FALSE(rejected.Succeeded());
        EXPECT_EQ(rejected.Method, config.Method);
        EXPECT_EQ(rejected.RequestedBackend, config.Backend);
        EXPECT_TRUE(rejected.ActualBackend.empty());
        EXPECT_FALSE(props.Exists("estimated"));
    }
    props.Get<glm::vec3>("samples")[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), base).Enabled);
    EXPECT_FALSE(props.Exists("estimated"));
}

TEST(NormalEstimation, RadiusNeighborhoodAndDegenerateFallbackMatchAcrossBackends)
{
    R::WorldRegistry worlds;
    auto world = worlds.CreateWorld("normals");
    auto &scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    auto entity = Make(scene, D::PointCloudPoint);
    R::EditorProcessingContext context{.Scene = &scene, .World = world, .SpatialIndices = &cache};
    auto config = Config(entity, D::PointCloudPoint);
    config.UseRadiusSearch = true;
    config.Radius = 10;
    auto reference = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(reference.Succeeded());
    config.Backend = R::NormalEstimationBackend::CpuLBVH;
    auto accelerated = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(accelerated.Succeeded());
    EXPECT_EQ(accelerated.ChangedCount, 0);
    EXPECT_EQ(accelerated.ValidCount, reference.ValidCount);
    config.Radius = .01f;
    config.FallbackNormal = {1, 0, 0};
    auto fallback = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(fallback.Succeeded());
    EXPECT_EQ(fallback.FallbackCount, 4);
    EXPECT_EQ(fallback.ValidCount, 0);
}

TEST(NormalEstimation, QueuedJobsGuardInputsTopologyDeletionAndOutputButAllowUnrelatedEdits)
{
    for (unsigned change = 0; change < 6; ++change)
    {
        SCOPED_TRACE(change);
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D::MeshVertex);
        auto config = Config(entity, D::MeshVertex);
        config.Method = R::NormalEstimationMethod::GraphNeighborhood;
        auto &props = PointDomainProperties(scene, entity, D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        R::EditorCommandHistory history;
        context.CommandHistory = &history;
        std::optional<R::EditorNormalEstimationResult> delivered;
        std::function<void(R::EditorNormalEstimationResult)> onComplete = [&](auto r) { delivered = std::move(r); };
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config, onComplete).Status,
                  R::EditorCommandStatus::Pending);
        EXPECT_EQ(R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config, onComplete).Status,
                  R::EditorCommandStatus::Pending);
        EXPECT_EQ(jobs.Snapshot().Entries.size(), 1);
        switch (change)
        {
        case 0:
            props.Get<float>("keep")[0] = 99.f;
            break;
        case 1:
            props.Get<glm::vec3>("samples")[0] = plane[0];
            break;
        case 2:
            props.GetOrAdd<bool>("v:deleted")[0] = true;
            break;
        case 3:
            props.GetOrAdd<glm::vec3>("estimated")[0] = {1, 0, 0};
            break;
        case 4:
            scene.Raw().get<GS::Edges>(entity).Properties.Get<std::uint32_t>("e:v0")[0] = 0;
            break;
        case 5:
            (void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);
            break;
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_TRUE(delivered);
        if (change == 0)
        {
            EXPECT_TRUE(delivered->Succeeded()) << delivered->Message;
            EXPECT_TRUE(history.CanUndo());
            EXPECT_TRUE(props.Exists("estimated"));
        }
        else
        {
            EXPECT_FALSE(delivered->Succeeded());
            EXPECT_FALSE(history.CanUndo());
            EXPECT_EQ(props.Exists("estimated"), change == 3);
        }
    }
}

TEST(NormalEstimationConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::MeshFace);
    auto config = Config(entity, D::MeshFace);
    config.UseRadiusSearch = true;
    config.Radius = 5;
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeNormalEstimationConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;
    C::PopulateEngineConfigSectionDefaults(state.ActiveConfig, registry);
    R::EditorProcessingContext context{.Scene = &scene};
    context.EngineConfigControlState = &state;
    context.EngineConfigCommandsAvailable = true;
    unsigned previews = 0, applies = 0;
    context.PreviewEngineConfigDocument = [&](const auto &document, const auto &origin) {
        ++previews;
        return C::PreviewEngineConfig(document, state.ActiveConfig, {origin, &registry});
    };
    context.ApplyEngineConfigHotSubset = [&](const auto &preview) {
        ++applies;
        state.ActiveConfig = preview.Preview.Config;
        return R::RuntimeEngineConfigApplyResult{.Status = R::RuntimeEngineConfigApplyStatus::Applied};
    };
    // Preview and apply share the config-lane predicate, including expired handles.
    const auto unavailable = R::ResolveEditorProcessingActionReadiness({}, {false, "method blocked"});
    ASSERT_FALSE(unavailable.Enabled);
    ASSERT_FALSE(unavailable.DisabledReason.empty());
    for (unsigned missing = 0; missing < 5; ++missing)
    {
        auto incomplete = context;
        if (missing == 0) incomplete.EngineConfigControlState = nullptr;
        if (missing == 1) incomplete.EngineConfigCommandsAvailable = false;
        if (missing == 2) incomplete.PreviewEngineConfigDocument = {};
        if (missing == 3) incomplete.ApplyEngineConfigHotSubset = {};
        if (missing == 4) incomplete.AttachmentActive = [] { return false; };
        const auto handle = R::BindEditorProcessingCommands(incomplete);
        const auto action = R::ResolveEditorProcessingActionReadiness(handle, {true, {}});
        EXPECT_FALSE(action.Enabled) << missing;
        EXPECT_EQ(action.DisabledReason, unavailable.DisabledReason) << missing;
        EXPECT_EQ(R::ResolveEditorProcessingActionReadiness(handle, {false, "method blocked"}).DisabledReason,
                  unavailable.DisabledReason) << missing;
        EXPECT_FALSE(R::ApplyEditorNormalEstimationConfig(handle, config).Succeeded()) << missing;
    }
    EXPECT_EQ(previews, 0u);
    EXPECT_EQ(applies, 0u);
    auto commands = R::BindEditorProcessingCommands(context);
    const auto ready = R::ResolveEditorProcessingActionReadiness(commands, {true, "obsolete reason"});
    EXPECT_TRUE(ready.Enabled);
    EXPECT_TRUE(ready.DisabledReason.empty());
    const auto blocked = R::ResolveEditorProcessingActionReadiness(commands, {false, "Choose an input property."});
    EXPECT_FALSE(blocked.Enabled);
    EXPECT_EQ(blocked.DisabledReason, "Choose an input property.");
    EXPECT_FALSE(R::ResolveEditorProcessingActionReadiness(commands, {}).DisabledReason.empty());
    EXPECT_EQ(previews, 0u);
    EXPECT_EQ(applies, 0u);
    const auto method = R::PreviewEditorNormalEstimationCommand(commands, config);
    ASSERT_TRUE(method.Enabled);
    EXPECT_TRUE(method.DisabledReason.empty());
    EXPECT_TRUE(R::ResolveEditorProcessingActionReadiness(commands, method).Enabled);
    auto invalidConfig = config;
    invalidConfig.KNeighbors = 0;
    const auto invalidMethod = R::PreviewEditorNormalEstimationCommand(commands, invalidConfig);
    ASSERT_FALSE(invalidMethod.Enabled);
    ASSERT_FALSE(invalidMethod.DisabledReason.empty());
    EXPECT_EQ(R::ResolveEditorProcessingActionReadiness(commands, invalidMethod).DisabledReason,
              invalidMethod.DisabledReason);
    EXPECT_EQ(R::ApplyEditorNormalEstimationCommand(commands, invalidConfig).Message,
              invalidMethod.DisabledReason);
    EXPECT_EQ(R::ResolveEditorProcessingActionReadiness({}, invalidMethod).DisabledReason,
              unavailable.DisabledReason);
    EXPECT_FALSE(PointDomainProperties(scene, entity, D::MeshFace).Exists("estimated"));
    ASSERT_TRUE(R::ApplyEditorNormalEstimationConfig(commands, config).Succeeded());
    ASSERT_TRUE(R::GetEditorNormalEstimationConfig(commands));
    EXPECT_EQ(R::SerializeNormalEstimationConfig(*R::GetEditorNormalEstimationConfig(commands)),
              R::SerializeNormalEstimationConfig(config));
    ASSERT_TRUE(R::ApplyEditorConfiguredNormalEstimation(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    auto fallback = context;
    fallback.PreviewEngineConfigDocument = [&](const auto&, const auto&) {
        C::EngineConfigLoadResult result;
        result.State = C::EngineConfigState::FallbackApplied;
        result.Preview.Config = state.ActiveConfig;
        return result;
    };
    auto changedConfig = config;
    changedConfig.KNeighbors = config.KNeighbors + 1;
    const auto rejectedFallback = R::ApplyEditorNormalEstimationConfig(
        R::BindEditorProcessingCommands(fallback), changedConfig);
    EXPECT_FALSE(rejectedFallback.Succeeded());
    EXPECT_EQ(rejectedFallback.LoadResult.State, C::EngineConfigState::FallbackApplied);
    EXPECT_EQ(applies, 1);
    EXPECT_EQ(R::GetNormalEstimationConfig(state.ActiveConfig)->KNeighbors, config.KNeighbors);
    fallback.PreviewEngineConfigDocument = [&](const auto& document, const auto& origin) {
        auto result = C::PreviewEngineConfig(document, state.ActiveConfig, {origin, &registry});
        result.State = C::EngineConfigState::FallbackApplied;
        result.Diagnostics.push_back({.State = C::EngineConfigState::FallbackApplied,
            .Severity = C::EngineConfigDiagnosticSeverity::Warning,
            .Subject = "unrelated.section", .Message = "Unrelated fallback retained its reference"});
        return result;
    };
    EXPECT_TRUE(R::ApplyEditorNormalEstimationConfig(
        R::BindEditorProcessingCommands(fallback), changedConfig).Succeeded());
    EXPECT_EQ(applies, 2);
    EXPECT_EQ(R::GetNormalEstimationConfig(state.ActiveConfig)->KNeighbors, changedConfig.KNeighbors);
    for (auto payload : {R"({"method":"automatic"})", R"({"backend":"vulkan"})", R"({"k_neighbors":0})",
                         R"({"minimum_neighbors":-1})", R"({"orientation":2})", R"({"weighting":5})",
                         R"({"use_radius":true,"radius":0})", R"({"radius":1e100})", R"({"unknown":1})",
                         R"({"fallback_normal":[1,2]})"})
        EXPECT_FALSE(R::ValidateNormalEstimationConfigSection(payload, {}, "test").Usable()) << payload;
    config.KNeighbors = 0;
    EXPECT_FALSE(R::ApplyEditorNormalEstimationConfig(commands, config).Succeeded());
    bool attached = true;
    context.AttachmentActive = [&] { return attached; };
    const auto expiring = R::BindEditorProcessingCommands(context);
    EXPECT_TRUE(R::ResolveEditorProcessingActionReadiness(expiring, {true, {}}).Enabled);
    attached = false;
    EXPECT_EQ(R::ResolveEditorProcessingActionReadiness(expiring, {true, {}}).DisabledReason,
              unavailable.DisabledReason);
    config.KNeighbors = 12;
    EXPECT_FALSE(R::ApplyEditorNormalEstimationConfig(expiring, config).Succeeded());
    EXPECT_EQ(applies, 2);
}

TEST(NormalEstimation, NamedOutputUsesSharedVectorVisualizationRecipe)
{
    for (unsigned d = 1; d <= 8; ++d)
    {
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D(d));
        auto config = Config(entity, D(d));
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        context.VisualizationCommandsAvailable = true;
        std::optional<R::VisualizationRecipe> stored;
        context.VisualizationRecipes.GetRecipe = [&](std::uint32_t) { return stored; };
        context.VisualizationRecipes.SetRecipe = [&](std::uint32_t, R::VisualizationRecipe r) {
            stored = std::move(r);
        };
        context.VisualizationRecipes.ClearRecipe = [&](std::uint32_t) { stored.reset(); };
        ASSERT_TRUE(R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
        EXPECT_EQ(
            R::ApplyEditorVisualizationRecipeCommand(
                context,
                {.StableEntityId = config.StableEntityId,
                 .Recipe = {.Data = R::VectorFieldVisualizationRecipe{.Source = config.Output,
                                                                      .PositionSource = config.Positions,
                                                                      .OutputName = "normal_vectors"}}}),
            R::EditorCommandStatus::Applied);
        ASSERT_TRUE(stored);
        const auto *recipe = std::get_if<R::VectorFieldVisualizationRecipe>(&stored->Data);
        ASSERT_NE(recipe, nullptr);
        EXPECT_EQ(recipe->Source, config.Output);
        EXPECT_EQ(recipe->PositionSource, config.Positions);
    }
}

TEST(NormalEstimation, TopologyCatalogRetainsSmallGraphsAndReportsPcaMinimum)
{
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::GraphNode);
    auto &vertices = PointDomainProperties(scene, entity, D::GraphNode);
    vertices.Resize(2);
    R::EditorProcessingContext context{.Scene = &scene};
    auto config = Config(entity, D::GraphNode);
    const auto catalog = R::GetEditorPointInputCatalog(R::BindEditorProcessingCommands(context), config.StableEntityId);
    EXPECT_TRUE(
        std::ranges::any_of(catalog.Entries, [&](const auto &e) { return e.Ref == config.Positions; }));
    EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
    config.Method = R::NormalEstimationMethod::GraphNeighborhood;
    EXPECT_TRUE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
    const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    EXPECT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_GT(result.InvalidEdges, 0);
}

TEST(NormalEstimation, GraphPositionSlotMayBindAnExistingNormalNamedProperty)
{
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::GraphNode);
    auto &vertices = PointDomainProperties(scene, entity, D::GraphNode);
    vertices.GetOrAdd<glm::vec3>("v:normal").Vector() =
        std::as_const(vertices).Get<glm::vec3>("samples").Vector();
    const auto revision = std::as_const(vertices).Get<glm::vec3>("v:normal").Revision();
    auto config = Config(entity, D::GraphNode);
    config.Positions.Name = "v:normal";
    config.Method = R::NormalEstimationMethod::GraphNeighborhood;
    R::EditorProcessingContext context{.Scene = &scene};
    const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.ValidCount, 4);
    EXPECT_EQ(std::as_const(vertices).Get<glm::vec3>("v:normal").Revision(), revision);
    EXPECT_EQ(std::as_const(vertices).Get<glm::vec3>("v:normal")[1], plane[1]);
}


TEST(NormalEstimation, FaceNormalsUseFullPolygonRingAndPublishOnlyFaceOutput)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    // First three corners are collinear; the full pentagon still has a +Z normal.
    std::vector<Geometry::VertexHandle> ring;
    for (const glm::vec3 p : {glm::vec3{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}})
        ring.push_back(mesh.AddVertex(p));
    ASSERT_TRUE(mesh.AddFace(ring));
    const auto a = mesh.AddVertex({3, 0, 0}), b = mesh.AddVertex({3, 0, 1}),
               c = mesh.AddVertex({3, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c)); // -X from winding.
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto &vertices = PointDomainProperties(scene, entity, D::MeshVertex);
    auto &faces = PointDomainProperties(scene, entity, D::MeshFace);
    (void)vertices.GetOrAdd<glm::vec3>("v:normal", {0, 1, 0});
    (void)faces.GetOrAdd<float>("keep", 42.f);
    const auto positions = std::as_const(vertices).Get<glm::vec3>("v:position");
    const auto revision = positions.Revision();
    R::NormalEstimationConfig config;
    config.StableEntityId = R::SelectionController::ToStableEntityId(entity);
    config.Method = R::NormalEstimationMethod::MeshFaceNormals;
    config.Positions.Domain = D::MeshVertex;
    config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
    const auto json = R::SerializeNormalEstimationConfig(config);
    EXPECT_TRUE(R::ValidateNormalEstimationConfigSection(json, {}, {}).Usable());
    Extrinsic::Core::Config::EngineConfig engineConfig;
    R::SetNormalEstimationConfig(engineConfig, config);
    ASSERT_TRUE(R::GetNormalEstimationConfig(engineConfig));
    EXPECT_EQ(R::GetNormalEstimationConfig(engineConfig)->Method, config.Method);
    R::EditorCommandHistory history;
    R::EditorProcessingContext context{.Scene = &scene, .CommandHistory = &history};
    ASSERT_TRUE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
    const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.SlotCount, 2u);
    EXPECT_EQ(result.ValidCount, 2u);
    EXPECT_EQ(result.ProcessedFaces, 2u);
    EXPECT_EQ(result.ActualBackend, "cpu_mesh_face_normals");
    const auto normals = std::as_const(faces).Get<glm::vec3>("f:normal");
    ASSERT_EQ(normals.Size(), 2u);
    EXPECT_EQ(normals[0], (glm::vec3{0, 0, 1}));
    EXPECT_EQ(normals[1], (glm::vec3{-1, 0, 0}));
    EXPECT_EQ(positions.Revision(), revision);
    EXPECT_EQ(std::as_const(vertices).Get<glm::vec3>("v:normal")[0], (glm::vec3{0, 1, 0}));
    EXPECT_EQ(std::as_const(faces).Get<float>("keep")[1], 42.f);
    EXPECT_EQ(history.Undo().Status, R::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(faces.Exists("f:normal"));
    EXPECT_EQ(history.Redo().Status, R::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[1], (glm::vec3{-1, 0, 0}));
}

TEST(NormalEstimation, FaceNormalsPreserveDeletedSlotsAndReportDegenerateFallback)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::MeshVertex);
    auto &faces = PointDomainProperties(scene, entity, D::MeshFace);
    faces.GetOrAdd<bool>("f:deleted")[0] = true;
    faces.Get<std::uint32_t>("f:halfedge")[0] = std::numeric_limits<std::uint32_t>::max();
    (void)faces.GetOrAdd<glm::vec3>("f:normal", {0, -1, 0});
    auto config = Config(entity, D::MeshVertex);
    config.Method = R::NormalEstimationMethod::MeshFaceNormals;
    config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
    R::EditorProcessingContext context{.Scene = &scene};
    auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.WrittenCount, 1u);
    EXPECT_EQ(result.SlotCount, 2u);
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[0], (glm::vec3{0, -1, 0}));
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[1], (glm::vec3{0, 0, 1}));
    PointDomainProperties(scene, entity, D::MeshVertex).Get<glm::vec3>("samples").Vector() =
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    config.FallbackNormal = {0, 2, 0};
    result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.FallbackCount, 1u);
    EXPECT_EQ(result.ValidCount, 0u);
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[1], (glm::vec3{0, 1, 0}));
    config.Output.Domain = D::MeshVertex;
    EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
}


TEST(NormalEstimation, QueuedFaceNormalsPublishToFacesAndRejectStaleTopology)
{
    for (const bool stale : {false, true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D::MeshVertex);
        auto config = Config(entity, D::MeshVertex);
        config.Method = R::NormalEstimationMethod::MeshFaceNormals;
        config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        std::optional<R::EditorNormalEstimationResult> delivered;
        std::function<void(R::EditorNormalEstimationResult)> onComplete = [&](auto result) { delivered = std::move(result); };
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config, onComplete).Status, R::EditorCommandStatus::Pending);
        auto &faces = PointDomainProperties(scene, entity, D::MeshFace);
        if (stale)
            faces.GetOrAdd<bool>("f:deleted")[0] = true;
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->Succeeded(), !stale) << delivered->Message;
        EXPECT_EQ(faces.Exists("f:normal"), !stale);
        EXPECT_FALSE(PointDomainProperties(scene, entity, D::MeshVertex).Exists("f:normal"));
        if (!stale)
            EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal").Vector(),
                      (std::vector<glm::vec3>{{0, 0, 1}, {0, 0, 1}}));
    }
}

TEST(NormalEstimation, QueuedTopologyNormalsGuardDeletionMaskTransitions)
{
    for (const auto method : {R::NormalEstimationMethod::MeshFaceNormals,
                             R::NormalEstimationMethod::MeshFaceWeighted,
                             R::NormalEstimationMethod::GraphNeighborhood})
        for (const bool faceMask : {false, true})
        {
            if (faceMask && method == R::NormalEstimationMethod::GraphNeighborhood) continue;
            for (unsigned change = 0; change < 5; ++change)
            {
                SCOPED_TRACE(std::string(R::ToString(method)) + ":" + std::to_string(faceMask) +
                             ":" + std::to_string(change));
                Extrinsic::ECS::Scene::Registry scene;
                const auto entity = Make(scene, D::MeshVertex);
                auto config = Config(entity, D::MeshVertex);
                config.Method = method;
                if (method == R::NormalEstimationMethod::MeshFaceNormals)
                    config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
                auto& props = PointDomainProperties(scene, entity, faceMask ? D::MeshFace : D::MeshEdge);
                const char* name = faceMask ? "f:deleted" : "e:deleted";
                auto mask = props.GetOrAdd<bool>(name);
                if (change == 0) props.Remove(mask);
                Intrinsic::Tests::EditorFeatureTestContext context;
                context.Scene = &scene;
                R::EditorCommandHistory history;
                context.CommandHistory = &history;
                std::optional<R::EditorNormalEstimationResult> delivered;
                Extrinsic::Tests::EditorJobHarness jobs;
                jobs.Attach(context);
                ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config,
                    [&](auto result) { delivered = std::move(result); }).Status, R::EditorCommandStatus::Pending);
                if (change == 0) (void)props.GetOrAdd<bool>(name);
                if (change == 1) mask[0] = true;
                if (change == 2 || change == 3) props.Remove(mask);
                if (change == 3) (void)props.GetOrAdd<float>(name);
                if (change == 4) mask.Vector().pop_back();
                ASSERT_TRUE(jobs.DrainUntilTerminal());
                ASSERT_TRUE(delivered);
                EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity) << delivered->Message;
                EXPECT_FALSE(PointDomainProperties(scene, entity, config.Output.Domain).Exists(config.Output.Name));
                EXPECT_FALSE(history.CanUndo());
            }
        }
}

TEST(NormalEstimationConfig, VulkanSelectionRoundTripsAndRejectsInvalidBatchSizes)
{
    R::NormalEstimationConfig config;
    config.Backend = R::NormalEstimationBackend::VulkanLBVH;
    config.GpuQueryBatchSize = 256;
    const auto payload = R::SerializeNormalEstimationConfig(config);
    EXPECT_NE(payload.find("vulkan_lbvh"), std::string::npos);
    EXPECT_TRUE(R::ValidateNormalEstimationConfigSection(payload, {}, "test").Usable());
    Extrinsic::Core::Config::EngineConfig engine;
    R::SetNormalEstimationConfig(engine, config);
    const auto restored = R::GetNormalEstimationConfig(engine);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->Backend, R::NormalEstimationBackend::VulkanLBVH);
    EXPECT_EQ(restored->GpuQueryBatchSize, 256);
    for (auto invalid : {0u, 16385u})
    {
        config.GpuQueryBatchSize = invalid;
        EXPECT_FALSE(R::ValidateNormalEstimationConfigSection(R::SerializeNormalEstimationConfig(config), {}, "test").Usable());
    }
}
TEST(NormalEstimation, VulkanUnavailablePreservesOutputsOnEveryDomain)
{
    for (unsigned d = 1; d <= 8; ++d)
    {
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D(d));
        auto config = Config(entity, D(d));
        config.Backend = R::NormalEstimationBackend::VulkanLBVH;
        R::EditorProcessingContext context{.Scene=&scene};
        EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config).Enabled);
        const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(context), config);
        EXPECT_EQ(result.RequestedBackend, R::NormalEstimationBackend::VulkanLBVH);
        EXPECT_FALSE(result.Succeeded());
        EXPECT_TRUE(result.ActualBackend.empty());
        EXPECT_FALSE(PointDomainProperties(scene, entity, D(d)).Exists(config.Output.Name));
    }
}

TEST(NormalEstimation, CanonicalVariantsRetainWeightingPublicationAndUndoWithoutNoChangeHistory)
{
    using Weight = Geometry::HalfedgeMesh::VertexNormals::AveragingMode;
    for (auto domain : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
        for (auto weighting : {Weight::UniformFace, Weight::AreaWeighted, Weight::AngleWeighted,
                               Weight::AreaAngleWeighted, Weight::MaxWeighted})
        {
            Extrinsic::ECS::Scene::Registry scene;
            const auto entity = Make(scene, domain);
            auto config = Config(entity, domain);
            config.Method = domain == D::MeshVertex ? R::NormalEstimationMethod::MeshFaceWeighted
                : domain == D::GraphNode ? R::NormalEstimationMethod::GraphNeighborhood
                                        : R::NormalEstimationMethod::PointSetPCA;
            config.Weighting = weighting;
            config.Output.Name = "v:normal";
            R::EditorCommandHistory history;
            const auto commands = R::BindEditorProcessingCommands({.Scene = &scene, .CommandHistory = &history});
            unsigned callbacks = 0;
            const auto result = R::ApplyEditorNormalEstimationCommand(commands, config, [&](auto) { ++callbacks; });
            ASSERT_EQ(result.Status, R::EditorCommandStatus::Applied) << result.Message;
            EXPECT_EQ(callbacks, 0u);
            EXPECT_EQ(result.ValidCount, 4u);
            EXPECT_EQ(result.ChangedCount, 4u);
            auto& props = PointDomainProperties(scene, entity, domain);
            const auto values = std::as_const(props).Get<glm::vec3>("v:normal").Vector();
            for (std::size_t i = 0; i < 4; ++i)
            {
                EXPECT_NEAR(glm::length(values[i]), 1.0f, 1e-5f);
                EXPECT_GT(values[i].z, 0.9f);
            }
            EXPECT_TRUE(scene.Raw().all_of<Extrinsic::ECS::Components::DirtyTags::DirtyVertexNormals>(entity));
            EXPECT_EQ(R::ApplyEditorNormalEstimationCommand(commands, config).Status, R::EditorCommandStatus::NoChange);
            ASSERT_TRUE(history.Undo().Succeeded());
            EXPECT_FALSE(history.CanUndo());
            EXPECT_FALSE(props.Exists("v:normal"));
            ASSERT_TRUE(history.Redo().Succeeded());
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("v:normal").Vector(), values);
        }
}

TEST(NormalEstimation, EveryVariantQueuesOnceAndReportsNoChangeWithoutAdditionalHistory)
{
    for (auto domain : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, domain);
        auto config = Config(entity, domain);
        config.Method = domain == D::MeshVertex ? R::NormalEstimationMethod::MeshFaceWeighted
            : domain == D::GraphNode ? R::NormalEstimationMethod::GraphNeighborhood
                                    : R::NormalEstimationMethod::PointSetPCA;
        R::EditorCommandHistory history;
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        context.CommandHistory = &history;
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        const auto commands = R::BindEditorProcessingCommands(context);
        unsigned firstCallbacks = 0, duplicateCallbacks = 0;
        std::optional<R::EditorNormalEstimationResult> completed;
        auto finish = [&](auto result) { ++firstCallbacks; completed = std::move(result); };
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(commands, config, finish).Status, R::EditorCommandStatus::Pending);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(commands, config, [&](auto) { ++duplicateCallbacks; }).Status,
                  R::EditorCommandStatus::Pending);
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_TRUE(completed);
        EXPECT_EQ(completed->Status, R::EditorCommandStatus::Applied);
        EXPECT_EQ(firstCallbacks, 1u);
        EXPECT_EQ(duplicateCallbacks, 0u);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(commands, config, finish).Status, R::EditorCommandStatus::Pending);
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        EXPECT_EQ(completed->Status, R::EditorCommandStatus::NoChange);
        EXPECT_EQ(firstCallbacks, 2u);
        ASSERT_TRUE(history.Undo().Succeeded());
        EXPECT_FALSE(history.CanUndo());
        EXPECT_FALSE(PointDomainProperties(scene, entity, domain).Exists(config.Output.Name));
    }
}

TEST(NormalEstimation, EveryVariantRejectsDestroyedTargetsBeforeQueuedPublication)
{
    for (auto domain : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, domain);
        auto config = Config(entity, domain);
        config.Method = domain == D::MeshVertex ? R::NormalEstimationMethod::MeshFaceWeighted
            : domain == D::GraphNode ? R::NormalEstimationMethod::GraphNeighborhood
                                    : R::NormalEstimationMethod::PointSetPCA;
        R::EditorCommandHistory history;
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        context.CommandHistory = &history;
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        std::optional<R::EditorNormalEstimationResult> completed;
        const auto commands = R::BindEditorProcessingCommands(context);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(commands, config, [&](auto r) { completed = std::move(r); }).Status,
                  R::EditorCommandStatus::Pending);
        scene.Raw().destroy(entity);
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_TRUE(completed);
        EXPECT_EQ(completed->Status, R::EditorCommandStatus::StaleEntity);
        EXPECT_FALSE(history.CanUndo());
    }
}

TEST(NormalEstimation, ExpiredAttachmentGuardsFreedSceneHistoryAndCommands)
{
    auto scene = std::make_unique<Extrinsic::ECS::Scene::Registry>();
    const auto entity = Make(*scene, D::MeshVertex);
    const auto config = Config(entity, D::MeshVertex);
    bool active = true;
    R::EditorCommandHistory history;
    const auto commands = R::BindEditorProcessingCommands({.Scene = scene.get(), .CommandHistory = &history,
                                                           .AttachmentActive = [&] { return active; }});
    ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(commands, config).Status, R::EditorCommandStatus::Applied);
    active = false;
    scene.reset();
    EXPECT_EQ(history.Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(commands, config).Enabled);
    EXPECT_TRUE(R::GetEditorPointInputCatalog(commands, config.StableEntityId).Entries.empty());
    EXPECT_FALSE(R::ApplyEditorNormalEstimationCommand(commands, config).Succeeded());
    EXPECT_FALSE(R::GetEditorNormalEstimationConfig(commands));
}

TEST(NormalEstimation, ExpiredAttachmentSuppressesQueuedCallbacksAfterSceneDestruction)
{
    for (auto domain : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
    {
        auto scene = std::make_unique<Extrinsic::ECS::Scene::Registry>();
        const auto entity = Make(*scene, domain);
        auto config = Config(entity, domain);
        config.Method = domain == D::MeshVertex ? R::NormalEstimationMethod::MeshFaceWeighted
            : domain == D::GraphNode ? R::NormalEstimationMethod::GraphNeighborhood
                                    : R::NormalEstimationMethod::PointSetPCA;
        bool active = true;
        unsigned callbacks = 0;
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = scene.get();
        context.AttachmentActive = [&] { return active; };
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        const auto commands = R::BindEditorProcessingCommands(context);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(commands, config, [&](auto) { ++callbacks; }).Status,
                  R::EditorCommandStatus::Pending);
        active = false;
        scene.reset();
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        EXPECT_EQ(callbacks, 0u);
    }
}
