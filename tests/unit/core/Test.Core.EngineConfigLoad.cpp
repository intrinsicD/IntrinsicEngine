#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    config.Sandbox.Parameterization.Strategy = ParameterizationStrategyKind::Bff;
    config.Sandbox.Parameterization.Lscm.AutoPins = false;
    config.Sandbox.Parameterization.Lscm.PinVertex0 = 3u;
    config.Sandbox.Parameterization.Lscm.PinVertex1 = 17u;
    config.Sandbox.Parameterization.Lscm.PinUv0 = {-0.25, 0.5};
    config.Sandbox.Parameterization.Lscm.PinUv1 = {1.25, 0.75};
    config.Sandbox.Parameterization.Lscm.SolverTolerance = 2.0e-9;
    config.Sandbox.Parameterization.Lscm.MaxSolverIterations = 9000u;
    config.Sandbox.Parameterization.Harmonic.Boundary =
        ParameterizationBoundaryPolicy::Custom;
    config.Sandbox.Parameterization.Harmonic.ArcLengthSpacing = false;
    config.Sandbox.Parameterization.Harmonic.ClampNonConvexWeights = false;
    config.Sandbox.Parameterization.Harmonic.PinnedVertices = {0u, 2u, 5u};
    config.Sandbox.Parameterization.Harmonic.PinnedUvs = {
        {0.0, 0.0}, {1.0, 0.0}, {0.25, 1.0}};
    config.Sandbox.Parameterization.Bff.Mode =
        ParameterizationBffBoundaryMode::TargetAngles;
    config.Sandbox.Parameterization.Bff.BoundaryData = {
        1.0, 2.0, 3.2831853071795864769};
    config.Sandbox.Parameterization.Bff.AngleSumTolerance = 3.0e-8;
    config.Sandbox.Parameterization.Bff.DegeneracyTolerance = 4.0e-12;
    config.Sandbox.Parameterization.View.RenderMode =
        ParameterizationUvRenderMode::GpuShaded;
    config.Sandbox.Parameterization.View.BackgroundMode =
        ParameterizationUvBackgroundMode::Texture;
    config.Sandbox.Parameterization.View.ShowDistortionHeatmap = true;

    const std::string document = SerializeEngineConfig(config);
    const EngineConfigLoadResult preview = PreviewEngineConfig(document, EngineConfig{});

    ASSERT_EQ(preview.State, EngineConfigState::Valid);
    EXPECT_FALSE(HasErrors(preview));
    EXPECT_TRUE(preview.Preview.SideEffectFree);
    EXPECT_EQ(preview.Preview.ParsedFieldCount, 55u);
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
    EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Strategy,
              ParameterizationStrategyKind::Bff);
    EXPECT_FALSE(preview.Preview.Config.Sandbox.Parameterization.Lscm.AutoPins);
    EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Lscm.PinVertex0, 3u);
    EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Lscm.PinVertex1, 17u);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.Parameterization.Lscm.PinUv0.U,
                     -0.25);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.Parameterization.Lscm.PinUv0.V,
                     0.5);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.Parameterization.Lscm.PinUv1.U,
                     1.25);
    EXPECT_DOUBLE_EQ(preview.Preview.Config.Sandbox.Parameterization.Lscm.PinUv1.V,
                     0.75);
    EXPECT_DOUBLE_EQ(
        preview.Preview.Config.Sandbox.Parameterization.Lscm.SolverTolerance,
        2.0e-9);
    EXPECT_EQ(
        preview.Preview.Config.Sandbox.Parameterization.Lscm.MaxSolverIterations,
        9000u);
    EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Harmonic.Boundary,
              ParameterizationBoundaryPolicy::Custom);
    EXPECT_FALSE(
        preview.Preview.Config.Sandbox.Parameterization.Harmonic.ArcLengthSpacing);
    EXPECT_FALSE(preview.Preview.Config.Sandbox.Parameterization.Harmonic
                     .ClampNonConvexWeights);
    EXPECT_EQ(
        preview.Preview.Config.Sandbox.Parameterization.Harmonic.PinnedVertices,
        (std::vector<std::uint32_t>{0u, 2u, 5u}));
    ASSERT_EQ(preview.Preview.Config.Sandbox.Parameterization.Harmonic.PinnedUvs.size(),
              3u);
    EXPECT_DOUBLE_EQ(
        preview.Preview.Config.Sandbox.Parameterization.Harmonic.PinnedUvs[2].U,
        0.25);
    EXPECT_DOUBLE_EQ(
        preview.Preview.Config.Sandbox.Parameterization.Harmonic.PinnedUvs[2].V,
        1.0);
    EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Bff.Mode,
              ParameterizationBffBoundaryMode::TargetAngles);
    EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Bff.BoundaryData,
              (std::vector<double>{1.0, 2.0, 3.2831853071795864769}));
    EXPECT_DOUBLE_EQ(
        preview.Preview.Config.Sandbox.Parameterization.Bff.AngleSumTolerance,
        3.0e-8);
    EXPECT_DOUBLE_EQ(
        preview.Preview.Config.Sandbox.Parameterization.Bff.DegeneracyTolerance,
        4.0e-12);
    EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.View.RenderMode,
              ParameterizationUvRenderMode::GpuShaded);
    EXPECT_EQ(
        preview.Preview.Config.Sandbox.Parameterization.View.BackgroundMode,
        ParameterizationUvBackgroundMode::Texture);
    EXPECT_TRUE(preview.Preview.Config.Sandbox.Parameterization.View
                    .ShowDistortionHeatmap);
    EXPECT_EQ(SerializeEngineConfig(preview.Preview.Config), document);

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
    EXPECT_EQ(loaded.Preview.Config.Sandbox.Parameterization.Strategy,
              ParameterizationStrategyKind::Bff);
    EXPECT_EQ(
        loaded.Preview.Config.Sandbox.Parameterization.Harmonic.PinnedVertices,
        (std::vector<std::uint32_t>{0u, 2u, 5u}));
    EXPECT_EQ(loaded.Preview.Config.Sandbox.Parameterization.Bff.Mode,
              ParameterizationBffBoundaryMode::TargetAngles);
    EXPECT_EQ(loaded.Preview.Config.Sandbox.Parameterization.View.RenderMode,
              ParameterizationUvRenderMode::GpuShaded);
    EXPECT_EQ(
        loaded.Preview.Config.Sandbox.Parameterization.View.BackgroundMode,
        ParameterizationUvBackgroundMode::Texture);
    EXPECT_TRUE(loaded.Preview.Config.Sandbox.Parameterization.View
                    .ShowDistortionHeatmap);
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

TEST(CoreEngineConfigLoad, ParameterizationEnumTokensRoundTripStably)
{
    struct StrategyCase
    {
        ParameterizationStrategyKind Value;
        std::string Token;
    };
    for (const StrategyCase& testCase : {
             StrategyCase{ParameterizationStrategyKind::Lscm, "lscm"},
             StrategyCase{ParameterizationStrategyKind::HarmonicCotangent,
                          "harmonic_cotangent"},
             StrategyCase{ParameterizationStrategyKind::TutteUniform,
                          "tutte_uniform"},
             StrategyCase{ParameterizationStrategyKind::Bff, "bff"},
         })
    {
        EngineConfig config{};
        config.Sandbox.Parameterization.Strategy = testCase.Value;
        const std::string document = SerializeEngineConfig(config);
        EXPECT_NE(document.find("\"strategy\": \"" + testCase.Token + "\""),
                  std::string::npos);
        const EngineConfigLoadResult preview = PreviewEngineConfig(document);
        ASSERT_EQ(preview.State, EngineConfigState::Valid);
        EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Strategy,
                  testCase.Value);
    }

    struct RenderModeCase
    {
        ParameterizationUvRenderMode Value;
        std::string Token;
    };
    for (const RenderModeCase& testCase : {
             RenderModeCase{ParameterizationUvRenderMode::CpuLayout,
                            "cpu_layout"},
             RenderModeCase{ParameterizationUvRenderMode::GpuShaded,
                            "gpu_shaded"},
         })
    {
        EngineConfig config{};
        config.Sandbox.Parameterization.View.RenderMode = testCase.Value;
        const std::string document = SerializeEngineConfig(config);
        EXPECT_NE(document.find(
                      "\"render_mode\": \"" + testCase.Token + "\""),
                  std::string::npos);
        const EngineConfigLoadResult preview = PreviewEngineConfig(document);
        ASSERT_EQ(preview.State, EngineConfigState::Valid);
        EXPECT_EQ(
            preview.Preview.Config.Sandbox.Parameterization.View.RenderMode,
            testCase.Value);
    }

    struct BackgroundModeCase
    {
        ParameterizationUvBackgroundMode Value;
        std::string Token;
    };
    for (const BackgroundModeCase& testCase : {
             BackgroundModeCase{ParameterizationUvBackgroundMode::Grid,
                                "grid"},
             BackgroundModeCase{ParameterizationUvBackgroundMode::Checker,
                                "checker"},
             BackgroundModeCase{
                 ParameterizationUvBackgroundMode::TexelDensity,
                 "texel_density"},
             BackgroundModeCase{ParameterizationUvBackgroundMode::Texture,
                                "texture"},
         })
    {
        EngineConfig config{};
        config.Sandbox.Parameterization.View.BackgroundMode = testCase.Value;
        const std::string document = SerializeEngineConfig(config);
        EXPECT_NE(document.find(
                      "\"background_mode\": \"" + testCase.Token + "\""),
                  std::string::npos);
        const EngineConfigLoadResult preview = PreviewEngineConfig(document);
        ASSERT_EQ(preview.State, EngineConfigState::Valid);
        EXPECT_EQ(
            preview.Preview.Config.Sandbox.Parameterization.View.BackgroundMode,
            testCase.Value);
    }

    struct BoundaryCase
    {
        ParameterizationBoundaryPolicy Value;
        std::string Token;
    };
    for (const BoundaryCase& testCase : {
             BoundaryCase{ParameterizationBoundaryPolicy::Circle, "circle"},
             BoundaryCase{ParameterizationBoundaryPolicy::Square, "square"},
             BoundaryCase{ParameterizationBoundaryPolicy::Custom, "custom"},
         })
    {
        EngineConfig config{};
        config.Sandbox.Parameterization.Harmonic.Boundary = testCase.Value;
        const std::string document = SerializeEngineConfig(config);
        EXPECT_NE(document.find("\"boundary\": \"" + testCase.Token + "\""),
                  std::string::npos);
        const EngineConfigLoadResult preview = PreviewEngineConfig(document);
        ASSERT_EQ(preview.State, EngineConfigState::Valid);
        EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Harmonic.Boundary,
                  testCase.Value);
    }

    struct BffModeCase
    {
        ParameterizationBffBoundaryMode Value;
        std::string Token;
    };
    for (const BffModeCase& testCase : {
             BffModeCase{ParameterizationBffBoundaryMode::AutomaticConformal,
                         "automatic_conformal"},
             BffModeCase{ParameterizationBffBoundaryMode::TargetLengths,
                         "target_lengths"},
             BffModeCase{ParameterizationBffBoundaryMode::TargetAngles,
                         "target_angles"},
         })
    {
        EngineConfig config{};
        config.Sandbox.Parameterization.Bff.Mode = testCase.Value;
        if (testCase.Value == ParameterizationBffBoundaryMode::TargetLengths)
        {
            config.Sandbox.Parameterization.Bff.BoundaryData = {1.0};
        }
        else if (testCase.Value == ParameterizationBffBoundaryMode::TargetAngles)
        {
            config.Sandbox.Parameterization.Bff.BoundaryData = {
                6.2831853071795864769};
        }
        const std::string document = SerializeEngineConfig(config);
        EXPECT_NE(document.find("\"mode\": \"" + testCase.Token + "\""),
                  std::string::npos);
        const EngineConfigLoadResult preview = PreviewEngineConfig(document);
        ASSERT_EQ(preview.State, EngineConfigState::Valid);
        EXPECT_EQ(preview.Preview.Config.Sandbox.Parameterization.Bff.Mode,
                  testCase.Value);
    }
}

TEST(CoreEngineConfigLoad,
     InvalidParameterizationViewFieldsRetainReferenceValues)
{
    EngineConfig defaults{};
    defaults.Sandbox.Parameterization.View.RenderMode =
        ParameterizationUvRenderMode::GpuShaded;
    defaults.Sandbox.Parameterization.View.BackgroundMode =
        ParameterizationUvBackgroundMode::Texture;
    defaults.Sandbox.Parameterization.View.ShowDistortionHeatmap = true;

    const EngineConfigLoadResult result = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {"view": {
            "render_mode": "path_traced",
            "background_mode": "udim",
            "show_distortion_heatmap": "yes",
            "unused": true
          }}}
        })json",
        defaults);

    ASSERT_EQ(result.State, EngineConfigState::FallbackApplied);
    EXPECT_FALSE(HasErrors(result));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::InvalidValue));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::UnknownField));
    const ParameterizationViewConfig& view =
        result.Preview.Config.Sandbox.Parameterization.View;
    EXPECT_EQ(view.RenderMode, ParameterizationUvRenderMode::GpuShaded);
    EXPECT_EQ(view.BackgroundMode, ParameterizationUvBackgroundMode::Texture);
    EXPECT_TRUE(view.ShowDistortionHeatmap);
}

TEST(CoreEngineConfigLoad, InvalidParameterizationFieldsRetainReferenceValues)
{
    EngineConfig defaults{};
    ParameterizationConfig& reference = defaults.Sandbox.Parameterization;
    reference.Strategy = ParameterizationStrategyKind::TutteUniform;
    reference.Lscm.PinVertex0 = 7u;
    reference.Lscm.PinVertex1 = 9u;
    reference.Lscm.PinUv0 = {0.25, 0.5};
    reference.Lscm.PinUv1 = {0.75, 0.5};
    reference.Lscm.SolverTolerance = 5.0e-7;
    reference.Lscm.MaxSolverIterations = 44u;
    reference.Harmonic.Boundary = ParameterizationBoundaryPolicy::Square;
    reference.Harmonic.PinnedVertices = {4u, 8u};
    reference.Harmonic.PinnedUvs = {{0.0, 0.0}, {1.0, 1.0}};
    reference.Bff.Mode = ParameterizationBffBoundaryMode::TargetLengths;
    reference.Bff.BoundaryData = {2.0, 3.0};
    reference.Bff.AngleSumTolerance = 6.0e-7;
    reference.Bff.DegeneracyTolerance = 7.0e-9;

    const std::string document = R"json(
{
  "schema": "intrinsic.core.engine-config",
  "version": 1,
  "sandbox": {
    "parameterization": {
      "strategy": "arap",
      "lscm": {
        "auto_pins": false,
        "pin_vertex_0": -1,
        "pin_vertex_1": 4294967296,
        "pin_uv_0": [0.0],
        "pin_uv_1": ["east", 0.0],
        "solver_tolerance": 0.0,
        "max_solver_iterations": 0
      },
      "harmonic": {
        "boundary": "polygon",
        "arc_length_spacing": false,
        "clamp_non_convex_weights": false,
        "pinned_vertices": [0, 1],
        "pinned_uvs": [[0.0, 0.0]]
      },
      "bff": {
        "mode": "target_lengths",
        "boundary_data": [1.0, -2.0],
        "angle_sum_tolerance": 0.0,
        "degeneracy_tolerance": -1.0
      }
    }
  }
}
)json";

    const EngineConfigLoadResult result = PreviewEngineConfig(document, defaults);

    ASSERT_EQ(result.State, EngineConfigState::FallbackApplied);
    EXPECT_FALSE(HasErrors(result));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::InvalidValue));
    const ParameterizationConfig& parsed =
        result.Preview.Config.Sandbox.Parameterization;
    EXPECT_EQ(parsed.Strategy, ParameterizationStrategyKind::TutteUniform);
    EXPECT_FALSE(parsed.Lscm.AutoPins);
    EXPECT_EQ(parsed.Lscm.PinVertex0, 7u);
    EXPECT_EQ(parsed.Lscm.PinVertex1, 9u);
    EXPECT_DOUBLE_EQ(parsed.Lscm.PinUv0.U, 0.25);
    EXPECT_DOUBLE_EQ(parsed.Lscm.PinUv0.V, 0.5);
    EXPECT_DOUBLE_EQ(parsed.Lscm.PinUv1.U, 0.75);
    EXPECT_DOUBLE_EQ(parsed.Lscm.PinUv1.V, 0.5);
    EXPECT_DOUBLE_EQ(parsed.Lscm.SolverTolerance, 5.0e-7);
    EXPECT_EQ(parsed.Lscm.MaxSolverIterations, 44u);
    EXPECT_EQ(parsed.Harmonic.Boundary, ParameterizationBoundaryPolicy::Square);
    EXPECT_FALSE(parsed.Harmonic.ArcLengthSpacing);
    EXPECT_FALSE(parsed.Harmonic.ClampNonConvexWeights);
    EXPECT_EQ(parsed.Harmonic.PinnedVertices,
              (std::vector<std::uint32_t>{4u, 8u}));
    ASSERT_EQ(parsed.Harmonic.PinnedUvs.size(), 2u);
    EXPECT_DOUBLE_EQ(parsed.Harmonic.PinnedUvs[1].U, 1.0);
    EXPECT_DOUBLE_EQ(parsed.Harmonic.PinnedUvs[1].V, 1.0);
    EXPECT_EQ(parsed.Bff.Mode, ParameterizationBffBoundaryMode::TargetLengths);
    EXPECT_EQ(parsed.Bff.BoundaryData, (std::vector<double>{2.0, 3.0}));
    EXPECT_DOUBLE_EQ(parsed.Bff.AngleSumTolerance, 6.0e-7);
    EXPECT_DOUBLE_EQ(parsed.Bff.DegeneracyTolerance, 7.0e-9);
}

TEST(CoreEngineConfigLoad, InvalidParameterizationCrossFieldsRestoreExecutableRecords)
{
    EngineConfig defaults{};
    defaults.Sandbox.Parameterization.Lscm.PinVertex0 = 2u;
    defaults.Sandbox.Parameterization.Lscm.PinVertex1 = 6u;
    defaults.Sandbox.Parameterization.Bff.Mode =
        ParameterizationBffBoundaryMode::TargetLengths;
    defaults.Sandbox.Parameterization.Bff.BoundaryData = {4.0, 5.0};

    const EngineConfigLoadResult invalidManualPins = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {
            "lscm": {
              "auto_pins": false,
              "pin_vertex_0": 12,
              "pin_vertex_1": 12,
              "pin_uv_0": [1.0e39, 0.0]
            },
            "bff": {
              "mode": "target_angles",
              "boundary_data": [1.0, 2.0, 3.0]
            }
          }}
        })json",
        defaults);
    ASSERT_EQ(invalidManualPins.State, EngineConfigState::FallbackApplied);
    const ParameterizationConfig& restored =
        invalidManualPins.Preview.Config.Sandbox.Parameterization;
    EXPECT_TRUE(restored.Lscm.AutoPins);
    EXPECT_EQ(restored.Lscm.PinVertex0, 2u);
    EXPECT_EQ(restored.Lscm.PinVertex1, 6u);
    EXPECT_EQ(restored.Bff.Mode, ParameterizationBffBoundaryMode::TargetLengths);
    EXPECT_EQ(restored.Bff.BoundaryData, (std::vector<double>{4.0, 5.0}));

    const EngineConfigLoadResult automaticWithData = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {"bff": {
            "mode": "automatic_conformal",
            "boundary_data": [1.0]
          }}}
        })json",
        defaults);
    ASSERT_EQ(automaticWithData.State, EngineConfigState::FallbackApplied);
    EXPECT_EQ(automaticWithData.Preview.Config.Sandbox.Parameterization.Bff.Mode,
              ParameterizationBffBoundaryMode::TargetLengths);
    EXPECT_EQ(automaticWithData.Preview.Config.Sandbox.Parameterization.Bff
                  .BoundaryData,
              (std::vector<double>{4.0, 5.0}));

    const EngineConfigLoadResult targetLengthsWithoutData = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {"bff": {
            "mode": "target_lengths",
            "boundary_data": []
          }}}
        })json",
        defaults);
    ASSERT_EQ(targetLengthsWithoutData.State, EngineConfigState::FallbackApplied);
    EXPECT_EQ(targetLengthsWithoutData.Preview.Config.Sandbox.Parameterization.Bff
                  .BoundaryData,
              (std::vector<double>{4.0, 5.0}));

    const EngineConfigLoadResult targetAnglesWithoutData = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {"bff": {
            "mode": "target_angles",
            "boundary_data": []
          }}}
        })json",
        defaults);
    ASSERT_EQ(targetAnglesWithoutData.State, EngineConfigState::FallbackApplied);
    EXPECT_EQ(targetAnglesWithoutData.Preview.Config.Sandbox.Parameterization.Bff
                  .BoundaryData,
              (std::vector<double>{4.0, 5.0}));
}

TEST(CoreEngineConfigLoad, MalformedParameterizationArraysFailClosedAsPairs)
{
    EngineConfig defaults{};
    defaults.Sandbox.Parameterization.Lscm.PinUv0 = {0.25, 0.75};
    defaults.Sandbox.Parameterization.Harmonic.PinnedVertices = {3u};
    defaults.Sandbox.Parameterization.Harmonic.PinnedUvs = {{0.5, 0.5}};
    defaults.Sandbox.Parameterization.Bff.BoundaryData = {4.0};

    const EngineConfigLoadResult malformedUvs = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {
            "harmonic": {
              "pinned_vertices": [0],
              "pinned_uvs": [[0.0, 1.0, 2.0]]
            },
            "bff": {"boundary_data": [1.0, "invalid"]}
          }}
        })json",
        defaults);

    ASSERT_EQ(malformedUvs.State, EngineConfigState::FallbackApplied);
    EXPECT_EQ(malformedUvs.Preview.Config.Sandbox.Parameterization.Harmonic
                  .PinnedVertices,
              (std::vector<std::uint32_t>{3u}));
    ASSERT_EQ(malformedUvs.Preview.Config.Sandbox.Parameterization.Harmonic
                  .PinnedUvs.size(),
              1u);
    EXPECT_DOUBLE_EQ(malformedUvs.Preview.Config.Sandbox.Parameterization.Harmonic
                         .PinnedUvs[0].U,
                     0.5);
    EXPECT_EQ(malformedUvs.Preview.Config.Sandbox.Parameterization.Bff.BoundaryData,
              (std::vector<double>{4.0}));

    const EngineConfigLoadResult missingPair = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {
            "harmonic": {"pinned_vertices": [0, 1]}
          }}
        })json",
        defaults);
    ASSERT_EQ(missingPair.State, EngineConfigState::FallbackApplied);
    EXPECT_EQ(missingPair.Preview.Config.Sandbox.Parameterization.Harmonic
                  .PinnedVertices,
              (std::vector<std::uint32_t>{3u}));
    EXPECT_EQ(missingPair.Preview.Config.Sandbox.Parameterization.Harmonic
                  .PinnedUvs.size(),
              1u);

    const EngineConfigLoadResult oversizedUvs = PreviewEngineConfig(
        R"json({
          "schema": "intrinsic.core.engine-config",
          "version": 1,
          "sandbox": {"parameterization": {
            "lscm": {"pin_uv_0": [1.0e39, 0.0]},
            "harmonic": {
              "pinned_vertices": [0],
              "pinned_uvs": [[0.0, -1.0e39]]
            }
          }}
        })json",
        defaults);
    ASSERT_EQ(oversizedUvs.State, EngineConfigState::FallbackApplied);
    EXPECT_DOUBLE_EQ(
        oversizedUvs.Preview.Config.Sandbox.Parameterization.Lscm.PinUv0.U,
        0.25);
    EXPECT_DOUBLE_EQ(
        oversizedUvs.Preview.Config.Sandbox.Parameterization.Lscm.PinUv0.V,
        0.75);
    EXPECT_EQ(oversizedUvs.Preview.Config.Sandbox.Parameterization.Harmonic
                  .PinnedVertices,
              (std::vector<std::uint32_t>{3u}));
    EXPECT_DOUBLE_EQ(oversizedUvs.Preview.Config.Sandbox.Parameterization.Harmonic
                         .PinnedUvs[0].U,
                     0.5);
}
