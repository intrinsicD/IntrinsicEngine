// METHOD-020 - frozen v3 LOP/isotropic-WLOP Vulkan benchmark runner.
//
// This executable composes the canonical PointCloudConsolidationService and
// emits one schema-v2-compatible result for tools/benchmark/. Timing is
// descriptive; CPU parity and actual backend identity are the acceptance gate.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Backends.Vulkan;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Profiler;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SelectionController;
import Geometry.PointCloud.Consolidation;
import Geometry.Properties;

namespace
{
    namespace ECS = Extrinsic::ECS;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace Runtime = Extrinsic::Runtime;
    namespace Consolidation = Geometry::PointCloud::Consolidation;

    inline constexpr std::string_view kBenchmarkId =
        "geometry.lop_family.gpu_vulkan.v3.smoke";
    inline constexpr std::string_view kMethod =
        "geometry.lop_family.gpu_vulkan";
    inline constexpr std::string_view kDataset =
        "builtin.lop_family.gpu_vulkan.v3";
    inline constexpr int kWarmupIterations = 1;
    inline constexpr int kMeasuredIterations = 3;
    inline constexpr double kRmsTolerance = 5.0e-4;
    inline constexpr double kLinfTolerance = 2.0e-3;

    struct Fixture
    {
        std::string Name{};
        std::vector<glm::vec3> Input{};
        Runtime::PointCloudConsolidationConfig Config{};
    };

    struct PositionError
    {
        double Rms{0.0};
        double Linf{0.0};
    };

    struct CpuFixtureResult
    {
        Consolidation::Result Result{};
        double RuntimeMilliseconds{0.0};
        bool Succeeded{false};
    };

    struct GpuFixtureResult
    {
        Runtime::PointCloudConsolidationResult Completion{};
        double RuntimeMilliseconds{0.0};
        double MaxParityRms{0.0};
        double MaxParityLinf{0.0};
        double MaxRepeatRms{0.0};
        double MaxRepeatLinf{0.0};
        bool Succeeded{false};
    };

    struct GpuBenchmarkResult
    {
        std::array<GpuFixtureResult, 2u> Fixtures{};
        Runtime::PointCloudConsolidationModuleStats Stats{};
        std::string DeviceDiagnostic{};
        std::string Diagnostic{};
        bool Succeeded{false};
    };

    [[nodiscard]] std::string EscapeJson(const std::string_view input)
    {
        std::string output{};
        output.reserve(input.size());
        for (const char ch : input)
        {
            switch (ch)
            {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default: output += ch; break;
            }
        }
        return output;
    }

    [[nodiscard]] std::string ResolveCommit()
    {
        const char* value = std::getenv("GIT_COMMIT");
        return value != nullptr && value[0] != '\0'
            ? std::string{value}
            : std::string{"local-dev"};
    }

    [[nodiscard]] std::vector<glm::vec3> MakePlane(
        const std::uint32_t width,
        const std::uint32_t height,
        const double xWarpExponent)
    {
        std::vector<glm::vec3> points{};
        points.reserve(static_cast<std::size_t>(width) * height);
        for (std::uint32_t row = 0u; row < height; ++row)
        {
            const double v = static_cast<double>(row) /
                static_cast<double>(height - 1u);
            const double y = -1.0 + 2.0 * v;
            for (std::uint32_t column = 0u; column < width; ++column)
            {
                const double u = static_cast<double>(column) /
                    static_cast<double>(width - 1u);
                const double x = -1.0 +
                    2.0 * std::pow(u, xWarpExponent);
                const double z = 0.018 * std::sin(13.0 * x + 7.0 * y) +
                    0.006 * std::cos(5.0 * x - 11.0 * y);
                points.emplace_back(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z));
            }
        }
        return points;
    }

    [[nodiscard]] Runtime::PointCloudConsolidationConfig MakeConfig(
        const Runtime::PointCloudConsolidationStrategy strategy,
        const double supportRadius,
        const std::uint32_t targetPointCount,
        const std::uint32_t seed)
    {
        return Runtime::PointCloudConsolidationConfig{
            .Backend =
                Runtime::PointCloudConsolidationBackend::VulkanCompute,
            .Strategy = strategy,
            .SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual,
            .SupportRadius = supportRadius,
            .RepulsionWeight = 0.35,
            .MaxIterations = 8u,
            .ConvergenceTolerance = 0.0,
            .TargetPointCount = targetPointCount,
            .Seed = seed,
            .NormalRefinementRounds = 3u,
        };
    }

    [[nodiscard]] std::array<Fixture, 2u> MakeFixtures()
    {
        return {{
            Fixture{
                .Name = "lop",
                .Input = MakePlane(32u, 32u, 1.0),
                .Config = MakeConfig(
                    Runtime::PointCloudConsolidationStrategy::Lop,
                    0.18,
                    256u,
                    1901u),
            },
            Fixture{
                .Name = "wlop_isotropic",
                .Input = MakePlane(40u, 24u, 1.7),
                .Config = MakeConfig(
                    Runtime::PointCloudConsolidationStrategy::Wlop,
                    0.32,
                    240u,
                    1902u),
            },
        }};
    }

    [[nodiscard]] Consolidation::Params MakeCpuParams(
        const Runtime::PointCloudConsolidationConfig& config)
    {
        Consolidation::Params params{
            .SupportRadius = config.SupportRadius,
            .RepulsionWeight = config.RepulsionWeight,
            .MaxIterations = config.MaxIterations,
            .ConvergenceTolerance = config.ConvergenceTolerance,
            .TargetPointCount = config.TargetPointCount,
            .Seed = config.Seed,
        };
        params.Method = config.Strategy ==
                Runtime::PointCloudConsolidationStrategy::Lop
            ? Consolidation::Strategy{Consolidation::LopStrategy{}}
            : Consolidation::Strategy{Consolidation::WlopStrategy{
                  .Weighting = Consolidation::WeightingMode::Isotropic,
              }};
        return params;
    }

    void SetPositions(
        GS::Vertices& vertices,
        const std::span<const glm::vec3> positions)
    {
        vertices.Properties.Resize(positions.size());
        auto property = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{GS::PropertyNames::kPosition}, glm::vec3{0.0f});
        property.Vector().assign(positions.begin(), positions.end());
    }

    [[nodiscard]] PositionError MeasurePositionError(
        const std::span<const glm::vec3> lhs,
        const std::span<const glm::vec3> rhs)
    {
        if (lhs.size() != rhs.size() || lhs.empty())
        {
            return PositionError{
                .Rms = std::numeric_limits<double>::infinity(),
                .Linf = std::numeric_limits<double>::infinity(),
            };
        }

        double squared = 0.0;
        double maximum = 0.0;
        for (std::size_t index = 0u; index < lhs.size(); ++index)
        {
            const glm::dvec3 delta =
                glm::dvec3{lhs[index]} - glm::dvec3{rhs[index]};
            const double distance = glm::length(delta);
            squared += distance * distance;
            maximum = std::max(maximum, distance);
        }
        return PositionError{
            .Rms = std::sqrt(squared / static_cast<double>(lhs.size())),
            .Linf = maximum,
        };
    }

    [[nodiscard]] std::array<CpuFixtureResult, 2u> RunCpuReferences(
        const std::array<Fixture, 2u>& fixtures)
    {
        std::array<CpuFixtureResult, 2u> results{};
        for (std::size_t fixtureIndex = 0u;
             fixtureIndex < fixtures.size();
             ++fixtureIndex)
        {
            const Consolidation::Params params =
                MakeCpuParams(fixtures[fixtureIndex].Config);
            for (int iteration = 0; iteration < kWarmupIterations; ++iteration)
                (void)Consolidation::Consolidate(
                    fixtures[fixtureIndex].Input, params);

            Consolidation::Result last{};
            const auto started = std::chrono::steady_clock::now();
            for (int iteration = 0; iteration < kMeasuredIterations;
                 ++iteration)
            {
                last = Consolidation::Consolidate(
                    fixtures[fixtureIndex].Input, params);
            }
            const auto completed = std::chrono::steady_clock::now();
            const double totalMilliseconds = static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    completed - started)
                    .count()) *
                1.0e-6;
            const bool usable = last.Succeeded() ||
                last.State == Consolidation::Status::NotConverged;
            results[fixtureIndex] = CpuFixtureResult{
                .Result = std::move(last),
                .RuntimeMilliseconds = totalMilliseconds /
                    static_cast<double>(kMeasuredIterations),
                .Succeeded = usable,
            };
        }
        return results;
    }

    class LopGpuBenchmarkDriver final : public Runtime::IRuntimeModule
    {
    public:
        LopGpuBenchmarkDriver(
            Runtime::Engine& engine,
            const std::array<Fixture, 2u>& fixtures,
            const std::array<CpuFixtureResult, 2u>& references) noexcept
            : Engine(engine)
            , Fixtures(fixtures)
            , References(references)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "Benchmark.PointCloudConsolidationVulkanDriver";
        }

        [[nodiscard]] Runtime::RuntimeModuleResult OnRegister(
            Runtime::EngineSetup& setup) override
        {
            return setup.RegisterFrameHook(
                Runtime::FramePhase::UiBuild,
                [this](Runtime::RuntimeFrameHookContext& context)
                { Frame(context); });
        }

        [[nodiscard]] Runtime::RuntimeModuleResult OnResolve(
            Runtime::EngineSetup& setup) override
        {
            Service = setup.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = setup.Worlds().Get(Engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                Fail("PointCloudConsolidationService is unavailable.");
                return Runtime::RuntimeModuleOk();
            }
            CompletionSubscription = Service->SubscribeCompleted(
                [this](
                    const Runtime::PointCloudConsolidationResult& completed)
                {
                    if (Submitted &&
                        completed.Correlation == ActiveCorrelation)
                    {
                        PendingCompletion = completed;
                        CompletionReceivedAt =
                            std::chrono::steady_clock::now();
                    }
                });
            Deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds{120};
            return Runtime::RuntimeModuleOk();
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext& context) override
        {
            if (CompletionSubscription.IsValid())
                context.Events.Unsubscribe(CompletionSubscription);
            CompletionSubscription = {};
            Service = nullptr;
            Scene = nullptr;
        }

        [[nodiscard]] GpuBenchmarkResult Result() const
        {
            return GpuBenchmarkResult{
                .Fixtures = Results,
                .Stats = Service != nullptr ? Service->Stats() : Stats,
                .Diagnostic = Diagnostic,
                .Succeeded = Succeeded,
            };
        }

    private:
        void Fail(std::string diagnostic)
        {
            Diagnostic = std::move(diagnostic);
            Failed = true;
            Engine.RequestExit();
        }

        [[nodiscard]] std::vector<glm::vec3> CapturePositions() const
        {
            if (Scene == nullptr || !Scene->Raw().valid(ActiveEntity) ||
                !Scene->Raw().all_of<GS::Vertices>(ActiveEntity))
            {
                return {};
            }
            const auto positions = Scene->Raw()
                .get<GS::Vertices>(ActiveEntity)
                .Properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
            return positions ? positions.Vector() :
                               std::vector<glm::vec3>{};
        }

        void ConsumeCompletion()
        {
            const Runtime::PointCloudConsolidationResult completed =
                std::move(*PendingCompletion);
            PendingCompletion.reset();
            Submitted = false;
            const std::size_t fixtureIndex = CurrentFixture;
            const Fixture& fixture = Fixtures[fixtureIndex];
            const Consolidation::Result& reference =
                References[fixtureIndex].Result;

            if (!completed.Succeeded() ||
                completed.ActualBackend !=
                    Runtime::PointCloudConsolidationBackend::VulkanCompute ||
                completed.FellBackToCpu)
            {
                Fail(completed.BackendDiagnostic.empty()
                         ? completed.Message
                         : completed.BackendDiagnostic);
                return;
            }
            if (completed.GeometryStatus != reference.State ||
                completed.Iterations != reference.Diagnostics.Iterations ||
                completed.Converged != reference.Diagnostics.Converged)
            {
                Fail(fixture.Name +
                     " status/iteration/convergence parity mismatch.");
                return;
            }

            const std::vector<glm::vec3> positions = CapturePositions();
            const PositionError parity = MeasurePositionError(
                positions, reference.Positions);
            if (!std::isfinite(parity.Rms) ||
                parity.Rms > kRmsTolerance ||
                parity.Linf > kLinfTolerance)
            {
                Fail(fixture.Name + " position parity exceeded tolerance.");
                return;
            }

            GpuFixtureResult& result = Results[fixtureIndex];
            result.Completion = completed;
            result.MaxParityRms = std::max(result.MaxParityRms, parity.Rms);
            result.MaxParityLinf =
                std::max(result.MaxParityLinf, parity.Linf);

            if (RunWithinFixture >= kWarmupIterations)
            {
                const double elapsedMilliseconds = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        CompletionReceivedAt - SubmittedAt)
                        .count()) *
                    1.0e-6;
                result.RuntimeMilliseconds += elapsedMilliseconds;
                if (FirstMeasuredPositions.empty())
                {
                    FirstMeasuredPositions = positions;
                }
                else
                {
                    const PositionError repeat = MeasurePositionError(
                        positions, FirstMeasuredPositions);
                    result.MaxRepeatRms =
                        std::max(result.MaxRepeatRms, repeat.Rms);
                    result.MaxRepeatLinf =
                        std::max(result.MaxRepeatLinf, repeat.Linf);
                    if (repeat.Rms > kRmsTolerance ||
                        repeat.Linf > kLinfTolerance)
                    {
                        Fail(fixture.Name +
                             " same-host repeat exceeded tolerance.");
                        return;
                    }
                }
            }

            ++RunWithinFixture;
            if (RunWithinFixture >=
                kWarmupIterations + kMeasuredIterations)
            {
                result.RuntimeMilliseconds /=
                    static_cast<double>(kMeasuredIterations);
                result.Succeeded = true;
                ++CurrentFixture;
                RunWithinFixture = 0;
                FirstMeasuredPositions.clear();
            }

            if (CurrentFixture >= Fixtures.size())
            {
                Stats = Service->Stats();
                Succeeded = true;
                Diagnostic =
                    "actual Vulkan LOP/WLOP parity passed frozen v3 fixtures";
                Engine.RequestExit();
            }
        }

        void Submit()
        {
            const Fixture& fixture = Fixtures[CurrentFixture];
            ActiveEntity = Scene->Create();
            auto& vertices =
                Scene->Raw().emplace<GS::Vertices>(ActiveEntity);
            SetPositions(vertices, fixture.Input);
            SubmittedAt = std::chrono::steady_clock::now();
            ActiveCorrelation = Service->Run(
                Runtime::PointCloudConsolidationRequest{
                    .StableEntityId = Runtime::SelectionController::
                        ToStableEntityId(ActiveEntity),
                    .Config = fixture.Config,
                });
            if (!ActiveCorrelation.IsValid())
            {
                Fail("PointCloudConsolidationService returned an invalid correlation.");
                return;
            }
            Submitted = true;
        }

        void Frame(Runtime::RuntimeFrameHookContext&)
        {
            if (Failed || Succeeded)
                return;
            if (std::chrono::steady_clock::now() >= Deadline)
            {
                Fail("Timed out waiting for Vulkan LOP/WLOP completion.");
                return;
            }
            if (PendingCompletion.has_value())
                ConsumeCompletion();
            if (!Failed && !Succeeded && !Submitted &&
                CurrentFixture < Fixtures.size() &&
                Engine.GetDevice().IsOperational())
            {
                Submit();
            }
        }

        Runtime::Engine& Engine;
        const std::array<Fixture, 2u>& Fixtures;
        const std::array<CpuFixtureResult, 2u>& References;
        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId ActiveCorrelation{};
        Runtime::PointCloudConsolidationModuleStats Stats{};
        std::optional<Runtime::PointCloudConsolidationResult>
            PendingCompletion{};
        std::array<GpuFixtureResult, 2u> Results{};
        std::vector<glm::vec3> FirstMeasuredPositions{};
        ECS::EntityHandle ActiveEntity{ECS::InvalidEntityHandle};
        std::chrono::steady_clock::time_point SubmittedAt{};
        std::chrono::steady_clock::time_point CompletionReceivedAt{};
        std::chrono::steady_clock::time_point Deadline{};
        std::string Diagnostic{};
        std::size_t CurrentFixture{0u};
        int RunWithinFixture{0};
        bool Submitted{false};
        bool Succeeded{false};
        bool Failed{false};
    };

    [[nodiscard]] GpuBenchmarkResult RunGpuMeasurements(
        const std::array<Fixture, 2u>& fixtures,
        const std::array<CpuFixtureResult, 2u>& references,
        std::string& diagnostic,
        bool& skipped)
    {
        if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        {
            diagnostic =
                "GLFW could not initialize; gpu;vulkan benchmark is opt-in.";
            skipped = true;
            return {};
        }

        auto config = Runtime::CreateReferenceEngineConfig();
        config.Window.Title = "Intrinsic LOP gpu;vulkan benchmark";
        config.Window.Width = 64;
        config.Window.Height = 64;
        config.Window.Resizable = false;
        config.Render.EnablePromotedVulkanDevice = true;
        config.Render.EnableValidation = false;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Simulation.WorkerThreadCount = 1u;

        Runtime::Engine engine{std::move(config)};
        engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
        auto driver = std::make_unique<LopGpuBenchmarkDriver>(
            engine, fixtures, references);
        LopGpuBenchmarkDriver* const driverPointer = driver.get();
        engine.AddModule(std::move(driver));
        engine.Initialize();

        const auto operationalInputs =
            Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(
                &engine.GetDevice());
        if (!operationalInputs.LogicalDeviceReady ||
            !operationalInputs.SwapchainReady ||
            !operationalInputs.CommandSyncReady)
        {
            diagnostic =
                "Promoted Vulkan did not reach device/swapchain/command readiness.";
            skipped = true;
            engine.Shutdown();
            return {};
        }

        std::string deviceDiagnostic = "operational Vulkan device";
        if (Extrinsic::RHI::IProfiler* profiler =
                engine.GetDevice().GetProfiler();
            profiler != nullptr)
        {
            deviceDiagnostic = profiler->GetStatus().Diagnostic;
        }

        engine.Run();
        GpuBenchmarkResult result = driverPointer->Result();
        result.DeviceDiagnostic = std::move(deviceDiagnostic);
        engine.Shutdown();
        diagnostic = result.Diagnostic;
        return result;
    }

    [[nodiscard]] std::filesystem::path ResolveOutputPath(
        const std::filesystem::path& argument)
    {
        std::error_code error{};
        const bool directory =
            std::filesystem::is_directory(argument, error) ||
            !argument.has_extension();
        if (directory)
        {
            std::filesystem::create_directories(argument, error);
            return argument / (std::string{kBenchmarkId} + ".json");
        }
        if (!argument.parent_path().empty())
            std::filesystem::create_directories(argument.parent_path(), error);
        return argument;
    }

    [[nodiscard]] bool WriteFile(
        const std::filesystem::path& path,
        const std::string_view payload)
    {
        std::ofstream file{path, std::ios::trunc};
        if (!file.is_open())
            return false;
        file << payload;
        return file.good();
    }
}

int main(const int argc, char** argv)
{
    const std::filesystem::path outputArgument = argc > 1
        ? std::filesystem::path{argv[1]}
        : std::filesystem::path{
              "geometry.lop_family.gpu_vulkan.v3.smoke.json"};
    const std::filesystem::path outputPath =
        ResolveOutputPath(outputArgument);
    const std::array<Fixture, 2u> fixtures = MakeFixtures();
    const std::array<CpuFixtureResult, 2u> cpu =
        RunCpuReferences(fixtures);

    std::string diagnostic{};
    bool skipped = false;
    GpuBenchmarkResult gpu{};
    if (!cpu[0].Succeeded || !cpu[1].Succeeded)
    {
        diagnostic = "cpu_reference failed the frozen confirmation fixture";
    }
    else
    {
        gpu = RunGpuMeasurements(fixtures, cpu, diagnostic, skipped);
    }

    const bool passed = !skipped && cpu[0].Succeeded && cpu[1].Succeeded &&
        gpu.Succeeded && gpu.Fixtures[0].Succeeded &&
        gpu.Fixtures[1].Succeeded;
    const bool gpuExecutionObserved = gpu.Stats.GpuRequestsAccepted != 0u ||
        gpu.Stats.GpuCompletions != 0u || gpu.Stats.GpuFallbacks != 0u;
    const std::string_view actualBackend = gpu.Stats.GpuFallbacks != 0u
        ? "cpu_reference"
        : gpuExecutionObserved ? "gpu_vulkan_compute" : "none";
    const std::string_view status = skipped
        ? "skipped"
        : passed ? "passed" : "failed";
    const double runtimeMilliseconds =
        (gpu.Fixtures[0].RuntimeMilliseconds +
         gpu.Fixtures[1].RuntimeMilliseconds) /
        2.0;
    const double qualityError = std::max({
        gpu.Fixtures[0].MaxParityRms,
        gpu.Fixtures[1].MaxParityRms,
        gpu.Fixtures[0].MaxRepeatRms,
        gpu.Fixtures[1].MaxRepeatRms,
    });
    const double qualityErrorLinf = std::max({
        gpu.Fixtures[0].MaxParityLinf,
        gpu.Fixtures[1].MaxParityLinf,
        gpu.Fixtures[0].MaxRepeatLinf,
        gpu.Fixtures[1].MaxRepeatLinf,
    });

    std::ostringstream output{};
    output.setf(std::ios::fixed);
    output.precision(9);
    output << "{\n"
           << "  \"benchmark_id\": \"" << EscapeJson(kBenchmarkId)
           << "\",\n"
           << "  \"method\": \"" << EscapeJson(kMethod) << "\",\n"
           << "  \"backend\": \"gpu_vulkan_compute\",\n"
           << "  \"dataset\": \"" << EscapeJson(kDataset) << "\",\n"
           << "  \"commit\": \"" << EscapeJson(ResolveCommit())
           << "\",\n"
           << "  \"metrics\": {\n"
           << "    \"runtime_ms\": " << runtimeMilliseconds << ",\n"
           << "    \"gpu_time_ms\": " << runtimeMilliseconds << ",\n"
           << "    \"quality_error_l2\": " << qualityError << ",\n"
           << "    \"quality_error_linf\": " << qualityErrorLinf
           << "\n"
           << "  },\n"
           << "  \"diagnostics\": {\n"
           << "    \"runner\": \"IntrinsicLopFamilyGpuBenchmarkSmoke\",\n"
           << "    \"mode\": \"gpu_smoke\",\n"
           << "    \"execution_route\": \"PointCloudConsolidationService\",\n"
           << "    \"requested_backend\": \"gpu_vulkan_compute\",\n"
           << "    \"warmup_iterations\": " << kWarmupIterations << ",\n"
           << "    \"measured_iterations\": " << kMeasuredIterations
           << ",\n"
           << "    \"actual_backend\": \"" << actualBackend << "\",\n"
           << "    \"execution_observed\": "
           << (gpuExecutionObserved ? "true" : "false") << ",\n"
           << "    \"fallback_allowed\": false,\n"
           << "    \"fallback_observed\": "
           << (!gpuExecutionObserved
                   ? "null"
                   : gpu.Stats.GpuFallbacks == 0u ? "false" : "true")
           << ",\n"
           << "    \"baseline_comparison\": \"cpu_reference_same_fixture\",\n"
           << "    \"speedup_claimed\": false,\n"
           << "    \"gpu_time_source\": \"host_service_command_to_applied_event\",\n"
           << "    \"device_identity\": \""
           << EscapeJson(gpu.DeviceDiagnostic) << "\",\n"
           << "    \"lop_gpu_runtime_ms\": "
           << gpu.Fixtures[0].RuntimeMilliseconds << ",\n"
           << "    \"wlop_gpu_runtime_ms\": "
           << gpu.Fixtures[1].RuntimeMilliseconds << ",\n"
           << "    \"lop_cpu_runtime_ms\": "
           << cpu[0].RuntimeMilliseconds << ",\n"
           << "    \"wlop_cpu_runtime_ms\": "
           << cpu[1].RuntimeMilliseconds << ",\n"
           << "    \"lop_rms_delta\": "
           << gpu.Fixtures[0].MaxParityRms << ",\n"
           << "    \"lop_linf_delta\": "
           << gpu.Fixtures[0].MaxParityLinf << ",\n"
           << "    \"wlop_rms_delta\": "
           << gpu.Fixtures[1].MaxParityRms << ",\n"
           << "    \"wlop_linf_delta\": "
           << gpu.Fixtures[1].MaxParityLinf << ",\n"
           << "    \"lop_repeat_rms_delta\": "
           << gpu.Fixtures[0].MaxRepeatRms << ",\n"
           << "    \"lop_repeat_linf_delta\": "
           << gpu.Fixtures[0].MaxRepeatLinf << ",\n"
           << "    \"wlop_repeat_rms_delta\": "
           << gpu.Fixtures[1].MaxRepeatRms << ",\n"
           << "    \"wlop_repeat_linf_delta\": "
           << gpu.Fixtures[1].MaxRepeatLinf << ",\n"
           << "    \"gpu_requests_accepted\": "
           << gpu.Stats.GpuRequestsAccepted << ",\n"
           << "    \"gpu_fallbacks\": " << gpu.Stats.GpuFallbacks
           << ",\n"
           << "    \"gpu_completions\": " << gpu.Stats.GpuCompletions
           << ",\n"
           << "    \"diagnostic\": \"" << EscapeJson(diagnostic)
           << "\"\n"
           << "  },\n"
           << "  \"status\": \"" << status << "\"\n"
           << "}\n";

    if (!WriteFile(outputPath, output.str()))
        return 1;
    return passed ? 0 : 2;
}
