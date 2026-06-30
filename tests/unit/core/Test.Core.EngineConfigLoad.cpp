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
    config.Render.DefaultRecipeConfigPath = "config/runtime-test-recipe.json";
    config.Render.SynchronousExtraction = false;
    config.Simulation.WorkerThreadCount = 4;
    config.ReferenceScene.Enabled = true;
    config.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;
    config.Camera.Enabled = false;
    config.Camera.Controller = CameraControllerKind::TopDown;
    config.Sandbox.ProgressivePoisson.Dimension = 2;
    config.Sandbox.ProgressivePoisson.GridWidth = 8;
    config.Sandbox.ProgressivePoisson.MaxLevels = 12;
    config.Sandbox.ProgressivePoisson.HashLoadFactor = 0.5;
    config.Sandbox.ProgressivePoisson.RadiusAlpha = 0.25;
    config.Sandbox.ProgressivePoisson.RandomizeGridOrigin = false;
    config.Sandbox.ProgressivePoisson.GridOriginSeed = 99;
    config.Sandbox.ProgressivePoisson.ShuffleWithinLevels = false;
    config.Sandbox.ProgressivePoisson.ShuffleSeed = 1234;
    config.Sandbox.ProgressivePoisson.PrefixCount = 64;
    config.Sandbox.ProgressivePoisson.Channel =
        ProgressivePoissonPlaygroundChannel::Phase;
    config.Sandbox.ProgressivePoisson.Backend =
        ProgressivePoissonPlaygroundBackend::VulkanCompute;
    config.Sandbox.ProgressivePoisson.MeshSurfaceSampleCount = 2048;
    config.Sandbox.ProgressivePoisson.MeshSurfaceSampleSeed = 77;
    config.Sandbox.ProgressivePoisson.MeshSurfaceMinTriangleArea = 1.0e-9;
    config.Sandbox.ProgressivePoisson.MeshSurfaceInterpolateNormals = false;
    config.Sandbox.ProgressivePoisson.AutoRunOnEdit = false;
    config.Sandbox.ProgressivePoisson.DebounceSeconds = 0.5;

    const std::string document = SerializeEngineConfig(config);
    const EngineConfigLoadResult preview = PreviewEngineConfig(document, EngineConfig{});

    ASSERT_EQ(preview.State, EngineConfigState::Valid);
    EXPECT_FALSE(HasErrors(preview));
    EXPECT_TRUE(preview.Preview.SideEffectFree);
    EXPECT_EQ(preview.Preview.ParsedFieldCount, 35u);
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
    EXPECT_EQ(preview.Preview.Config.Render.DefaultRecipeConfigPath,
              "config/runtime-test-recipe.json");
    EXPECT_FALSE(preview.Preview.Config.Render.SynchronousExtraction);
    EXPECT_EQ(preview.Preview.Config.Simulation.WorkerThreadCount, 4u);
    EXPECT_TRUE(preview.Preview.Config.ReferenceScene.Enabled);
    EXPECT_EQ(preview.Preview.Config.ReferenceScene.Selector, ReferenceSceneSelector::Triangle);
    EXPECT_FALSE(preview.Preview.Config.Camera.Enabled);
    EXPECT_EQ(preview.Preview.Config.Camera.Controller, CameraControllerKind::TopDown);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.Dimension, 2u);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.GridWidth, 8u);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.MaxLevels, 12u);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.HashLoadFactor,
                     0.5);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.RadiusAlpha,
                     0.25);
    EXPECT_FALSE(preview.Preview.Config.Sandbox.ProgressivePoisson.RandomizeGridOrigin);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.GridOriginSeed, 99u);
    EXPECT_FALSE(preview.Preview.Config.Sandbox.ProgressivePoisson.ShuffleWithinLevels);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.ShuffleSeed, 1234u);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.PrefixCount, 64u);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.Channel,
              ProgressivePoissonPlaygroundChannel::Phase);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.Backend,
              ProgressivePoissonPlaygroundBackend::VulkanCompute);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.MeshSurfaceSampleCount,
              2048u);
    EXPECT_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.MeshSurfaceSampleSeed,
              77u);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson
                         .MeshSurfaceMinTriangleArea,
                     1.0e-9);
    EXPECT_FALSE(preview.Preview.Config.Sandbox.ProgressivePoisson
                     .MeshSurfaceInterpolateNormals);
    EXPECT_FALSE(preview.Preview.Config.Sandbox.ProgressivePoisson.AutoRunOnEdit);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.ProgressivePoisson.DebounceSeconds,
                     0.5);

    const std::filesystem::path path = TempConfigPath("intrinsic_engine_config_roundtrip");
    WriteTextFile(path, document);
    const EngineConfigLoadResult loaded = LoadEngineConfigFile(path.string(), EngineConfig{});
    std::filesystem::remove(path);

    ASSERT_EQ(loaded.State, EngineConfigState::Valid);
    EXPECT_EQ(loaded.SchemaVersion, kEngineConfigSchemaVersion);
    EXPECT_EQ(loaded.Preview.Config.Window.Width, 1280);
    EXPECT_EQ(loaded.Preview.Config.Render.FramesInFlight, 3u);
    EXPECT_EQ(loaded.Preview.Config.Render.DefaultRecipeConfigPath,
              "config/runtime-test-recipe.json");
    EXPECT_EQ(loaded.Preview.Config.Camera.Controller, CameraControllerKind::TopDown);
    EXPECT_EQ(loaded.Preview.Config.Sandbox.ProgressivePoisson.Channel,
              ProgressivePoissonPlaygroundChannel::Phase);
    EXPECT_EQ(loaded.Preview.Config.Sandbox.ProgressivePoisson.Backend,
              ProgressivePoissonPlaygroundBackend::VulkanCompute);
    EXPECT_EQ(loaded.Preview.Config.Sandbox.ProgressivePoisson.MeshSurfaceSampleCount,
              2048u);
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
    defaults.Render.DefaultRecipeConfigPath = "config/reference-recipe.json";
    defaults.Render.SynchronousExtraction = true;
    defaults.Simulation.WorkerThreadCount = 6;
    defaults.ReferenceScene.Enabled = true;
    defaults.Camera.Enabled = true;
    defaults.Camera.Controller = CameraControllerKind::Orbit;
    defaults.Sandbox.ProgressivePoisson.Dimension = 2;
    defaults.Sandbox.ProgressivePoisson.GridWidth = 7;
    defaults.Sandbox.ProgressivePoisson.MaxLevels = 11;
    defaults.Sandbox.ProgressivePoisson.Channel =
        ProgressivePoissonPlaygroundChannel::SplatRadius;
    defaults.Sandbox.ProgressivePoisson.Backend =
        ProgressivePoissonPlaygroundBackend::VulkanCompute;
    defaults.Sandbox.ProgressivePoisson.AutoRunOnEdit = false;
    defaults.Sandbox.ProgressivePoisson.DebounceSeconds = 0.75;

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
    "default_recipe_config_path": 42,
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
  },
  "sandbox": {
    "progressive_poisson": {
      "dimension": 4,
      "grid_width": 0,
      "max_levels": 0,
      "hash_load_factor": "dense",
      "radius_alpha": 2.0,
      "randomize_grid_origin": "yes",
      "grid_origin_seed": -1,
      "shuffle_within_levels": "no",
      "shuffle_seed": -4,
      "prefix_count": -1,
      "channel": "Variance",
      "backend": "Cuda",
      "mesh_surface_sample_count": 0,
      "mesh_surface_seed": -1,
      "mesh_surface_min_triangle_area": 0.0,
      "mesh_surface_interpolate_normals": "yes",
      "auto_run_on_edit": "sometimes",
      "debounce_seconds": -0.1,
      "unused": true
    }
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
    EXPECT_EQ(result.Preview.Config.Render.DefaultRecipeConfigPath,
              "config/reference-recipe.json");
    EXPECT_FALSE(result.Preview.Config.Render.SynchronousExtraction);
    EXPECT_EQ(result.Preview.Config.Simulation.WorkerThreadCount, 6u);
    EXPECT_FALSE(result.Preview.Config.ReferenceScene.Enabled);
    EXPECT_EQ(result.Preview.Config.ReferenceScene.Selector, ReferenceSceneSelector::Triangle);
    EXPECT_FALSE(result.Preview.Config.Camera.Enabled);
    EXPECT_EQ(result.Preview.Config.Camera.Controller, CameraControllerKind::Orbit);
    EXPECT_EQ(result.Preview.Config.Sandbox.ProgressivePoisson.Dimension, 2u);
    EXPECT_EQ(result.Preview.Config.Sandbox.ProgressivePoisson.GridWidth, 7u);
    EXPECT_EQ(result.Preview.Config.Sandbox.ProgressivePoisson.MaxLevels, 11u);
    EXPECT_EQ(result.Preview.Config.Sandbox.ProgressivePoisson.Channel,
              ProgressivePoissonPlaygroundChannel::SplatRadius);
    EXPECT_EQ(result.Preview.Config.Sandbox.ProgressivePoisson.Backend,
              ProgressivePoissonPlaygroundBackend::VulkanCompute);
    EXPECT_FALSE(result.Preview.Config.Sandbox.ProgressivePoisson.AutoRunOnEdit);
    EXPECT_DOUBLE_EQ(result.Preview.Config.Sandbox.ProgressivePoisson.DebounceSeconds,
                     0.75);
}
