// RUNTIME-196 - opt-in production ClusteringService Vulkan benchmark runner.
//
// This runner remains separate from IntrinsicBenchmarkSmoke because it composes
// the runtime/Vulkan stack and requires a promoted Vulkan device. It emits one
// result JSON for the schema validators under tools/benchmark/.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SelectionController;
import Geometry.KMeans;
import Geometry.Properties;

namespace
{
    namespace ECS = Extrinsic::ECS;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
    namespace Runtime = Extrinsic::Runtime;
    namespace GK = Geometry::KMeans;

    inline constexpr const char* kBenchmarkId =
        "geometry.kmeans.gpu_vulkan.smoke";
    inline constexpr const char* kMethod = "geometry.kmeans";
    inline constexpr const char* kDataset =
        "builtin.kmeans.separated_clusters_6x2";
    inline constexpr int kWarmupIterations = 1;
    inline constexpr int kMeasuredIterations = 3;
    inline constexpr Runtime::KMeansParameters kParameters{
        .ClusterCount = 2u,
        .MaxIterations = 6u,
        .Seed = 13u,
        .Initialization = Runtime::KMeansInitialization::Hierarchical,
    };

    struct Fixture
    {
        std::array<glm::vec3, 6> Points{{
            glm::vec3{-2.0f, 0.0f, 0.0f},
            glm::vec3{-1.0f, 1.0f, 0.0f},
            glm::vec3{-1.0f, -1.0f, 0.0f},
            glm::vec3{8.0f, 0.0f, 0.0f},
            glm::vec3{9.0f, 1.0f, 0.0f},
            glm::vec3{9.0f, -1.0f, 0.0f},
        }};
    };

    struct GpuRunResult
    {
        Runtime::KMeansRunCompleted Completion{};
        Runtime::ClusteringModuleStats Stats{};
        std::vector<std::uint32_t> Labels{};
        std::vector<glm::vec3> Centroids{};
        std::string Diagnostic{};
        double RuntimeMilliseconds{0.0};
        bool ColorsCommitted{false};
        bool Succeeded{false};
    };

    struct CpuRunResult
    {
        GK::KMeansResult Result{};
        double RuntimeMilliseconds{0.0};
        bool Succeeded{false};
    };

    [[nodiscard]] std::string EscapeJson(const std::string_view input)
    {
        std::string out{};
        out.reserve(input.size());
        for (const char ch : input)
        {
            switch (ch)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
            }
        }
        return out;
    }

    [[nodiscard]] std::string ResolveCommit()
    {
        const char* env = std::getenv("GIT_COMMIT");
        return env != nullptr && env[0] != '\0' ? std::string{env}
                                                : std::string{"local-dev"};
    }

    void SetPositions(
        GS::Vertices& vertices,
        const std::span<const glm::vec3> positions)
    {
        vertices.Properties.Resize(positions.size());
        auto property = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        property.Vector().assign(positions.begin(), positions.end());
    }

    [[nodiscard]] GK::KMeansParams MakeCpuParameters()
    {
        GK::KMeansParams params{};
        params.ClusterCount = kParameters.ClusterCount;
        params.MaxIterations = kParameters.MaxIterations;
        params.Seed = kParameters.Seed;
        params.ConvergenceTolerance = 1.0e-5f;
        params.Init = GK::Initialization::Hierarchical;
        params.Compute = GK::Backend::CPU;
        return params;
    }

    [[nodiscard]] std::vector<glm::vec3> ComputeCentroids(
        const std::span<const glm::vec3> points,
        const std::span<const std::uint32_t> labels,
        const std::uint32_t clusterCount)
    {
        std::vector<glm::vec3> centroids(clusterCount, glm::vec3{0.0f});
        std::vector<std::uint32_t> counts(clusterCount, 0u);
        const std::size_t count = std::min(points.size(), labels.size());
        for (std::size_t index = 0u; index < count; ++index)
        {
            if (labels[index] >= clusterCount)
                continue;
            centroids[labels[index]] += points[index];
            counts[labels[index]] += 1u;
        }
        for (std::uint32_t cluster = 0u; cluster < clusterCount; ++cluster)
        {
            if (counts[cluster] > 0u)
            {
                centroids[cluster] /= static_cast<float>(counts[cluster]);
            }
        }
        return centroids;
    }

    class ClusteringBenchmarkDriver final : public Runtime::IRuntimeModule
    {
    public:
        ClusteringBenchmarkDriver(Runtime::Engine& engine,
                                  const Fixture& fixture) noexcept
            : m_Engine{engine}
            , m_Fixture{fixture}
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "Benchmark.ClusteringServiceDriver";
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
            m_Service = setup.Services().Find<Runtime::ClusteringService>();
            if (m_Service == nullptr || !m_Service->Available())
            {
                Fail("ClusteringService is unavailable.");
                return Runtime::RuntimeModuleOk();
            }

            auto* scene = setup.Worlds().Get(m_Engine.ActiveWorld());
            if (scene == nullptr)
            {
                Fail("The active runtime world is unavailable.");
                return Runtime::RuntimeModuleOk();
            }

            m_Entity = scene->Create();
            auto& vertices = scene->Raw().emplace<GS::Vertices>(m_Entity);
            SetPositions(vertices, m_Fixture.Points);
            m_StableEntityId =
                Runtime::SelectionController::ToStableEntityId(m_Entity);
            m_CompletionSubscription = m_Service->SubscribeRunCompleted(
                [this](const Runtime::KMeansRunCompleted& completed)
                {
                    if (m_Submitted &&
                        completed.Correlation == m_ActiveCorrelation)
                    {
                        m_PendingCompletion = completed;
                        m_CompletionReceivedAt =
                            std::chrono::steady_clock::now();
                    }
                });
            m_Deadline = std::chrono::steady_clock::now() +
                         std::chrono::seconds{30};
            return Runtime::RuntimeModuleOk();
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext& context) override
        {
            if (m_CompletionSubscription.IsValid())
                context.Events.Unsubscribe(m_CompletionSubscription);
            m_CompletionSubscription = {};
            m_Service = nullptr;
        }

        [[nodiscard]] GpuRunResult Result() const
        {
            GpuRunResult result{
                .Stats = m_Stats,
                .Labels = m_Labels,
                .Centroids = m_Centroids,
                .Diagnostic = m_Diagnostic,
                .RuntimeMilliseconds = m_MeasuredMilliseconds /
                    static_cast<double>(kMeasuredIterations),
                .ColorsCommitted = m_ColorsCommitted,
                .Succeeded = m_Succeeded,
            };
            if (m_LastCompletion.has_value())
                result.Completion = *m_LastCompletion;
            return result;
        }

    private:
        void Fail(std::string diagnostic)
        {
            m_Diagnostic = std::move(diagnostic);
            m_Failed = true;
            m_Engine.RequestExit();
        }

        void CaptureCommittedProperties()
        {
            auto* scene = m_Engine.Worlds().Get(m_Engine.ActiveWorld());
            if (scene == nullptr || !scene->Raw().valid(m_Entity) ||
                !scene->Raw().all_of<GS::Vertices>(m_Entity))
            {
                Fail("The benchmark entity became stale before result capture.");
                return;
            }

            auto& properties =
                scene->Raw().get<GS::Vertices>(m_Entity).Properties;
            const auto labels =
                properties.Get<std::uint32_t>("p:kmeans_label");
            if (!labels)
            {
                Fail("ClusteringService did not commit p:kmeans_label.");
                return;
            }
            m_Labels = labels.Vector();
            m_Centroids = ComputeCentroids(
                m_Fixture.Points,
                m_Labels,
                kParameters.ClusterCount);
            m_ColorsCommitted = static_cast<bool>(
                properties.Get<glm::vec4>("p:kmeans_color"));
        }

        void ConsumeCompletion()
        {
            const Runtime::KMeansRunCompleted completed =
                std::move(*m_PendingCompletion);
            m_PendingCompletion.reset();
            m_Submitted = false;

            if (!completed.Succeeded())
            {
                Fail(completed.Message.empty()
                         ? std::string{Runtime::ToString(completed.Status)}
                         : completed.Message);
                return;
            }
            if (completed.ActualBackend !=
                    Runtime::ClusteringBackend::VulkanCompute ||
                completed.FellBackToCpu)
            {
                Fail(completed.BackendDiagnostic.empty()
                         ? "ClusteringService did not execute Vulkan compute."
                         : completed.BackendDiagnostic);
                return;
            }

            CaptureCommittedProperties();
            if (m_Failed)
                return;

            const double elapsedMilliseconds = static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    m_CompletionReceivedAt - m_SubmittedAt)
                    .count()) *
                1.0e-6;
            if (m_Iteration >= kWarmupIterations)
                m_MeasuredMilliseconds += elapsedMilliseconds;

            m_LastCompletion = completed;
            m_Stats = m_Service->Stats();
            ++m_Iteration;
            if (m_Iteration >=
                kWarmupIterations + kMeasuredIterations)
            {
                m_Succeeded = true;
                m_Diagnostic =
                    "clustering_service_vulkan_compute completed";
                m_Engine.RequestExit();
            }
        }

        void Submit()
        {
            m_SubmittedAt = std::chrono::steady_clock::now();
            m_ActiveCorrelation = m_Service->RunKMeans(Runtime::RunKMeans{
                .StableEntityId = m_StableEntityId,
                .Properties = Runtime::MakeKMeansPropertyRefs(
                    Runtime::GeometryElementDomain::PointCloudPoint),
                .Parameters = kParameters,
                .Backend = Runtime::ClusteringBackend::VulkanCompute,
            });
            if (!m_ActiveCorrelation.IsValid())
            {
                Fail("ClusteringService returned an invalid correlation.");
                return;
            }
            m_Submitted = true;
        }

        void Frame(Runtime::RuntimeFrameHookContext&)
        {
            if (m_Failed || m_Succeeded)
                return;
            if (std::chrono::steady_clock::now() >= m_Deadline)
            {
                Fail("Timed out waiting for ClusteringService completion.");
                return;
            }
            if (m_PendingCompletion.has_value())
                ConsumeCompletion();
            if (!m_Failed && !m_Succeeded && !m_Submitted &&
                m_Engine.GetDevice().IsOperational())
            {
                Submit();
            }
        }

        Runtime::Engine& m_Engine;
        const Fixture& m_Fixture;
        Runtime::ClusteringService* m_Service{};
        Runtime::KernelEventSubscription m_CompletionSubscription{};
        Runtime::CommandCorrelationId m_ActiveCorrelation{};
        Runtime::ClusteringModuleStats m_Stats{};
        std::optional<Runtime::KMeansRunCompleted> m_PendingCompletion{};
        std::optional<Runtime::KMeansRunCompleted> m_LastCompletion{};
        std::vector<std::uint32_t> m_Labels{};
        std::vector<glm::vec3> m_Centroids{};
        ECS::EntityHandle m_Entity{ECS::InvalidEntityHandle};
        std::uint32_t m_StableEntityId{0u};
        std::chrono::steady_clock::time_point m_SubmittedAt{};
        std::chrono::steady_clock::time_point m_CompletionReceivedAt{};
        std::chrono::steady_clock::time_point m_Deadline{};
        std::string m_Diagnostic{};
        double m_MeasuredMilliseconds{0.0};
        int m_Iteration{0};
        bool m_Submitted{false};
        bool m_ColorsCommitted{false};
        bool m_Succeeded{false};
        bool m_Failed{false};
    };

    [[nodiscard]] CpuRunResult RunCpuReference(const Fixture& fixture)
    {
        const GK::KMeansParams params = MakeCpuParameters();
        for (int iteration = 0; iteration < kWarmupIterations; ++iteration)
        {
            (void)GK::Cluster(fixture.Points, params);
        }

        std::optional<GK::KMeansResult> last{};
        const auto started = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < kMeasuredIterations; ++iteration)
        {
            last = GK::Cluster(fixture.Points, params);
        }
        const auto completed = std::chrono::steady_clock::now();
        if (!last.has_value())
            return {};

        const double totalMilliseconds = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                completed - started)
                .count()) *
            1.0e-6;
        return CpuRunResult{
            .Result = std::move(*last),
            .RuntimeMilliseconds = totalMilliseconds /
                static_cast<double>(kMeasuredIterations),
            .Succeeded = true,
        };
    }

    [[nodiscard]] GpuRunResult RunGpuMeasurements(const Fixture& fixture,
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
        config.Window.Title =
            "Intrinsic ClusteringService gpu;vulkan benchmark";
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
        engine.EmplaceModule<Runtime::ClusteringModule>();
        auto driver =
            std::make_unique<ClusteringBenchmarkDriver>(engine, fixture);
        ClusteringBenchmarkDriver* const driverPtr = driver.get();
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

        engine.Run();
        GpuRunResult result = driverPtr->Result();
        engine.Shutdown();
        diagnostic = result.Diagnostic;
        return result;
    }

    [[nodiscard]] std::uint32_t CountLabelMismatches(
        const std::span<const std::uint32_t> lhs,
        const std::span<const std::uint32_t> rhs)
    {
        std::uint32_t mismatches = 0u;
        const std::size_t count = std::min(lhs.size(), rhs.size());
        for (std::size_t index = 0u; index < count; ++index)
        {
            if (lhs[index] != rhs[index])
                ++mismatches;
        }
        return mismatches + static_cast<std::uint32_t>(
                                std::max(lhs.size(), rhs.size()) - count);
    }

    [[nodiscard]] float MaxCentroidDelta(
        const std::span<const glm::vec3> lhs,
        const std::span<const glm::vec3> rhs)
    {
        float maxDelta = 0.0f;
        const std::size_t count = std::min(lhs.size(), rhs.size());
        for (std::size_t index = 0u; index < count; ++index)
            maxDelta = std::max(
                maxDelta,
                glm::length(lhs[index] - rhs[index]));
        return maxDelta;
    }

    [[nodiscard]] std::filesystem::path ResolveOutputPath(
        const std::filesystem::path& argument)
    {
        std::error_code error{};
        const bool existingDirectory =
            std::filesystem::is_directory(argument, error);
        const bool looksLikeDirectory =
            !argument.has_filename() || !argument.has_extension();
        if (existingDirectory || looksLikeDirectory)
        {
            std::filesystem::create_directories(argument, error);
            return argument / (std::string{kBenchmarkId} + ".json");
        }
        if (!argument.parent_path().empty())
            std::filesystem::create_directories(argument.parent_path(), error);
        return argument;
    }

    [[nodiscard]] bool WriteFile(const std::filesystem::path& path,
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
        : std::filesystem::path{"geometry.kmeans.gpu_vulkan.smoke.json"};
    const std::filesystem::path outputPath =
        ResolveOutputPath(outputArgument);
    const std::string commit = ResolveCommit();
    const Fixture fixture{};

    const CpuRunResult cpu = RunCpuReference(fixture);
    std::string diagnostic{};
    bool skipped = false;
    const GpuRunResult gpu =
        RunGpuMeasurements(fixture, diagnostic, skipped);

    std::string status = skipped ? "skipped" : "failed";
    std::uint32_t labelMismatches = 0u;
    float centroidDelta = 0.0f;
    float inertiaDelta = 0.0f;
    bool maxDistanceIndexMatches = false;

    if (!skipped && gpu.Succeeded && cpu.Succeeded)
    {
        labelMismatches =
            CountLabelMismatches(gpu.Labels, cpu.Result.Labels);
        centroidDelta =
            MaxCentroidDelta(gpu.Centroids, cpu.Result.Centroids);
        inertiaDelta =
            std::abs(gpu.Completion.Inertia - cpu.Result.Inertia);
        maxDistanceIndexMatches =
            gpu.Completion.MaxDistanceIndex == cpu.Result.MaxDistanceIndex;

        const bool parity = labelMismatches == 0u &&
                            centroidDelta <= 1.0e-4f &&
                            inertiaDelta <= 1.0e-4f &&
                            maxDistanceIndexMatches &&
                            gpu.ColorsCommitted;
        status = parity ? "passed" : "failed";
        diagnostic = parity
            ? "clustering_service_vulkan_compute matches cpu_reference"
            : "clustering_service_vulkan_compute parity mismatch";
    }
    else if (!skipped && !cpu.Succeeded)
    {
        diagnostic = "cpu_reference failed";
    }

    const double ratio = gpu.RuntimeMilliseconds > 0.0
        ? cpu.RuntimeMilliseconds / gpu.RuntimeMilliseconds
        : 0.0;
    const double qualityError = std::sqrt(
        static_cast<double>(labelMismatches * labelMismatches) +
        static_cast<double>(centroidDelta * centroidDelta) +
        static_cast<double>(inertiaDelta * inertiaDelta));

    std::ostringstream output{};
    output.setf(std::ios::fixed);
    output.precision(6);
    output << "{\n"
           << "  \"benchmark_id\": \"" << EscapeJson(kBenchmarkId)
           << "\",\n"
           << "  \"method\": \"" << EscapeJson(kMethod) << "\",\n"
           << "  \"backend\": \"gpu_vulkan_compute\",\n"
           << "  \"dataset\": \"" << EscapeJson(kDataset) << "\",\n"
           << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
           << "  \"metrics\": {\n"
           << "    \"runtime_ms\": " << gpu.RuntimeMilliseconds << ",\n"
           << "    \"gpu_time_ms\": " << gpu.RuntimeMilliseconds << ",\n"
           << "    \"quality_error_l2\": " << qualityError << "\n"
           << "  },\n"
           << "  \"diagnostics\": {\n"
           << "    \"runner\": \"IntrinsicKMeansGpuBenchmarkSmoke\",\n"
           << "    \"mode\": \"gpu_smoke\",\n"
           << "    \"execution_route\": \"ClusteringService\",\n"
           << "    \"warmup_iterations\": " << kWarmupIterations << ",\n"
           << "    \"measured_iterations\": " << kMeasuredIterations
           << ",\n"
           << "    \"point_count\": " << fixture.Points.size() << ",\n"
           << "    \"cluster_count\": " << kParameters.ClusterCount << ",\n"
           << "    \"max_iterations\": " << kParameters.MaxIterations
           << ",\n"
           << "    \"seed\": " << kParameters.Seed << ",\n"
           << "    \"initialization\": \"Hierarchical\",\n"
           << "    \"cpu_reference_runtime_ms\": "
           << cpu.RuntimeMilliseconds << ",\n"
           << "    \"cpu_to_gpu_runtime_ratio\": " << ratio << ",\n"
           << "    \"baseline_comparison\": \"cpu_reference_same_fixture\",\n"
           << "    \"speedup_claimed\": false,\n"
           << "    \"gpu_time_source\": \"service_command_to_applied_event\",\n"
           << "    \"label_mismatch_count\": " << labelMismatches << ",\n"
           << "    \"max_centroid_delta\": " << centroidDelta << ",\n"
           << "    \"inertia_delta\": " << inertiaDelta << ",\n"
           << "    \"max_distance_index_matches\": "
           << (maxDistanceIndexMatches ? "true" : "false") << ",\n"
           << "    \"colors_committed\": "
           << (gpu.ColorsCommitted ? "true" : "false") << ",\n"
           << "    \"gpu_requests_accepted\": "
           << gpu.Stats.GpuRequestsAccepted << ",\n"
           << "    \"gpu_fallbacks\": " << gpu.Stats.GpuFallbacks << ",\n"
           << "    \"gpu_completions\": " << gpu.Stats.GpuCompletions
           << ",\n"
           << "    \"labels_committed\": " << gpu.Stats.LabelsCommitted
           << ",\n"
           << "    \"diagnostic\": \"" << EscapeJson(diagnostic)
           << "\"\n"
           << "  },\n"
           << "  \"status\": \"" << status << "\"\n"
           << "}\n";

    if (!WriteFile(outputPath, output.str()))
        return 1;
    return status == "failed" ? 2 : 0;
}
