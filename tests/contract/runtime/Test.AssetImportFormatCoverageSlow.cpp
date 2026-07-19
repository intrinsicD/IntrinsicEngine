#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SandboxEditorFacades;
import Geometry.HalfedgeMesh.IO;

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

    class OneFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}
    };

    class WaitForConditionApplication final : public Runtime::IApplication
    {
    public:
        explicit WaitForConditionApplication(
            std::function<bool(Runtime::Engine&)> ready,
            std::uint32_t maxFrames = 512u)
            : m_Ready(std::move(ready))
            , m_MaxFrames(maxFrames)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_ObservedFrames;
            if ((m_Ready && m_Ready(engine)) || m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::function<bool(Runtime::Engine&)> m_Ready{};
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    class TempAssetFile final
    {
    public:
        TempAssetFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string{name})
        {
            std::ofstream out(Path, std::ios::binary);
            out << contents;
        }

        ~TempAssetFile()
        {
            std::error_code ignored;
            std::filesystem::remove(Path, ignored);
        }

        std::filesystem::path Path{};
    };

    [[nodiscard]] std::string GridObjText(const std::uint32_t side)
    {
        std::ostringstream out;
        for (std::uint32_t y = 0u; y <= side; ++y)
        {
            for (std::uint32_t x = 0u; x <= side; ++x)
            {
                out << "v " << x << ' ' << y << " 0\n";
            }
        }

        const auto vertex = [side](const std::uint32_t x,
                                   const std::uint32_t y)
        {
            return y * (side + 1u) + x + 1u;
        };
        for (std::uint32_t y = 0u; y < side; ++y)
        {
            for (std::uint32_t x = 0u; x < side; ++x)
            {
                const std::uint32_t v00 = vertex(x, y);
                const std::uint32_t v10 = vertex(x + 1u, y);
                const std::uint32_t v01 = vertex(x, y + 1u);
                const std::uint32_t v11 = vertex(x + 1u, y + 1u);
                out << "f " << v00 << ' ' << v10 << ' ' << v11 << '\n';
                out << "f " << v00 << ' ' << v11 << ' ' << v01 << '\n';
            }
        }
        return out.str();
    }

    [[nodiscard]] std::optional<ECS::EntityHandle> FindFirstEntityWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        std::optional<ECS::EntityHandle> found{};
        auto& raw = registry.Raw();
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (found.has_value() || !raw.all_of<Sel::SelectableTag>(entity))
            {
                return;
            }
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
            {
                found = entity;
            }
        });
        return found;
    }

    void ExpectMeshVertexTexcoordsFinite(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        ASSERT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        ASSERT_NE(view.VertexSource, nullptr);

        auto texcoords =
            view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
        ASSERT_TRUE(texcoords.IsValid());
        ASSERT_EQ(texcoords.Vector().size(), view.VerticesAlive());
        for (const glm::vec2 texcoord : texcoords.Vector())
        {
            EXPECT_TRUE(std::isfinite(texcoord.x));
            EXPECT_TRUE(std::isfinite(texcoord.y));
        }
    }

    void ExpectMeshLacksVertexProperty(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity,
        const std::string_view propertyName)
    {
        auto& raw = registry.Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        ASSERT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        ASSERT_NE(view.VertexSource, nullptr);
        EXPECT_FALSE(view.VertexSource->Properties.Exists(propertyName));
    }

    [[nodiscard]] bool MeshHasVertexProperty(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::string_view propertyName)
    {
        if (!engine.Worlds().Get(engine.ActiveWorld())->IsValid(entity))
        {
            return false;
        }

        auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        return view.Valid() &&
            view.ActiveDomain == GS::Domain::Mesh &&
            view.VertexSource != nullptr &&
            view.VertexSource->Properties.Exists(propertyName);
    }

    [[nodiscard]] bool DirectMeshPostProcessReady(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity)
    {
        return MeshHasVertexProperty(engine, entity, "v:texcoord") &&
            MeshHasVertexProperty(engine, entity, "v:normal") &&
            RequiredEngineService<
                Extrinsic::Runtime::AssetImportPipeline>(engine)
                .GetLastAssetImportEvent()
                .has_value();
    }

    void InstallSandboxDefaultRuntimePolicies(Runtime::Engine& engine)
    {
        auto* const pipeline =
            engine.Services().Find<Runtime::AssetImportPipeline>();
        ASSERT_NE(pipeline, nullptr);

        auto authoring =
            Runtime::MakeSandboxDefaultImportAuthoringPolicies();
        for (auto& desc : authoring)
        {
            ASSERT_TRUE(
                pipeline->RegisterImportEntityAuthoringPolicy(
                    std::move(desc))
                    .IsValid());
        }
        ASSERT_TRUE(
            pipeline->RegisterImportCompletedHandler(
                Runtime::MakeSandboxDefaultImportCompletedHandler(nullptr))
                .IsValid());
        ASSERT_TRUE(
            pipeline->RegisterPostImportProcessor(
                Runtime::MakeSandboxDefaultDirectMeshPostProcessor())
                .IsValid());
    }
}

TEST(RuntimeAssetImportFormatCoverage, DirectMeshEnrichmentCloseDrainsGeneratedGridAndCompletesDeterministically)
{
    constexpr std::uint32_t side = 32u;
    constexpr std::size_t expectedVertexCount =
        static_cast<std::size_t>(side + 1u) *
        static_cast<std::size_t>(side + 1u);
    constexpr std::size_t expectedFaceCount =
        static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 2u;
    TempAssetFile meshFile(
        "bug101_direct_mesh_grid.obj",
        GridObjText(side));

    {
        Runtime::Engine closingEngine(
            HeadlessConfig(),
            std::make_unique<OneFrameApplication>());
        closingEngine.EmplaceModule<Runtime::AsyncWorkModule>();
        closingEngine.EmplaceModule<Runtime::SceneDocumentModule>();
        closingEngine.EmplaceModule<Runtime::AssetWorkflowModule>();
        closingEngine.Initialize();
        InstallSandboxDefaultRuntimePolicies(closingEngine);

        auto imported =
            RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(closingEngine).ImportAssetFromPath(
                Runtime::RuntimeAssetImportRequest{
                    .Path = meshFile.Path.string(),
                    .PayloadKind = Assets::AssetPayloadKind::Mesh,
                });
        ASSERT_TRUE(imported.has_value())
            << static_cast<int>(imported.error());
        const std::optional<ECS::EntityHandle> meshEntity =
            FindFirstEntityWithDomain(
                *closingEngine.Worlds().Get(closingEngine.ActiveWorld()), GS::Domain::Mesh);
        ASSERT_TRUE(meshEntity.has_value());
        ExpectMeshLacksVertexProperty(
            *closingEngine.Worlds().Get(closingEngine.ActiveWorld()), *meshEntity, "v:texcoord");

        ASSERT_FALSE(closingEngine.GetWindow().ShouldClose());
        closingEngine.Run();
        closingEngine.Shutdown();
    }

    std::optional<ECS::EntityHandle> completedEntity{};
    Runtime::Engine completedEngine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&completedEntity](Runtime::Engine& runningEngine)
            {
                return completedEntity.has_value() &&
                    DirectMeshPostProcessReady(
                        runningEngine, *completedEntity);
            },
            4096u));
    completedEngine.EmplaceModule<Runtime::AsyncWorkModule>();
    completedEngine.EmplaceModule<Runtime::SceneDocumentModule>();
    completedEngine.EmplaceModule<Runtime::AssetWorkflowModule>();
    completedEngine.Initialize();
    InstallSandboxDefaultRuntimePolicies(completedEngine);

    auto imported =
        RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(completedEngine).ImportAssetFromPath(
            Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    completedEntity = FindFirstEntityWithDomain(
        *completedEngine.Worlds().Get(completedEngine.ActiveWorld()), GS::Domain::Mesh);
    ASSERT_TRUE(completedEntity.has_value());

    completedEngine.Run();

    ASSERT_TRUE(DirectMeshPostProcessReady(
        completedEngine, *completedEntity));
    const GS::ConstSourceView completed = GS::BuildConstView(
        completedEngine.Worlds().Get(completedEngine.ActiveWorld())->Raw(), *completedEntity);
    ASSERT_TRUE(completed.Valid());
    EXPECT_EQ(completed.ActiveDomain, GS::Domain::Mesh);
    EXPECT_EQ(completed.VerticesAlive(), expectedVertexCount);
    EXPECT_EQ(completed.FacesAlive(), expectedFaceCount);
    ExpectMeshVertexTexcoordsFinite(
        *completedEngine.Worlds().Get(completedEngine.ActiveWorld()), *completedEntity);
    completedEngine.Shutdown();
}
