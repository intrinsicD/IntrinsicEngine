// GEOM-081: Vulkan explicit property filters against the CPU reference through the real editor
// command path (framed GPU job, readback, guarded publication).
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.Properties;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace S = Geometry::Smoothing;
    using Domain = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    struct Case
    {
        std::string Name;
        Runtime::PropertySmoothingConfig Config;
        std::size_t Entity{};
        bool Stale{}; // edit the input while the GPU job is pending
    };

    class SmoothingApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(std::size_t entity, Domain domain)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(), Entities[entity]), domain));
        }

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
            std::mt19937 random(81);
            std::uniform_real_distribution<float> coordinate(-1, 1);
            // One entity per canonical domain with kNN samples, a random signal and one deleted row.
            for (unsigned d = 0; d < 8; ++d)
            {
                const auto domain = Domain(d + 1);
                auto entity = Context.Scene->Create();
                Entities.push_back(entity);
                if (domain <= Domain::MeshFace)
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
                    (void)mesh.AddTriangle(a, b, c);
                    GS::PopulateFromMesh(Context.Scene->Raw(), entity, mesh);
                }
                else if (domain < Domain::PointCloudPoint)
                {
                    Geometry::Graph::Graph graph;
                    auto a = graph.AddVertex({0, 0, 0}), b = graph.AddVertex({1, 0, 0});
                    (void)graph.AddEdge(a, b);
                    GS::PopulateFromGraph(Context.Scene->Raw(), entity, graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p = Props(d, domain);
                p.Resize(48);
                auto samples = p.GetOrAdd<glm::vec3>("samples");
                auto signal = p.GetOrAdd<double>("signal");
                auto flow = p.GetOrAdd<glm::vec4>("flow");
                for (std::size_t i = 0; i < p.Size(); ++i)
                {
                    samples[i] = {coordinate(random), coordinate(random), 0.2f * coordinate(random)};
                    signal[i] = std::sin(3.0 * samples[i].x) + 0.3 * coordinate(random);
                    flow[i] = {coordinate(random), coordinate(random), coordinate(random), coordinate(random)};
                }
                const bool half = domain == Domain::MeshHalfedge || domain == Domain::GraphHalfedge;
                if (half)
                {
                    auto& edges = Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(24);
                    edges.GetOrAdd<bool>("e:deleted")[3] = true;
                }
                else p.GetOrAdd<bool>(domain == Domain::MeshFace ? "f:deleted" :
                    (domain == Domain::MeshEdge || domain == Domain::GraphEdge) ? "e:deleted" : "v:deleted")[5] = true;
            }
            // A 7x7 grid mesh for cotangent weights, boundary pins and vector kinds.
            Geometry::HalfedgeMesh::Mesh grid;
            std::vector<Geometry::VertexHandle> vertices;
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 7; ++x)
                    vertices.push_back(grid.AddVertex({float(x) + 0.1f * coordinate(random), float(y) + 0.1f * coordinate(random),
                                                       0.3f * coordinate(random)}));
            for (int y = 0; y < 6; ++y)
                for (int x = 0; x < 6; ++x)
                {
                    const auto v = [&](int i, int j) { return vertices[std::size_t(j * 7 + i)]; };
                    (void)grid.AddTriangle(v(x, y), v(x + 1, y), v(x + 1, y + 1));
                    (void)grid.AddTriangle(v(x, y), v(x + 1, y + 1), v(x, y + 1));
                }
            auto gridEntity = Context.Scene->Create();
            Entities.push_back(gridEntity);
            GS::PopulateFromMesh(Context.Scene->Raw(), gridEntity, grid);
            auto& gp = Props(8, Domain::MeshVertex);
            auto uv = gp.GetOrAdd<glm::vec2>("uv");
            auto heat = gp.GetOrAdd<float>("heat");
            for (std::size_t i = 0; i < gp.Size(); ++i)
            {
                uv[i] = {coordinate(random), coordinate(random)};
                heat[i] = coordinate(random);
            }
            BuildCases();
        }

        void BuildCases()
        {
            const auto make = [](Domain domain, const char* input, K kind, S::PropertyFilter method, S::PropertyLaplacian laplacian) {
                Runtime::PropertySmoothingConfig c;
                c.Input = {domain, input, kind};
                c.Output = {domain, "gpu_out", kind};
                c.Positions = {domain, "samples", K::Vec3};
                c.Weight = S::PropertyWeight::Gaussian;
                c.Neighbors = 6;
                c.SpatialSigma = 0.4;
                c.Filter.Method = method;
                c.Filter.Laplacian = laplacian;
                c.Filter.Iterations = method == S::PropertyFilter::SpectralHeat ? 2 : 7;
                c.Filter.HeatTime = 0.6;
                c.Filter.RangeSigma = 0.5;
                c.Backend = Runtime::PropertySmoothingBackend::Vulkan;
                return c;
            };
            const S::PropertyFilter filters[] = {S::PropertyFilter::Averaging, S::PropertyFilter::SpectralHeat,
                                                 S::PropertyFilter::Taubin, S::PropertyFilter::Bilateral};
            for (std::size_t d = 0; d < 8; ++d)
                for (const auto method : filters)
                    for (const auto laplacian : {S::PropertyLaplacian::RandomWalk, S::PropertyLaplacian::Combinatorial})
                        Cases.push_back({"domain" + std::to_string(d + 1) + "_method" + std::to_string(int(method)) +
                                         "_laplacian" + std::to_string(int(laplacian)),
                                         make(Domain(d + 1), "signal", K::Double, method, laplacian), d});
            for (const auto method : filters)
            {
                auto c = make(Domain::PointCloudPoint, "flow", K::Vec4, method, S::PropertyLaplacian::RandomWalk);
                Cases.push_back({"vec4_method" + std::to_string(int(method)), c, 7});
                for (const auto weight : {S::PropertyWeight::Cotangent, S::PropertyWeight::MeshUniform})
                {
                    auto g = make(Domain::MeshVertex, "uv", K::Vec2, method, S::PropertyLaplacian::Combinatorial);
                    g.Positions = {Domain::MeshVertex, "v:position", K::Vec3};
                    g.Weight = weight;
                    g.PreserveBoundary = true;
                    Cases.push_back({"grid_uv_weight" + std::to_string(int(weight)) + "_method" + std::to_string(int(method)), g, 8});
                }
                auto positions = make(Domain::MeshVertex, "v:position", K::Vec3, method, S::PropertyLaplacian::RandomWalk);
                positions.Positions = {Domain::MeshVertex, "v:position", K::Vec3};
                positions.Weight = S::PropertyWeight::Cotangent;
                positions.PreserveBoundary = true;
                Cases.push_back({"grid_positions_method" + std::to_string(int(method)), positions, 8});
                auto scalar = make(Domain::MeshVertex, "heat", K::Float, method, S::PropertyLaplacian::RandomWalk);
                scalar.Positions = {Domain::MeshVertex, "v:position", K::Vec3};
                scalar.Weight = S::PropertyWeight::MeshUniform;
                Cases.push_back({"grid_float_method" + std::to_string(int(method)), scalar, 8});
            }
            if (StaleOnly)
            {
                auto c = make(Domain::GraphNode, "signal", K::Double, S::PropertyFilter::Taubin, S::PropertyLaplacian::RandomWalk);
                Cases = {{"stale_input", c, 4, true}};
            }
        }

        // Reads a property as doubles (all channels) from the given entity/domain.
        std::vector<double> Read(std::size_t entity, const Runtime::GeometryPropertyRef& ref)
        {
            auto& p = Props(entity, ref.Domain);
            std::vector<double> out;
            const auto add = [&](const auto& vector) {
                for (const auto& v : vector)
                {
                    if constexpr (std::is_arithmetic_v<std::decay_t<decltype(v)>>) out.push_back(double(v));
                    else for (int c = 0; c < v.length(); ++c) out.push_back(double(v[c]));
                }
            };
            switch (ref.ValueKind)
            {
            case K::Float: add(std::as_const(p).Get<float>(ref.Name).Vector()); break;
            case K::Double: add(std::as_const(p).Get<double>(ref.Name).Vector()); break;
            case K::Vec2: add(std::as_const(p).Get<glm::vec2>(ref.Name).Vector()); break;
            case K::Vec3: add(std::as_const(p).Get<glm::vec3>(ref.Name).Vector()); break;
            default: add(std::as_const(p).Get<glm::vec4>(ref.Name).Vector()); break;
            }
            return out;
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(100))
            {
                ADD_FAILURE() << "GEOM-081 timeout at case " << Next;
                TimedOut = true;
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational()) return;
            ++Frames;
            if (Waiting) return;
            if (std::getenv("GEOM081_TRACE")) std::printf("case %zu frames %zu t=%.0f ms\n", Next, Frames,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count());
            if (Result)
            {
                Check(*Result);
                Result.reset();
                ++Next;
            }
            if (Next == Cases.size())
            {
                Done = true;
                Kernel().RequestExit();
                return;
            }
            auto& current = Cases[Next];
            auto& c = current.Config;
            // Positions overwrite their input; restore the original before each position case.
            if (c.Input.Name == "v:position")
            {
                if (OriginalPositions.empty()) OriginalPositions = std::as_const(Props(8, Domain::MeshVertex)).Get<glm::vec3>("v:position").Vector();
                Props(8, Domain::MeshVertex).Get<glm::vec3>("v:position").Vector() = OriginalPositions;
                c.Output = c.Input;
            }
            if (c.Output.Name != c.Input.Name && Props(current.Entity, c.Output.Domain).Exists(c.Output.Name))
                RemoveOutput(current.Entity, c.Output);
            Before = Read(current.Entity, c.Input);
            Waiting = true;
            PhaseStarted = std::chrono::steady_clock::now();
            const auto id = Runtime::SelectionController::ToStableEntityId(Entities[current.Entity]);
            const auto pending = Runtime::ApplyEditorPropertySmoothingCommand(Runtime::BindEditorProcessingCommands(Context), id, c,
                [this](Runtime::EditorPropertySmoothingResult result) { Result = std::move(result); Waiting = false; });
            if (pending.Status != Runtime::EditorCommandStatus::Pending)
            {
                ADD_FAILURE() << current.Name << ": expected a pending Vulkan job: " << pending.Message;
                Result = pending;
                Waiting = false;
                return;
            }
            if (current.Stale)
                Props(current.Entity, c.Input.Domain).Get<double>(c.Input.Name)[0] += 1.0;
        }

        void RemoveOutput(std::size_t entity, const Runtime::GeometryPropertyRef& ref)
        {
            auto& p = Props(entity, ref.Domain);
            const auto remove = [&](auto property) { if (property) p.Remove(property); };
            switch (ref.ValueKind)
            {
            case K::Float: remove(p.Get<float>(ref.Name)); break;
            case K::Double: remove(p.Get<double>(ref.Name)); break;
            case K::Vec2: remove(p.Get<glm::vec2>(ref.Name)); break;
            case K::Vec3: remove(p.Get<glm::vec3>(ref.Name)); break;
            default: remove(p.Get<glm::vec4>(ref.Name)); break;
            }
        }

        void Check(const Runtime::EditorPropertySmoothingResult& gpu)
        {
            auto& current = Cases[Next];
            const auto& c = current.Config;
            GpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - PhaseStarted).count();
            if (current.Stale)
            {
                EXPECT_EQ(gpu.Status, Runtime::EditorCommandStatus::StaleEntity) << gpu.Message;
                EXPECT_FALSE(Props(current.Entity, c.Output.Domain).Exists(c.Output.Name)) << "stale work must not publish";
                return;
            }
            ASSERT_TRUE(gpu.Succeeded()) << current.Name << ": " << gpu.Message;
            EXPECT_EQ(gpu.RequestedBackend, Runtime::PropertySmoothingBackend::Vulkan);
            EXPECT_EQ(gpu.BackendId, "vulkan_compute") << current.Name;
            const auto gpuValues = Read(current.Entity, c.Output);
            // CPU reference on the same inputs through the same command path.
            auto reference = c;
            reference.Backend = Runtime::PropertySmoothingBackend::Cpu;
            if (c.Input.Name == "v:position")
            {
                Props(8, Domain::MeshVertex).Get<glm::vec3>("v:position").Vector() = OriginalPositions;
            }
            else reference.Output.Name = "cpu_out";
            const auto id = Runtime::SelectionController::ToStableEntityId(Entities[current.Entity]);
            const auto cpuStart = std::chrono::steady_clock::now();
            const auto cpu = Runtime::ApplyEditorPropertySmoothingCommand(Runtime::BindEditorProcessingCommands(Context), id, reference);
            CpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count();
            ASSERT_TRUE(cpu.Succeeded()) << current.Name << ": " << cpu.Message;
            const auto cpuValues = Read(current.Entity, reference.Output);
            ASSERT_EQ(cpuValues.size(), gpuValues.size());
            double range = 0;
            for (const double v : Before) range = std::max(range, std::abs(v));
            double error = 0;
            for (std::size_t j = 0; j < cpuValues.size(); ++j) error = std::max(error, std::abs(cpuValues[j] - gpuValues[j]));
            const bool bilateral = c.Filter.Method == S::PropertyFilter::Bilateral;
            (bilateral ? MaxBilateralError : MaxLinearError) = std::max(bilateral ? MaxBilateralError : MaxLinearError, error / std::max(range, 1.0));
            EXPECT_LE(error, 1e-12 * std::max(range, 1.0)) << current.Name;
            if (c.Output.Name != c.Input.Name)
            {
                RemoveOutput(current.Entity, reference.Output);
                RemoveOutput(current.Entity, c.Output);
            }
            ++Compared;
        }

        void Shutdown() override { Context = {}; }

        Runtime::EditorProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;
        std::vector<Case> Cases;
        std::vector<glm::vec3> OriginalPositions;
        std::vector<double> Before;
        std::optional<Runtime::EditorPropertySmoothingResult> Result;
        std::chrono::steady_clock::time_point Started{}, PhaseStarted{};
        std::size_t Next{}, Compared{}, Frames{};
        double MaxLinearError{}, MaxBilateralError{}, GpuMs{}, CpuMs{};
        bool Waiting{}, Done{}, TimedOut{}, StaleOnly{};
    };

    bool Prepare(Extrinsic::Core::Config::EngineConfig& config)
    {
        config = Runtime::CreateReferenceEngineConfig();
        config.Window.Width = 64;
        config.Window.Height = 64;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        return Extrinsic::Platform::Backends::Glfw::CanInitialize();
    }
}

TEST(GEOM081VulkanPropertySmoothing, ExplicitFiltersMatchTheCpuReferenceOnEveryDomainAndKind)
{
    Extrinsic::Core::Config::EngineConfig config;
    if (!Prepare(config)) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<SmoothingApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done);
    EXPECT_EQ(run->Compared, run->Cases.size());
    std::printf("GEOM-081 parity: %zu cases, max relative error linear %.3g, bilateral %.3g; GPU %.1f ms, CPU %.1f ms\n",
                run->Compared, run->MaxLinearError, run->MaxBilateralError, run->GpuMs, run->CpuMs);
    if (const auto* output = std::getenv("INTRINSIC_GEOM081_BENCHMARK_OUTPUT"))
    {
        const nlohmann::json json{{"benchmark_id", "geometry.property_smoothing.vulkan_explicit_parity"},
            {"method", "geometry.property_smoothing"}, {"backend", "gpu_vulkan_compute"},
            {"dataset", "builtin.property_smoothing.eight_domains_and_grid.seed81"}, {"commit", "local-dev"},
            {"metrics", {{"runtime_ms", run->GpuMs}, {"quality_error_linf", std::max(run->MaxLinearError, run->MaxBilateralError)}}},
            {"diagnostics", {{"runner", "IntrinsicPointLBVHGpuTests"}, {"mode", "smoke"}, {"cases", run->Compared},
                {"cpu_reference_total_ms", run->CpuMs}, {"vulkan_total_ms_including_frames", run->GpuMs},
                {"max_relative_error_linear_filters", run->MaxLinearError},
                {"max_relative_error_bilateral", run->MaxBilateralError},
                {"cpu_stages", "sample capture, graph and weight construction, plan, incidence build, final restore"},
                {"speedup_claimed", false}, {"warmup_iterations", 0}, {"measured_iterations", 1}}},
            {"status", ::testing::Test::HasFailure() ? "failed" : "passed"}};
        std::ofstream stream(output);
        stream << json.dump(2) << '\n';
    }
}

TEST(GEOM081VulkanPropertySmoothing, StaleInputRejectsPublication)
{
    Extrinsic::Core::Config::EngineConfig config;
    if (!Prepare(config)) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<SmoothingApp>();
    auto* run = app.get();
    run->StaleOnly = true;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done);
}
