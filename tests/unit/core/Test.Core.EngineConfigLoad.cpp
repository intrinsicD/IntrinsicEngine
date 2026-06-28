#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;

using namespace Extrinsic::Core::Config;

namespace
{
    [[nodiscard]] std::filesystem::path TempConfigPath(const std::string& stem)
    {
        return std::filesystem::temp_directory_path() / (stem + ".json");
    }

    void WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        ASSERT_TRUE(file.is_open());
        file << text;
    }
}

TEST(CoreEngineConfigLoad, SerializesAndLoadsEveryBootField)
{
    EngineConfig config{};
    config.Window.Title = "Engine Config Test";
    config.Window.Width = 1280;
    config.Window.Height = 720;
    config.Window.Resizable = false;
    config.Window.Backend = WindowBackend::Null;
    config.Render.Backend = GraphicsBackend::Vulkan;
    config.Render.EnablePromotedVulkanDevice = true;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    config.Render.FramesInFlight = 3;
    config.Render.SynchronousExtraction = false;
    config.Simulation.WorkerThreadCount = 4;
    config.ReferenceScene.Enabled = true;
    config.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;
    config.Camera.Enabled = false;
    config.Camera.Controller = CameraControllerKind::TopDown;

    const std::string document = SerializeEngineConfig(config);
    const EngineConfigLoadResult preview = PreviewEngineConfig(document, EngineConfig{});

    ASSERT_EQ(preview.State, EngineConfigState::Valid);
    EXPECT_FALSE(HasErrors(preview));
    EXPECT_TRUE(preview.Preview.SideEffectFree);
    EXPECT_EQ(preview.Preview.ParsedFieldCount, 16u);
    EXPECT_EQ(preview.Preview.Config.Window.Title, "Engine Config Test");
    EXPECT_EQ(preview.Preview.Config.Window.Width, 1280);
    EXPECT_EQ(preview.Preview.Config.Window.Height, 720);
    EXPECT_FALSE(preview.Preview.Config.Window.Resizable);
    EXPECT_EQ(preview.Preview.Config.Window.Backend, WindowBackend::Null);
    EXPECT_EQ(preview.Preview.Config.Render.Backend, GraphicsBackend::Vulkan);
    EXPECT_TRUE(preview.Preview.Config.Render.EnablePromotedVulkanDevice);
    EXPECT_FALSE(preview.Preview.Config.Render.EnableValidation);
    EXPECT_FALSE(preview.Preview.Config.Render.EnableVSync);
    EXPECT_EQ(preview.Preview.Config.Render.FramesInFlight, 3u);
    EXPECT_FALSE(preview.Preview.Config.Render.SynchronousExtraction);
    EXPECT_EQ(preview.Preview.Config.Simulation.WorkerThreadCount, 4u);
    EXPECT_TRUE(preview.Preview.Config.ReferenceScene.Enabled);
    EXPECT_EQ(preview.Preview.Config.ReferenceScene.Selector, ReferenceSceneSelector::Triangle);
    EXPECT_FALSE(preview.Preview.Config.Camera.Enabled);
    EXPECT_EQ(preview.Preview.Config.Camera.Controller, CameraControllerKind::TopDown);

    const std::filesystem::path path = TempConfigPath("intrinsic_engine_config_roundtrip");
    WriteTextFile(path, document);
    const EngineConfigLoadResult loaded = LoadEngineConfigFile(path.string(), EngineConfig{});
    std::filesystem::remove(path);

    ASSERT_EQ(loaded.State, EngineConfigState::Valid);
    EXPECT_EQ(loaded.SchemaVersion, kEngineConfigSchemaVersion);
    EXPECT_EQ(loaded.Preview.Config.Window.Width, 1280);
    EXPECT_EQ(loaded.Preview.Config.Render.FramesInFlight, 3u);
    EXPECT_EQ(loaded.Preview.Config.Camera.Controller, CameraControllerKind::TopDown);
}

TEST(CoreEngineConfigLoad, InvalidFieldsFallBackWithDiagnostics)
{
    EngineConfig defaults{};
    defaults.Window.Title = "Reference";
    defaults.Window.Width = 1600;
    defaults.Window.Height = 900;
    defaults.Window.Backend = WindowBackend::Configured;
    defaults.Render.EnablePromotedVulkanDevice = true;
    defaults.Render.EnableValidation = true;
    defaults.Render.EnableVSync = true;
    defaults.Render.FramesInFlight = 2;
    defaults.Render.SynchronousExtraction = true;
    defaults.Simulation.WorkerThreadCount = 6;
    defaults.ReferenceScene.Enabled = true;
    defaults.Camera.Enabled = true;
    defaults.Camera.Controller = CameraControllerKind::Orbit;

    const std::string document = R"json(
{
  "schema": "intrinsic.core.engine-config",
  "version": 1,
  "unexpected_top_level": true,
  "window": {
    "title": "",
    "width": -1,
    "height": 0,
    "resizable": "yes",
    "backend": "Glfw",
    "unused": 1
  },
  "render": {
    "enable_promoted_vulkan_device": false,
    "enable_validation": "true",
    "enable_vsync": false,
    "frames_in_flight": 0,
    "synchronous_extraction": false
  },
  "simulation": {
    "worker_thread_count": -4
  },
  "reference_scene": {
    "enabled": false,
    "selector": "Cube"
  },
  "camera": {
    "enabled": false,
    "controller": "Arcball"
  }
}
)json";

    const EngineConfigLoadResult result = PreviewEngineConfig(document, defaults);

    EXPECT_EQ(result.State, EngineConfigState::FallbackApplied);
    EXPECT_FALSE(HasErrors(result));
    EXPECT_TRUE(IsConfigUsable(result));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::UnknownField));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::InvalidValue));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::FallbackApplied));
    EXPECT_GT(CountByState(result, EngineConfigState::FallbackApplied), 0u);

    EXPECT_EQ(result.Preview.Config.Window.Title, "Reference");
    EXPECT_EQ(result.Preview.Config.Window.Width, 1600);
    EXPECT_EQ(result.Preview.Config.Window.Height, 900);
    EXPECT_EQ(result.Preview.Config.Window.Backend, WindowBackend::Configured);
    EXPECT_FALSE(result.Preview.Config.Render.EnablePromotedVulkanDevice);
    EXPECT_TRUE(result.Preview.Config.Render.EnableValidation);
    EXPECT_FALSE(result.Preview.Config.Render.EnableVSync);
    EXPECT_EQ(result.Preview.Config.Render.FramesInFlight, 2u);
    EXPECT_FALSE(result.Preview.Config.Render.SynchronousExtraction);
    EXPECT_EQ(result.Preview.Config.Simulation.WorkerThreadCount, 6u);
    EXPECT_FALSE(result.Preview.Config.ReferenceScene.Enabled);
    EXPECT_EQ(result.Preview.Config.ReferenceScene.Selector, ReferenceSceneSelector::Triangle);
    EXPECT_FALSE(result.Preview.Config.Camera.Enabled);
    EXPECT_EQ(result.Preview.Config.Camera.Controller, CameraControllerKind::Orbit);
}
