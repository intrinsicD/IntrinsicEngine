// GEOM-056 Slice D - opt-in Vulkan KMeans benchmark runner.
//
// This runner is separate from IntrinsicBenchmarkSmoke because it imports the
// runtime/Vulkan stack and requires a promoted Vulkan device. It emits one
// result JSON for the schema validators under tools/benchmark/.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.KMeansGpuBackend;
import Geometry.KMeans;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;
    namespace GK = Geometry::KMeans;

    inline constexpr const char* kBenchmarkId = "geometry.kmeans.gpu_vulkan.smoke";
    inline constexpr const char* kMethod = "geometry.kmeans";
    inline constexpr const char* kDataset = "builtin.kmeans.separated_clusters_6x2";
    inline constexpr int kWarmupIterations = 1;
    inline constexpr int kMeasuredIterations = 3;

    class NoopCommandContext final : public RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
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
        std::array<glm::vec3, 2> Seeds{{
            Points[0],
            Points[3],
        }};
        GK::KMeansParams Params{};

        Fixture()
        {
            Params.ClusterCount = static_cast<std::uint32_t>(Seeds.size());
            Params.MaxIterations = 6u;
            Params.ConvergenceTolerance = 1.0e-5f;
            Params.Compute = GK::Backend::CPU;
        }
    };

    struct BootstrapResult
    {
        std::unique_ptr<Extrinsic::Platform::IWindow> Window{};
        std::unique_ptr<RHI::IDevice> Device{};
        std::string Diagnostic{};
        bool Skipped{false};
    };

    struct GpuRunResult
    {
        Runtime::KMeansGpuReadbackResult Readback{};
        std::string Diagnostic{};
        double RuntimeMilliseconds{0.0};
        bool Succeeded{false};
    };

    struct CpuRunResult
    {
        GK::KMeansResult Result{};
        double RuntimeMilliseconds{0.0};
        bool Succeeded{false};
    };

    [[nodiscard]] std::string EscapeJson(std::string_view input)
    {
        std::string out;
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
        return (env != nullptr && env[0] != '\0') ? std::string{env} : std::string{"local-dev"};
    }

    [[nodiscard]] BootstrapResult BootstrapVulkan()
    {
        if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        {
            return BootstrapResult{
                .Diagnostic = "GLFW could not initialize; gpu;vulkan KMeans benchmark is opt-in.",
                .Skipped = true,
            };
        }

        Extrinsic::Core::Config::WindowConfig windowConfig{};
        windowConfig.Title = "Intrinsic KMeans gpu;vulkan benchmark";
        windowConfig.Width = 64;
        windowConfig.Height = 64;
        windowConfig.Resizable = false;

        std::unique_ptr<Extrinsic::Platform::IWindow> window =
            Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
        if (window == nullptr || window->GetNativeHandle() == nullptr)
        {
            return BootstrapResult{
                .Diagnostic = "GLFW window creation failed.",
                .Skipped = true,
            };
        }

        Extrinsic::Core::Config::RenderConfig renderConfig{};
        renderConfig.EnablePromotedVulkanDevice = true;
        renderConfig.EnableValidation = false;

        std::unique_ptr<RHI::IDevice> device =
            Extrinsic::Backends::Vulkan::CreateVulkanDevice();
        if (device == nullptr)
        {
            return BootstrapResult{
                .Diagnostic = "Vulkan device factory returned null.",
                .Skipped = true,
            };
        }

        device->Initialize(RHI::MakeDeviceCreateDesc(renderConfig,
                                                     window->GetFramebufferExtent(),
                                                     window->GetNativeHandle()));

        const auto bootstrap =
            Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
        const auto services =
            Extrinsic::Backends::Vulkan::GetVulkanServiceDiagnosticsSnapshot();
        if (bootstrap.Status != Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages ||
            services.Status != Extrinsic::Backends::Vulkan::VulkanServiceBootstrapStatus::Ready ||
            !services.PublicTransferQueueExposed ||
            !bootstrap.BufferDeviceAddressEnabled)
        {
            device->Shutdown();
            return BootstrapResult{
                .Diagnostic = "Promoted Vulkan did not reach service-ready BDA/transfer state.",
                .Skipped = true,
            };
        }

        device->NoteRecipeGraphValidation(true);
        if (!device->IsOperational())
        {
            device->Shutdown();
            return BootstrapResult{
                .Diagnostic = "Promoted Vulkan operational gate did not flip.",
                .Skipped = true,
            };
        }

        return BootstrapResult{
            .Window = std::move(window),
            .Device = std::move(device),
        };
    }

    [[nodiscard]] bool SubmitNoopFrame(RHI::IDevice& device)
    {
        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;
        RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        return true;
    }

    [[nodiscard]] bool SubmitComputeFrame(RHI::IDevice& device,
                                          Runtime::KMeansGpuResourceCache& cache,
                                          Runtime::KMeansGpuPipelineSet pipelines,
                                          const Fixture& fixture,
                                          Runtime::KMeansGpuExecutionResult& out)
    {
        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;

        RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        out = Runtime::RecordKMeansGpuExecution(Runtime::KMeansGpuExecutionDesc{
            .Device = &device,
            .CommandContext = &cmd,
            .ResourceCache = &cache,
            .Pipelines = pipelines,
            .Points = fixture.Points,
            .SeedCentroids = fixture.Seeds,
            .Plan = Runtime::KMeansGpuPlanDesc{
                .PointCount = static_cast<std::uint32_t>(fixture.Points.size()),
                .ClusterCount = static_cast<std::uint32_t>(fixture.Seeds.size()),
                .MaxIterations = fixture.Params.MaxIterations,
                .GroupSize = Runtime::kKMeansGpuGroupSize,
                .ConvergenceTolerance = fixture.Params.ConvergenceTolerance,
            },
        });
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        return true;
    }

    [[nodiscard]] bool SubmitReadbacksAfterProducerRetired(
        RHI::IDevice& device,
        Runtime::KMeansGpuAsyncReadbacks& readbacks,
        const Runtime::KMeansGpuExecutionResources& resources)
    {
        const std::uint32_t framesInFlight = std::max(device.GetFramesInFlight(), 1u);
        for (std::uint32_t frame = 1u; frame < framesInFlight; ++frame)
        {
            if (!SubmitNoopFrame(device))
                return false;
        }

        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;
        RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        const bool queued = readbacks.Enqueue(cmd, resources);
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        return queued;
    }

    [[nodiscard]] bool DrainReadbacks(RHI::ITransferQueue& queue,
                                      Graphics::GpuTransfer& transfer,
                                      Runtime::KMeansGpuAsyncReadbacks& readbacks)
    {
        NoopCommandContext noop;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            transfer.DrainCompleted(noop);
            if (readbacks.Poll())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        queue.CollectCompleted();
        transfer.DrainCompleted(noop);
        return readbacks.Poll();
    }

    [[nodiscard]] CpuRunResult RunCpuReference(const Fixture& fixture)
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            GK::CpuScratch scratch{};
            (void)GK::Cluster(fixture.Points, fixture.Seeds, fixture.Params, &scratch);
        }

        std::optional<GK::KMeansResult> last;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            GK::CpuScratch scratch{};
            last = GK::Cluster(fixture.Points, fixture.Seeds, fixture.Params, &scratch);
        }
        const auto t1 = std::chrono::steady_clock::now();

        if (!last.has_value())
            return {};

        const double totalMs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) * 1.0e-6;
        return CpuRunResult{
            .Result = *last,
            .RuntimeMilliseconds = totalMs / static_cast<double>(kMeasuredIterations),
            .Succeeded = true,
        };
    }

    [[nodiscard]] GpuRunResult RunGpuMeasurements(RHI::IDevice& device,
                                                  Runtime::KMeansGpuPipelineSet pipelines,
                                                  const Fixture& fixture)
    {
        RHI::BufferManager buffers{device};
        Runtime::KMeansGpuResourceCache cache{buffers};
        RHI::ITransferQueue& queue = device.GetTransferQueue();
        Graphics::GpuTransfer transfer{queue};
        Runtime::KMeansGpuAsyncReadbacks readbacks{transfer};

        GpuRunResult last{};
        double measuredMs = 0.0;
        const int totalIterations = kWarmupIterations + kMeasuredIterations;
        for (int iteration = 0; iteration < totalIterations; ++iteration)
        {
            Runtime::KMeansGpuExecutionResult execution{};
            const auto t0 = std::chrono::steady_clock::now();
            if (!SubmitComputeFrame(device, cache, pipelines, fixture, execution))
            {
                return GpuRunResult{.Diagnostic = "failed to submit KMeans compute frame"};
            }
            if (!execution.Succeeded() || !execution.Recorded || execution.Resources == nullptr)
            {
                return GpuRunResult{
                    .Diagnostic = Runtime::DebugNameForKMeansGpuStatus(execution.Status),
                };
            }
            if (!SubmitReadbacksAfterProducerRetired(device, readbacks, *execution.Resources))
            {
                return GpuRunResult{.Diagnostic = "failed to enqueue async readbacks"};
            }
            if (!DrainReadbacks(queue, transfer, readbacks))
            {
                return GpuRunResult{.Diagnostic = "timed out waiting for async readbacks"};
            }
            const auto t1 = std::chrono::steady_clock::now();

            last.Readback = readbacks.Collect(execution.Plan);
            if (!last.Readback.Succeeded())
            {
                last.Diagnostic = last.Readback.Diagnostic;
                return last;
            }
            readbacks.Reset();

            if (iteration >= kWarmupIterations)
            {
                measuredMs += static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) * 1.0e-6;
            }
        }

        last.RuntimeMilliseconds = measuredMs / static_cast<double>(kMeasuredIterations);
        last.Succeeded = true;
        return last;
    }

    [[nodiscard]] std::uint32_t CountLabelMismatches(
        std::span<const std::uint32_t> a,
        std::span<const std::uint32_t> b)
    {
        std::uint32_t mismatches = 0u;
        const std::size_t count = std::min(a.size(), b.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            if (a[index] != b[index])
                ++mismatches;
        }
        return mismatches +
               static_cast<std::uint32_t>(std::max(a.size(), b.size()) - count);
    }

    [[nodiscard]] float MaxCentroidDelta(std::span<const glm::vec3> a,
                                         std::span<const glm::vec3> b)
    {
        float maxDelta = 0.0f;
        const std::size_t count = std::min(a.size(), b.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            maxDelta = std::max(maxDelta, glm::length(a[index] - b[index]));
        }
        return maxDelta;
    }

    [[nodiscard]] std::filesystem::path ResolveOutputPath(const std::filesystem::path& arg)
    {
        std::error_code ec;
        const bool isExistingDir = std::filesystem::is_directory(arg, ec);
        const bool looksLikeDir = !arg.has_filename() || !arg.has_extension();
        if (isExistingDir || looksLikeDir)
        {
            std::filesystem::create_directories(arg, ec);
            return arg / (std::string{kBenchmarkId} + ".json");
        }
        if (!arg.parent_path().empty())
        {
            std::filesystem::create_directories(arg.parent_path(), ec);
        }
        return arg;
    }

    [[nodiscard]] bool WriteFile(const std::filesystem::path& path,
                                 std::string_view payload)
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
            return false;
        file << payload;
        return file.good();
    }
}

int main(int argc, char** argv)
{
    const std::filesystem::path outArg = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("geometry.kmeans.gpu_vulkan.smoke.json");
    const std::filesystem::path outPath = ResolveOutputPath(outArg);
    const std::string commit = ResolveCommit();
    const Fixture fixture{};

    CpuRunResult cpu = RunCpuReference(fixture);
    BootstrapResult bootstrap = BootstrapVulkan();

    std::string status = "failed";
    std::string diagnostic = bootstrap.Diagnostic;
    GpuRunResult gpu{};
    std::uint32_t labelMismatches = 0u;
    float centroidDelta = 0.0f;
    float inertiaDelta = 0.0f;
    bool maxDistanceIndexMatches = false;

    if (bootstrap.Skipped)
    {
        status = "skipped";
    }
    else if (bootstrap.Device != nullptr && cpu.Succeeded)
    {
        RHI::IDevice& device = *bootstrap.Device;
        const std::string resetShader =
            Extrinsic::Core::Filesystem::GetShaderPath("shaders/kmeans_reset.comp.spv");
        const std::string assignShader =
            Extrinsic::Core::Filesystem::GetShaderPath("shaders/kmeans_assign.comp.spv");
        const std::string updateShader =
            Extrinsic::Core::Filesystem::GetShaderPath("shaders/kmeans_update.comp.spv");
        Runtime::KMeansGpuPipelineSet pipelines{
            .Reset = device.CreatePipeline(
                Runtime::BuildKMeansResetPipelineDesc(resetShader.c_str())),
            .Assign = device.CreatePipeline(
                Runtime::BuildKMeansAssignPipelineDesc(assignShader.c_str())),
            .Update = device.CreatePipeline(
                Runtime::BuildKMeansUpdatePipelineDesc(updateShader.c_str())),
        };

        if (pipelines.IsValid())
        {
            gpu = RunGpuMeasurements(device, pipelines, fixture);
            if (gpu.Succeeded)
            {
                labelMismatches = CountLabelMismatches(gpu.Readback.Labels,
                                                       cpu.Result.Labels);
                centroidDelta = MaxCentroidDelta(gpu.Readback.Centroids,
                                                 cpu.Result.Centroids);
                inertiaDelta = std::abs(gpu.Readback.Inertia - cpu.Result.Inertia);
                maxDistanceIndexMatches =
                    gpu.Readback.MaxDistanceIndex == cpu.Result.MaxDistanceIndex;

                const bool parity =
                    labelMismatches == 0u &&
                    centroidDelta <= 1.0e-4f &&
                    inertiaDelta <= 1.0e-4f &&
                    maxDistanceIndexMatches;
                status = parity ? "passed" : "failed";
                diagnostic = parity ? "gpu_vulkan_compute matches cpu_reference"
                                    : "gpu_vulkan_compute parity mismatch";
            }
            else
            {
                diagnostic = gpu.Diagnostic;
            }
        }
        else
        {
            diagnostic = "failed to create KMeans compute pipelines";
        }

        if (pipelines.Update.IsValid()) device.DestroyPipeline(pipelines.Update);
        if (pipelines.Assign.IsValid()) device.DestroyPipeline(pipelines.Assign);
        if (pipelines.Reset.IsValid()) device.DestroyPipeline(pipelines.Reset);
        device.Shutdown();
    }
    else if (!cpu.Succeeded)
    {
        diagnostic = "cpu_reference failed";
    }

    const double ratio = gpu.RuntimeMilliseconds > 0.0
        ? cpu.RuntimeMilliseconds / gpu.RuntimeMilliseconds
        : 0.0;
    const double qualityError =
        std::sqrt(static_cast<double>(labelMismatches * labelMismatches) +
                  static_cast<double>(centroidDelta * centroidDelta) +
                  static_cast<double>(inertiaDelta * inertiaDelta));

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(6);
    out << "{\n"
        << "  \"benchmark_id\": \"" << EscapeJson(kBenchmarkId) << "\",\n"
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
        << "    \"warmup_iterations\": " << kWarmupIterations << ",\n"
        << "    \"measured_iterations\": " << kMeasuredIterations << ",\n"
        << "    \"point_count\": " << fixture.Points.size() << ",\n"
        << "    \"cluster_count\": " << fixture.Seeds.size() << ",\n"
        << "    \"max_iterations\": " << fixture.Params.MaxIterations << ",\n"
        << "    \"cpu_reference_runtime_ms\": " << cpu.RuntimeMilliseconds << ",\n"
        << "    \"cpu_to_gpu_runtime_ratio\": " << ratio << ",\n"
        << "    \"baseline_comparison\": \"cpu_reference_same_fixture\",\n"
        << "    \"speedup_claimed\": false,\n"
        << "    \"gpu_time_source\": \"wall_submit_to_async_readback_ready\",\n"
        << "    \"label_mismatch_count\": " << labelMismatches << ",\n"
        << "    \"max_centroid_delta\": " << centroidDelta << ",\n"
        << "    \"inertia_delta\": " << inertiaDelta << ",\n"
        << "    \"max_distance_index_matches\": "
        << (maxDistanceIndexMatches ? "true" : "false") << ",\n"
        << "    \"diagnostic\": \"" << EscapeJson(diagnostic) << "\"\n"
        << "  },\n"
        << "  \"status\": \"" << status << "\"\n"
        << "}\n";

    if (!WriteFile(outPath, out.str()))
        return 1;

    return status == "failed" ? 2 : 0;
}
