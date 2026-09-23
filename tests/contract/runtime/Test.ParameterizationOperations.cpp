#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"

#include "EditorFeatureTestContext.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SelectionController;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace Config = Extrinsic::Core::Config;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            engine.RequestExit();
        }
        void Shutdown() override {}
    };

    class ConfigControlHarness final
    {
    public:
        explicit ConfigControlHarness(
            Runtime::RuntimeEngineConfigSectionRegistry sectionRegistry)
        {
            Config::EngineConfig config =
                Runtime::CreateReferenceEngineConfig(sectionRegistry);
            config.Simulation.WorkerThreadCount = 1u;
            config.ReferenceScene.Enabled = false;
            config.Camera.Enabled = false;
            config.Window.Backend = Config::WindowBackend::Null;
            config.Render.EnablePromotedVulkanDevice = false;
            config.Render.DefaultRecipeConfigPath.clear();
            m_Engine = std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(
                std::move(config), std::make_unique<OneFrameApplication>());
            m_Engine->EmplaceModule<Runtime::EngineConfigControl>(
                std::move(sectionRegistry));
            m_Engine->Initialize();
            m_Control =
                m_Engine->Services().Find<Runtime::EngineConfigControl>();
            EXPECT_NE(m_Control, nullptr);
        }

        ~ConfigControlHarness()
        {
            if (m_Engine)
            {
                m_Engine->Shutdown();
            }
        }

        [[nodiscard]] Runtime::EngineConfigControl& Control() const
        {
            return *m_Control;
        }

        [[nodiscard]] const Config::EngineConfig& ActiveConfig() const
        {
            return m_Engine->GetEngineConfig();
        }

    private:
        std::unique_ptr<Intrinsic::Tests::RuntimeTestKernel> m_Engine{};
        Runtime::EngineConfigControl* m_Control{};
    };

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeGridMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh{};
        std::array<Geometry::VertexHandle, 9u> vertices{};
        for (std::uint32_t y = 0u; y < 3u; ++y)
        {
            for (std::uint32_t x = 0u; x < 3u; ++x)
            {
                vertices[y * 3u + x] = mesh.AddVertex(glm::vec3{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    0.1f * static_cast<float>(x * y),
                });
            }
        }
        const auto at = [&vertices](const std::uint32_t x,
                                    const std::uint32_t y)
        {
            return vertices[y * 3u + x];
        };
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 2u; ++x)
            {
                EXPECT_TRUE(mesh.AddTriangle(
                    at(x, y), at(x + 1u, y), at(x + 1u, y + 1u)));
                EXPECT_TRUE(mesh.AddTriangle(
                    at(x, y), at(x + 1u, y + 1u), at(x, y + 1u)));
            }
        }
        return mesh;
    }

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeQuadMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh{};
        const Geometry::VertexHandle v0 =
            mesh.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
        const Geometry::VertexHandle v1 =
            mesh.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
        const Geometry::VertexHandle v2 =
            mesh.AddVertex(glm::vec3{1.0f, 1.0f, 0.0f});
        const Geometry::VertexHandle v3 =
            mesh.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
        EXPECT_TRUE(mesh.AddQuad(v0, v1, v2, v3));
        return mesh;
    }

    // Closed surface: reaches the solver and is rejected on topology, which is
    // the case BUG-141 saw reported without a cause.
    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeClosedTetrahedronMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh{};
        const Geometry::VertexHandle v0 =
            mesh.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
        const Geometry::VertexHandle v1 =
            mesh.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
        const Geometry::VertexHandle v2 =
            mesh.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
        const Geometry::VertexHandle v3 =
            mesh.AddVertex(glm::vec3{0.0f, 0.0f, 1.0f});
        (void)mesh.AddTriangle(v0, v2, v1);
        (void)mesh.AddTriangle(v0, v1, v3);
        (void)mesh.AddTriangle(v0, v3, v2);
        (void)mesh.AddTriangle(v1, v2, v3);
        return mesh;
    }

    struct ParameterizationHarness
    {
        ECS::Scene::Registry Scene{};
        Runtime::SelectionController Selection{};
        Runtime::EditorCommandHistory History{};
        Runtime::EditorSelectedModelCache ModelCache{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        Intrinsic::Tests::EditorFeatureTestContext Context{};
        Runtime::EditorParameterizationResultsSnapshot Results{};
        Runtime::EditorParameterizationUvViewCommandSurface UvViewCommands{};

        explicit ParameterizationHarness(
            Geometry::HalfedgeMesh::Mesh mesh = MakeGridMesh())
        {
            Entity = Scene.Create();
            GS::PopulateFromMesh(Scene.Raw(), Entity, mesh);
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

        [[nodiscard]] GS::Halfedges& Halfedges()
        {
            return Scene.Raw().get<GS::Halfedges>(Entity);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec2>> CornerUvs() const
        {
            const GS::Halfedges& halfedges =
                Scene.Raw().get<GS::Halfedges>(Entity);
            const auto uvs =
                halfedges.Properties.Get<glm::vec2>("h:texcoord");
            if (!uvs)
                return std::nullopt;
            return uvs.Vector();
        }

        [[nodiscard]] std::optional<std::vector<glm::vec2>> Uvs() const
        {
            const GS::Vertices& vertices =
                Scene.Raw().get<GS::Vertices>(Entity);
            const auto uvs = vertices.Properties.Get<glm::vec2>(
                "v:texcoord");
            if (!uvs)
                return std::nullopt;
            return uvs.Vector();
        }
    };

    [[nodiscard]] Runtime::ParameterizationConfig MakeConfig(
        const Runtime::ParameterizationStrategyKind strategy)
    {
        Runtime::ParameterizationConfig config{};
        config.Strategy = strategy;
        return config;
    }

    [[nodiscard]] Runtime::EditorParameterizationResult Apply(
        ParameterizationHarness& harness,
        const Runtime::ParameterizationStrategyKind strategy)
    {
        return Runtime::ApplyEditorParameterizationCommand(
            harness.Context,
            Runtime::EditorParameterizationCommand{
                .StableEntityId = harness.StableEntityId,
                .Config = MakeConfig(strategy),
            });
    }

    [[nodiscard]] bool AllFinite(const std::vector<glm::vec2>& uvs)
    {
        for (const glm::vec2 uv : uvs)
        {
            if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                return false;
        }
        return true;
    }
}

TEST(ParameterizationOperations, StableTokensAndAllImplementedStrategiesWriteFiniteUvs)
{
    constexpr std::array strategies{
        Runtime::ParameterizationStrategyKind::Lscm,
        Runtime::ParameterizationStrategyKind::HarmonicCotangent,
        Runtime::ParameterizationStrategyKind::TutteUniform,
        Runtime::ParameterizationStrategyKind::Bff,
    };
    constexpr std::array<std::string_view, 4u> tokens{
        "lscm", "harmonic_cotangent", "tutte_uniform", "bff"};

    for (std::size_t i = 0u; i < strategies.size(); ++i)
    {
        SCOPED_TRACE(tokens[i]);
        ParameterizationHarness harness{};
        const Runtime::EditorParameterizationResult result =
            Apply(harness, strategies[i]);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.Strategy, strategies[i]);
        EXPECT_EQ(result.StrategyToken, tokens[i]);
        EXPECT_EQ(
            Runtime::StableTokenForEditorParameterizationStrategy(
                result.Strategy),
            tokens[i]);
        ASSERT_TRUE(harness.Uvs().has_value());
        EXPECT_EQ(harness.Uvs()->size(), 9u);
        EXPECT_TRUE(AllFinite(*harness.Uvs()));
        EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::DirtyVertexTexcoords>(
            harness.Entity));
        EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::DirtyVertexAttributes>(
            harness.Entity));
    }
}

TEST(ParameterizationOperations, UndoRedoRestoresAbsentUvProperty)
{
    ParameterizationHarness harness{};
    ASSERT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    const Runtime::EditorParameterizationResult result = Apply(
        harness, Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const std::vector<glm::vec2> generated = *harness.Uvs();

    harness.Scene.Raw()
        .remove<Dirty::DirtyVertexTexcoords,
                Dirty::DirtyVertexAttributes>(harness.Entity);
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));
    EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::DirtyVertexTexcoords>(
        harness.Entity));
    EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::DirtyVertexAttributes>(
        harness.Entity));

    harness.Scene.Raw()
        .remove<Dirty::DirtyVertexTexcoords,
                Dirty::DirtyVertexAttributes>(harness.Entity);
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), generated);
    EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::DirtyVertexTexcoords>(
        harness.Entity));
    EXPECT_TRUE(harness.Scene.Raw().all_of<Dirty::DirtyVertexAttributes>(
        harness.Entity));
}

TEST(ParameterizationOperations, UndoRedoRestoresPresentUvValues)
{
    ParameterizationHarness harness{};
    const std::vector<glm::vec2> authored(
        harness.Vertices().Properties.Size(),
        glm::vec2{0.25f, 0.75f});
    harness.Vertices()
        .Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f})
        .Vector() = authored;

    const Runtime::EditorParameterizationResult result = Apply(
        harness, Runtime::ParameterizationStrategyKind::TutteUniform);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const std::vector<glm::vec2> generated = *harness.Uvs();
    EXPECT_NE(generated, authored);

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), authored);

    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_EQ(*harness.Uvs(), generated);

    std::vector<glm::vec2> intervening = generated;
    intervening.front().x += 0.125f;
    harness.Vertices()
        .Properties.Get<glm::vec2>("v:texcoord")
        .Vector() = intervening;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        harness.History.Snapshot();
    EXPECT_EQ(harness.History.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), intervening);
    EXPECT_EQ(harness.History.UndoCount(), 1u);
    EXPECT_EQ(harness.History.RedoCount(), 0u);
    EXPECT_EQ(harness.History.Snapshot().Revision,
              beforeRejectedUndo.Revision);
}

TEST(ParameterizationOperations, HistoryRejectsInterveningGeometryState)
{
    ParameterizationHarness harness{};
    const Runtime::EditorParameterizationResult result = Apply(
        harness, Runtime::ParameterizationStrategyKind::TutteUniform);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const std::vector<glm::vec2> generated = *harness.Uvs();

    auto positions =
        harness.Vertices().Properties.Get<glm::vec3>(
            GS::PropertyNames::kPosition);
    ASSERT_TRUE(positions);
    const float originalX = positions[0].x;
    positions[0].x += 0.5f;
    const float interveningX = positions[0].x;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedPositionUndo =
        harness.History.Snapshot();
    EXPECT_EQ(harness.History.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_FLOAT_EQ(
        harness.Vertices()
            .Properties.Get<glm::vec3>(
                GS::PropertyNames::kPosition)[0]
            .x,
        interveningX);
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), generated);
    EXPECT_EQ(harness.History.Snapshot().Revision,
              beforeRejectedPositionUndo.Revision);

    positions =
        harness.Vertices().Properties.Get<glm::vec3>(
            GS::PropertyNames::kPosition);
    ASSERT_TRUE(positions);
    positions[0].x = originalX;
    auto next =
        harness.Scene.Raw()
            .get<GS::Halfedges>(harness.Entity)
            .Properties.Get<std::uint32_t>(
                GS::PropertyNames::kHalfedgeNext);
    ASSERT_TRUE(next);
    ASSERT_GT(next.Vector().size(), 1u);
    const std::uint32_t originalNext = next[0];
    const std::uint32_t interveningNext = next[1];
    ASSERT_NE(interveningNext, originalNext);
    next[0] = interveningNext;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedTopologyUndo =
        harness.History.Snapshot();
    EXPECT_EQ(harness.History.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(
        harness.Scene.Raw()
            .get<GS::Halfedges>(harness.Entity)
            .Properties.Get<std::uint32_t>(
                GS::PropertyNames::kHalfedgeNext)[0],
        interveningNext);
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), generated);
    EXPECT_EQ(harness.History.UndoCount(), 1u);
    EXPECT_EQ(harness.History.RedoCount(), 0u);
    EXPECT_EQ(harness.History.Snapshot().Revision,
              beforeRejectedTopologyUndo.Revision);
    next =
        harness.Scene.Raw()
            .get<GS::Halfedges>(harness.Entity)
            .Properties.Get<std::uint32_t>(
                GS::PropertyNames::kHalfedgeNext);
    ASSERT_TRUE(next);
    next[0] = originalNext;
}

TEST(ParameterizationOperations, HistoryDoesNotRetainSessionOwnedModelCache)
{
    ECS::Scene::Registry scene{};
    Runtime::SelectionController selection{};
    Runtime::EditorCommandHistory history{};
    const ECS::EntityHandle entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh = MakeGridMesh();
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));
    const std::uint32_t stableEntityId =
        Runtime::SelectionController::ToStableEntityId(entity);

    {
        auto modelCache =
            std::make_unique<Runtime::EditorSelectedModelCache>();
        Intrinsic::Tests::EditorFeatureTestContext context{};
        context.Scene = &scene;
        context.Selection = &selection;
        context.CommandHistory = &history;
        context.SelectedModelCache = modelCache.get();
        const Runtime::EditorParameterizationResult result =
            Runtime::ApplyEditorParameterizationCommand(
                context,
                Runtime::EditorParameterizationCommand{
                    .StableEntityId = stableEntityId,
                    .Config = MakeConfig(
                        Runtime::ParameterizationStrategyKind::Lscm),
                });
        ASSERT_TRUE(result.Succeeded()) << result.Message;
    }

    ASSERT_TRUE(history.Undo().Succeeded());
    const GS::Vertices& vertices = scene.Raw().get<GS::Vertices>(entity);
    EXPECT_FALSE(vertices.Properties.Exists("v:texcoord"));
    ASSERT_TRUE(history.Redo().Succeeded());
    EXPECT_TRUE(vertices.Properties.Get<glm::vec2>("v:texcoord"));
}

TEST(ParameterizationOperations, DeletedVertexTombstonePreservesStorageUvs)
{
    Geometry::HalfedgeMesh::Mesh mesh = MakeGridMesh();
    const Geometry::VertexHandle tombstone =
        mesh.AddVertex(glm::vec3{8.0f, 8.0f, 8.0f});
    mesh.DeleteVertex(tombstone);
    ASSERT_EQ(mesh.VerticesSize(), 10u);
    ASSERT_EQ(mesh.DeletedVertexCount(), 1u);

    ParameterizationHarness harness{mesh};
    const Runtime::EditorParameterizationResult result = Apply(
        harness, Runtime::ParameterizationStrategyKind::Lscm);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.VertexCount, 10u);
    ASSERT_TRUE(harness.Uvs().has_value());
    ASSERT_EQ(harness.Uvs()->size(), 10u);
    EXPECT_EQ((*harness.Uvs())[tombstone.Index], glm::vec2(0.0f));
}

TEST(ParameterizationOperations, ConfiguredPathReadsActiveEngineConfig)
{
    ParameterizationHarness harness{};
    Runtime::RuntimeEngineConfigControlState state{};
    Runtime::SetParameterizationConfig(
        state.ActiveConfig,
        MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform));
    harness.Context.EngineConfigControlState = &state;

    const Runtime::EditorParameterizationResult result =
        Runtime::ApplyEditorConfiguredParameterizationCommand(
            harness.Context,
            Runtime::EditorConfiguredParameterizationCommand{
                .StableEntityId = harness.StableEntityId,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Strategy,
              Runtime::ParameterizationStrategyKind::TutteUniform);
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_TRUE(AllFinite(*harness.Uvs()));
}

TEST(ParameterizationOperations, EditorConfigHelperUsesValidatedHotApplyLane)
{
    ParameterizationHarness harness{};
    Runtime::RuntimeEngineConfigSectionRegistry registry{};
    ASSERT_TRUE(registry.Register(
        Runtime::MakeParameterizationConfigSectionRegistration()));
    ConfigControlHarness controlHarness{std::move(registry)};
    Runtime::EngineConfigControl& control = controlHarness.Control();
    harness.Context.EngineConfigControlState =
        &control.GetEngineConfigControlState();
    harness.Context.EngineConfigCommandsAvailable = true;
    int previewCalls = 0, applyCalls = 0;
    harness.Context.PreviewEngineConfigDocument =
        [&](const std::string& document, const std::string& sourceId)
        {
            ++previewCalls;
            return control.PreviewEngineConfigControlDocument(
                document, sourceId);
        };
    harness.Context.ApplyEngineConfigHotSubset =
        [&](const Config::EngineConfigLoadResult& preview)
        {
            ++applyCalls;
            return control.ApplyEngineConfigHotSubset(
                preview,
                Runtime::RuntimeConfigControlSource::Editor);
        };

    const Runtime::RuntimeEngineConfigApplyResult result =
        Runtime::ApplyEditorParameterizationConfig(
            harness.Context,
            MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform));
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Applied);
    EXPECT_EQ(result.Source,
              Runtime::RuntimeConfigControlSource::Editor);
    EXPECT_TRUE(result.SectionChanged(
        Runtime::kParameterizationConfigSectionName));
    const auto config =
        Runtime::GetEditorParameterizationConfig(harness.Context);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->Strategy,
              Runtime::ParameterizationStrategyKind::TutteUniform);
    EXPECT_EQ(result.LoadResult.SourceId, Runtime::kParameterizationConfigSectionName);
    const auto noChange = Runtime::ApplyEditorParameterizationConfig(harness.Context, *config, "test-parameterization");
    EXPECT_TRUE(noChange.Succeeded());
    EXPECT_EQ(noChange.Status, Runtime::RuntimeEngineConfigApplyStatus::NoChange);
    EXPECT_EQ(noChange.LoadResult.SourceId, "test-parameterization");
    for (unsigned missing = 0; missing < 5; ++missing)
    {
        auto unavailable = harness.Context;
        if (missing == 0) unavailable.EngineConfigControlState = nullptr;
        if (missing == 1) unavailable.EngineConfigCommandsAvailable = false;
        if (missing == 2) unavailable.PreviewEngineConfigDocument = {};
        if (missing == 3) unavailable.ApplyEngineConfigHotSubset = {};
        if (missing == 4) unavailable.AttachmentActive = [] { return false; };
        EXPECT_FALSE(Runtime::ApplyEditorParameterizationConfig(unavailable, *config).Succeeded());
    }
    EXPECT_EQ(previewCalls, 2);
    EXPECT_EQ(applyCalls, 2);

    auto rejectedApply = harness.Context;
    rejectedApply.ApplyEngineConfigHotSubset = [](const auto& preview) {
        return Runtime::RuntimeEngineConfigApplyResult{
            .Status = Runtime::RuntimeEngineConfigApplyStatus::Rejected, .LoadResult = preview};
    };
    EXPECT_FALSE(Runtime::ApplyEditorParameterizationConfig(rejectedApply, *config).Succeeded());
    EXPECT_EQ(previewCalls, 3);
    EXPECT_EQ(applyCalls, 2);
    auto lostEdit = harness.Context;
    lostEdit.PreviewEngineConfigDocument = [&](const auto&, const auto&) {
        Config::EngineConfigLoadResult fallback;
        fallback.State = Config::EngineConfigState::FallbackApplied;
        fallback.Preview.Config = control.GetEngineConfigControlState().ActiveConfig;
        return fallback;
    };
    auto edited = *config;
    edited.Lscm.MaxSolverIterations += 1;
    const auto rejectedEdit = Runtime::ApplyEditorParameterizationConfig(lostEdit, edited);
    EXPECT_EQ(rejectedEdit.Status, Runtime::RuntimeEngineConfigApplyStatus::Rejected);
    EXPECT_EQ(rejectedEdit.LoadResult.State, Config::EngineConfigState::FallbackApplied);
    EXPECT_EQ(applyCalls, 2);
    EXPECT_EQ(Runtime::GetEditorParameterizationConfig(harness.Context)->Lscm.MaxSolverIterations,
              config->Lscm.MaxSolverIterations);

}

TEST(ParameterizationOperations, ConfigSourcesProduceIdenticalStateAndUvs)
{
    constexpr std::array sources{
        Runtime::RuntimeConfigControlSource::Editor,
        Runtime::RuntimeConfigControlSource::AgentCli,
        Runtime::RuntimeConfigControlSource::Programmatic,
    };
    const Runtime::ParameterizationConfig parameterization =
        MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform);
    std::optional<std::string> referenceSerializedConfig{};
    std::optional<std::vector<glm::vec2>> referenceUvs{};

    for (const Runtime::RuntimeConfigControlSource source : sources)
    {
        ParameterizationHarness harness{};
        Runtime::RuntimeEngineConfigSectionRegistry registry{};
        ASSERT_TRUE(registry.Register(
            Runtime::MakeParameterizationConfigSectionRegistration()));
        ConfigControlHarness controlHarness{std::move(registry)};
        Runtime::EngineConfigControl& control = controlHarness.Control();
        Config::EngineConfig candidate = controlHarness.ActiveConfig();
        Runtime::SetParameterizationConfig(candidate, parameterization);
        const Config::EngineConfigLoadResult preview =
            control.PreviewEngineConfigControlDocument(
                Config::SerializeEngineConfig(candidate),
                "parameterization-source-parity");
        ASSERT_TRUE(Config::IsConfigUsable(preview));
        const Runtime::RuntimeEngineConfigApplyResult applied =
            control.ApplyEngineConfigHotSubset(preview, source);
        ASSERT_TRUE(applied.Succeeded());
        EXPECT_EQ(applied.Source, source);
        EXPECT_TRUE(applied.SectionChanged(
            Runtime::kParameterizationConfigSectionName));

        const Runtime::RuntimeEngineConfigControlState& state =
            control.GetEngineConfigControlState();
        const auto activeParameterization =
            Runtime::GetParameterizationConfig(state.ActiveConfig);
        ASSERT_TRUE(activeParameterization.has_value());
        EXPECT_EQ(activeParameterization->Strategy,
                  Runtime::ParameterizationStrategyKind::TutteUniform);
        const std::string serialized =
            Config::SerializeEngineConfig(state.ActiveConfig);
        if (!referenceSerializedConfig.has_value())
            referenceSerializedConfig = serialized;
        else
            EXPECT_EQ(serialized, *referenceSerializedConfig);

        harness.Context.EngineConfigControlState = &state;
        const Runtime::EditorParameterizationResult result =
            Runtime::ApplyEditorConfiguredParameterizationCommand(
                harness.Context,
                Runtime::EditorConfiguredParameterizationCommand{
                    .StableEntityId = harness.StableEntityId,
                });
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        ASSERT_TRUE(harness.Uvs().has_value());
        if (!referenceUvs.has_value())
            referenceUvs = *harness.Uvs();
        else
            EXPECT_EQ(*harness.Uvs(), *referenceUvs);
    }
}

TEST(ParameterizationOperations, IdenticalInputAndConfigAreDeterministic)
{
    ParameterizationHarness first{};
    ParameterizationHarness second{};
    const auto firstResult =
        Apply(first, Runtime::ParameterizationStrategyKind::Bff);
    const auto secondResult =
        Apply(second, Runtime::ParameterizationStrategyKind::Bff);
    ASSERT_TRUE(firstResult.Succeeded()) << firstResult.Message;
    ASSERT_TRUE(secondResult.Succeeded()) << secondResult.Message;
    ASSERT_TRUE(first.Uvs().has_value());
    ASSERT_TRUE(second.Uvs().has_value());
    EXPECT_EQ(*first.Uvs(), *second.Uvs());
}

TEST(ParameterizationOperations,
     SectionValidationKeepsNestedReferenceFallbackAtomic)
{
    Runtime::ParameterizationConfig reference{};
    reference.Lscm.PinVertex0 = 4u;
    reference.Lscm.PinVertex1 = 5u;
    reference.Lscm.SolverTolerance = 0.125;

    const Config::EngineConfigSectionValidationResult validation =
        Runtime::ValidateParameterizationConfigSection(
            R"({"view":{"background_mode":"checker"},"lscm":{"auto_pins":false,"pin_vertex_0":9,"pin_vertex_1":9}})",
            Runtime::SerializeParameterizationConfig(reference),
            "app.sections.sandbox.parameterization.payload");

    EXPECT_EQ(validation.State, Config::EngineConfigState::FallbackApplied);
    EXPECT_EQ(validation.ParsedFieldCount, 4u);
    bool hasInvalidValue = false;
    for (const Config::EngineConfigDiagnostic& diagnostic :
         validation.Diagnostics)
    {
        hasInvalidValue =
            hasInvalidValue ||
            diagnostic.Code == Config::EngineConfigDiagnosticCode::InvalidValue;
    }
    EXPECT_TRUE(hasInvalidValue);

    Config::EngineConfig canonical{};
    Config::UpsertEngineConfigSection(
        canonical.AppSections,
        Config::EngineConfigSection{
            .Name = std::string{Runtime::kParameterizationConfigSectionName},
            .SchemaId =
                std::string{Runtime::kParameterizationConfigSectionSchemaId},
            .SchemaVersion =
                Runtime::kParameterizationConfigSectionSchemaVersion,
            .PayloadJson = validation.CanonicalPayloadJson,
        });
    const auto decoded = Runtime::GetParameterizationConfig(canonical);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->View.BackgroundMode,
              Runtime::ParameterizationUvBackgroundMode::Checker);
    EXPECT_TRUE(decoded->Lscm.AutoPins);
    EXPECT_EQ(decoded->Lscm.PinVertex0, 4u);
    EXPECT_EQ(decoded->Lscm.PinVertex1, 5u);
    EXPECT_DOUBLE_EQ(decoded->Lscm.SolverTolerance, 0.125);
}

TEST(ParameterizationOperations, InvalidEnumsAndNarrowingFailClosed)
{
    ParameterizationHarness harness{};
    Runtime::ParameterizationConfig config{};
    config.Strategy =
        static_cast<Runtime::ParameterizationStrategyKind>(999u);
    auto result = Runtime::ApplyEditorParameterizationCommand(
        harness.Context,
        Runtime::EditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    config = {};
    config.Lscm.PinUv0.U = std::numeric_limits<double>::max();
    result = Runtime::ApplyEditorParameterizationCommand(
        harness.Context,
        Runtime::EditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    config = {};
    config.Lscm.SolverTolerance = 1.0e31;
    result = Runtime::ApplyEditorParameterizationCommand(
        harness.Context,
        Runtime::EditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);

    config = MakeConfig(
        Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    config.Harmonic.Boundary =
        static_cast<Runtime::ParameterizationBoundaryPolicy>(999u);
    result = Runtime::ApplyEditorParameterizationCommand(
        harness.Context,
        Runtime::EditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);

    config = MakeConfig(Runtime::ParameterizationStrategyKind::Bff);
    config.Bff.Mode =
        static_cast<Runtime::ParameterizationBffBoundaryMode>(999u);
    result = Runtime::ApplyEditorParameterizationCommand(
        harness.Context,
        Runtime::EditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
}

TEST(ParameterizationOperations, InvalidConfigEditDoesNotSerializeFallbackToken)
{
    ParameterizationHarness harness{};
    Runtime::RuntimeEngineConfigControlState state{};
    harness.Context.EngineConfigControlState = &state;
    harness.Context.EngineConfigCommandsAvailable = true;
    bool previewCalled = false;
    harness.Context.PreviewEngineConfigDocument =
        [&previewCalled](const std::string&, const std::string&)
        {
            previewCalled = true;
            return Config::EngineConfigLoadResult{};
        };
    harness.Context.ApplyEngineConfigHotSubset =
        [](const Config::EngineConfigLoadResult&)
        {
            return Runtime::RuntimeEngineConfigApplyResult{};
        };

    Runtime::ParameterizationConfig invalid{};
    invalid.Strategy =
        static_cast<Runtime::ParameterizationStrategyKind>(999u);
    const Runtime::RuntimeEngineConfigApplyResult result =
        Runtime::ApplyEditorParameterizationConfig(
            harness.Context,
            invalid);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(
        result.Status,
        Runtime::RuntimeEngineConfigApplyStatus::Rejected);
    EXPECT_FALSE(previewCalled);

    invalid = {};
    invalid.Harmonic.Boundary =
        static_cast<Runtime::ParameterizationBoundaryPolicy>(999u);
    const Runtime::RuntimeEngineConfigApplyResult inactiveResult =
        Runtime::ApplyEditorParameterizationConfig(
            harness.Context,
            invalid);
    EXPECT_FALSE(inactiveResult.Succeeded());
    EXPECT_EQ(
        inactiveResult.Status,
        Runtime::RuntimeEngineConfigApplyStatus::Rejected);
    EXPECT_FALSE(previewCalled);
}

TEST(ParameterizationOperations, ConversionFailureNamesParameterizationAndRetainsUvs)
{
    ParameterizationHarness harness{};
    auto positions = harness.Vertices().Properties.Get<glm::vec3>("v:position");
    for (auto& position : positions.Vector())
        position = glm::vec3{0.0f};
    auto uvs = harness.Vertices().Properties.GetOrAdd<glm::vec2>("v:texcoord", {0.25f, 0.5f});
    const auto before = uvs.Vector();

    const auto result = Apply(harness, Runtime::ParameterizationStrategyKind::Lscm);
    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_TRUE(result.Message.starts_with("Parameterization")) << result.Message;
    EXPECT_EQ(result.Message.find("denoise"), std::string::npos) << result.Message;
    EXPECT_FALSE(result.Rejection.Evaluated);
    EXPECT_EQ(uvs.Vector(), before);
    EXPECT_EQ(harness.History.UndoCount(), 0u);
}

TEST(ParameterizationOperations, WrongTypedUvAndNonTriangleFacesFailClosed)
{
    ParameterizationHarness wrongType{};
    (void)wrongType.Vertices()
        .Properties.GetOrAdd<float>("v:texcoord", 0.0f);
    auto result = Apply(
        wrongType, Runtime::ParameterizationStrategyKind::Lscm);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_TRUE(wrongType.Vertices().Properties.Get<float>("v:texcoord"));
    EXPECT_FALSE(wrongType.Vertices().Properties.Get<glm::vec2>("v:texcoord"));

    ParameterizationHarness quad{MakeQuadMesh()};
    result = Apply(quad, Runtime::ParameterizationStrategyKind::Lscm);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(quad.Vertices().Properties.Exists("v:texcoord"));
    const Runtime::EditorUvRegenerationCommand atlasCommand{.StableEntityId = quad.StableEntityId};
    EXPECT_FALSE(Runtime::PreviewEditorUvRegenerationCommand(quad.Context, atlasCommand).Enabled);
    const auto atlas = Runtime::ApplyEditorUvRegenerationCommand(quad.Context, atlasCommand);
    EXPECT_EQ(atlas.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_NE(atlas.Diagnostic.find("triangular source faces"), std::string::npos);
    EXPECT_FALSE(quad.Vertices().Properties.Exists("v:texcoord"));
    EXPECT_EQ(quad.History.UndoCount(), 0u);
}

// BUG-141: "Mesh parameterization solver rejected the selected mesh or config."
// named neither the mesh property that was wrong nor a way to fix it. A solver
// rejection now carries the two counts it rejects on, and the message repeats
// them so a user who never opens a debugger can act.
TEST(ParameterizationOperations, SolverRejectionCarriesStructuredTopologyCause)
{
    ParameterizationHarness closed{MakeClosedTetrahedronMesh()};
    const Runtime::EditorParameterizationResult result =
        Apply(closed, Runtime::ParameterizationStrategyKind::Lscm);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    ASSERT_TRUE(result.Rejection.Evaluated);
    EXPECT_EQ(result.Rejection.ConnectedComponentCount, 1u);
    EXPECT_EQ(result.Rejection.BoundaryLoopCount, 0u);
    EXPECT_TRUE(result.Rejection.ViolatesDiskTopology());
    EXPECT_NE(result.Message.find("1 connected component"),
              std::string::npos)
        << result.Message;
    EXPECT_NE(result.Message.find("0 boundary loops"), std::string::npos)
        << result.Message;
    EXPECT_NE(result.Message.find("disk topology"), std::string::npos)
        << result.Message;

    // A rejection raised before the solver ran has no mesh to count, and must
    // not pretend otherwise.
    ParameterizationHarness quad{MakeQuadMesh()};
    const Runtime::EditorParameterizationResult preSolver =
        Apply(quad, Runtime::ParameterizationStrategyKind::Lscm);
    ASSERT_FALSE(preSolver.Succeeded());
    EXPECT_FALSE(preSolver.Rejection.Evaluated);

    // The success path pays nothing for the summary.
    ParameterizationHarness disk{};
    const Runtime::EditorParameterizationResult applied =
        Apply(disk, Runtime::ParameterizationStrategyKind::Lscm);
    ASSERT_TRUE(applied.Succeeded());
    EXPECT_FALSE(applied.Rejection.Evaluated);
}

// BUG-137: corner UVs win the canonical resolution order, so publishing a
// vertex-domain parameterization underneath a surviving `h:texcoord` would
// compute a result that no consumer reads. The operation retires what it
// supersedes, and undo restores it.
TEST(ParameterizationOperations, RetiresSupersededCornerUvsAndUndoRestoresThem)
{
    ParameterizationHarness harness{};
    ASSERT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    std::vector<glm::vec2> authoredCorners(
        harness.Halfedges().Properties.Size(), glm::vec2{0.0f});
    for (std::size_t i = 0u; i < authoredCorners.size(); ++i)
    {
        authoredCorners[i] =
            glm::vec2{static_cast<float>(i) * 0.03125f, 0.5f};
    }
    harness.Halfedges()
        .Properties.GetOrAdd<glm::vec2>("h:texcoord", glm::vec2{0.0f})
        .Vector() = authoredCorners;
    ASSERT_TRUE(harness.CornerUvs().has_value());

    auto config = MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform);
    config.CornerTexcoordsToRetire = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::MeshHalfedge, "h:texcoord", Geometry::PropertyValueKind::Vec2};
    const auto result = Runtime::ApplyEditorParameterizationCommand(harness.Context,
        {.StableEntityId = harness.StableEntityId, .Config = config});
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    ASSERT_TRUE(harness.Uvs().has_value());
    const std::vector<glm::vec2> generated = *harness.Uvs();
    EXPECT_TRUE(AllFinite(generated));
    EXPECT_FALSE(harness.Halfedges().Properties.Exists("h:texcoord"))
        << "a superseded corner property would keep winning the resolution "
           "order over the parameterization just computed";

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));
    ASSERT_TRUE(harness.CornerUvs().has_value());
    EXPECT_EQ(*harness.CornerUvs(), authoredCorners);

    ASSERT_TRUE(harness.History.Redo().Succeeded());
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), generated);
    EXPECT_FALSE(harness.Halfedges().Properties.Exists("h:texcoord"));
}

// BUG-137: the vertex-indexed UV layout view cannot draw corner UVs, and
// reporting nothing at all read as "this mesh has no UVs" for exactly the
// meshes an atlas produces.
TEST(ParameterizationOperations, ViewModelDrawsAuthoritativeCornerUvs)
{
    ParameterizationHarness harness{};
    ASSERT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));
    harness.Halfedges()
        .Properties.GetOrAdd<glm::vec2>("h:texcoord", glm::vec2{0.25f, 0.5f});

    const Runtime::EditorParameterizationViewModel model =
        Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    ASSERT_TRUE(model.HasUvCoordinates);
    ASSERT_TRUE(model.HasFiniteUvBounds);
    EXPECT_EQ(model.UvBoundsMin, (glm::vec2{0.25f, 0.5f}));
    EXPECT_EQ(model.UvBoundsMax, (glm::vec2{0.25f, 0.5f}));
    EXPECT_FALSE(model.Triangles.empty());
    for (const auto triangle : model.Triangles)
        for (const auto index : triangle)
        {
            ASSERT_LT(index, model.UVs.size());
            EXPECT_EQ(model.UVs[index], (glm::vec2{0.25f, 0.5f}));
        }
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));
}

// BUG-141: the panel renders its header from the view model's `Message` and
// its "Last run diagnostics" block from the result, so mirroring the result's
// message onto the view model printed one command's outcome twice in one
// window. The header keeps only what is true of the view itself.
TEST(ParameterizationOperations, ViewModelDoesNotMirrorTheLastResultMessage)
{
    ParameterizationHarness closed{MakeClosedTetrahedronMesh()};
    const Runtime::EditorParameterizationResult result =
        Apply(closed, Runtime::ParameterizationStrategyKind::Lscm);
    ASSERT_FALSE(result.Succeeded());
    ASSERT_FALSE(result.Message.empty());

    closed.Results.LastParameterizationResult = result;
    const Runtime::EditorParameterizationViewModel model =
        Runtime::BuildEditorParameterizationViewModel(closed.Context, closed.Results);
    ASSERT_TRUE(model.HasLastResult);
    EXPECT_NE(model.Message, result.Message);
    EXPECT_TRUE(model.LastStatus.has_value())
        << "the outcome must still reach the panel through the result";
}

TEST(ParameterizationOperations, ViewModelIsPointerFreeAndCarriesAggregateDiagnostics)
{
    ParameterizationHarness harness{};
    const Runtime::EditorParameterizationResult result = Apply(
        harness, Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    harness.Results.LastParameterizationResult = result;

    const Runtime::EditorParameterizationViewModel model =
        Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    const Runtime::EditorParameterizationViewModel repeated =
        Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    EXPECT_TRUE(model.HasSelectedEntity);
    EXPECT_TRUE(model.SelectedEntityIsMesh);
    EXPECT_TRUE(model.HasUvCoordinates);
    EXPECT_TRUE(model.HasFiniteUvBounds);
    EXPECT_TRUE(model.HasLastResult);
    EXPECT_EQ(model.SelectedStableEntityId, harness.StableEntityId);
    EXPECT_EQ(model.Strategy,
              Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    EXPECT_EQ(model.UVs.size(), 9u);
    EXPECT_EQ(model.Triangles.size(), 8u);
    EXPECT_EQ(model.Triangles, repeated.Triangles);
    EXPECT_TRUE(std::isfinite(model.UvBoundsMin.x));
    EXPECT_TRUE(std::isfinite(model.UvBoundsMin.y));
    EXPECT_TRUE(std::isfinite(model.UvBoundsMax.x));
    EXPECT_TRUE(std::isfinite(model.UvBoundsMax.y));
    ASSERT_TRUE(model.LastStatus.has_value());
    EXPECT_EQ(*model.LastStatus,
              Geometry::Parameterization::ParameterizationStatus::Success);
    ASSERT_TRUE(model.LastDiagnostics.has_value());
    EXPECT_EQ(model.LastDiagnostics->VertexStorageCount, 9u);
    EXPECT_EQ(model.LastDiagnostics->LiveFaceCount, 8u);
}

TEST(ParameterizationOperations, ViewModelFansFaceDiagnosticsIntoRenderedTriangles)
{
    ParameterizationHarness harness{MakeQuadMesh()};
    Runtime::RuntimeEngineConfigControlState state{};
    Runtime::ParameterizationConfig config{};
    config.View.RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded;
    Runtime::SetParameterizationConfig(state.ActiveConfig, config);
    harness.Context.EngineConfigControlState = &state;
    harness.Vertices()
        .Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f})
        .Vector() = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f},
        };
    Runtime::EditorParameterizationResult last{};
    const Runtime::EditorParameterizationViewModel current =
        Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    ASSERT_TRUE(current.DiagnosticInputFingerprint.has_value());
    last.Status = Runtime::EditorCommandStatus::Applied;
    last.StableEntityId = harness.StableEntityId;
    last.ParameterizationStatus =
        Geometry::Parameterization::ParameterizationStatus::Success;
    last.DiagnosticInputFingerprint = current.DiagnosticInputFingerprint;
    last.Diagnostics.FaceStorageCount = 1u;
    last.Diagnostics.FaceConformalDistortion = {2.5f};
    harness.Results.LastParameterizationResult = last;

    const Runtime::EditorParameterizationViewModel model =
        Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    ASSERT_EQ(model.Triangles.size(), 2u);
    EXPECT_EQ(model.Triangles[0],
              (std::array<std::uint32_t, 3u>{0u, 1u, 2u}));
    EXPECT_EQ(model.Triangles[1],
              (std::array<std::uint32_t, 3u>{0u, 2u, 3u}));
    EXPECT_EQ(
        model.LineIndices,
        (std::vector<std::uint32_t>{
            0u, 1u, 1u, 2u, 2u, 0u,
            0u, 2u, 2u, 3u, 3u, 0u,
        }));
    EXPECT_EQ(
        model.TriangleConformalDistortion,
        (std::vector<float>{2.5f, 2.5f}));
}

TEST(ParameterizationOperations,
     FaceDiagnosticsRequireTheExactGeometryAndUvFingerprint)
{
    ParameterizationHarness harness{};
    Runtime::RuntimeEngineConfigControlState state{};
    Runtime::ParameterizationConfig config{};
    config.View.RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded;
    Runtime::SetParameterizationConfig(state.ActiveConfig, config);
    harness.Context.EngineConfigControlState = &state;

    Runtime::EditorParameterizationResult last = Apply(
        harness, Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    ASSERT_TRUE(last.Succeeded()) << last.Message;
    ASSERT_TRUE(last.DiagnosticInputFingerprint.has_value());
    last.Diagnostics.FaceConformalDistortion.assign(8u, 2.0f);
    harness.Results.LastParameterizationResult = last;

    Runtime::EditorParameterizationViewModel model =
        Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    EXPECT_EQ(model.TriangleConformalDistortion.size(), 8u);
    EXPECT_EQ(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    model = Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    EXPECT_FALSE(model.DiagnosticInputFingerprint.has_value());
    EXPECT_TRUE(model.TriangleConformalDistortion.empty());

    ASSERT_TRUE(harness.History.Redo().Succeeded());
    model = Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    EXPECT_EQ(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_EQ(model.TriangleConformalDistortion.size(), 8u);

    auto uvs = harness.Vertices().Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(uvs);
    const glm::vec2 originalUv = uvs.Vector().front();
    uvs.Vector().front().x = originalUv.x + 0.125f;
    model = Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    ASSERT_TRUE(model.DiagnosticInputFingerprint.has_value());
    EXPECT_NE(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_TRUE(model.TriangleConformalDistortion.empty());

    uvs.Vector().front() = originalUv;
    model = Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    EXPECT_EQ(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_EQ(model.TriangleConformalDistortion.size(), 8u);

    auto positions = harness.Vertices().Properties.Get<glm::vec3>(
        GS::PropertyNames::kPosition);
    ASSERT_TRUE(positions);
    positions.Vector().front().x += 0.25f;
    model = Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    ASSERT_TRUE(model.DiagnosticInputFingerprint.has_value());
    EXPECT_NE(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_TRUE(model.TriangleConformalDistortion.empty());
}

TEST(ParameterizationOperations,
     DeletedFaceTombstoneKeepsDiagnosticsAlignedWithSourceFaceStorage)
{
    Geometry::HalfedgeMesh::Mesh mesh = MakeGridMesh();
    mesh.DeleteFace(Geometry::FaceHandle{0u});
    ASSERT_EQ(mesh.FacesSize(), 8u);
    ASSERT_EQ(mesh.DeletedFaceCount(), 1u);

    ParameterizationHarness harness{mesh};
    Runtime::RuntimeEngineConfigControlState state{};
    Runtime::ParameterizationConfig config{};
    config.View.RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded;
    Runtime::SetParameterizationConfig(state.ActiveConfig, config);
    harness.Context.EngineConfigControlState = &state;

    Runtime::EditorParameterizationResult result = Apply(
        harness, Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Diagnostics.FaceStorageCount, 8u);
    EXPECT_EQ(result.Diagnostics.LiveFaceCount, 7u);
    EXPECT_EQ(result.Diagnostics.DeletedFaceCount, 1u);
    ASSERT_EQ(result.Diagnostics.FaceConformalDistortion.size(), 8u);
    EXPECT_TRUE(std::isnan(result.Diagnostics.FaceConformalDistortion[0u]));
    for (std::size_t sourceFace = 1u; sourceFace < 8u; ++sourceFace)
    {
        EXPECT_TRUE(std::isfinite(
            result.Diagnostics.FaceConformalDistortion[sourceFace]));
    }

    harness.Results.LastParameterizationResult = result;
    const Runtime::EditorParameterizationViewModel model =
        Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    ASSERT_EQ(model.TriangleConformalDistortion.size(), 7u);
    for (std::size_t triangle = 0u;
         triangle < model.TriangleConformalDistortion.size();
         ++triangle)
    {
        EXPECT_EQ(
            model.TriangleConformalDistortion[triangle],
            result.Diagnostics.FaceConformalDistortion[triangle + 1u]);
    }
}

TEST(ParameterizationOperations, CpuViewDisablesGpuWorkAndReportsBackgroundFallback)
{
    Runtime::EditorParameterizationUvViewCommandSurface uvViewCommands{};
    std::vector<Runtime::EditorParameterizationUvViewRequest> requests{};
    uvViewCommands.Submit =
        [&requests](Runtime::EditorParameterizationUvViewRequest request)
        {
            requests.push_back(request);
            return Runtime::EditorParameterizationUvViewState{};
        };

    Runtime::EditorParameterizationViewModel model{};
    model.View.RenderMode = Runtime::ParameterizationUvRenderMode::CpuLayout;
    for (const Runtime::ParameterizationUvBackgroundMode background : {
             Runtime::ParameterizationUvBackgroundMode::Texture,
             Runtime::ParameterizationUvBackgroundMode::TexelDensity})
    {
        model.View.BackgroundMode = background;
        const Runtime::EditorParameterizationUvViewState state =
            Runtime::SubmitEditorParameterizationUvView(
                uvViewCommands, model, 320u, 180u);
        ASSERT_FALSE(requests.empty());
        EXPECT_FALSE(requests.back().Enabled);
        EXPECT_EQ(requests.back().Width, 320u);
        EXPECT_EQ(requests.back().Height, 180u);
        EXPECT_EQ(
            state.Status,
            Runtime::EditorParameterizationUvViewStatus::CpuLayout);
        EXPECT_EQ(state.RequestedMode,
                  Runtime::ParameterizationUvRenderMode::CpuLayout);
        EXPECT_EQ(state.ActiveMode,
                  Runtime::ParameterizationUvRenderMode::CpuLayout);
        EXPECT_EQ(state.RequestedBackground, background);
        EXPECT_EQ(
            state.ActiveBackground,
            Runtime::ParameterizationUvBackgroundMode::Checker);
        EXPECT_FALSE(state.GpuReady);
    }
}

TEST(ParameterizationOperations, GpuViewWithoutCommandSurfaceReportsCpuFallback)
{
    const Runtime::EditorParameterizationUvViewCommandSurface uvViewCommands{};
    Runtime::EditorParameterizationViewModel model{};
    model.HasSelectedEntity = true;
    model.SelectedEntityIsMesh = true;
    model.HasUvCoordinates = true;
    model.HasFiniteUvBounds = true;
    model.SelectedStableEntityId = 17u;
    model.View.RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded;
    model.View.BackgroundMode =
        Runtime::ParameterizationUvBackgroundMode::Texture;
    model.UVs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    model.UvBoundsMin = {0.0f, 0.0f};
    model.UvBoundsMax = {1.0f, 1.0f};
    model.Triangles = {{0u, 1u, 2u}};
    model.LineIndices = {0u, 1u, 1u, 2u, 2u, 0u};

    const Runtime::EditorParameterizationUvViewState state =
        Runtime::SubmitEditorParameterizationUvView(
            uvViewCommands, model, 400u, 240u);
    EXPECT_EQ(
        state.Status,
        Runtime::EditorParameterizationUvViewStatus::CpuFallbackNonOperational);
    EXPECT_EQ(state.RequestedMode,
              Runtime::ParameterizationUvRenderMode::GpuShaded);
    EXPECT_EQ(state.ActiveMode,
              Runtime::ParameterizationUvRenderMode::CpuLayout);
    EXPECT_EQ(
        state.ActiveBackground,
        Runtime::ParameterizationUvBackgroundMode::Checker);
    EXPECT_FALSE(state.GpuReady);
}

TEST(ParameterizationOperations, GpuViewRequestTokenIsStableAndSemantic)
{
    Runtime::EditorParameterizationUvViewCommandSurface uvViewCommands{};
    std::vector<Runtime::EditorParameterizationUvViewRequest> requests{};
    uvViewCommands.Submit =
        [&requests](Runtime::EditorParameterizationUvViewRequest request)
        {
            requests.push_back(request);
            return Runtime::EditorParameterizationUvViewState{
                .Status =
                    Runtime::EditorParameterizationUvViewStatus::Ready,
                .RequestedMode = request.View.RenderMode,
                .ActiveMode = Runtime::ParameterizationUvRenderMode::GpuShaded,
                .RequestedBackground = request.View.BackgroundMode,
                .ActiveBackground = request.View.BackgroundMode,
                .HeatmapActive = request.View.ShowDistortionHeatmap,
                .GpuReady = true,
                .RequestToken = request.RequestToken,
                .BindlessIndex = 41u,
                .Width = request.Width,
                .Height = request.Height,
                .TargetGeneration = 7u,
                .RecordedPassCount = 3u,
                .Message = "ready",
            };
        };

    Runtime::EditorParameterizationViewModel model{};
    model.HasSelectedEntity = true;
    model.SelectedEntityIsMesh = true;
    model.HasUvCoordinates = true;
    model.HasFiniteUvBounds = true;
    model.SelectedStableEntityId = 29u;
    model.View.RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded;
    model.View.BackgroundMode = Runtime::ParameterizationUvBackgroundMode::Grid;
    model.UVs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    model.UvBoundsMin = {0.0f, 0.0f};
    model.UvBoundsMax = {1.0f, 1.0f};
    model.Triangles = {{0u, 1u, 2u}};
    model.LineIndices = {0u, 1u, 1u, 2u, 2u, 0u};
    model.TriangleConformalDistortion = {1.25f};

    const auto submit = [&uvViewCommands](
                            const Runtime::EditorParameterizationViewModel& value)
    {
        return Runtime::SubmitEditorParameterizationUvView(
            uvViewCommands, value, 640u, 360u);
    };
    const auto first = submit(model);
    const auto repeated = submit(model);
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_TRUE(requests[0].Enabled);
    EXPECT_EQ(requests[0].StableEntityId, 29u);
    EXPECT_EQ(requests[0].Width, 640u);
    EXPECT_EQ(requests[0].Height, 360u);
    EXPECT_EQ(requests[0].LineIndices, model.LineIndices);
    EXPECT_EQ(requests[0].TriangleConformalDistortion,
              model.TriangleConformalDistortion);
    EXPECT_EQ(requests[0].RequestToken, requests[1].RequestToken);
    EXPECT_EQ(first.RequestToken, repeated.RequestToken);
    EXPECT_EQ(first.Status,
              Runtime::EditorParameterizationUvViewStatus::Ready);
    EXPECT_EQ(first.ActiveMode,
              Runtime::ParameterizationUvRenderMode::GpuShaded);
    EXPECT_TRUE(first.GpuReady);
    EXPECT_EQ(first.BindlessIndex, 41u);
    EXPECT_EQ(first.TargetGeneration, 7u);
    EXPECT_EQ(first.RecordedPassCount, 3u);
    EXPECT_EQ(first.Width, 640u);
    EXPECT_EQ(first.Height, 360u);
    EXPECT_EQ(first.Message, "ready");

    const std::uint64_t referenceToken = requests.back().RequestToken;
    auto signedZeroModel = model;
    signedZeroModel.UVs[0].x = -0.0f;
    static_cast<void>(submit(signedZeroModel));
    EXPECT_NE(requests.back().RequestToken, referenceToken);
    static_cast<void>(submit(model));
    EXPECT_EQ(requests.back().RequestToken, referenceToken);

    model.View.BackgroundMode =
        Runtime::ParameterizationUvBackgroundMode::Checker;
    (void)submit(model);
    EXPECT_NE(requests.back().RequestToken, referenceToken);
    const std::uint64_t backgroundToken = requests.back().RequestToken;

    model.View.ShowDistortionHeatmap = true;
    (void)submit(model);
    EXPECT_NE(requests.back().RequestToken, backgroundToken);
    const std::uint64_t heatmapToken = requests.back().RequestToken;

    model.LineIndices[0] = 2u;
    (void)submit(model);
    EXPECT_NE(requests.back().RequestToken, heatmapToken);
    const std::uint64_t topologyToken = requests.back().RequestToken;

    model.TriangleConformalDistortion[0] = 3.0f;
    (void)submit(model);
    EXPECT_NE(requests.back().RequestToken, topologyToken);
    const std::uint64_t diagnosticToken = requests.back().RequestToken;

    model.SelectedStableEntityId = 30u;
    (void)submit(model);
    EXPECT_NE(requests.back().RequestToken, diagnosticToken);
}

TEST(ParameterizationOperations, CustomBindingsPreserveBothTextureChannelsThroughUndo)
{
    ParameterizationHarness h;
    auto& props = h.Vertices().Properties;
    auto alternate = props.GetOrAdd<glm::vec3>("v:rest", {});
    alternate.Vector() = props.Get<glm::vec3>("v:position").Vector();
    for (auto& p : alternate.Vector()) p *= 2.f;
    auto canonical = props.GetOrAdd<glm::vec2>("v:texcoord", {0.2f, 0.7f});
    auto corners = h.Halfedges().Properties.GetOrAdd<glm::vec2>("h:texcoord", {0.9f, 0.3f});
    const auto originalUvs = canonical.Vector(), originalCorners = corners.Vector();
    Runtime::EditorParameterizationCommand command{.StableEntityId=h.StableEntityId};
    command.Config.Strategy = Runtime::ParameterizationStrategyKind::HarmonicCotangent;
    command.Config.Positions.Name = "v:rest";
    command.Config.Texcoords.Name = "v:custom_uv";
    Config::EngineConfig engine;
    Runtime::SetParameterizationConfig(engine, command.Config);
    const auto decoded = Runtime::GetParameterizationConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Positions.Name, "v:rest");
    EXPECT_EQ(decoded->Texcoords.Name, "v:custom_uv");
    const auto result = Runtime::ApplyEditorParameterizationCommand(h.Context, command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(props.Exists("v:custom_uv"));
    EXPECT_EQ(canonical.Vector(), originalUvs);
    EXPECT_EQ(corners.Vector(), originalCorners);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(props.Exists("v:custom_uv"));
    EXPECT_EQ(canonical.Vector(), originalUvs);
    EXPECT_EQ(corners.Vector(), originalCorners);
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_TRUE(props.Exists("v:custom_uv"));
}


TEST(ParameterizationOperations, ArbitraryNamesWorkAndStructuralOutputsFailClosed)
{
    ParameterizationHarness harness;
    auto& properties = harness.Vertices().Properties;
    auto positions = properties.Get<glm::vec3>("v:position");
    properties.GetOrAdd<glm::vec3>("samples").Vector() = positions.Vector();
    properties.Remove(positions);
    Runtime::ParameterizationConfig config;
    config.Positions.Name = "samples";
    config.Texcoords.Name = "coordinates";
    auto result = Runtime::ApplyEditorParameterizationCommand(harness.Context,
        {.StableEntityId = harness.StableEntityId, .Config = config});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    ASSERT_TRUE(properties.Get<glm::vec2>("coordinates"));
    ASSERT_FALSE(properties.Exists("v:position"));
    const auto history = harness.History.UndoCount();
    for (const auto* name : {"v:position", "v:deleted", "v:connectivity", "v:halfedge"})
    {
        SCOPED_TRACE(name);
        config.Texcoords.Name = name;
        const bool existed = properties.Exists(name);
        result = Runtime::ApplyEditorParameterizationCommand(harness.Context,
            {.StableEntityId = harness.StableEntityId, .Config = config});
        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(properties.Exists(name), existed);
        EXPECT_EQ(harness.History.UndoCount(), history);
    }
}

TEST(ParameterizationOperations, ExplicitCornerBindingRetiresOnlyItsPropertyAndRoundTrips)
{
    ParameterizationHarness h;
    auto& corners = h.Halfedges().Properties;
    auto untouched = corners.GetOrAdd<glm::vec2>("h:texcoord", {0.9f, 0.3f});
    const auto originalUntouched = untouched.Vector();
    const auto originalRetired = corners.GetOrAdd<glm::vec2>("atlas_corners", {0.2f, 0.7f}).Vector();
    auto config = MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform);
    config.Texcoords.Name = "custom_uv";
    config.CornerTexcoordsToRetire = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::MeshHalfedge, "atlas_corners", Geometry::PropertyValueKind::Vec2};
    Config::EngineConfig engine;
    Runtime::SetParameterizationConfig(engine, config);
    const auto decoded = Runtime::GetParameterizationConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->CornerTexcoordsToRetire, config.CornerTexcoordsToRetire);
    ASSERT_TRUE(Runtime::ApplyEditorParameterizationCommand(h.Context,
        {.StableEntityId = h.StableEntityId, .Config = *decoded}).Succeeded());
    EXPECT_FALSE(corners.Exists("atlas_corners"));
    EXPECT_EQ(corners.Get<glm::vec2>("h:texcoord").Vector(), originalUntouched);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(corners.Get<glm::vec2>("atlas_corners").Vector(), originalRetired);
    EXPECT_EQ(corners.Get<glm::vec2>("h:texcoord").Vector(), originalUntouched);
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_FALSE(corners.Exists("atlas_corners"));
}

TEST(ParameterizationOperations, CornerRetirementRejectsWrongDomainKindAndStructuralStorage)
{
    for (const auto& ref : {
             Runtime::GeometryPropertyRef{Runtime::GeometryElementDomain::MeshVertex, "v:texcoord", Geometry::PropertyValueKind::Vec2},
             Runtime::GeometryPropertyRef{Runtime::GeometryElementDomain::MeshHalfedge, "corners", Geometry::PropertyValueKind::Vec3},
             Runtime::GeometryPropertyRef{Runtime::GeometryElementDomain::MeshHalfedge, "h:face", Geometry::PropertyValueKind::Vec2},
             Runtime::GeometryPropertyRef{Runtime::GeometryElementDomain::MeshHalfedge, "", Geometry::PropertyValueKind::Vec2}})
    {
        ParameterizationHarness h;
        auto config = MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform);
        config.CornerTexcoordsToRetire = ref;
        const auto result = Runtime::ApplyEditorParameterizationCommand(h.Context,
            {.StableEntityId = h.StableEntityId, .Config = config});
        EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
        EXPECT_FALSE(h.Uvs());
        EXPECT_EQ(h.History.UndoCount(), 0u);
    }
}

TEST(ParameterizationOperations, PlainConfigPreservesCornersAndDefaultSectionAuthorsRetirement)
{
    Runtime::ParameterizationConfig config;
    EXPECT_FALSE(config.CornerTexcoordsToRetire);
    const auto registration = Runtime::MakeParameterizationConfigSectionRegistration();
    Config::EngineConfig engine;
    Config::UpsertEngineConfigSection(engine.AppSections, registration.DefaultSection);
    const auto authored = Runtime::GetParameterizationConfig(engine);
    ASSERT_TRUE(authored);
    ASSERT_TRUE(authored->CornerTexcoordsToRetire);
    EXPECT_EQ(authored->CornerTexcoordsToRetire->Domain, Runtime::GeometryElementDomain::MeshHalfedge);
    EXPECT_EQ(authored->CornerTexcoordsToRetire->Name, "h:texcoord");
    EXPECT_EQ(authored->CornerTexcoordsToRetire->ValueKind, Geometry::PropertyValueKind::Vec2);
}

TEST(ParameterizationOperations, UndoRejectsChangesToTheRetiredCornerBinding)
{
    ParameterizationHarness h;
    auto& corners = h.Halfedges().Properties;
    (void)corners.GetOrAdd<glm::vec2>("retired_uv", {0.2f, 0.7f});
    auto config = MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform);
    config.CornerTexcoordsToRetire = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::MeshHalfedge, "retired_uv", Geometry::PropertyValueKind::Vec2};
    ASSERT_TRUE(Runtime::ApplyEditorParameterizationCommand(h.Context,
        {.StableEntityId = h.StableEntityId, .Config = config}).Succeeded());
    (void)corners.GetOrAdd<glm::vec2>("retired_uv", {0.8f, 0.1f});
    EXPECT_FALSE(h.History.Undo().Succeeded());
    EXPECT_TRUE(h.Uvs());
    EXPECT_EQ(corners.Get<glm::vec2>("retired_uv")[0], glm::vec2(0.8f, 0.1f));
}


TEST(ParameterizationOperations, AtlasConfigRoundTripsAndRejectsUnusableBudgets)
{
    Runtime::ParameterizationConfig config{};
    config.Atlas.Guide = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::MeshFace, "measured_saliency", Geometry::PropertyValueKind::Double};
    config.Atlas.Distortion = Geometry::UvAtlas::UvAtlasDistortion::Area;
    config.Atlas.RegionCount = 3u;
    config.Atlas.Resolution = 2048u;
    config.Atlas.MaxCharts = 512u;
    config.View.AtlasOnLeft = true;
    config.View.SplitRatio = 0.4f;
    Config::EngineConfig document{};
    Runtime::SetParameterizationConfig(document, config);
    const auto restored = Runtime::GetParameterizationConfig(document);
    ASSERT_TRUE(restored);
    EXPECT_EQ(Runtime::SerializeParameterizationConfig(*restored), Runtime::SerializeParameterizationConfig(config));
    for (const float split : {0.2f, 0.8f})
    {
        config.View.SplitRatio = split;
        Runtime::SetParameterizationConfig(document, config);
        const auto edge = Runtime::GetParameterizationConfig(document);
        ASSERT_TRUE(edge);
        EXPECT_FLOAT_EQ(edge->View.SplitRatio, split);
    }
    for (const auto payload : {
        R"({"atlas":{"resolution":0}})", R"({"atlas":{"resolution":16,"padding":8}})",
        R"({"atlas":{"max_iterations":0}})", R"({"atlas":{"max_conformal_distortion":0.9}})",
        R"({"atlas":{"method":"imaginary"}})", R"({"atlas":{"distortion":"imaginary"}})",
        R"({"atlas":{"method":"xatlas","distortion":"both"}})",
        R"({"view":{"split_ratio":0.0}})"})
        EXPECT_EQ(Runtime::ValidateParameterizationConfigSection(payload,
            Runtime::SerializeParameterizationConfig(config), "parameterization").State,
            Config::EngineConfigState::Invalid) << payload;
}

TEST(ParameterizationOperations, GuidedAtlasPreservesSourceSlotsAndUnrelatedCornerAttributes)
{
    ParameterizationHarness harness{};
    auto& faces = harness.Scene.Raw().get<GS::Faces>(harness.Entity).Properties;
    auto guide = faces.GetOrAdd<double>("arbitrary_scalar", 0.0);
    for (std::size_t i = 0; i < guide.Vector().size(); ++i)
        guide[i] = i < guide.Vector().size() / 2u ? -5.0 : 5.0;
    auto untouched = harness.Halfedges().Properties.GetOrAdd<glm::vec3>("custom_corner", glm::vec3{0.0f});
    for (std::size_t i = 0; i < untouched.Vector().size(); ++i)
        untouched[i] = {static_cast<float>(i), -3.0f, 0.125f};
    const auto expectedCorner = untouched.Vector();
    const auto expectedGuide = guide.Vector();
    const auto expectedFaceHalfedge = faces.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge).Vector();
    const auto expectedToVertex = harness.Halfedges().Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeToVertex).Vector();
    const auto expectedNext = harness.Halfedges().Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext).Vector();
    const auto expectedVertexCount = harness.Vertices().Properties.Size();
    const auto expectedFaceCount = faces.Size();
    const auto expectedHalfedgeCount = harness.Halfedges().Properties.Size();
    Runtime::EditorUvRegenerationCommand command{};
    command.StableEntityId = harness.StableEntityId;
    command.Atlas.Resolution = 256u;
    command.Atlas.Guide = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::MeshFace, "arbitrary_scalar", Geometry::PropertyValueKind::Double};
    command.Atlas.RegionCount = 2u;
    const auto result = Runtime::ApplyEditorUvRegenerationCommand(harness.Context, command);
    ASSERT_TRUE(result.Succeeded()) << result.Diagnostic;
    EXPECT_EQ(result.StableEntityId, harness.StableEntityId);
    EXPECT_GE(result.RegionCount, 2u);
    EXPECT_GE(result.ChartCount, result.RegionCount);
    EXPECT_EQ(harness.Vertices().Properties.Size(), expectedVertexCount);
    EXPECT_EQ(faces.Size(), expectedFaceCount);
    EXPECT_EQ(harness.Halfedges().Properties.Size(), expectedHalfedgeCount);
    EXPECT_EQ(harness.Halfedges().Properties.Get<glm::vec3>("custom_corner").Vector(), expectedCorner);
    EXPECT_EQ(faces.Get<double>("arbitrary_scalar").Vector(), expectedGuide);
    EXPECT_EQ(faces.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge).Vector(), expectedFaceHalfedge);
    EXPECT_EQ(harness.Halfedges().Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeToVertex).Vector(), expectedToVertex);
    EXPECT_EQ(harness.Halfedges().Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext).Vector(), expectedNext);
    const auto regions = faces.Get<std::uint32_t>("f:atlas_region");
    const auto charts = faces.Get<std::uint32_t>("f:atlas_chart");
    ASSERT_TRUE(regions);
    ASSERT_TRUE(charts);
    for (std::size_t i = 0; i < expectedFaceCount; ++i)
    {
        EXPECT_LT(regions[i], result.RegionCount);
        EXPECT_LT(charts[i], result.ChartCount);
        for (std::size_t j = 0; j < i; ++j)
            if (charts[i] == charts[j]) EXPECT_EQ(regions[i], regions[j]);
    }
    const auto view = Runtime::BuildEditorParameterizationViewModel(harness.Context, harness.Results);
    EXPECT_TRUE(view.HasUvCoordinates);
    EXPECT_EQ(view.Triangles.size(), expectedFaceCount);
    EXPECT_EQ(harness.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(harness.Halfedges().Properties.Get<glm::vec3>("custom_corner").Vector(), expectedCorner);
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));
    EXPECT_FALSE(harness.Halfedges().Properties.Exists("h:texcoord"));
    EXPECT_FALSE(faces.Exists("f:atlas_region"));
    EXPECT_FALSE(faces.Exists("f:atlas_chart"));
}

TEST(ParameterizationOperations, AtlasGuideRejectsPrecisionLossAndWrongDomainBeforePublication)
{
    ParameterizationHarness harness{};
    (void)harness.Vertices().Properties.GetOrAdd<std::uint64_t>("precise_ids", 9007199254740993ull);
    Runtime::EditorUvRegenerationCommand command{};
    command.StableEntityId = harness.StableEntityId;
    command.Atlas.Resolution = 256u;
    command.Atlas.Guide = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::MeshVertex, "precise_ids", Geometry::PropertyValueKind::UInt64};
    const auto result = Runtime::ApplyEditorUvRegenerationCommand(harness.Context, command);
    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_NE(result.Diagnostic.find("exact numeric"), std::string::npos);
    EXPECT_EQ(harness.History.UndoCount(), 0u);
    command.Atlas.Guide->Domain = Runtime::GeometryElementDomain::MeshHalfedge;
    const auto readiness = Runtime::PreviewEditorUvRegenerationCommand(harness.Context, command);
    EXPECT_FALSE(readiness.Enabled);
}

TEST(ParameterizationOperations, GeneratedAtlasExtentFollowsItsUvsThroughHistory)
{
    ParameterizationHarness harness{};
    auto& raw = harness.Scene.Raw();
    const auto regenerate = [&harness](const std::uint32_t resolution)
    {
        Runtime::EditorUvRegenerationCommand command{};
        command.StableEntityId = harness.StableEntityId;
        command.Atlas.Resolution = resolution;
        return Runtime::ApplyEditorUvRegenerationCommand(harness.Context, command);
    };
    const auto extent = [&raw, &harness]
    {
        const auto recorded = Runtime::FindCurrentMeshUvAtlasExtent(raw, harness.Entity);
        return recorded ? glm::uvec2{recorded->Width, recorded->Height} : glm::uvec2{0u};
    };

    // Arbitrary, non-power-of-two resolutions are recorded exactly.
    const auto large = regenerate(1536u);
    ASSERT_TRUE(large.Succeeded()) << large.Diagnostic;
    EXPECT_EQ(large.AtlasWidth, 1536u);
    EXPECT_EQ(extent(), glm::uvec2(1536u));
    const auto small = regenerate(512u);
    ASSERT_TRUE(small.Succeeded()) << small.Diagnostic;
    EXPECT_EQ(extent(), glm::uvec2(512u));

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(1536u)) << "undo restores the extent together with its UVs";
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(512u));

    // Identical UVs under a different recorded extent are still an undoable change.
    ASSERT_TRUE(Runtime::PublishMeshUvAtlasExtent(raw, harness.Entity, 700u, 700u));
    const std::size_t undoCount = harness.History.UndoCount();
    const auto metadataOnly = regenerate(512u);
    ASSERT_TRUE(metadataOnly.Succeeded()) << metadataOnly.Diagnostic;
    EXPECT_EQ(harness.History.UndoCount(), undoCount + 1u);
    EXPECT_EQ(extent(), glm::uvec2(512u));
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(700u));
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_EQ(regenerate(512u).Status, Runtime::EditorCommandStatus::NoChange)
        << "the same UVs and extent publish nothing";

    // Another UV writer leaves no stale extent to describe its UVs.
    ASSERT_TRUE(Apply(harness, Runtime::ParameterizationStrategyKind::Lscm).Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(0u));
    EXPECT_FALSE(Runtime::RefreshMeshUvAtlasExtent(raw, harness.Entity));
    EXPECT_FALSE(Runtime::RefreshMeshUvAtlasExtent(raw, harness.Entity));
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(512u)) << "an appearance refresh must not destroy undo metadata";
    ASSERT_TRUE(Runtime::RefreshMeshUvAtlasExtent(raw, harness.Entity));
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_FALSE(Runtime::RefreshMeshUvAtlasExtent(raw, harness.Entity));

    ASSERT_TRUE(regenerate(1536u).Succeeded());
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(0u));
    EXPECT_FALSE(Runtime::RefreshMeshUvAtlasExtent(raw, harness.Entity));
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(512u)) << "undo past another regeneration retains the original grid";
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_EQ(extent(), glm::uvec2(1536u));
}

TEST(ParameterizationOperations, PreservationKeepsValidCornerUvsAndRegeneratesOnlyWhenItMust)
{
    ParameterizationHarness harness{};
    auto& raw = harness.Scene.Raw();
    auto& faces = raw.get<GS::Faces>(harness.Entity).Properties;
    auto guide = faces.GetOrAdd<double>("arbitrary_scalar", 0.0);
    for (std::size_t i = 0; i < guide.Vector().size(); ++i)
        guide[i] = i < guide.Vector().size() / 2u ? -5.0 : 5.0;

    // Artist corner UVs with a seam around face 0; no shadow vertex UVs.
    const auto toVertex = harness.Halfedges().Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeToVertex).Vector();
    const auto halfedgeFace = harness.Halfedges().Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeFace).Vector();
    const auto positions = harness.Vertices().Properties.Get<glm::vec3>(GS::PropertyNames::kPosition).Vector();
    std::vector<glm::vec2> authored(toVertex.size());
    for (std::size_t h = 0; h < authored.size(); ++h)
        authored[h] = glm::vec2{positions[toVertex[h]]} * 0.4f + (halfedgeFace[h] == 0u ? glm::vec2{0.1f} : glm::vec2{0.0f});
    harness.Halfedges().Properties.GetOrAdd<glm::vec2>("h:texcoord", glm::vec2{0.0f}).Vector() = authored;
    ASSERT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));
    ASSERT_TRUE(Runtime::PublishMeshUvAtlasExtent(raw, harness.Entity, 777u, 777u));

    Runtime::EditorUvRegenerationCommand preserve{};
    preserve.StableEntityId = harness.StableEntityId;
    preserve.PreserveValidAuthoredUvs = true;
    preserve.ForceRegenerate = false;
    preserve.Atlas.Resolution = 256u;
    const auto kept = Runtime::ApplyEditorUvRegenerationCommand(harness.Context, preserve);
    EXPECT_EQ(kept.Status, Runtime::EditorCommandStatus::NoChange) << kept.Diagnostic;
    EXPECT_EQ(kept.Provenance, Geometry::UvAtlas::UvAtlasProvenance::AuthoredPreserved);
    EXPECT_EQ(harness.History.UndoCount(), 0u);
    EXPECT_EQ(harness.CornerUvs().value_or(std::vector<glm::vec2>{}), authored) << "artist seams are kept bit for bit";
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord")) << "no shadow vertex UVs are resurrected";
    ASSERT_TRUE(Runtime::FindCurrentMeshUvAtlasExtent(raw, harness.Entity).has_value());
    EXPECT_EQ(Runtime::FindCurrentMeshUvAtlasExtent(raw, harness.Entity)->Width, 777u);

    // Preservation cannot honor a region guide, so a guided request regenerates and says why.
    Runtime::EditorUvRegenerationCommand guided = preserve;
    guided.Atlas.Guide = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::MeshFace, "arbitrary_scalar", Geometry::PropertyValueKind::Double};
    guided.Atlas.RegionCount = 2u;
    const auto regioned = Runtime::ApplyEditorUvRegenerationCommand(harness.Context, guided);
    ASSERT_TRUE(regioned.Succeeded()) << regioned.Diagnostic;
    EXPECT_EQ(regioned.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
    EXPECT_NE(regioned.Diagnostic.find("region guide"), std::string::npos) << regioned.Diagnostic;
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(harness.CornerUvs().value_or(std::vector<glm::vec2>{}), authored);
    ASSERT_TRUE(Runtime::FindCurrentMeshUvAtlasExtent(raw, harness.Entity).has_value());

    // The atlas action forces regeneration over valid corners.
    Runtime::EditorUvRegenerationCommand force = preserve;
    force.PreserveValidAuthoredUvs = false;
    force.ForceRegenerate = true;
    const auto forced = Runtime::ApplyEditorUvRegenerationCommand(harness.Context, force);
    ASSERT_TRUE(forced.Succeeded()) << forced.Diagnostic;
    EXPECT_EQ(forced.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_EQ(harness.CornerUvs().value_or(std::vector<glm::vec2>{}), authored);

    // Invalid corners regenerate with a truthful diagnostic.
    auto corners = harness.Halfedges().Properties.Get<glm::vec2>("h:texcoord");
    for (std::size_t h = 0; h < authored.size(); ++h)
        if (halfedgeFace[h] == 0u)
            corners[h] = glm::vec2{0.5f};
    const auto repaired = Runtime::ApplyEditorUvRegenerationCommand(harness.Context, preserve);
    ASSERT_TRUE(repaired.Succeeded()) << repaired.Diagnostic;
    EXPECT_EQ(repaired.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
    EXPECT_NE(repaired.Diagnostic.find("authored corner UVs were not preserved ("), std::string::npos)
        << repaired.Diagnostic;
    EXPECT_EQ(harness.History.UndoCount(), 1u);
}
