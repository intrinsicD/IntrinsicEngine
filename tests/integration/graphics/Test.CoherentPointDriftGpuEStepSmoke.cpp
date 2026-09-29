// METHOD-056: the Vulkan dense CPD E-step against the CPU dense E-step, through the runtime
// broker (worker thread waits, main thread pumps QueueGpuCompute), then one editor-command run
// against its CPU twin (the same policy route, CPU dense where the device ran). Frozen
// tolerances (METHOD-049-style fixtures):
//   row denominators: |log den_gpu - log den_cpu| <= 1e-5 (relative denominator error);
//   Pt1 and P1 entries relative <= 1e-5 (entries below 1e-20 of the largest: absolute <= 1e-20
//   of the largest, the fp32 underflow of far terms); PX entries <= 1e-5 P1_m max |x|;
//   registered points <= 1e-4 of the target extent; two device runs bitwise equal.
#include "RuntimeTestModule.hpp"
#include "CoherentPointDriftScalingFixture.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.CoherentPointDriftGpuEStep;
import Extrinsic.Runtime.CoherentPointDriftConfig;
import Extrinsic.Runtime.RegistrationOperations;
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
import Geometry.Registration.CoherentPointDrift;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace T = Extrinsic::ECS::Components::Transform;
    namespace CPD = Geometry::CoherentPointDrift;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    std::vector<glm::vec3> Cloud(std::size_t count, unsigned seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        std::vector<glm::vec3> points(count);
        for (auto& p : points) p = {uniform(random), uniform(random), 0.6f * uniform(random)};
        return points;
    }

    std::vector<glm::vec3> Rigid(const std::vector<glm::vec3>& points, double angle, glm::dvec3 t, double noise, unsigned seed)
    {
        const glm::dmat3 r(glm::rotate(glm::dmat4(1.0), angle, glm::normalize(glm::dvec3(1.0, 2.0, 0.5))));
        std::mt19937 random(seed);
        std::normal_distribution<double> gaussian(0.0, noise);
        std::vector<glm::vec3> out;
        for (const auto& p : points)
            out.push_back(glm::vec3(r * glm::dvec3(p) + t + glm::dvec3(gaussian(random), gaussian(random), gaussian(random))));
        return out;
    }

    struct Soa
    {
        std::vector<double> X, Y, Z;
        explicit Soa(const std::vector<glm::vec3>& p)
        {
            for (const auto& q : p) { X.push_back(q.x); Y.push_back(q.y); Z.push_back(q.z); }
        }
        [[nodiscard]] CPD::EStep::PointSet View() const { return {X, Y, Z}; }
    };

    struct Errors
    {
        double LogDenominator{0.0}, Pt1{0.0}, P1{0.0}, PX{0.0};
        void Merge(const Errors& o)
        {
            LogDenominator = std::max(LogDenominator, o.LogDenominator);
            Pt1 = std::max(Pt1, o.Pt1);
            P1 = std::max(P1, o.P1);
            PX = std::max(PX, o.PX);
        }
    };

    // gpuLogDen: the device's rows as returned (rows it skipped for the CPU are not compared).
    Errors Compare(const CPD::EStep::Sums& gpu, const std::vector<double>& gpuLogDen, const std::vector<std::uint32_t>& skipped,
                   const CPD::EStep::Sums& cpu, const std::vector<double>& cpuLogDen, double maxAbsX)
    {
        Errors e;
        for (std::size_t j = 0; j < cpuLogDen.size(); ++j)
        {
            if (skipped.empty() || skipped[j] == 0u)
                e.LogDenominator = std::max(e.LogDenominator, std::abs(gpuLogDen[j] - cpuLogDen[j]));
            e.Pt1 = std::max(e.Pt1, std::abs(gpu.Pt1[j] - cpu.Pt1[j]) / std::max(cpu.Pt1[j], 1e-300));
        }
        const double top = *std::ranges::max_element(cpu.P1);
        for (std::size_t i = 0; i < cpu.P1.size(); ++i)
        {
            const double floor = 1e-20 * top;
            const double d = std::abs(gpu.P1[i] - cpu.P1[i]);
            e.P1 = std::max(e.P1, cpu.P1[i] >= floor ? d / cpu.P1[i] : (d <= floor ? 0.0 : d / floor));
            const double scale = std::max(cpu.P1[i], floor) * maxAbsX;
            for (const auto [a, b] : {std::pair{gpu.PXx[i], cpu.PXx[i]}, std::pair{gpu.PXy[i], cpu.PXy[i]},
                                      std::pair{gpu.PXz[i], cpu.PXz[i]}})
                e.PX = std::max(e.PX, std::abs(a - b) / scale);
        }
        return e;
    }

    class CpdGpuApp final : public Intrinsic::Tests::RuntimeTestModule
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
            SourcePoints = Cloud(1000, 91);
            TargetPoints = Rigid(SourcePoints, 0.35, {0.2, -0.1, 0.15}, 0.004, 92);
            const auto make = [&](const std::vector<glm::vec3>& points) {
                const auto entity = Context.Scene->Create();
                Context.Scene->Raw().emplace<T::Component>(entity);
                auto& vertices = Context.Scene->Raw().emplace<GS::Vertices>(entity).Properties;
                vertices.Resize(points.size());
                vertices.GetOrAdd<glm::vec3>("v:position").Vector() = points;
                return entity;
            };
            Source = make(SourcePoints);
            Target = make(TargetPoints);
        }

        // Worker thread: device E-steps through the broker against CPU dense.
        void ParityWork()
        {
            const Soa target(Cloud(1500, 93)), moved(Rigid(Cloud(1400, 94), 0.2, {0.1, 0.0, 0.05}, 0.01, 95));
            std::vector<double> logWeights(1400);
            for (std::size_t i = 0; i < logWeights.size(); ++i) logWeights[i] = -2.0 + 0.002 * double(i % 900);
            double maxAbsX = 0.0;
            for (std::size_t j = 0; j < target.X.size(); ++j)
                maxAbsX = std::max({maxAbsX, std::abs(target.X[j]), std::abs(target.Y[j]), std::abs(target.Z[j])});
            CPD::EStep::Evaluator device, cpu;
            device.SetTarget(target.View());
            cpu.SetTarget(target.View());
            std::vector<double> deviceLogDen, cpuLogDen;
            std::vector<std::uint32_t> deviceSkipped;
            // Wraps the broker to keep the per-row denominators (Sums only carries their sum).
            const CPD::EStep::Settings vulkan{.Policy = CPD::EStepPolicy::Vulkan, .Tolerance = 1e-6,
                .External = [&](const CPD::EStep::ExternalRequest& r) {
                    const bool ok = Broker->Evaluate(r);
                    deviceLogDen.assign(r.LogDenominator.begin(), r.LogDenominator.end());
                    deviceSkipped.assign(r.SkipRows.begin(), r.SkipRows.end());
                    return ok;
                }};
            for (const double sigma2 : {0.2, 0.6, 2.0})
                for (const bool weighted : {false, true})
                    for (const double logOutlier : {-std::numeric_limits<double>::infinity(), std::log(0.05)})
                    {
                        const std::span<const double> w = weighted ? std::span<const double>(logWeights) : std::span<const double>{};
                        CPD::EStep::Sums gpu, again, reference;
                        if (!device.Evaluate(moved.View(), sigma2, logOutlier, vulkan, gpu, w) || gpu.Used != CPD::EStepPolicy::Vulkan)
                        {
                            Failure = "device E-step did not run at sigma^2 " + std::to_string(sigma2) + " (" +
                                      std::string(CPD::ToString(gpu.Used)) + "): " + Broker->Diagnostic();
                            return;
                        }
                        const auto gpuLogDen = deviceLogDen;
                        // Repeatability on the first sigma^2 (weighted and not), to bound the frame count.
                        if (sigma2 == 0.2 && std::isinf(logOutlier))
                        {
                            ++RepeatChecks;
                            if (!device.Evaluate(moved.View(), sigma2, logOutlier, vulkan, again, w) || again.P1 != gpu.P1 ||
                                again.Pt1 != gpu.Pt1 || again.PXz != gpu.PXz || deviceLogDen != gpuLogDen)
                                Bitwise = false;
                        }
                        if (!cpu.Evaluate(moved.View(), sigma2, logOutlier, {.Policy = CPD::EStepPolicy::Dense}, reference, w))
                        {
                            Failure = "CPU dense failed";
                            return;
                        }
                        cpuLogDen = RowLogDenominators(target, moved, sigma2, logOutlier, w);
                        // The evaluator hands the device the frame shifted by the largest log-weight.
                        if (weighted)
                            for (double& v : cpuLogDen) v -= *std::ranges::max_element(logWeights);
                        const auto e = Compare(gpu, gpuLogDen, deviceSkipped, reference, cpuLogDen, maxAbsX);
                        MaxErrors.Merge(e);
                        LogDenSumError = std::max(LogDenSumError, std::abs(gpu.LogDenominatorSum - reference.LogDenominatorSum) /
                                                                      std::abs(reference.LogDenominatorSum));
                        ++ParityCases;
                    }

            // METHOD-063: two tight clusters plus one target row far from every source, narrow
            // kernel, no uniform term: the far row goes to the CPU, the rest to the device.
            std::mt19937 random(73u);
            std::normal_distribution<float> spread(0.0f, 0.03f);
            std::vector<glm::vec3> clusterTargets, clusterSources;
            for (int k = 0; k < 1200; ++k)
            {
                const float side = k % 2 ? 1.0f : -1.0f;
                clusterTargets.push_back({side + spread(random), spread(random), spread(random)});
                clusterSources.push_back({side + spread(random), spread(random), spread(random)});
            }
            clusterTargets.push_back({-1.0f, 0.4f, 0.0f});
            const Soa ct(clusterTargets), cs(clusterSources);
            CPD::EStep::Evaluator clusterDevice, clusterCpu;
            clusterDevice.SetTarget(ct.View());
            clusterCpu.SetTarget(ct.View());
            const double none = -std::numeric_limits<double>::infinity();
            CPD::EStep::Sums gpu, reference;
            if (!clusterDevice.Evaluate(cs.View(), 1e-3, none, vulkan, gpu) || gpu.Used != CPD::EStepPolicy::Vulkan ||
                gpu.ExternalCpuRows == 0u || !clusterCpu.Evaluate(cs.View(), 1e-3, none, {.Policy = CPD::EStepPolicy::Dense}, reference))
            {
                Failure = "cluster case did not mix device and CPU rows (" + std::string(CPD::ToString(gpu.Used)) + ", " +
                          std::to_string(gpu.ExternalCpuRows) + " CPU rows): " + Broker->Diagnostic();
                return;
            }
            ClusterCpuRows = gpu.ExternalCpuRows;
            double clusterX = 1.2;
            MaxErrors.Merge(Compare(gpu, deviceLogDen, deviceSkipped, reference,
                                    RowLogDenominators(ct, cs, 1e-3, none, {}), clusterX));
            ++ParityCases;
        }

        // Exact per-row log-denominators (the CPU Sums expose only their sum).
        static std::vector<double> RowLogDenominators(const Soa& target, const Soa& moved, double sigma2, double logC,
                                                      std::span<const double> w)
        {
            std::vector<double> out(target.X.size());
            for (std::size_t j = 0; j < out.size(); ++j)
            {
                double top = -std::numeric_limits<double>::infinity();
                std::vector<double> a(moved.X.size());
                for (std::size_t i = 0; i < a.size(); ++i)
                {
                    const double dx = target.X[j] - moved.X[i], dy = target.Y[j] - moved.Y[i], dz = target.Z[j] - moved.Z[i];
                    a[i] = (w.empty() ? 0.0 : w[i]) - (dx * dx + dy * dy + dz * dz) / (2.0 * sigma2);
                    top = std::max(top, a[i]);
                }
                double sum = 0.0;
                for (const double v : a) sum += std::exp(v - top);
                out[j] = top + std::log(sum + std::exp(logC - top));
            }
            return out;
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(240))
            {
                ADD_FAILURE() << "METHOD-056 timeout in phase " << Phase;
                TimedOut = true;
                if (Broker) Broker->Close("test timeout");
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational()) return;
            ++Frames;
            if (Phase == 0)
            {
                Broker = std::make_shared<Runtime::CoherentPointDriftGpuEStep>(*Context.SpatialIndices, Kernel().GetDevice());
                PhaseStarted = std::chrono::steady_clock::now();
                Worker = std::thread([this] { ParityWork(); ParityDone = true; });
                Phase = 1;
                return;
            }
            if (Phase == 1)
            {
                Broker->Pump();
                if (!ParityDone) return;
                Worker.join();
                Broker->ReleaseDeviceResources();
                ParityMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - PhaseStarted).count();
                Stats = Broker->Stats();
                Phase = 2;
                return;
            }
            if (Phase == 2)
            {
                PhaseStarted = std::chrono::steady_clock::now();
                Runtime::CoherentPointDriftConfig config{
                    .SourceStableEntityId = Runtime::SelectionController::ToStableEntityId(Source),
                    .TargetStableEntityId = Runtime::SelectionController::ToStableEntityId(Target),
                    .OutlierWeight = 0.05, .EStep = Runtime::CoherentPointDriftEStep::Vulkan};
                const auto pending = Runtime::ApplyEditorCoherentPointDriftCommand(
                    Runtime::BindEditorProcessingCommands(Context), config,
                    [this](Runtime::EditorCoherentPointDriftResult result) { Editor = std::move(result); });
                if (pending.Status != Runtime::EditorCommandStatus::Pending)
                {
                    ADD_FAILURE() << "expected a queued CPD run: " << pending.Message;
                    Kernel().RequestExit();
                    return;
                }
                Phase = 3;
                return;
            }
            if (Phase == 3 && Editor)
            {
                EditorMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - PhaseStarted).count();
                Done = true;
                Kernel().RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Worker.joinable())
            {
                if (Broker) Broker->Close("shutdown");
                Worker.join();
            }
            Broker.reset();
            Context = {};
        }

        Runtime::EditorProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::shared_ptr<Runtime::CoherentPointDriftGpuEStep> Broker{};
        Runtime::CoherentPointDriftGpuEStepStats Stats{};
        std::thread Worker{};
        std::atomic<bool> ParityDone{false};
        std::vector<glm::vec3> SourcePoints{}, TargetPoints{};
        entt::entity Source{entt::null}, Target{entt::null};
        std::optional<Runtime::EditorCoherentPointDriftResult> Editor{};
        std::chrono::steady_clock::time_point Started{}, PhaseStarted{};
        Errors MaxErrors{};
        double LogDenSumError{0.0}, ParityMs{0.0}, EditorMs{0.0};
        std::size_t ParityCases{0u}, RepeatChecks{0u}, Frames{0u}, ClusterCpuRows{0u};
        int Phase{0};
        bool Bitwise{true}, Done{false}, TimedOut{false};
        std::string Failure{};
    };

    // Opt-in scaling profile (coherent_point_drift_gpu_vulkan_smoke.yaml): the METHOD-049 rigid
    // fixture per size, registered with the Vulkan E-step (solver on this worker thread, E-steps
    // pumped by the frame loop), its CPU twin (the same policy route with the dense CPU pass),
    // CPU dense, Auto and Nystroem. Parity is the max point delta to the CPU twin.
    class CpdScalingApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        struct Run
        {
            std::string Name;
            CPD::Result Result;
            double Median{0.0}, Min{0.0}, Max{0.0}, Rms{0.0};
            std::size_t Repetitions{0u}, DeviceIterations{0u}, DeviceEvaluations{0u};
            double WaitSeconds{0.0};
            std::optional<double> Parity{};
        };

        Run Execute(const std::string& name, const CpdScaling::Fixture& f, CPD::Params params, std::size_t repetitions)
        {
            Run run;
            run.Name = name;
            std::vector<double> times;
            for (std::size_t k = 0; k < repetitions; ++k)
            {
                std::size_t device = 0;
                const auto before = Broker->Stats();
                const auto start = std::chrono::steady_clock::now();
                CPD::Result result = CPD::Register(f.Target, f.Source, params,
                    [&](const CPD::IterationTrace& t) { device += t.EStep == CPD::EStepPolicy::Vulkan ? 1u : 0u; });
                times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                if (k == 0)
                {
                    run.Result = std::move(result);
                    run.DeviceIterations = device;
                    run.DeviceEvaluations = Broker->Stats().Completed - before.Completed;
                    run.WaitSeconds = Broker->Stats().WaitSeconds - before.WaitSeconds;
                }
            }
            std::ranges::sort(times);
            run.Repetitions = times.size();
            run.Min = times.front();
            run.Max = times.back();
            run.Median = times.size() % 2 ? times[times.size() / 2] : 0.5 * (times[times.size() / 2 - 1] + times[times.size() / 2]);
            if (run.Result.Succeeded())
            {
                double sum = 0.0;
                for (std::size_t i = 0; i < f.Source.size(); ++i)
                {
                    const glm::dvec3 d = run.Result.TransformedSource[i] - glm::dvec3(f.Truth[i]);
                    sum += glm::dot(d, d);
                }
                run.Rms = std::sqrt(sum / double(f.Source.size()));
            }
            std::fprintf(stderr, "  %s: %s, %u iterations (%zu on the device), median %.1f ms (%.1f - %.1f, %zu runs), rms %.3g\n",
                         name.c_str(), std::string(CPD::ToString(run.Result.State)).c_str(), run.Result.Iterations,
                         run.DeviceIterations, run.Median, run.Min, run.Max, run.Repetitions, run.Rms);
            return run;
        }

        void Work()
        {
            std::vector<std::size_t> sizes;
            if (const char* text = std::getenv("INTRINSIC_METHOD056_SCALING_SIZES"))
            {
                std::istringstream in(text);
                for (std::size_t v; in >> v;) sizes.push_back(v);
            }
            if (sizes.empty()) sizes = {1'000, 10'000, 100'000};
            const auto external = [broker = Broker](const CPD::EStep::ExternalRequest& r) { return broker->Evaluate(r); };
            const CPD::Params base{.OutlierWeight = 0.05, .MaxIterations = 100, .EStepTolerance = 1e-6};
            {
                CPD::Params warm = base;
                warm.EStep = CPD::EStepPolicy::Vulkan;
                warm.EStepExternal = external;
                (void)CPD::Register(CpdScaling::RigidFixture(1'000).Target, CpdScaling::RigidFixture(1'000).Source, warm);
            }
            std::ostringstream out;
            out.precision(9);
            for (std::size_t s = 0; s < sizes.size(); ++s)
            {
                const std::size_t count = sizes[s];
                std::fprintf(stderr, "size %zu\n", count);
                const auto fixture = CpdScaling::RigidFixture(count);
                const std::size_t repetitions = count <= 10'000 ? 3u : 1u;
                std::vector<Run> runs;
                CPD::Params p = base;
                p.EStep = CPD::EStepPolicy::Vulkan;
                p.EStepExternal = external;
                runs.push_back(Execute("rigid_vulkan", fixture, p, count <= 10'000 ? 3u : 2u));
                p.EStepExternal = {};
                runs.push_back(Execute("rigid_vulkan_cpu_twin", fixture, p, repetitions));
                for (const auto policy : {CPD::EStepPolicy::Dense, CPD::EStepPolicy::Auto, CPD::EStepPolicy::Nystrom})
                {
                    p.EStep = policy;
                    runs.push_back(Execute("rigid_" + std::string(CPD::ToString(policy)), fixture, p, repetitions));
                }
                for (auto& run : runs)
                {
                    if (&run == &runs[1] || !run.Result.Succeeded() || !runs[1].Result.Succeeded()) continue;
                    double worst = 0.0;
                    for (std::size_t i = 0; i < run.Result.TransformedSource.size(); ++i)
                        worst = std::max(worst, glm::length(run.Result.TransformedSource[i] - runs[1].Result.TransformedSource[i]));
                    run.Parity = worst;
                }
                for (const auto& run : runs)
                {
                    Passed = Passed && run.Result.Succeeded();
                    WorstRms = std::max(WorstRms, run.Rms);
                }
                if (runs[0].Parity) WorstParity = std::max(WorstParity, *runs[0].Parity);
                // The device must actually have run: a silent CPU fallback fails the profile.
                Passed = Passed && runs[0].Result.Backend == "gpu_vulkan_fp32_dense" && runs[0].DeviceIterations > 0 &&
                         runs[0].Result.EStepFallbacks == 0u;
                VulkanMilliseconds = runs[0].Median;
                out << "      {\"points\": " << count << ", \"rigid\": [\n";
                for (std::size_t r = 0; r < runs.size(); ++r)
                {
                    const auto& run = runs[r];
                    const auto& res = run.Result;
                    out << "        {\"name\": \"" << run.Name << "\", \"status\": \"" << CPD::ToString(res.State)
                        << "\", \"backend\": \"" << res.Backend << "\", \"iterations\": " << res.Iterations
                        << ", \"device_iterations\": " << run.DeviceIterations
                        << ", \"e_step_fallbacks\": " << res.EStepFallbacks
                        << ", \"device_cpu_rows\": " << res.EStepDeviceCpuRows
                        << ", \"runtime_ms\": " << run.Median << ", \"runtime_ms_min\": " << run.Min
                        << ", \"runtime_ms_max\": " << run.Max << ", \"repetitions\": " << run.Repetitions
                        << ", \"ms_per_iteration\": " << (res.Iterations ? run.Median / double(res.Iterations) : 0.0)
                        << ", \"worker_wait_ms_first_run\": " << 1e3 * run.WaitSeconds
                        << ", \"rms_error_to_truth\": " << run.Rms << ", \"kernel_evaluations\": " << res.KernelEvaluations
                        << ", \"e_step_sampled_error\": " << res.EStepSampledError;
                    if (run.Parity) out << ", \"max_point_delta\": " << *run.Parity << ", \"parity_against\": \"rigid_vulkan_cpu_twin\"";
                    out << '}' << (r + 1 == runs.size() ? "\n" : ",\n");
                }
                out << "      ]}" << (s + 1 == sizes.size() ? "\n" : ",\n");
            }
            Sizes = out.str();
        }

        void Frame(double, double) override
        {
            if (!Kernel().GetDevice().IsOperational()) return;
            if (!Broker)
            {
                Broker = std::make_shared<Runtime::CoherentPointDriftGpuEStep>(
                    *Kernel().Services().Find<Runtime::SpatialIndexCache>(), Kernel().GetDevice(), std::chrono::minutes{10});
                Worker = std::thread([this] { Work(); Finished = true; });
                return;
            }
            Broker->Pump();
            if (Finished && Worker.joinable())
            {
                Worker.join();
                Broker->ReleaseDeviceResources();
                Kernel().RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Worker.joinable())
            {
                if (Broker) Broker->Close("shutdown");
                Worker.join();
            }
            Broker.reset();
        }

        std::shared_ptr<Runtime::CoherentPointDriftGpuEStep> Broker{};
        std::thread Worker{};
        std::atomic<bool> Finished{false};
        std::string Sizes{};
        double VulkanMilliseconds{0.0}, WorstRms{0.0}, WorstParity{0.0};
        bool Passed{true};
    };

    bool Prepare(Extrinsic::Core::Config::EngineConfig& config, bool validation = true)
    {
        config = Runtime::CreateReferenceEngineConfig();
        config.Window.Width = 64;
        config.Window.Height = 64;
        config.Render.EnableValidation = validation;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        return Extrinsic::Platform::Backends::Glfw::CanInitialize();
    }
}

TEST(METHOD056VulkanCpdEStep, DenseStatisticsAndRegistrationMatchTheCpu)
{
    Extrinsic::Core::Config::EngineConfig config;
    if (!Prepare(config)) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<CpdGpuApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Failure.empty()) << run->Failure;
    ASSERT_TRUE(run->Done);

    EXPECT_EQ(run->ParityCases, 13u);
    EXPECT_EQ(run->ClusterCpuRows, 1u) << "only the far row needs the CPU";
    EXPECT_EQ(run->RepeatChecks, 2u);
    EXPECT_TRUE(run->Bitwise) << "two device runs must be bitwise equal";
    EXPECT_LE(run->MaxErrors.LogDenominator, 1e-5);
    EXPECT_LE(run->MaxErrors.Pt1, 1e-5);
    EXPECT_LE(run->MaxErrors.P1, 1e-5);
    EXPECT_LE(run->MaxErrors.PX, 1e-5);
    EXPECT_EQ(run->Stats.Failed, 0u);

    // End to end: the editor run against its CPU twin (the same policy route with the dense CPU
    // pass where the device ran) on the same world points.
    ASSERT_TRUE(run->Editor.has_value());
    const auto& gpu = *run->Editor;
    ASSERT_TRUE(gpu.Succeeded()) << gpu.Message << " / " << gpu.GpuDiagnostic;
    EXPECT_EQ(gpu.Backend, "gpu_vulkan_fp32_dense");
    EXPECT_EQ(gpu.EStepFallbacks, 0u) << gpu.GpuDiagnostic;
    const auto cpu = CPD::Register(run->TargetPoints, run->SourcePoints,
                                   CPD::Params{.OutlierWeight = 0.05, .EstimateScale = false, .EStep = CPD::EStepPolicy::Vulkan});
    ASSERT_TRUE(cpu.Succeeded());
    EXPECT_EQ(cpu.Backend, "cpu_auto");
    EXPECT_EQ(cpu.Iterations, gpu.Iterations);
    double pointError = 0.0;
    for (const auto& p : run->SourcePoints)
        pointError = std::max(pointError, glm::length(glm::dvec3(gpu.Transform * glm::dvec4(glm::dvec3(p), 1.0)) -
                                                      glm::dvec3(cpu.Transform * glm::dvec4(glm::dvec3(p), 1.0))));
    const double extent = 2.0; // the fixture's x/y range
    EXPECT_LE(pointError, 1e-4 * extent);

    std::printf("METHOD-056 parity: %zu cases, max |dlog den| %.3g, Pt1 %.3g, P1 %.3g, PX %.3g, log-den sum %.3g; "
                "bitwise %d; %llu device E-steps in %.1f ms (worker wait %.1f ms); editor run %u iterations in %.1f ms, "
                "point error %.3g\n",
                run->ParityCases, run->MaxErrors.LogDenominator, run->MaxErrors.Pt1, run->MaxErrors.P1, run->MaxErrors.PX,
                run->LogDenSumError, int(run->Bitwise), static_cast<unsigned long long>(run->Stats.Completed), run->ParityMs,
                1e3 * run->Stats.WaitSeconds, gpu.Iterations, run->EditorMs, pointError);
    if (const auto* output = std::getenv("INTRINSIC_METHOD056_BENCHMARK_OUTPUT"))
    {
        const nlohmann::json json{{"benchmark_id", "geometry.coherent_point_drift.vulkan_dense_e_step_smoke"},
            {"method", "geometry.coherent_point_drift"}, {"backend", "gpu_vulkan_compute"},
            {"dataset", "builtin.cpd.rigid_cloud.seed91_1000_and_parity_1500x1400.seed93"}, {"commit", "local-dev"},
            {"metrics", {{"runtime_ms", run->EditorMs}, {"quality_error_linf", pointError}}},
            {"diagnostics", {{"runner", "IntrinsicPointLBVHGpuTests"}, {"mode", "smoke"}, {"parity_cases", run->ParityCases},
                {"max_log_denominator_error", run->MaxErrors.LogDenominator}, {"max_pt1_relative_error", run->MaxErrors.Pt1},
                {"max_p1_relative_error", run->MaxErrors.P1}, {"max_px_relative_error", run->MaxErrors.PX},
                {"bitwise_repeatable", run->Bitwise}, {"device_e_steps", run->Stats.Completed},
                {"editor_iterations", gpu.Iterations}, {"editor_backend", gpu.Backend},
                {"speedup_claimed", false}, {"warmup_iterations", 0}, {"measured_iterations", 1}}},
            {"status", ::testing::Test::HasFailure() ? "failed" : "passed"}};
        std::ofstream stream(output);
        stream << json.dump(2) << '\n';
    }
}

TEST(METHOD056VulkanCpdEStep, ScalingProfile)
{
    const char* output = std::getenv("INTRINSIC_METHOD056_SCALING_OUTPUT");
    if (output == nullptr) GTEST_SKIP() << "opt-in: set INTRINSIC_METHOD056_SCALING_OUTPUT (Release build)";
    Extrinsic::Core::Config::EngineConfig config;
    if (!Prepare(config, false)) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<CpdScalingApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(run->Finished);
    // Manifest thresholds (coherent_point_drift_gpu_vulkan_smoke.yaml).
    const bool passed = run->Passed && run->WorstRms <= 1e-3 && run->WorstParity <= 1e-4;
    EXPECT_TRUE(passed) << "rms " << run->WorstRms << ", parity " << run->WorstParity;
    const auto& device = engine.GetDevice();
    const char* commit = std::getenv("GITHUB_SHA");
    std::ostringstream json;
    json.precision(9);
    json << "{\n  \"benchmark_id\": \"geometry.coherent_point_drift.vulkan_dense_e_step_scaling\",\n"
         << "  \"method\": \"geometry.coherent_point_drift\",\n  \"backend\": \"gpu_vulkan_compute\",\n"
         << "  \"dataset\": \"builtin.cpd_bumpy_ellipsoid_scaling.v1\",\n"
         << "  \"commit\": \"" << (commit ? commit : "local-dev") << "\",\n"
         << "  \"metrics\": {\n    \"runtime_ms\": " << run->VulkanMilliseconds << ",\n    \"quality_error_l2\": "
         << run->WorstRms << ",\n    \"quality_error_linf\": " << run->WorstParity << "\n  },\n"
         << "  \"diagnostics\": {\n    \"runner\": \"IntrinsicPointLBVHGpuTests --gtest_filter=METHOD056VulkanCpdEStep.ScalingProfile\",\n"
         << "    \"mode\": \"performance_scaling_profile\",\n    \"reported_backend\": \"gpu_vulkan_fp32_dense\",\n"
         << "    \"device_subgroup_size\": " << device.SubgroupSize() << ",\n"
         << "    \"validation_layers\": false,\n    \"vsync\": false,\n"
         << "    \"e_step_tolerance\": 1e-06,\n    \"sizes\": [\n" << run->Sizes << "    ]\n  },\n"
         << "  \"status\": \"" << (passed ? "passed" : "failed") << "\"\n}\n";
    std::filesystem::path path(output);
    if (!path.has_extension())
    {
        std::filesystem::create_directories(path);
        path /= "geometry.coherent_point_drift.vulkan_dense_e_step_scaling.json";
    }
    std::ofstream(path) << json.str();
    std::printf("%s", json.str().c_str());
}
