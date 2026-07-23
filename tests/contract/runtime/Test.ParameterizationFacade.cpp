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
import Extrinsic.Runtime.SandboxConfigSections;
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
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Simulate(double) override {}
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
            Runtime::EngineConfigSectionRegistry sectionRegistry)
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

    [[nodiscard]] Runtime::ParameterizationConfig MakeConfig(
        const Runtime::ParameterizationStrategyKind strategy)
    {
        Runtime::ParameterizationConfig config{};
        config.Strategy = strategy;
        return config;
    }

    [[nodiscard]] Runtime::SandboxEditorParameterizationResult Apply(
        ParameterizationHarness& harness,
        const Runtime::ParameterizationStrategyKind strategy)
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
        harness, Runtime::ParameterizationStrategyKind::HarmonicCotangent);
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
        harness, Runtime::ParameterizationStrategyKind::TutteUniform);
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
        harness, Runtime::ParameterizationStrategyKind::Lscm);
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
    Runtime::SetParameterizationConfig(
        state.ActiveConfig,
        MakeConfig(Runtime::ParameterizationStrategyKind::TutteUniform));
    harness.Context.EngineConfigControlState = &state;

    const Runtime::SandboxEditorParameterizationResult result =
        Runtime::ApplySandboxEditorConfiguredParameterizationCommand(
            harness.Context,
            Runtime::SandboxEditorConfiguredParameterizationCommand{
                .StableEntityId = harness.StableEntityId,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Strategy,
              Runtime::ParameterizationStrategyKind::TutteUniform);
    ASSERT_TRUE(harness.Uvs().has_value());
    EXPECT_TRUE(AllFinite(*harness.Uvs()));
}

TEST(ParameterizationFacade, EditorConfigHelperUsesValidatedHotApplyLane)
{
    ParameterizationHarness harness{};
    Runtime::EngineConfigSectionRegistry registry{};
    ASSERT_TRUE(registry.Register(
        Runtime::MakeParameterizationConfigSectionRegistration()));
    ConfigControlHarness controlHarness{std::move(registry)};
    Runtime::EngineConfigControl& control = controlHarness.Control();
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
                    Runtime::ParameterizationStrategyKind::TutteUniform),
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorParameterizationConfigStatus::Applied);
    EXPECT_EQ(result.Apply.Source,
              Runtime::RuntimeConfigControlSource::Editor);
    EXPECT_TRUE(result.Apply.SectionChanged(
        Runtime::kParameterizationConfigSectionName));
    const auto config =
        Runtime::GetSandboxEditorParameterizationConfig(harness.Context);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->Strategy,
              Runtime::ParameterizationStrategyKind::TutteUniform);
}

TEST(ParameterizationFacade, ConfigSourcesProduceIdenticalStateAndUvs)
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
        Runtime::EngineConfigSectionRegistry registry{};
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
        Apply(first, Runtime::ParameterizationStrategyKind::Bff);
    const auto secondResult =
        Apply(second, Runtime::ParameterizationStrategyKind::Bff);
    ASSERT_TRUE(firstResult.Succeeded()) << firstResult.Message;
    ASSERT_TRUE(secondResult.Succeeded()) << secondResult.Message;
    ASSERT_TRUE(first.Uvs().has_value());
    ASSERT_TRUE(second.Uvs().has_value());
    EXPECT_EQ(*first.Uvs(), *second.Uvs());
}

TEST(ParameterizationFacade,
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

TEST(ParameterizationFacade, InvalidEnumsAndNarrowingFailClosed)
{
    ParameterizationHarness harness{};
    Runtime::ParameterizationConfig config{};
    config.Strategy =
        static_cast<Runtime::ParameterizationStrategyKind>(999u);
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
        Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    config.Harmonic.Boundary =
        static_cast<Runtime::ParameterizationBoundaryPolicy>(999u);
    result = Runtime::ApplySandboxEditorParameterizationCommand(
        harness.Context,
        Runtime::SandboxEditorParameterizationCommand{
            .StableEntityId = harness.StableEntityId,
            .Config = config,
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    config = MakeConfig(Runtime::ParameterizationStrategyKind::Bff);
    config.Bff.Mode =
        static_cast<Runtime::ParameterizationBffBoundaryMode>(999u);
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

    Runtime::ParameterizationConfig invalid{};
    invalid.Strategy =
        static_cast<Runtime::ParameterizationStrategyKind>(999u);
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
        static_cast<Runtime::ParameterizationBoundaryPolicy>(999u);
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
        wrongType, Runtime::ParameterizationStrategyKind::Lscm);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_TRUE(wrongType.Vertices().Properties.Get<float>("v:texcoord"));
    EXPECT_FALSE(wrongType.Vertices().Properties.Get<glm::vec2>("v:texcoord"));

    ParameterizationHarness quad{MakeQuadMesh()};
    result = Apply(quad, Runtime::ParameterizationStrategyKind::Lscm);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(quad.Vertices().Properties.Exists("v:texcoord"));
}

TEST(ParameterizationFacade, ViewModelIsPointerFreeAndCarriesAggregateDiagnostics)
{
    ParameterizationHarness harness{};
    const Runtime::SandboxEditorParameterizationResult result = Apply(
        harness, Runtime::ParameterizationStrategyKind::HarmonicCotangent);
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

TEST(ParameterizationFacade, ViewModelFansFaceDiagnosticsIntoRenderedTriangles)
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
    Runtime::SandboxEditorParameterizationResult last{};
    const Runtime::SandboxEditorParameterizationViewModel current =
        Runtime::BuildSandboxEditorParameterizationViewModel(harness.Context);
    ASSERT_TRUE(current.DiagnosticInputFingerprint.has_value());
    last.Status = Runtime::SandboxEditorCommandStatus::Applied;
    last.StableEntityId = harness.StableEntityId;
    last.ParameterizationStatus =
        Geometry::Parameterization::ParameterizationStatus::Success;
    last.DiagnosticInputFingerprint = current.DiagnosticInputFingerprint;
    last.Diagnostics.FaceStorageCount = 1u;
    last.Diagnostics.FaceConformalDistortion = {2.5f};
    harness.Context.LastParameterizationResult = &last;

    const Runtime::SandboxEditorParameterizationViewModel model =
        Runtime::BuildSandboxEditorParameterizationViewModel(harness.Context);
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

TEST(ParameterizationFacade,
     FaceDiagnosticsRequireTheExactGeometryAndUvFingerprint)
{
    ParameterizationHarness harness{};
    Runtime::RuntimeEngineConfigControlState state{};
    Runtime::ParameterizationConfig config{};
    config.View.RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded;
    Runtime::SetParameterizationConfig(state.ActiveConfig, config);
    harness.Context.EngineConfigControlState = &state;

    Runtime::SandboxEditorParameterizationResult last = Apply(
        harness, Runtime::ParameterizationStrategyKind::HarmonicCotangent);
    ASSERT_TRUE(last.Succeeded()) << last.Message;
    ASSERT_TRUE(last.DiagnosticInputFingerprint.has_value());
    last.Diagnostics.FaceConformalDistortion.assign(8u, 2.0f);
    harness.Context.LastParameterizationResult = &last;

    Runtime::SandboxEditorParameterizationViewModel model =
        Runtime::BuildSandboxEditorParameterizationViewModel(harness.Context);
    EXPECT_EQ(model.TriangleConformalDistortion.size(), 8u);
    EXPECT_EQ(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);

    ASSERT_TRUE(harness.History.Undo().Succeeded());
    model = Runtime::BuildSandboxEditorParameterizationViewModel(
        harness.Context);
    EXPECT_FALSE(model.DiagnosticInputFingerprint.has_value());
    EXPECT_TRUE(model.TriangleConformalDistortion.empty());

    ASSERT_TRUE(harness.History.Redo().Succeeded());
    model = Runtime::BuildSandboxEditorParameterizationViewModel(
        harness.Context);
    EXPECT_EQ(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_EQ(model.TriangleConformalDistortion.size(), 8u);

    auto uvs = harness.Vertices().Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(uvs);
    const glm::vec2 originalUv = uvs.Vector().front();
    uvs.Vector().front().x = originalUv.x + 0.125f;
    model = Runtime::BuildSandboxEditorParameterizationViewModel(
        harness.Context);
    ASSERT_TRUE(model.DiagnosticInputFingerprint.has_value());
    EXPECT_NE(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_TRUE(model.TriangleConformalDistortion.empty());

    uvs.Vector().front() = originalUv;
    model = Runtime::BuildSandboxEditorParameterizationViewModel(
        harness.Context);
    EXPECT_EQ(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_EQ(model.TriangleConformalDistortion.size(), 8u);

    auto positions = harness.Vertices().Properties.Get<glm::vec3>(
        GS::PropertyNames::kPosition);
    ASSERT_TRUE(positions);
    positions.Vector().front().x += 0.25f;
    model = Runtime::BuildSandboxEditorParameterizationViewModel(
        harness.Context);
    ASSERT_TRUE(model.DiagnosticInputFingerprint.has_value());
    EXPECT_NE(model.DiagnosticInputFingerprint,
              last.DiagnosticInputFingerprint);
    EXPECT_TRUE(model.TriangleConformalDistortion.empty());
}

TEST(ParameterizationFacade,
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

    Runtime::SandboxEditorParameterizationResult result = Apply(
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

    harness.Context.LastParameterizationResult = &result;
    const Runtime::SandboxEditorParameterizationViewModel model =
        Runtime::BuildSandboxEditorParameterizationViewModel(harness.Context);
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

TEST(ParameterizationFacade, CpuViewDisablesGpuWorkAndReportsBackgroundFallback)
{
    Runtime::SandboxEditorContext context{};
    std::vector<Runtime::SandboxEditorParameterizationUvViewRequest> requests{};
    context.ParameterizationUvViewCommands.Submit =
        [&requests](Runtime::SandboxEditorParameterizationUvViewRequest request)
        {
            requests.push_back(request);
            return Runtime::SandboxEditorParameterizationUvViewState{};
        };

    Runtime::SandboxEditorParameterizationViewModel model{};
    model.View.RenderMode = Runtime::ParameterizationUvRenderMode::CpuLayout;
    for (const Runtime::ParameterizationUvBackgroundMode background : {
             Runtime::ParameterizationUvBackgroundMode::Texture,
             Runtime::ParameterizationUvBackgroundMode::TexelDensity})
    {
        model.View.BackgroundMode = background;
        const Runtime::SandboxEditorParameterizationUvViewState state =
            Runtime::SubmitSandboxEditorParameterizationUvView(
                context, model, 320u, 180u);
        ASSERT_FALSE(requests.empty());
        EXPECT_FALSE(requests.back().Enabled);
        EXPECT_EQ(requests.back().Width, 320u);
        EXPECT_EQ(requests.back().Height, 180u);
        EXPECT_EQ(
            state.Status,
            Runtime::SandboxEditorParameterizationUvViewStatus::CpuLayout);
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

TEST(ParameterizationFacade, GpuViewWithoutCommandSurfaceReportsCpuFallback)
{
    Runtime::SandboxEditorContext context{};
    Runtime::SandboxEditorParameterizationViewModel model{};
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

    const Runtime::SandboxEditorParameterizationUvViewState state =
        Runtime::SubmitSandboxEditorParameterizationUvView(
            context, model, 400u, 240u);
    EXPECT_EQ(
        state.Status,
        Runtime::SandboxEditorParameterizationUvViewStatus::CpuFallbackNonOperational);
    EXPECT_EQ(state.RequestedMode,
              Runtime::ParameterizationUvRenderMode::GpuShaded);
    EXPECT_EQ(state.ActiveMode,
              Runtime::ParameterizationUvRenderMode::CpuLayout);
    EXPECT_EQ(
        state.ActiveBackground,
        Runtime::ParameterizationUvBackgroundMode::Checker);
    EXPECT_FALSE(state.GpuReady);
}

TEST(ParameterizationFacade, GpuViewRequestTokenIsStableAndSemantic)
{
    Runtime::SandboxEditorContext context{};
    std::vector<Runtime::SandboxEditorParameterizationUvViewRequest> requests{};
    context.ParameterizationUvViewCommands.Submit =
        [&requests](Runtime::SandboxEditorParameterizationUvViewRequest request)
        {
            requests.push_back(request);
            return Runtime::SandboxEditorParameterizationUvViewState{
                .Status =
                    Runtime::SandboxEditorParameterizationUvViewStatus::Ready,
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

    Runtime::SandboxEditorParameterizationViewModel model{};
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

    const auto submit = [&context](
                            const Runtime::SandboxEditorParameterizationViewModel& value)
    {
        return Runtime::SubmitSandboxEditorParameterizationUvView(
            context, value, 640u, 360u);
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
              Runtime::SandboxEditorParameterizationUvViewStatus::Ready);
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
