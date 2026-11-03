// ARCH-022 S3b: the job form spike's preview on a real device. A paused coherent point drift run shows its moving
// source in the viewport (the render block shows the positions' ring front and the drawn pixels move) while the
// canonical positions and the history stay unchanged, and an editor GPU position run on the same positions is
// refused; Discard brings the CPU positions back (the ring is gone before the output is free); a successor run on
// the same positions previews in its own ring; its Accept writes the positions once and the frame keeps the moved
// cloud.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include "PluginGpuSmoke.hpp"

import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StableEntityLookup;
import Intrinsic.Plugin.JobFormSpike;

namespace
{
    namespace R = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace ECSC = Extrinsic::ECS::Components;
    namespace GC = Extrinsic::Graphics::Components;
    namespace JFS = Intrinsic::Plugins::JobFormSpike;
    using Json = nlohmann::json;
    using D = R::GeometryElementDomain;

    struct Harness : Intrinsic::Tests::Plugins::Harness
    {
        Harness(Extrinsic::Core::Config::EngineConfig config, std::unique_ptr<Intrinsic::Tests::RuntimeTestModule> app)
            : Intrinsic::Tests::Plugins::Harness(&JFS::Register, &JFS::Shutdown, std::move(config), std::move(app),
                                                 [](Intrinsic::Tests::RuntimeTestKernel& engine)
                                                 { engine.EmplaceModule<R::CameraModule>(); })
        {
        }
    };
    using Gpu = Intrinsic::Tests::Plugins::Gpu<Harness>;

    // A 10 x 10 grid on the left of the view; the target is the same grid on the right (not drawn).
    std::vector<glm::vec3> Grid(const float x0)
    {
        std::vector<glm::vec3> points;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                points.emplace_back(x0 + 0.5f * float(x) / 9.f, -0.5f + float(y) / 9.f, 0.f);
        return points;
    }

    entt::entity Cloud(Extrinsic::ECS::Scene::Registry& scene, const std::vector<glm::vec3>& points, const bool drawn,
                       const std::uint32_t stable)
    {
        auto& raw = scene.Raw();
        const auto entity = scene.Create();
        raw.emplace<ECSC::MetaData>(entity, std::string{drawn ? "Source" : "Target"});
        raw.emplace<ECSC::Transform::Component>(entity).Scale = glm::vec3{1.f};
        raw.emplace<ECSC::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<ECSC::StableId>(entity, ECSC::StableId{stable, 1u});
        if (drawn)
        {
            raw.emplace<ECSC::Selection::SelectableTag>(entity);
            raw.emplace<GC::RenderPoints>(entity).Size = 6.f;
        }
        auto& vertices = raw.emplace<GS::Vertices>(entity).Properties;
        vertices.Resize(points.size());
        vertices.GetOrAdd<glm::vec3>("v:position", glm::vec3{0.f}).Vector().assign(points.begin(), points.end());
        return entity;
    }

    struct Frame
    {
        std::vector<std::uint8_t> Bytes{};
        std::uint32_t PixelBytes{}, Width{};

        [[nodiscard]] std::size_t Changed(const Frame& other) const
        {
            std::size_t changed = 0;
            for (std::size_t i = 0; i + 3 < Bytes.size() && i + 3 < other.Bytes.size(); i += PixelBytes)
                for (std::size_t c = 0; c < 3; ++c)
                    if (std::abs(int(Bytes[i + c]) - int(other.Bytes[i + c])) > 8)
                    {
                        ++changed;
                        break;
                    }
            return changed;
        }
        // Non-background pixels (the clear colour is the first pixel) in the right half.
        [[nodiscard]] std::size_t Right() const
        {
            std::size_t count = 0;
            for (std::size_t i = PixelBytes; i + 3 < Bytes.size(); i += PixelBytes)
                if ((i / PixelBytes) % Width >= Width / 2 &&
                    (std::abs(int(Bytes[i]) - int(Bytes[0])) > 8 || std::abs(int(Bytes[i + 1]) - int(Bytes[1])) > 8 ||
                     std::abs(int(Bytes[i + 2]) - int(Bytes[2])) > 8))
                    ++count;
            return count;
        }
    };
    // The engine, the drawn source and the hidden target, the services, and the tools as the agent server calls them.
    struct Scenario
    {
        Gpu Engine{};
        Harness& H{*Engine.H};
        std::vector<glm::vec3> Original{Grid(-0.6f)};
        entt::entity Source{Cloud(H.Scene(), Original, true, 4101u)}, Target{Cloud(H.Scene(), Grid(0.1f), false, 4102u)};
        std::uint32_t RenderId{R::StableEntityLookup::ToRenderId(Source)};
        R::RenderExtractionCache* Extraction{H.Engine.Services().Find<R::RenderExtractionCache>()};
        R::SpatialIndexCache* Spatial{H.Engine.Services().Find<R::SpatialIndexCache>()};
        R::GeometryPropertyRef Positions{.Domain = D::PointCloudPoint, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        Extrinsic::Graphics::GpuPropertyKey Key{R::MakeGpuPropertyKey(H.Engine.ActiveWorld(), Source, Positions)};

        Scenario()
        {
            if (auto* cameras = H.Engine.Services().Find<R::CameraControllerRegistry>())
            {
                Extrinsic::Graphics::CameraViewInput seed{};
                seed.Position = {0.f, 0.f, 3.f};
                seed.Forward = {0.f, 0.f, -1.f};
                seed.Up = {0.f, 1.f, 0.f};
                seed.NearPlane = 0.1f;
                seed.FarPlane = 100.f;
                seed.Valid = true;
                (void)cameras->SetWorldSeed(H.Engine.ActiveWorld(), seed);
            }
        }
        Extrinsic::Graphics::GpuPropertyResidency& Residency() { return *Spatial->PropertyResidency(); }
        std::vector<glm::vec3> Canonical()
        {
            return std::as_const(H.Scene().Raw()).get<GS::Vertices>(Source).Properties.Get<glm::vec3>("v:position").Vector();
        }
        Json Observe(const std::uint64_t run)
        {
            return Json::parse(R::InvokeAgentOperation(H.Registry, "run_observe", H.Context, Json{{"run", run}}.dump(), false).Text);
        }
        bool Control(const std::uint64_t run, const char* action, const int count = 1)
        {
            return !R::InvokeAgentOperation(H.Registry, "run_control", H.Context,
                                            Json{{"run", run}, {"action", action}, {"count", count}}.dump(), false).IsError;
        }
        // 0 when refused (e.g. the output is still held: callers retry on a later frame).
        std::uint64_t Start(const Json& extra = Json::object())
        {
            Json args{{"source", R::SelectionController::ToStableEntityId(Source)},
                      {"target", R::SelectionController::ToStableEntityId(Target)}, {"max_iterations", 80},
                      {"tolerance", 1e-12}};
            args.update(extra);
            const auto out = R::InvokeAgentOperation(H.Registry, "job_form_spike_start", H.Context, args.dump(), false);
            return out.IsError ? 0u : Json::parse(out.Text)["run"].get<std::uint64_t>();
        }
        // Paused (a step count done) and its newest copy is the one shown.
        bool ShownAndPaused(const std::uint64_t run)
        {
            const auto o = Observe(run);
            return o["phase"] == "paused" && o["steps"].get<int>() > 0 && o["shown_step"] == o["steps"];
        }
    };
}

TEST(JobFormSpikePluginGpuSmoke, APausedRunShowsItsPreviewAndOnlyAcceptWritesIt)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Scenario t;
    auto& h = t.H;
    ASSERT_NE(t.Extraction, nullptr);
    ASSERT_NE(t.Spatial, nullptr);
    const auto& original = t.Original;
    const auto source = t.Source;
    const auto renderId = t.RenderId;
    auto* extraction = t.Extraction;
    auto* spatial = t.Spatial;
    const auto& positions = t.Positions;
    const auto& key = t.Key;
    const auto canonical = [&] { return t.Canonical(); };
    const auto observe = [&](const std::uint64_t run) { return t.Observe(run); };
    const auto control = [&](const std::uint64_t run, const char* action, const int count = 1) { return t.Control(run, action, count); };
    const auto start = [&] { return t.Start(); };
    auto& gpu = t.Engine;

    Extrinsic::RHI::BufferHandle pixels{};
    Frame before{}, preview{}, restored{}, accepted{};
    std::uint64_t first = 0, second = 0;
    int step = 0, frames = 0, settle = 0;
    const auto capture = [&]
    {
        auto& device = h.Engine.GetDevice();
        Frame frame{.PixelBytes = Extrinsic::RHI::BytesPerBlock(device.GetBackbufferFormat()),
                    .Width = std::uint32_t(device.GetBackbufferExtent().Width)};
        frame.Bytes.resize(std::size_t(frame.PixelBytes) * frame.Width * device.GetBackbufferExtent().Height);
        device.ReadBuffer(pixels, frame.Bytes.data(), frame.Bytes.size(), 0u);
        return frame;
    };
    const auto shownAndPaused = [&](const std::uint64_t run) { return t.ShownAndPaused(run); };

    ASSERT_TRUE(gpu.Run([&]
    {
        ++frames;
        switch (step)
        {
        case 0: // the CPU positions show; start a run and step it
        {
            if (frames < 12) return false;
            if (!pixels.IsValid())
            {
                auto& device = h.Engine.GetDevice();
                const auto extent = device.GetBackbufferExtent();
                pixels = device.CreateBuffer({.SizeBytes = std::uint64_t(Extrinsic::RHI::BytesPerBlock(device.GetBackbufferFormat())) *
                                                           extent.Width * extent.Height,
                                              .Usage = Extrinsic::RHI::BufferUsage::TransferDst, .HostVisible = true,
                                              .DebugName = "JobFormSpike.Pixels"});
                h.Engine.GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer(pixels);
                return false;
            }
            if (++settle < 8) return false;
            before = capture();
            EXPECT_EQ(before.Right(), 0u) << "the source starts on the left";
            EXPECT_FALSE(extraction->ShowsUncommittedPositions(renderId));
            first = start();
            EXPECT_TRUE(control(first, "step", 40));
            settle = 0;
            ++step;
            return false;
        }
        case 1: // paused: the preview shows, nothing is written, an editor GPU position run is refused
        {
            if (!shownAndPaused(first) || ++settle < 8) return false;
            preview = capture();
            EXPECT_TRUE(extraction->ShowsUncommittedPositions(renderId));
            EXPECT_GT(preview.Changed(before), 0u) << "the drawn pixels move";
            EXPECT_GT(preview.Right(), 0u) << "towards the target on the right";
            EXPECT_EQ(canonical(), original) << "the canonical positions are untouched";
            EXPECT_FALSE(h.History().CanUndo());
            std::string refusal;
            EXPECT_FALSE(R::BeginEditorGpuPositionRun(R::PrepareEditorProcessingCommands(h.Shell),
                                                      R::SelectionController::ToStableEntityId(source), positions,
                                                      *spatial->PropertyResidency(), refusal));
            EXPECT_NE(refusal.find("awaits Accept or Discard"), std::string::npos) << refusal;
            EXPECT_TRUE(control(first, "discard"));
            settle = 0;
            ++step;
            return false;
        }
        case 2: // discarded: the ring is gone and the CPU positions show again; a successor starts
        {
            if (observe(first)["phase"] != "discarded" || ++settle < 8) return false;
            EXPECT_FALSE(spatial->PropertyResidency()->HasRing(key));
            EXPECT_FALSE(extraction->ShowsUncommittedPositions(renderId));
            restored = capture();
            EXPECT_EQ(restored.Changed(before), 0u) << "the CPU frame is back";
            second = start();
            EXPECT_NE(second, 0u) << "the output is free";
            EXPECT_TRUE(control(second, "step", 40));
            settle = 0;
            ++step;
            return false;
        }
        case 3: // the successor previews in its own ring; Accept
        {
            if (!shownAndPaused(second) || ++settle < 8) return false;
            EXPECT_TRUE(spatial->PropertyResidency()->HasRing(key));
            EXPECT_TRUE(extraction->ShowsUncommittedPositions(renderId));
            EXPECT_EQ(capture().Changed(preview), 0u) << "the same computation shows the same frame";
            EXPECT_TRUE(control(second, "accept"));
            settle = 0;
            ++step;
            return false;
        }
        case 4: // committed: written once, the ring released, the frame keeps the moved cloud
        {
            if (observe(second)["phase"] != "committed" || ++settle < 8) return false;
            EXPECT_FALSE(spatial->PropertyResidency()->HasRing(key));
            EXPECT_FALSE(extraction->ShowsUncommittedPositions(renderId));
            EXPECT_NE(canonical(), original);
            EXPECT_TRUE(h.History().CanUndo());
            accepted = capture();
            EXPECT_EQ(accepted.Changed(preview), 0u) << "the accepted frame is the previewed one";
            h.Engine.GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer({});
            h.Engine.GetDevice().DestroyBuffer(pixels);
            return true;
        }
        default: return true;
        }
    })) << "stopped at step " << step;
}

// Revision 3's ordering: A is cancelled while its step is held inside the plugin's code; its ring goes at once, B is
// admitted and previews in its own ring while A's step still runs, and A's late completion leaves B's ring.
TEST(JobFormSpikePluginGpuSmoke, ALateCompletionLeavesItsSuccessorsRing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Scenario t;
    std::uint64_t a = 0, b = 0, ring = 0;
    int step = 0;
    ASSERT_TRUE(t.Engine.Run([&]
    {
        switch (step)
        {
        case 0: // A computes, each step held for 1.5 s
            a = t.Start({{"hold_step_ms", 1500}});
            EXPECT_NE(a, 0u);
            EXPECT_TRUE(t.Control(a, "run"));
            ++step;
            return false;
        case 1: // A shows step 1 and holds step 2: discard it now
        {
            const auto o = t.Observe(a);
            if (o["shown_step"] != 1 || !o["computing"].get<bool>()) return false;
            EXPECT_TRUE(t.Residency().HasRing(t.Key));
            EXPECT_TRUE(t.Control(a, "discard"));
            ++step;
            return false;
        }
        case 2: // A is terminal and its ring gone at once, while its step still runs; B starts
        {
            if (t.Observe(a)["phase"] != "discarded" || t.Residency().HasRing(t.Key)) return false;
            b = t.Start(); // refused while A's holder job still finishes: again next frame
            if (b == 0u) return false;
            EXPECT_TRUE(t.Control(b, "step", 20));
            ++step;
            return false;
        }
        case 3: // B previews in its own ring while A's held step has not returned
            if (!t.ShownAndPaused(b)) return false;
            EXPECT_TRUE(t.Observe(a)["computing"].get<bool>()) << "A's step is still in flight (the ordering under test)";
            ring = t.Residency().RingGeneration(t.Key);
            EXPECT_NE(ring, 0u);
            EXPECT_TRUE(t.Extraction->ShowsUncommittedPositions(t.RenderId));
            ++step;
            return false;
        case 4: // A's step returns late: B's ring and preview stay
            if (t.Observe(a)["computing"].get<bool>()) return false;
            EXPECT_EQ(t.Observe(a)["phase"], "discarded");
            EXPECT_EQ(t.Residency().RingGeneration(t.Key), ring) << "A's late completion left B's ring";
            EXPECT_TRUE(t.Extraction->ShowsUncommittedPositions(t.RenderId));
            EXPECT_EQ(t.Canonical(), t.Original);
            EXPECT_TRUE(t.Control(b, "discard"));
            ++step;
            return false;
        default:
            return t.Observe(b)["phase"] == "discarded" && !t.Residency().HasRing(t.Key);
        }
    })) << "stopped at step " << step;
}
