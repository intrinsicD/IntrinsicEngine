// RUNTIME-292: the first end-to-end GPU property transaction on a real device (ADR 0030
// decisions 5-7). Vulkan scalar smoothing reads the canonical residency slot, writes the
// property's ring, the renderer binds the ring front as the colormap scalar (the backbuffer
// changes) before Accept, Accept reads the front back once and publishes it (CPU == readback
// bytewise, including deleted, fixed and isolated rows; == CPU reference within the parity
// bound), the front becomes the canonical slot so the next run on that revision uploads zero
// input bytes (explicit and implicit paths), and Discard / Stop keep the CPU property.
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.TextureUpload;
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
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Geometry.Sphere;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace ECSC = Extrinsic::ECS::Components;
    namespace GC = Extrinsic::Graphics::Components;
    namespace S = Geometry::Smoothing;
    using Domain = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    using Phase = Runtime::EditorPropertySmoothingPhase;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    class TransactionApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        bool StopScenario{}; // Discard with device work queued, then Stop a chunked solve and accept its preview

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
            Extraction = Kernel().Services().Find<Runtime::RenderExtractionCache>();
            // A rendered point cloud whose appearance shows the smoothed scalar; one row is deleted.
            auto& raw = Context.Scene->Raw();
            Entity = Context.Scene->Create();
            raw.emplace<ECSC::MetaData>(Entity, std::string{"TransactionCloud"});
            raw.emplace<ECSC::Transform::Component>(Entity);
            raw.emplace<ECSC::Transform::WorldMatrix>(Entity).Matrix = glm::mat4{1.f};
            raw.emplace<ECSC::Selection::SelectableTag>(Entity);
            raw.emplace<ECSC::StableId>(Entity, ECSC::StableId{292u, 1u});
            raw.emplace<GC::RenderPoints>(Entity).SizeSource = 6.0f;
            // Runtime-authored geometry carries its culling bounds; the reference camera at
            // (0, 0, 3) looks at the origin.
            raw.emplace<ECSC::Culling::World::Bounds>(Entity, ECSC::Culling::World::Bounds{
                .WorldBoundingSphere = Geometry::Sphere{.Center = {0.0f, 0.0f, 0.0f}, .Radius = 2.0f}});
            if (auto* cameras = Kernel().Services().Find<Runtime::CameraControllerRegistry>())
            {
                Extrinsic::Graphics::CameraViewInput seed{};
                seed.Position = {0.f, 0.f, 3.f};
                seed.Forward = {0.f, 0.f, -1.f};
                seed.Up = {0.f, 1.f, 0.f};
                seed.NearPlane = 0.1f;
                seed.FarPlane = 100.f;
                seed.Valid = true;
                (void)cameras->SetWorldSeed(Context.World, seed);
            }
            auto& vertices = raw.emplace<GS::Vertices>(Entity).Properties;
            std::mt19937 random(292);
            std::uniform_real_distribution<float> coordinate(-1.f, 1.f);
            constexpr std::size_t kSide = 12;
            vertices.Resize(kSide * kSide);
            auto positions = vertices.GetOrAdd<glm::vec3>("v:position", glm::vec3{0.f});
            auto signal = vertices.GetOrAdd<float>("signal", 0.f);
            for (std::size_t y = 0; y < kSide; ++y)
                for (std::size_t x = 0; x < kSide; ++x)
                {
                    const auto i = y * kSide + x;
                    positions[i] = {float(x) / float(kSide - 1) * 2.f - 1.f, float(y) / float(kSide - 1) * 2.f - 1.f, 0.f};
                    signal[i] = 0.5f + 0.5f * std::sin(3.f * positions[i].x) + 0.2f * coordinate(random);
                }
            vertices.GetOrAdd<bool>("v:deleted", false)[kDeletedRow] = true;
            auto& visualization = raw.emplace<GC::VisualizationConfig>(Entity);
            visualization.Source = GC::VisualizationConfig::ColorSource::ScalarField;
            visualization.ScalarFieldName = "signal";
            visualization.ScalarDomain = GC::VisualizationConfig::Domain::Vertex;
            visualization.Scalar.AutoRange = false;
            visualization.Scalar.RangeMin = 0.f;
            visualization.Scalar.RangeMax = 1.f;
            visualization.Scalar.Map = Extrinsic::Graphics::Colormap::Type::Viridis;
            RenderId = Runtime::StableEntityLookup::ToRenderId(Entity);
            Id = Runtime::SelectionController::ToStableEntityId(Entity);
            Config.Input = {Domain::PointCloudPoint, "signal", K::Float};
            Config.Output = Config.Input; // overwrite: the preview shows in place
            Config.Positions = {Domain::PointCloudPoint, "v:position", K::Vec3};
            Config.Weight = S::PropertyWeight::Gaussian;
            Config.Neighbors = 6;
            Config.SpatialSigma = 0.4;
            Config.Filter.Method = S::PropertyFilter::Averaging;
            Config.Filter.Iterations = 7;
            Config.Backend = Runtime::PropertySmoothingBackend::Vulkan;
            // A 7x7 grid mesh with a pinned boundary: fixed rows exercise the ring's restore.
            Geometry::HalfedgeMesh::Mesh grid;
            std::vector<Geometry::VertexHandle> handles;
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 7; ++x)
                    handles.push_back(grid.AddVertex({float(x) + 0.1f * coordinate(random), float(y) + 0.1f * coordinate(random), 0.f}));
            for (int y = 0; y < 6; ++y)
                for (int x = 0; x < 6; ++x)
                {
                    const auto v = [&](int i, int j) { return handles[std::size_t(j * 7 + i)]; };
                    (void)grid.AddTriangle(v(x, y), v(x + 1, y), v(x + 1, y + 1));
                    (void)grid.AddTriangle(v(x, y), v(x + 1, y + 1), v(x, y + 1));
                }
            Mesh = Context.Scene->Create();
            GS::PopulateFromMesh(raw, Mesh, grid);
            auto& meshVertices = raw.get<GS::Vertices>(Mesh).Properties;
            auto heat = meshVertices.GetOrAdd<float>("heat", 0.f);
            for (std::size_t i = 0; i < meshVertices.Size(); ++i) heat[i] = coordinate(random);
            MeshId = Runtime::SelectionController::ToStableEntityId(Mesh);
            MeshConfig.Input = {Domain::MeshVertex, "heat", K::Float};
            MeshConfig.Output = MeshConfig.Input;
            MeshConfig.Positions = {Domain::MeshVertex, "v:position", K::Vec3};
            MeshConfig.Weight = S::PropertyWeight::MeshUniform;
            MeshConfig.PreserveBoundary = true;
            MeshConfig.Filter.Method = S::PropertyFilter::SpectralHeat;
            MeshConfig.Filter.Iterations = 2;
            MeshConfig.Filter.HeatTime = 0.6;
            MeshConfig.Backend = Runtime::PropertySmoothingBackend::Vulkan;
        }

        static constexpr std::size_t kDeletedRow = 5;
        Geometry::PropertySet& Props(const entt::entity entity) { return Context.Scene->Raw().get<GS::Vertices>(entity).Properties; }
        std::vector<float> Signal(const char* name = "signal") { return std::as_const(Props(Entity)).Get<float>(name).Vector(); }
        std::vector<float> Heat() { return std::as_const(Props(Mesh)).Get<float>("heat").Vector(); }
        Extrinsic::Graphics::GpuPropertyResidency* Residency() { return Context.SpatialIndices->PropertyResidency(); }
        Extrinsic::Graphics::GpuPropertyKey Key() { return Runtime::MakeGpuPropertyKey(Context.World, Entity, Config.Output); }
        Extrinsic::Graphics::GpuPropertyKey MeshKey() { return Runtime::MakeGpuPropertyKey(Context.World, Mesh, MeshConfig.Output); }
        Runtime::EditorProcessingCommands Commands() { return Runtime::BindEditorProcessingCommands(Context); }
        Runtime::EditorPropertySmoothingTransactionSnapshot Snapshot() { return Runtime::SnapshotEditorPropertySmoothing(Commands(), Run); }

        // The scalar buffer the point lane's entity config binds this frame (and its colormap).
        std::uint32_t BoundColormap{};
        Extrinsic::RHI::GpuEntityConfig BoundConfig{};
        std::string DescribeBound() const
        {
            return "ElementCount " + std::to_string(BoundConfig.ElementCount) + " range [" + std::to_string(BoundConfig.ScalarRangeMin) + ", " +
                   std::to_string(BoundConfig.ScalarRangeMax) + "] point mode " + std::to_string(BoundConfig.Point.PointMode) + " size " +
                   std::to_string(BoundConfig.Point.PointSize) + " domain " + std::to_string(BoundConfig.VisDomain);
        }
        std::optional<std::uint64_t> BoundScalar()
        {
            auto& device = Kernel().GetDevice();
            auto& world = Kernel().GetRenderer().GetGpuWorld();
            const auto capacity = world.GetInstanceCapacity();
            if (capacity == 0u || !world.GetInstanceStaticBuffer().IsValid() || !world.GetEntityConfigBuffer().IsValid()) return std::nullopt;
            std::vector<Extrinsic::RHI::GpuInstanceStatic> instances(capacity);
            device.ReadBuffer(world.GetInstanceStaticBuffer(), instances.data(), instances.size() * sizeof(instances.front()), 0u);
            for (const auto& instance : instances)
            {
                if (instance.EntityID != RenderId || (instance.RenderFlags & Extrinsic::RHI::GpuRender_Visible) == 0u) continue;
                Extrinsic::RHI::GpuEntityConfig config{};
                device.ReadBuffer(world.GetEntityConfigBuffer(), &config, sizeof(config), std::uint64_t(instance.ConfigSlot) * sizeof(config));
                if (config.ColorSourceMode != 2u) return std::nullopt; // not the scalar-field mode
                BoundColormap = config.ColormapID;
                BoundConfig = config;
                return config.ScalarBDA;
            }
            return std::nullopt;
        }

        // The backbuffer of the last presented frame (the renderer copies it into our buffer).
        bool EnsurePixelReadback()
        {
            if (Pixels.IsValid()) return true;
            auto& device = Kernel().GetDevice();
            const auto extent = device.GetBackbufferExtent();
            PixelBytes = Extrinsic::RHI::BytesPerBlock(device.GetBackbufferFormat());
            if (PixelBytes < 4u || extent.Width == 0u || extent.Height == 0u) return false;
            PixelCount = std::size_t(extent.Width) * extent.Height;
            Pixels = device.CreateBuffer({.SizeBytes = std::uint64_t(PixelBytes) * PixelCount, .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
                                          .HostVisible = true, .DebugName = "RUNTIME292.Pixels"});
            if (!Pixels.IsValid()) return false;
            Kernel().GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer(Pixels);
            return true;
        }
        std::vector<std::uint8_t> CapturePixels()
        {
            std::vector<std::uint8_t> bytes(std::size_t(PixelBytes) * PixelCount);
            Kernel().GetDevice().ReadBuffer(Pixels, bytes.data(), bytes.size(), 0u);
            return bytes;
        }
        // Pixels that differ from the frame's clear colour (its first pixel).
        std::size_t NonBackground(const std::vector<std::uint8_t>& a) const
        {
            std::size_t count = 0;
            for (std::size_t i = PixelBytes; i + 3 < a.size(); i += PixelBytes)
                if (std::abs(int(a[i]) - int(a[0])) > 8 || std::abs(int(a[i + 1]) - int(a[1])) > 8 || std::abs(int(a[i + 2]) - int(a[2])) > 8)
                    ++count;
            return count;
        }
        // Failure diagnostics: the dominant colours of a capture next to the CPU Viridis samples.
        std::string ColourReport(const std::vector<std::uint8_t>& a)
        {
            std::vector<std::uint32_t> colours;
            for (std::size_t i = 0; i + 3 < a.size(); i += PixelBytes)
                colours.push_back(std::uint32_t(a[i]) | (std::uint32_t(a[i + 1]) << 8) | (std::uint32_t(a[i + 2]) << 16));
            std::sort(colours.begin(), colours.end());
            std::vector<std::pair<std::size_t, std::uint32_t>> runs;
            for (std::size_t i = 0; i < colours.size();)
            {
                std::size_t j = i;
                while (j < colours.size() && colours[j] == colours[i]) ++j;
                runs.push_back({j - i, colours[i]});
                i = j;
            }
            std::sort(runs.rbegin(), runs.rend());
            std::string report = "top colours (b,g,r x count):";
            for (std::size_t k = 0; k < runs.size() && k < 6; ++k)
                report += " (" + std::to_string(runs[k].second & 0xffu) + "," + std::to_string((runs[k].second >> 8) & 0xffu) + "," +
                          std::to_string((runs[k].second >> 16) & 0xffu) + ")x" + std::to_string(runs[k].first);
            const auto& colormaps = Kernel().GetRenderer().GetColormapSystem();
            for (const float t : {0.0f, 0.5f, 1.0f})
            {
                const auto c = colormaps.SampleCpu(Extrinsic::Graphics::Colormap::Type::Viridis, t);
                report += "; viridis(" + std::to_string(t) + ")=(" + std::to_string(c.R) + "," + std::to_string(c.G) + "," + std::to_string(c.B) + ")";
            }
            return report;
        }
        // Debug aid: the capture as a PPM (RGB order assumed BGRA) when INTRINSIC_RUNTIME292_DUMP names a directory.
        void Dump(const std::vector<std::uint8_t>& a, const char* name)
        {
            const char* directory = std::getenv("INTRINSIC_RUNTIME292_DUMP");
            if (!directory) return;
            const auto extent = Kernel().GetDevice().GetBackbufferExtent();
            std::FILE* file = std::fopen((std::string{directory} + "/" + name + ".ppm").c_str(), "wb");
            if (!file) return;
            std::fprintf(file, "P6\n%u %u\n255\n", extent.Width, extent.Height);
            for (std::size_t i = 0; i + 3 < a.size(); i += PixelBytes)
            {
                const unsigned char rgb[3]{a[i + 2], a[i + 1], a[i]};
                std::fwrite(rgb, 1, 3, file);
            }
            std::fclose(file);
        }
        int MaxDelta(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) const
        {
            int delta = 0;
            for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) delta = std::max(delta, std::abs(int(a[i]) - int(b[i])));
            return delta;
        }
        // Pixels whose colour differs noticeably between two captures.
        std::size_t ChangedPixels(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) const
        {
            std::size_t changed = 0;
            for (std::size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += PixelBytes)
                if (std::abs(int(a[i]) - int(b[i])) > 8 || std::abs(int(a[i + 1]) - int(b[i + 1])) > 8 || std::abs(int(a[i + 2]) - int(b[i + 2])) > 8)
                    ++changed;
            return changed;
        }

        // Reads the current front (or canonical slot) of a key back as floats.
        void QueueFrontReadback(const Extrinsic::Graphics::GpuPropertyKey& key)
        {
            const auto front = Residency()->Front(key);
            ASSERT_TRUE(front);
            FrontReadback = Context.SpatialIndices->QueueGpuCompute(std::size_t(front->Bytes),
                [buffer = front->Buffer, lease = front->Lease](Extrinsic::RHI::ICommandContext& commands, const Runtime::SpatialGpuIndexView&) {
                    commands.BufferBarrier(buffer, Extrinsic::RHI::MemoryAccess::ShaderRead | Extrinsic::RHI::MemoryAccess::ShaderWrite |
                                                   Extrinsic::RHI::MemoryAccess::TransferRead, Extrinsic::RHI::MemoryAccess::TransferRead);
                    return buffer;
                }, Runtime::SpatialGpuLatency::Immediate);
        }
        // True once the readback landed in FrontValues; fails the run on a readback failure.
        bool FrontReady()
        {
            if (FrontReadback->State == Runtime::SpatialQueryState::Failed) { Fail("front readback failed: " + FrontReadback->Diagnostic); return false; }
            if (FrontReadback->State != Runtime::SpatialQueryState::Ready) return false;
            FrontValues.resize(FrontReadback->Data.size() / sizeof(float));
            std::memcpy(FrontValues.data(), FrontReadback->Data.data(), FrontValues.size() * sizeof(float));
            return true;
        }
        void Accept()
        {
            Accepted.reset();
            const auto accepted = Runtime::AcceptEditorPropertySmoothing(Commands(), Run,
                [this](Runtime::EditorPropertySmoothingResult result) { Accepted = std::move(result); });
            if (accepted.Status != Runtime::EditorCommandStatus::Pending) Fail("accept refused: " + accepted.Message);
        }
        bool Start(const std::uint32_t id, const Runtime::PropertySmoothingConfig& config, const char* what)
        {
            Runtime::EditorPropertySmoothingResult failure;
            Run = Runtime::StartEditorPropertySmoothing(Commands(), id, config, failure);
            if (!Run) Fail(std::string{what} + " start rejected: " + failure.Message);
            return Run != nullptr;
        }
        // False while the run computes; fails the scenario when it ended without a result.
        bool Ready(const char* what)
        {
            const auto snapshot = Snapshot();
            if (snapshot.Phase == Phase::Running) return false;
            if (snapshot.Phase != Phase::ReadyToAccept) { Fail(std::string{what} + " ended without a result: " + snapshot.Result.Message); return false; }
            return true;
        }
        void ExpectBytewise(const std::vector<float>& cpu, const char* what)
        {
            ASSERT_EQ(cpu.size(), FrontValues.size()) << what;
            for (std::size_t i = 0; i < cpu.size(); ++i)
                EXPECT_EQ(cpu[i], FrontValues[i]) << what << ": row " << i << " (CPU revision and bound front differ)";
        }

        void Fail(const std::string& message)
        {
            ADD_FAILURE() << message;
            Kernel().RequestExit();
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(150)) { TimedOut = true; return Fail("RUNTIME-292 smoke timeout at step " + std::to_string(Step)); }
            if (!Kernel().GetDevice().IsOperational() || !Context.SpatialIndices || !Extraction) return;
            ++OperationalFrames;
            if (StopScenario) return StopFrame();
            switch (Step)
            {
            case 0: // The CPU property is the colormap scalar; capture the frame, then start the run.
            {
                if (OperationalFrames < 12) return;
                if (!EnsurePixelReadback()) return Fail("no backbuffer readback");
                const auto bound = BoundScalar();
                if (!bound) { if (OperationalFrames < 300) return; return Fail("the point lane never bound the CPU scalar"); }
                // The colormap LUT must be resident before a pixel says anything about the scalar.
                const auto& colormaps = Kernel().GetRenderer().GetColormapSystem();
                if (!colormaps.IsReady() || BoundColormap != colormaps.GetBindlessIndex(Extrinsic::Graphics::Colormap::Type::Viridis))
                {
                    if (OperationalFrames < 300) return;
                    return Fail("the Viridis colormap never became the bound LUT");
                }
                if (++SettleFrames < 6) return;
                CpuScalar = *bound;
                CpuPixels = CapturePixels();
                Before = Signal();
                // The CPU reference on the same input, into its own output, before the device runs.
                auto reference = Config;
                reference.Backend = Runtime::PropertySmoothingBackend::Cpu;
                reference.Output.Name = "cpu_out";
                const auto result = Runtime::ApplyEditorPropertySmoothingCommand(Commands(), Id, reference);
                if (!result.Succeeded()) return Fail("CPU reference failed: " + result.Message);
                Expected = Signal("cpu_out");
                StatsBefore = Residency()->Stats();
                if (!Start(Id, Config, "run 1")) return;
                ++Step;
                return;
            }
            case 1: // Ready: the first run uploaded its input once; the front exists.
            {
                if (!Ready("run 1")) return;
                EXPECT_TRUE(Snapshot().CanAccept) << Snapshot().AcceptDisabledReason;
                EXPECT_EQ(Residency()->Stats().Uploads, StatsBefore.Uploads + 1u) << "the first run uploads the input once";
                const auto front = Residency()->Front(Key());
                if (!front || !Residency()->HasRing(Key())) return Fail("no ring front after run 1");
                FrontAddress = front->Address;
                SettleFrames = 0;
                ++Step;
                return;
            }
            case 2: // Before Accept the renderer binds the front and the colormap visibly changes.
            {
                if (++SettleFrames < 6) return;
                const auto bound = BoundScalar();
                ASSERT_TRUE(bound) << "the point lane lost its scalar binding";
                EXPECT_EQ(*bound, FrontAddress) << "before Accept the colormap reads the ring front";
                EXPECT_NE(*bound, CpuScalar);
                EXPECT_GT(Extraction->GetLastStats().VisualizationRecipeScalarGpuFrontsObserved, 0u);
                const auto preview = CapturePixels();
                Dump(CpuPixels, "cpu");
                Dump(preview, "preview");
                EXPECT_GT(NonBackground(CpuPixels), 0u) << "the point cloud is not drawn (copies: "
                    << Kernel().GetRenderer().GetLastRenderGraphStats().DefaultRecipeBackbufferReadbackCopyCount << ")";
                EXPECT_GT(ChangedPixels(CpuPixels, preview), 0u) << "the preview changed no pixel of the backbuffer ("
                    << NonBackground(CpuPixels) << " / " << NonBackground(preview) << " non-background pixels, max channel delta "
                    << MaxDelta(CpuPixels, preview) << ", " << DescribeBound() << "; " << ColourReport(CpuPixels) << ")";
                EXPECT_EQ(Signal(), Before) << "the CPU property is untouched before Accept";
                QueueFrontReadback(Key());
                ++Step;
                return;
            }
            case 3: // The front's bytes are the result that Accept will publish.
            {
                if (!FrontReady()) return;
                EXPECT_NE(FrontValues, Before) << "the device changed the scalar";
                {
                    double moved = 0;
                    for (std::size_t i = 0; i < Before.size() && i < FrontValues.size(); ++i) moved = std::max(moved, double(std::abs(FrontValues[i] - Before[i])));
                    EXPECT_GT(moved, 0.02) << "the smoothing moved the scalar by too little for a visible colormap change";
                }
                EXPECT_EQ(FrontValues[kDeletedRow], Before[kDeletedRow]) << "a deleted row keeps its published value in the ring";
                Accept();
                ++Step;
                return;
            }
            case 4: // Applied only after the CPU publication; CPU == readback bytewise, == reference within the bound.
            {
                if (!Accepted) { EXPECT_NE(Snapshot().Phase, Phase::Applied) << "Applied before the publication landed"; return; }
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                EXPECT_EQ(Accepted->BackendId, "vulkan_compute");
                EXPECT_EQ(Snapshot().Phase, Phase::Applied);
                const auto cpu = Signal();
                ExpectBytewise(cpu, "run 1");
                ASSERT_EQ(Expected.size(), cpu.size());
                double range = 0, error = 0;
                for (const float v : Before) range = std::max(range, double(std::abs(v)));
                for (std::size_t i = 0; i < cpu.size(); ++i)
                    if (i != kDeletedRow) error = std::max(error, std::abs(double(cpu[i]) - double(Expected[i])));
                EXPECT_LE(error, 1e-9 * std::max(range, 1.0)) << "GPU result vs CPU reference";
                // The front is the canonical slot of the accepted revision; the ring is gone.
                EXPECT_FALSE(Residency()->HasRing(Key()));
                const auto canonical = Residency()->Front(Key());
                ASSERT_TRUE(canonical);
                EXPECT_EQ(canonical->Address, FrontAddress);
                EXPECT_EQ(canonical->Revision, std::as_const(Props(Entity)).Get<float>("signal").Revision());
                AcceptedSignal = cpu;
                // Run 2 right away on the bound revision: it must reuse the accepted front.
                StatsBefore = Residency()->Stats();
                if (!Start(Id, Config, "run 2")) return;
                SettleFrames = 0;
                ++Step;
                return;
            }
            case 5: // The accepted front served run 2's input: zero upload bytes; its preview shows, Discard hides it.
            {
                if (SettleFrames == 0)
                {
                    if (!Ready("run 2")) return;
                    const auto stats = Residency()->Stats();
                    EXPECT_EQ(stats.UploadBytes, StatsBefore.UploadBytes) << "run 2 on the accepted revision uploads zero input bytes";
                    EXPECT_EQ(stats.Uploads, StatsBefore.Uploads);
                    EXPECT_GT(stats.Hits, StatsBefore.Hits) << "the accepted front is the canonical hit";
                }
                if (++SettleFrames < 6) return;
                const auto preview = CapturePixels();
                Runtime::DiscardEditorPropertySmoothing(Commands(), Run);
                EXPECT_EQ(Snapshot().Phase, Phase::Discarded);
                EXPECT_FALSE(Residency()->HasRing(Key()));
                EXPECT_EQ(Signal(), AcceptedSignal) << "Discard publishes nothing";
                PreviewPixels = preview;
                SettleFrames = 0;
                ++Step;
                return;
            }
            case 6: // After Discard the accepted CPU revision shows again; then undo / redo move the revision on.
            {
                if (++SettleFrames < 6) return;
                const auto bound = BoundScalar();
                ASSERT_TRUE(bound);
                EXPECT_NE(*bound, FrontAddress) << "after Discard the canonical slot is not observed; the CPU revision is uploaded";
                const auto restored = CapturePixels();
                EXPECT_GT(ChangedPixels(CpuPixels, restored), 0u) << "control: the accepted CPU revision renders like the original ("
                    << MaxDelta(CpuPixels, restored) << ")";
                EXPECT_GT(ChangedPixels(restored, PreviewPixels), 0u) << "run 2's preview was visible";
                ASSERT_TRUE(History.Undo().Succeeded());
                EXPECT_EQ(Signal(), Before);
                ASSERT_TRUE(History.Redo().Succeeded());
                EXPECT_EQ(Signal(), AcceptedSignal);
                RedoneRevision = std::as_const(Props(Entity)).Get<float>("signal").Revision();
                EXPECT_NE(RedoneRevision, Residency()->Front(Key())->Revision) << "redo republishes under a new revision";
                StatsBefore = Residency()->Stats();
                if (!Start(Id, Config, "run 3")) return;
                ++Step;
                return;
            }
            case 7: // The redone revision uploads once; Discard.
            {
                if (!Ready("run 3")) return;
                EXPECT_EQ(Residency()->Stats().Uploads, StatsBefore.Uploads + 1u) << "the redone revision uploads once";
                Runtime::DiscardEditorPropertySmoothing(Commands(), Run);
                EXPECT_EQ(std::as_const(Props(Entity)).Get<float>("signal").Revision(), RedoneRevision) << "Discard publishes nothing";
                // Implicit CG on the same entity: its seeds come from the canonical slot.
                Implicit = Config;
                Implicit.Filter.Method = S::PropertyFilter::Implicit;
                Implicit.Filter.Solver = S::PropertySolver::ConjugateGradient;
                Implicit.Filter.TimeStep = 0.5;
                Implicit.Filter.Iterations = 3;
                Implicit.Filter.SolverTolerance = 1e-10;
                StatsBefore = Residency()->Stats();
                if (!Start(Id, Implicit, "implicit run")) return;
                ++Step;
                return;
            }
            case 8: // The implicit run reads the resident revision: zero input bytes uploaded.
            {
                if (!Ready("implicit run")) return;
                const auto stats = Residency()->Stats();
                EXPECT_EQ(stats.UploadBytes, StatsBefore.UploadBytes) << "the implicit run uploads zero input bytes";
                EXPECT_EQ(stats.Uploads, StatsBefore.Uploads);
                EXPECT_GT(stats.Hits, StatsBefore.Hits);
                QueueFrontReadback(Key());
                ++Step;
                return;
            }
            case 9:
            {
                if (!FrontReady()) return;
                Before = Signal();
                Accept();
                ++Step;
                return;
            }
            case 10: // The implicit result: CPU == front bytewise, the CPU stage is reported.
            {
                if (!Accepted) return;
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                EXPECT_NE(Accepted->Message.find("CPU-assembled coupling uploaded"), std::string::npos) << Accepted->Message;
                ExpectBytewise(Signal(), "implicit run");
                EXPECT_NE(Signal(), Before);
                // Spectral heat on the pinned grid mesh: fixed rows are restored in the ring.
                MeshBefore = Heat();
                if (!Start(MeshId, MeshConfig, "mesh run")) return;
                ++Step;
                return;
            }
            case 11:
            {
                if (!Ready("mesh run")) return;
                QueueFrontReadback(MeshKey());
                ++Step;
                return;
            }
            case 12:
            {
                if (!FrontReady()) return;
                Accept();
                ++Step;
                return;
            }
            case 13:
            {
                if (!Accepted) return;
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                const auto heat = Heat();
                ExpectBytewise(heat, "mesh run");
                EXPECT_NE(heat, MeshBefore);
                // Boundary rows are pinned: bytewise equal to the input on the CPU and in the ring.
                std::size_t pinned = 0;
                for (std::size_t i = 0; i < heat.size(); ++i)
                    if (heat[i] == MeshBefore[i]) ++pinned;
                EXPECT_GE(pinned, 24u) << "the 7x7 grid's boundary rows keep their values";
                EXPECT_EQ(Residency()->Front(MeshKey())->Revision, std::as_const(Props(Mesh)).Get<float>("heat").Revision());
                Done = true;
                Kernel().RequestExit();
                return;
            }
            default: return;
            }
        }

        void StopFrame()
        {
            switch (Step)
            {
            case 0: // Discard while the device is computing (its submission is queued).
            {
                if (OperationalFrames < 12) return;
                Before = Signal();
                if (!Start(Id, Config, "run")) return;
                ++Step;
                return;
            }
            case 1:
            {
                const auto snapshot = Snapshot();
                if (snapshot.Phase != Phase::Running) return Fail("the run ended before Discard: " + snapshot.Result.Message);
                if (!snapshot.DeviceWorkQueued) return;
                Runtime::DiscardEditorPropertySmoothing(Commands(), Run);
                EXPECT_EQ(Snapshot().Phase, Phase::Discarded);
                EXPECT_FALSE(Residency()->HasRing(Key()));
                SettleFrames = 0;
                ++Step;
                return;
            }
            case 2: // The cancelled job publishes nothing; then a chunked implicit solve to stop.
            {
                if (++SettleFrames < 12) return;
                EXPECT_EQ(Signal(), Before) << "a discarded run leaves the CPU property";
                EXPECT_FALSE(Residency()->HasRing(Key()));
                EXPECT_EQ(Residency()->Stats().Rings, 0u);
                auto implicit = Config;
                implicit.Filter.Method = S::PropertyFilter::Implicit;
                implicit.Filter.Solver = S::PropertySolver::ConjugateGradient;
                implicit.Filter.TimeStep = 0.5;
                implicit.Filter.Iterations = 600; // many chained solves: several immediate chunks
                implicit.Filter.SolverTolerance = 1e-10;
                implicit.Filter.MaxSolverIterations = 500;
                if (!Start(Id, implicit, "implicit run")) return;
                ++Step;
                return;
            }
            case 3: // Stop after the first preview; the stopped run waits for Accept.
            {
                const auto snapshot = Snapshot();
                if (snapshot.Phase == Phase::Running)
                {
                    if (snapshot.Previews >= 1u && !StopRequested) { Runtime::StopEditorPropertySmoothing(Run); StopRequested = true; }
                    return;
                }
                if (snapshot.Phase != Phase::ReadyToAccept) return Fail("stopped run ended without a result: " + snapshot.Result.Message);
                EXPECT_TRUE(StopRequested) << "the solve finished before Stop could take effect; raise the iteration count";
                EXPECT_TRUE(snapshot.CanAccept) << snapshot.AcceptDisabledReason;
                QueueFrontReadback(Key());
                ++Step;
                return;
            }
            case 4:
            {
                if (!FrontReady()) return;
                EXPECT_NE(FrontValues, Before) << "the stopped solve changed the scalar";
                Accept();
                ++Step;
                return;
            }
            case 5: // Accepting a stopped run takes its intermediate state.
            {
                if (!Accepted) return;
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                ExpectBytewise(Signal(), "stopped run");
                EXPECT_FALSE(Residency()->HasRing(Key()));
                ASSERT_TRUE(History.Undo().Succeeded());
                EXPECT_EQ(Signal(), Before);
                Done = true;
                Kernel().RequestExit();
                return;
            }
            default: return;
            }
        }

        void Shutdown() override
        {
            Run.reset();
            FrontReadback.reset();
            if (Pixels.IsValid())
            {
                Kernel().GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer({});
                Kernel().GetDevice().DestroyBuffer(Pixels);
                Pixels = {};
            }
            Context = {};
        }

        Runtime::EditorProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        Runtime::RenderExtractionCache* Extraction{};
        entt::entity Entity{}, Mesh{};
        std::uint32_t RenderId{}, Id{}, MeshId{};
        Runtime::PropertySmoothingConfig Config{}, Implicit{}, MeshConfig{};
        Runtime::EditorPropertySmoothingTransactionHandle Run{};
        std::shared_ptr<Runtime::SpatialGpuResult> FrontReadback{};
        std::optional<Runtime::EditorPropertySmoothingResult> Accepted{};
        std::vector<float> Before{}, FrontValues{}, Expected{}, AcceptedSignal{}, MeshBefore{};
        std::vector<std::uint8_t> CpuPixels{}, PreviewPixels{};
        Extrinsic::RHI::BufferHandle Pixels{};
        std::uint32_t PixelBytes{};
        std::size_t PixelCount{};
        std::uint64_t CpuScalar{}, FrontAddress{}, RedoneRevision{};
        Extrinsic::Graphics::GpuPropertyResidencyStats StatsBefore{};
        std::chrono::steady_clock::time_point Started{};
        std::size_t OperationalFrames{}, SettleFrames{};
        int Step{};
        bool Done{}, TimedOut{}, StopRequested{};
    };

    bool Prepare(Extrinsic::Core::Config::EngineConfig& config)
    {
        config = Runtime::CreateReferenceEngineConfig();
        config.Window.Width = 128;
        config.Window.Height = 128;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        return Extrinsic::Platform::Backends::Glfw::CanInitialize();
    }

    void RunScenario(const bool stopScenario)
    {
        Extrinsic::Core::Config::EngineConfig config;
        if (!Prepare(config)) GTEST_SKIP() << "GLFW unavailable";
        auto app = std::make_unique<TransactionApp>();
        auto* run = app.get();
        run->StopScenario = stopScenario;
        Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
        engine.EmplaceModule<Runtime::SpatialIndexCache>();
        engine.EmplaceModule<Runtime::CameraModule>(); // the main camera the seed drives
        engine.Initialize();
        Shutdown shutdown{engine};
        if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
        engine.Run();
        ASSERT_TRUE(engine.GetDevice().IsOperational());
        ASSERT_FALSE(run->TimedOut);
        ASSERT_TRUE(run->Done) << "the scenario stopped at step " << run->Step;
    }
}

TEST(RUNTIME292ScalarSmoothingTransaction, PreviewBindsTheFrontAndAcceptPublishesOnce) { RunScenario(false); }

TEST(RUNTIME292ScalarSmoothingTransaction, DiscardAndStopKeepTheCpuProperty) { RunScenario(true); }
