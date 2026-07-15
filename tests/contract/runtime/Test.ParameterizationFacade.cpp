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

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.SandboxEditorFacades;
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

    struct ParameterizationHarness
    {
        ECS::Scene::Registry Scene{};
        Runtime::SelectionController Selection{};
        Runtime::EditorCommandHistory History{};
        Runtime::SandboxEditorSelectedModelCache ModelCache{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        Runtime::SandboxEditorContext Context{};

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

    [[nodiscard]] Config::ParameterizationConfig MakeConfig(
        const Config::ParameterizationStrategyKind strategy)
    {
        Config::ParameterizationConfig config{};
        config.Strategy = strategy;
        return config;
    }

    [[nodiscard]] Runtime::SandboxEditorParameterizationResult Apply(
        ParameterizationHarness& harness,
        const Config::ParameterizationStrategyKind strategy)
    {
        return Runtime::ApplySandboxEditorParameterizationCommand(
            harness.Context,
            Runtime::SandboxEditorParameterizationCommand{
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

TEST(ParameterizationFacade, StableTokensAndAllImplementedStrategiesWriteFiniteUvs)
{
    constexpr std::array strategies{
        Config::ParameterizationStrategyKind::Lscm,
        Config::ParameterizationStrategyKind::HarmonicCotangent,
        Config::ParameterizationStrategyKind::TutteUniform,
        Config::ParameterizationStrategyKind::Bff,
    };
    constexpr std::array<std::string_view, 4u> tokens{
        "lscm", "harmonic_cotangent", "tutte_uniform", "bff"};

    for (std::size_t i = 0u; i < strategies.size(); ++i)
    {
        SCOPED_TRACE(tokens[i]);
        ParameterizationHarness harness{};
        const Runtime::SandboxEditorParameterizationResult result =
            Apply(harness, strategies[i]);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.Strategy, strategies[i]);
        EXPECT_EQ(result.StrategyToken, tokens[i]);
        EXPECT_EQ(
            Runtime::StableTokenForSandboxEditorParameterizationStrategy(
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

TEST(ParameterizationFacade, UndoRedoRestoresAbsentUvProperty)
{
    ParameterizationHarness harness{};
    ASSERT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    const Runtime::SandboxEditorParameterizationResult result = Apply(
        harness, Config::ParameterizationStrategyKind::HarmonicCotangent);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const std::vector<glm::vec2> generated = *harness.Uvs();

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    ASSERT_TRUE(harness.History.Redo().Succeeded());
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), generated);
}

TEST(ParameterizationFacade, UndoRedoRestoresPresentUvValues)
{
    ParameterizationHarness harness{};
    const std::vector<glm::vec2> authored(
        harness.Vertices().Properties.Size(),
        glm::vec2{0.25f, 0.75f});
    harness.Vertices()
        .Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f})
        .Vector() = authored;

    const Runtime::SandboxEditorParameterizationResult result = Apply(
        harness, Config::ParameterizationStrategyKind::TutteUniform);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const std::vector<glm::vec2> generated = *harness.Uvs();
    EXPECT_NE(generated, authored);

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_EQ(*harness.Uvs(), authored);

    ASSERT_TRUE(harness.History.Redo().Succeeded());
    EXPECT_EQ(*harness.Uvs(), generated);
}

TEST(ParameterizationFacade, HistoryDoesNotRetainSessionOwnedModelCache)
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
            std::make_unique<Runtime::SandboxEditorSelectedModelCache>();
        Runtime::SandboxEditorContext context{};
        context.Scene = &scene;
        context.Selection = &selection;
        context.CommandHistory = &history;
        context.SelectedModelCache = modelCache.get();
        const Runtime::SandboxEditorParameterizationResult result =
            Runtime::ApplySandboxEditorParameterizationCommand(
                context,
                Runtime::SandboxEditorParameterizationCommand{
                    .StableEntityId = stableEntityId,
                    .Config = MakeConfig(
                        Config::ParameterizationStrategyKind::Lscm),
                });
        ASSERT_TRUE(result.Succeeded()) << result.Message;
    }

    ASSERT_TRUE(history.Undo().Succeeded());
    const GS::Vertices& vertices = scene.Raw().get<GS::Vertices>(entity);
    EXPECT_FALSE(vertices.Properties.Exists("v:texcoord"));
    ASSERT_TRUE(history.Redo().Succeeded());
    EXPECT_TRUE(vertices.Properties.Get<glm::vec2>("v:texcoord"));
}

TEST(ParameterizationFacade, DeletedVertexTombstonePreservesStorageUvs)
{
    Geometry::HalfedgeMesh::Mesh mesh = MakeGridMesh();
    const Geometry::VertexHandle tombstone =
        mesh.AddVertex(glm::vec3{8.0f, 8.0f, 8.0f});
    mesh.DeleteVertex(tombstone);
    ASSERT_EQ(mesh.VerticesSize(), 10u);
    ASSERT_EQ(mesh.DeletedVertexCount(), 1u);

    ParameterizationHarness harness{mesh};
    const Runtime::SandboxEditorParameterizationResult result = Apply(
        harness, Config::ParameterizationStrategyKind::Lscm);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.VertexCount, 10u);
    ASSERT_TRUE(harness.Uvs().has_value());
    ASSERT_EQ(harness.Uvs()->size(), 10u);
    EXPECT_EQ((*harness.Uvs())[tombstone.Index], glm::vec2(0.0f));
}

TEST(ParameterizationFacade, ConfiguredPathReadsActiveEngineConfig)
{
    ParameterizationHarness harness{};
    Runtime::RuntimeEngineConfigControlState state{};
    state.ActiveConfig.Sandbox.Parameterization =
        MakeConfig(Config::ParameterizationStrategyKind::TutteUniform);
    harness.Context.EngineConfigControlState = &state;

    const Runtime::SandboxEditorParameterizationResult result =
        Runtime::ApplySandboxEditorConfiguredParameterizationCommand(
            harness.Context,
            Runtime::SandboxEditorConfiguredParameterizationCommand{
                .StableEntityId = harness.StableEntityId,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Strategy,
              Config::ParameterizationStrategyKind::TutteUniform);
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_TRUE(AllFinite(*harness.Uvs()));
}

TEST(ParameterizationFacade, EditorConfigHelperUsesValidatedHotApplyLane)
{
    ParameterizationHarness harness{};
    Config::EngineConfig active{};
    Runtime::EngineConfigControl control{
        Runtime::EngineConfigControlDependencies{
            .Config = &active,
        }};
    harness.Context.EngineConfigControlState =
        &control.GetEngineConfigControlState();
    harness.Context.EngineConfigCommandsAvailable = true;
    harness.Context.PreviewEngineConfigDocument =
        [&control](const std::string& document, const std::string& sourceId)
        {
            return control.PreviewEngineConfigControlDocument(
                document, sourceId);
        };
    harness.Context.ApplyEngineConfigHotSubset =
        [&control](const Config::EngineConfigLoadResult& preview)
        {
            return control.ApplyEngineConfigHotSubset(
                preview,
                Runtime::RuntimeConfigControlSource::Editor);
        };

    const Runtime::SandboxEditorParameterizationConfigResult result =
        Runtime::ApplySandboxEditorParameterizationConfigCommand(
            harness.Context,
            Runtime::SandboxEditorParameterizationConfigCommand{
                .Config = MakeConfig(
                    Config::ParameterizationStrategyKind::TutteUniform),
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorParameterizationConfigStatus::Applied);
    EXPECT_EQ(result.Apply.Source,
              Runtime::RuntimeConfigControlSource::Editor);
    EXPECT_TRUE(result.Apply.SandboxParameterizationChanged);
    const auto config =
        Runtime::GetSandboxEditorParameterizationConfig(harness.Context);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->Strategy,
              Config::ParameterizationStrategyKind::TutteUniform);
}

TEST(ParameterizationFacade, ConfigSourcesProduceIdenticalStateAndUvs)
{
    constexpr std::array sources{
        Runtime::RuntimeConfigControlSource::Editor,
        Runtime::RuntimeConfigControlSource::AgentCli,
        Runtime::RuntimeConfigControlSource::Programmatic,
    };
    const Config::ParameterizationConfig parameterization =
        MakeConfig(Config::ParameterizationStrategyKind::TutteUniform);
    std::optional<std::string> referenceSerializedConfig{};
    std::optional<std::vector<glm::vec2>> referenceUvs{};

    for (const Runtime::RuntimeConfigControlSource source : sources)
    {
        ParameterizationHarness harness{};
        Config::EngineConfig active{};
        Runtime::EngineConfigControl control{
            Runtime::EngineConfigControlDependencies{
                .Config = &active,
            }};
        Config::EngineConfig candidate = active;
        candidate.Sandbox.Parameterization = parameterization;
        const Config::EngineConfigLoadResult preview =
            control.PreviewEngineConfigControlDocument(
                Config::SerializeEngineConfig(candidate),
                "parameterization-source-parity");
        ASSERT_TRUE(Config::IsConfigUsable(preview));
        const Runtime::RuntimeEngineConfigApplyResult applied =
            control.ApplyEngineConfigHotSubset(preview, source);
        ASSERT_TRUE(applied.Succeeded());
        EXPECT_EQ(applied.Source, source);
        EXPECT_TRUE(applied.SandboxParameterizationChanged);

        const Runtime::RuntimeEngineConfigControlState& state =
            control.GetEngineConfigControlState();
        EXPECT_EQ(state.ActiveConfig.Sandbox.Parameterization.Strategy,
                  Config::ParameterizationStrategyKind::TutteUniform);
        const std::string serialized =
            Config::SerializeEngineConfig(state.ActiveConfig);
        if (!referenceSerializedConfig.has_value())
            referenceSerializedConfig = serialized;
        else
            EXPECT_EQ(serialized, *referenceSerializedConfig);

        harness.Context.EngineConfigControlState = &state;
        const Runtime::SandboxEditorParameterizationResult result =
            Runtime::ApplySandboxEditorConfiguredParameterizationCommand(
                harness.Context,
                Runtime::SandboxEditorConfiguredParameterizationCommand{
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

TEST(ParameterizationFacade, IdenticalInputAndConfigAreDeterministic)
{
    ParameterizationHarness first{};
    ParameterizationHarness second{};
    const auto firstResult =
        Apply(first, Config::ParameterizationStrategyKind::Bff);
    const auto secondResult =
        Apply(second, Config::ParameterizationStrategyKind::Bff);
    ASSERT_TRUE(firstResult.Succeeded()) << firstResult.Message;
    ASSERT_TRUE(secondResult.Succeeded()) << secondResult.Message;
    ASSERT_TRUE(first.Uvs().has_value());
    ASSERT_TRUE(second.Uvs().has_value());
    EXPECT_EQ(*first.Uvs(), *second.Uvs());
}

TEST(ParameterizationFacade, InvalidEnumsAndNarrowingFailClosed)
{
    ParameterizationHarness harness{};
    Config::ParameterizationConfig config{};
    config.Strategy =
        static_cast<Config::ParameterizationStrategyKind>(999u);
    auto result = Runtime::ApplySandboxEditorParameterizationCommand(
        harness.Context,
        Runtime::SandboxEditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    config = {};
    config.Lscm.PinUv0.U = std::numeric_limits<double>::max();
    result = Runtime::ApplySandboxEditorParameterizationCommand(
        harness.Context,
        Runtime::SandboxEditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(harness.Vertices().Properties.Exists("v:texcoord"));

    config = {};
    config.Lscm.SolverTolerance = 1.0e31;
    result = Runtime::ApplySandboxEditorParameterizationCommand(
        harness.Context,
        Runtime::SandboxEditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    config = MakeConfig(
        Config::ParameterizationStrategyKind::HarmonicCotangent);
    config.Harmonic.Boundary =
        static_cast<Config::ParameterizationBoundaryPolicy>(999u);
    result = Runtime::ApplySandboxEditorParameterizationCommand(
        harness.Context,
        Runtime::SandboxEditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    config = MakeConfig(Config::ParameterizationStrategyKind::Bff);
    config.Bff.Mode =
        static_cast<Config::ParameterizationBffBoundaryMode>(999u);
    result = Runtime::ApplySandboxEditorParameterizationCommand(
        harness.Context,
        Runtime::SandboxEditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
}

TEST(ParameterizationFacade, InvalidConfigEditDoesNotSerializeFallbackToken)
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

    Config::ParameterizationConfig invalid{};
    invalid.Strategy =
        static_cast<Config::ParameterizationStrategyKind>(999u);
    const Runtime::SandboxEditorParameterizationConfigResult result =
        Runtime::ApplySandboxEditorParameterizationConfigCommand(
            harness.Context,
            Runtime::SandboxEditorParameterizationConfigCommand{
                .Config = invalid,
            });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(
        result.Status,
        Runtime::SandboxEditorParameterizationConfigStatus::PreviewRejected);
    EXPECT_FALSE(previewCalled);

    invalid = {};
    invalid.Harmonic.Boundary =
        static_cast<Config::ParameterizationBoundaryPolicy>(999u);
    const Runtime::SandboxEditorParameterizationConfigResult inactiveResult =
        Runtime::ApplySandboxEditorParameterizationConfigCommand(
            harness.Context,
            Runtime::SandboxEditorParameterizationConfigCommand{
                .Config = invalid,
            });
    EXPECT_FALSE(inactiveResult.Succeeded());
    EXPECT_EQ(
        inactiveResult.Status,
        Runtime::SandboxEditorParameterizationConfigStatus::PreviewRejected);
    EXPECT_FALSE(previewCalled);
}

TEST(ParameterizationFacade, WrongTypedUvAndNonTriangleFacesFailClosed)
{
    ParameterizationHarness wrongType{};
    (void)wrongType.Vertices()
        .Properties.GetOrAdd<float>("v:texcoord", 0.0f);
    auto result = Apply(
        wrongType, Config::ParameterizationStrategyKind::Lscm);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_TRUE(wrongType.Vertices().Properties.Get<float>("v:texcoord"));
    EXPECT_FALSE(wrongType.Vertices().Properties.Get<glm::vec2>("v:texcoord"));

    ParameterizationHarness quad{MakeQuadMesh()};
    result = Apply(quad, Config::ParameterizationStrategyKind::Lscm);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(quad.Vertices().Properties.Exists("v:texcoord"));
}

TEST(ParameterizationFacade, ViewModelIsPointerFreeAndCarriesAggregateDiagnostics)
{
    ParameterizationHarness harness{};
    const Runtime::SandboxEditorParameterizationResult result = Apply(
        harness, Config::ParameterizationStrategyKind::HarmonicCotangent);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    harness.Context.LastParameterizationResult = &result;

    const Runtime::SandboxEditorParameterizationViewModel model =
        Runtime::BuildSandboxEditorParameterizationViewModel(harness.Context);
    const Runtime::SandboxEditorParameterizationViewModel repeated =
        Runtime::BuildSandboxEditorParameterizationViewModel(harness.Context);
    EXPECT_TRUE(model.HasSelectedEntity);
    EXPECT_TRUE(model.SelectedEntityIsMesh);
    EXPECT_TRUE(model.HasUvCoordinates);
    EXPECT_TRUE(model.HasFiniteUvBounds);
    EXPECT_TRUE(model.HasLastResult);
    EXPECT_EQ(model.SelectedStableEntityId, harness.StableEntityId);
    EXPECT_EQ(model.Strategy,
              Config::ParameterizationStrategyKind::HarmonicCotangent);
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
