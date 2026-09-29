// RUNTIME-290: the Vulkan point-sampling seam. Direct: an exact weighted farthest-point run in
// several chunks, of which only the last reads the order back (GRAPHICS-153; earlier observed
// prefixes are empty), which equals the CPU reference bitwise (order and clearances, duplicates included). Editor: the
// Vulkan backend of the point-sampling command against its CPU backend, three runs equal.
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.PointSamplingGpu;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace T = Extrinsic::ECS::Components::Transform;
    namespace PS = Geometry::PointSampling;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    // Uniform box samples plus exact duplicates (zero clearances and ties).
    std::vector<glm::vec3> Cloud(std::size_t count, std::size_t duplicates, unsigned seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        std::vector<glm::vec3> points(count);
        for (auto& p : points) p = {uniform(random), 0.5f * uniform(random), 0.3f * uniform(random)};
        for (std::size_t k = 0; k < duplicates; ++k) points.push_back(points[(k * 7u) % count]);
        return points;
    }

    class SamplingApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Context.Scene = Kernel().Worlds().Get(Kernel().ActiveWorld());
            Context.World = Kernel().ActiveWorld();
            Context.SpatialIndices = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            Context.Device = &Kernel().GetDevice();
            Context.CommandHistory = &History;
            Context.JobCommands.Submit = [this](Runtime::JobDesc desc, Runtime::EditorJobIdentity) {
                return Kernel().Jobs().Submit(std::move(desc));
            };
            Direct = Cloud(40000, 300, 7u);
            Weights.resize(Direct.size());
            std::mt19937 random(8u);
            std::uniform_real_distribution<double> weight(0.5, 2.0);
            for (auto& w : Weights) w = weight(random);
            Entity = Context.Scene->Create();
            Context.Scene->Raw().emplace<T::Component>(Entity);
            auto& vertices = Context.Scene->Raw().emplace<GS::Vertices>(Entity).Properties;
            const auto points = Cloud(6000, 50, 9u);
            vertices.Resize(points.size());
            vertices.GetOrAdd<glm::vec3>("v:position").Vector() = points;
        }

        std::vector<float> Rank(const std::string& name) const
        {
            return std::as_const(Context.Scene->Raw().get<GS::Vertices>(Entity).Properties).Get<float>(name).Vector();
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(120))
            {
                ADD_FAILURE() << "RUNTIME-290 timeout in phase " << Phase;
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational()) return;
            if (Phase == 0)
            {
                // Direct seam: 3000 weighted samples of 40300 points take two bounded chunks.
                const PS::Params params{.FirstIndex = 5u, .Weights = Weights};
                Reason = Runtime::PointSamplingGpuUnsupportedReason(params, Direct.size(), 3000u, &Kernel().GetDevice());
                if (!Reason.empty()) { Kernel().RequestExit(); return; }
                Run = std::make_unique<Runtime::PointSamplingGpuRun>(Kernel().GetDevice(), Direct, params, 3000u);
                Gpu = Run->QueueNext(*Context.SpatialIndices);
                Phase = 1;
                return;
            }
            if (Phase == 1)
            {
                if (!Gpu || Gpu->State == Runtime::SpatialQueryState::Failed)
                {
                    Reason = Gpu ? Gpu->Diagnostic : "nothing queued";
                    Kernel().RequestExit();
                    return;
                }
                if (Gpu->State != Runtime::SpatialQueryState::Ready) return;
                const bool finished = Run->Observe(*Gpu);
                Prefixes.push_back(Run->Current().Order);
                if (!finished) { Gpu = Run->QueueNext(*Context.SpatialIndices); return; }
                Verified = Run->VerifyPrefix(VerifyDiagnostic);
                Clearance = Run->Current().Clearance;
                Phase = 2;
                return;
            }
            if (Phase == 6) { Kernel().RequestExit(); return; }
            if (Phase >= 2 && !Waiting)
            {
                // 2: CPU backend; 3-5: Vulkan backend three times (rank properties compared).
                const bool vulkan = Phase >= 3;
                Runtime::PointSamplingOperationConfig config{
                    .SourceStableEntityId = Runtime::SelectionController::ToStableEntityId(Entity), .Count = 1500u,
                    .Backend = vulkan ? Runtime::PointSamplingBackend::Vulkan : Runtime::PointSamplingBackend::Cpu,
                    .RankName = vulkan ? "v:vulkan_rank" : "v:cpu_rank",
                    .SelectedName = vulkan ? "v:vulkan_selected" : "v:cpu_selected"};
                Waiting = true;
                const auto pending = Runtime::ApplyEditorPointSamplingCommand(
                    Runtime::BindEditorProcessingCommands(Context), config,
                    [this, vulkan](Runtime::EditorPointSamplingResult result) {
                        if (vulkan) VulkanRanks.push_back(Rank("v:vulkan_rank"));
                        (vulkan ? EditorVulkan : EditorCpu) = std::move(result);
                        Waiting = false;
                        ++Phase;
                    });
                if (vulkan && pending.Status != Runtime::EditorCommandStatus::Pending && Waiting)
                    ADD_FAILURE() << "expected a queued Vulkan run: " << pending.Message;
            }
        }

        void Shutdown() override
        {
            Gpu.reset();
            Run.reset();
            Context = {};
        }

        Runtime::EditorProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::vector<glm::vec3> Direct{};
        std::vector<double> Weights{};
        entt::entity Entity{entt::null};
        std::unique_ptr<Runtime::PointSamplingGpuRun> Run{};
        std::shared_ptr<Runtime::SpatialGpuResult> Gpu{};
        std::vector<std::vector<std::uint32_t>> Prefixes{};
        std::vector<double> Clearance{};
        std::vector<std::vector<float>> VulkanRanks{};
        std::optional<Runtime::EditorPointSamplingResult> EditorCpu{}, EditorVulkan{};
        std::chrono::steady_clock::time_point Started{};
        std::string Reason{}, VerifyDiagnostic{};
        bool Verified{false}, Waiting{false};
        int Phase{0};
    };
}

TEST(RUNTIME290VulkanPointSampling, FarthestPointMatchesTheCpuAcrossChunksAndThroughTheEditor)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    auto app = std::make_unique<SamplingApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(run->Reason.empty()) << run->Reason;
    ASSERT_EQ(run->Phase, 6);

    // Direct: several chunks, each observed prefix a prefix of the final order, equal to the CPU.
    ASSERT_GE(run->Prefixes.size(), 2u) << "the run must take more than one chunk";
    const auto& final = run->Prefixes.back();
    for (std::size_t c = 0; c + 1 < run->Prefixes.size(); ++c)
        EXPECT_TRUE(run->Prefixes[c].empty()) << "an intermediate chunk read samples back";
    const PS::Result reference = PS::Order(std::span<const glm::vec3>(run->Direct),
                                           PS::Params{.FirstIndex = 5u, .Weights = run->Weights}, 3000u);
    ASSERT_TRUE(reference.Succeeded());
    EXPECT_EQ(final, reference.Order);
    EXPECT_EQ(run->Clearance, reference.Clearance);
    EXPECT_TRUE(run->Verified) << run->VerifyDiagnostic;

    // Editor: the Vulkan backend equals the CPU backend, three times.
    ASSERT_TRUE(run->EditorCpu && run->EditorVulkan);
    ASSERT_TRUE(run->EditorVulkan->Succeeded()) << run->EditorVulkan->Message;
    EXPECT_EQ(run->EditorVulkan->RequestedBackend, "gpu_vulkan_compute");
    EXPECT_EQ(run->EditorVulkan->Backend, "gpu_vulkan_compute") << run->EditorVulkan->BackendDiagnostic;
    const auto cpuRank = run->Rank("v:cpu_rank");
    ASSERT_EQ(run->VulkanRanks.size(), 3u);
    for (const auto& rank : run->VulkanRanks) EXPECT_EQ(rank, cpuRank);
    std::printf("RUNTIME-290: %zu chunks for 3000 of %zu points; editor Vulkan %.1f ms vs CPU %.1f ms (1500 of 6050)\n",
                run->Prefixes.size(), run->Direct.size(), run->EditorVulkan->Milliseconds, run->EditorCpu->Milliseconds);
}
