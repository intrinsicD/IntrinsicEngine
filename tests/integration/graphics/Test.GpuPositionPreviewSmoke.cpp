// GRAPHICS-156 (ADR 0030 decision 5, positions) on a real device: a position ring front is
// copied into the point cloud's GpuWorld block at the culling head, so the observed pixels
// move before Accept and return on Discard (the block is restored from the CPU positions);
// a preview moved outside the CPU culling bounds is not culled. There is no GPU method that
// writes positions yet (RUNTIME-294), so the test drives the ring itself: AcquireBack, a
// transfer upload into the back slot, Publish.
#include "RuntimeTestModule.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.Graphics.SceneHandles;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace Graphics = Extrinsic::Graphics;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace ECSC = Extrinsic::ECS::Components;
    namespace GC = Extrinsic::Graphics::Components;
    using Domain = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    class PreviewApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        // Move scenario: the cloud sits at the origin and the preview shifts it right.
        // Cull scenario: the cloud sits far outside the frustum (culled by its CPU bounds) and
        // the preview moves it into view.
        // Mesh scenario: a seam-split quad surface (corner texcoords differ across the shared
        // edge, so the block holds six GPU vertices over four rows) is shifted right through
        // the gather kernel.
        bool CullScenario{};
        bool MeshScenario{};
        static constexpr float kCulledOffset = -50.0f;

        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Scene = Kernel().Worlds().Get(Kernel().ActiveWorld());
            World = Kernel().ActiveWorld();
            SpatialIndices = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            Extraction = Kernel().Services().Find<Runtime::RenderExtractionCache>();
            auto& raw = Scene->Raw();
            Entity = Scene->Create();
            raw.emplace<ECSC::MetaData>(Entity, std::string{"PreviewCloud"});
            const glm::vec3 translation{CullScenario ? kCulledOffset : 0.0f, 0.0f, 0.0f};
            auto& transform = raw.emplace<ECSC::Transform::Component>(Entity);
            transform.Position = translation;
            transform.Scale = glm::vec3{1.0f};
            raw.emplace<ECSC::Transform::WorldMatrix>(Entity).Matrix = glm::translate(glm::mat4{1.f}, translation);
            raw.emplace<ECSC::Selection::SelectableTag>(Entity);
            raw.emplace<ECSC::StableId>(Entity, ECSC::StableId{156u, 1u});
            if (MeshScenario)
                raw.emplace<GC::RenderSurface>(Entity);
            else
                raw.emplace<GC::RenderPoints>(Entity).SizeSource = 6.0f;
            // No authored culling bounds: extraction uses the default unit sphere at the
            // entity's translation, which is inside the frustum at the origin and far outside
            // it at kCulledOffset (the reference camera at (0, 0, 3) looks at the origin).
            if (auto* cameras = Kernel().Services().Find<Runtime::CameraControllerRegistry>())
            {
                Graphics::CameraViewInput seed{};
                seed.Position = {0.f, 0.f, 3.f};
                seed.Forward = {0.f, 0.f, -1.f};
                seed.Up = {0.f, 1.f, 0.f};
                seed.NearPlane = 0.1f;
                seed.FarPlane = 100.f;
                seed.Valid = true;
                (void)cameras->SetWorldSeed(World, seed);
            }
            if (MeshScenario)
            {
                Geometry::HalfedgeMesh::Mesh quad;
                const auto a = quad.AddVertex({-0.6f, -0.6f, 0.f}), b = quad.AddVertex({0.6f, -0.6f, 0.f});
                const auto c = quad.AddVertex({0.6f, 0.6f, 0.f}), d = quad.AddVertex({-0.6f, 0.6f, 0.f});
                (void)quad.AddTriangle(a, b, c);
                (void)quad.AddTriangle(a, c, d);
                GS::PopulateFromMesh(raw, Entity, quad);
                // Corner texcoords that differ per face split the shared vertices a and c.
                auto& halfedges = raw.get<GS::Halfedges>(Entity).Properties;
                const auto face = std::as_const(halfedges).Get<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeFace});
                auto uv = halfedges.GetOrAdd<glm::vec2>("h:texcoord", glm::vec2{0.f});
                for (std::size_t h = 0; h < halfedges.Size(); ++h)
                    uv[h] = face[h] == 0u ? glm::vec2{0.f, 0.f} : glm::vec2{1.f, 1.f};
                Original = std::as_const(raw.get<GS::Vertices>(Entity).Properties).Get<glm::vec3>("v:position").Vector();
                PositionRef = {Domain::MeshVertex, "v:position", K::Vec3};
            }
            else
            {
                auto& vertices = raw.emplace<GS::Vertices>(Entity).Properties;
                constexpr std::size_t kSide = 10;
                vertices.Resize(kSide * kSide);
                auto positions = vertices.GetOrAdd<glm::vec3>("v:position", glm::vec3{0.f});
                for (std::size_t y = 0; y < kSide; ++y)
                    for (std::size_t x = 0; x < kSide; ++x)
                        positions[y * kSide + x] = {float(x) / float(kSide - 1) * 1.2f - 0.6f,
                                                    float(y) / float(kSide - 1) * 1.2f - 0.6f, 0.f};
                Original = positions.Vector();
                PositionRef = {Domain::PointCloudPoint, "v:position", K::Vec3};
            }
            RenderId = Runtime::StableEntityLookup::ToRenderId(Entity);
        }

        Graphics::GpuPropertyResidency* Residency() { return SpatialIndices->PropertyResidency(); }
        Graphics::GpuPropertyKey Key() { return Runtime::MakeGpuPropertyKey(World, Entity, PositionRef); }

        bool EnsurePixelReadback()
        {
            if (Pixels.IsValid()) return true;
            auto& device = Kernel().GetDevice();
            const auto extent = device.GetBackbufferExtent();
            PixelBytes = Extrinsic::RHI::BytesPerBlock(device.GetBackbufferFormat());
            if (PixelBytes < 4u || extent.Width == 0u || extent.Height == 0u) return false;
            Width = extent.Width;
            Height = extent.Height;
            Pixels = device.CreateBuffer({.SizeBytes = std::uint64_t(PixelBytes) * Width * Height,
                                          .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
                                          .HostVisible = true, .DebugName = "GRAPHICS156.Pixels"});
            if (!Pixels.IsValid()) return false;
            Kernel().GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer(Pixels);
            return true;
        }
        std::vector<std::uint8_t> CapturePixels()
        {
            std::vector<std::uint8_t> bytes(std::size_t(PixelBytes) * Width * Height);
            Kernel().GetDevice().ReadBuffer(Pixels, bytes.data(), bytes.size(), 0u);
            return bytes;
        }
        [[nodiscard]] bool IsBackground(const std::vector<std::uint8_t>& a, const std::size_t i) const
        {
            return std::abs(int(a[i]) - int(a[0])) <= 8 && std::abs(int(a[i + 1]) - int(a[1])) <= 8 &&
                   std::abs(int(a[i + 2]) - int(a[2])) <= 8;
        }
        // Non-background pixels in the left / right half of the frame (the clear colour is
        // the frame's first pixel).
        struct Halves { std::size_t Left{}, Right{}; };
        [[nodiscard]] Halves NonBackgroundHalves(const std::vector<std::uint8_t>& a) const
        {
            Halves halves{};
            for (std::uint32_t y = 0; y < Height; ++y)
                for (std::uint32_t x = 0; x < Width; ++x)
                {
                    const std::size_t i = (std::size_t(y) * Width + x) * PixelBytes;
                    if (i == 0u || i + 3 >= a.size() || IsBackground(a, i)) continue;
                    (x < Width / 2u ? halves.Left : halves.Right)++;
                }
            return halves;
        }
        [[nodiscard]] std::size_t ChangedPixels(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) const
        {
            std::size_t changed = 0;
            for (std::size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += PixelBytes)
                if (std::abs(int(a[i]) - int(b[i])) > 8 || std::abs(int(a[i + 1]) - int(b[i + 1])) > 8 ||
                    std::abs(int(a[i + 2]) - int(b[i + 2])) > 8)
                    ++changed;
            return changed;
        }
        std::string Describe(const std::vector<std::uint8_t>& a) const
        {
            const auto halves = NonBackgroundHalves(a);
            return "left " + std::to_string(halves.Left) + " / right " + std::to_string(halves.Right) + " non-background pixels";
        }
        [[nodiscard]] std::optional<Graphics::GpuGeometryResidencyView> BlockView()
        {
            const auto sidecar = Extraction->FindRenderableSidecarForTest(RenderId);
            if (!sidecar) return std::nullopt;
            const auto geometry = MeshScenario ? sidecar->MeshGeometry : sidecar->PointCloudGeometry;
            if (!geometry.IsValid()) return std::nullopt;
            Graphics::GpuGeometryResidencyView view{};
            if (!Kernel().GetRenderer().GetGpuWorld().TryGetGeometryResidencyView(geometry, view))
                return std::nullopt;
            return view;
        }

        // Publishes a front whose positions are the CPU positions shifted by `shift`.
        bool PublishShifted(const glm::vec3 shift)
        {
            auto* residency = Residency();
            if (!residency) return Fail("no property residency"), false;
            const Graphics::GpuPropertyLayout layout{.Scalar = Graphics::GpuScalarType::Float32, .Channels = 3u,
                                                     .Stride = 0u, .Count = std::uint32_t(Original.size())};
            const auto back = residency->AcquireBack(Key(), layout, 2u);
            if (!back) return Fail("the position ring refused a back slot"), false;
            std::vector<glm::vec3> shifted = Original;
            for (auto& p : shifted) p += shift;
            const auto upload = Graphics::SubmitBufferUpload(Kernel().GetDevice(), back->Buffer, shifted.data(),
                                                             shifted.size() * sizeof(glm::vec3));
            if (!upload.Accepted()) return Fail("the transfer refused the front upload"), false;
            if (upload.IsAsynchronous()) residency->AddCompletion(back->Buffer, upload.Token);
            if (!residency->Publish(Key())) return Fail("Publish failed"), false;
            return true;
        }

        bool Fail(const std::string& message)
        {
            ADD_FAILURE() << message;
            Kernel().RequestExit();
            return false;
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(120))
            {
                TimedOut = true;
                (void)Fail("GRAPHICS-156 smoke timeout at step " + std::to_string(Step));
                return;
            }
            if (!Kernel().GetDevice().IsOperational() || !SpatialIndices || !Extraction) return;
            ++OperationalFrames;
            switch (Step)
            {
            case 0: // The CPU positions show (or are culled); publish the shifted front.
            {
                if (OperationalFrames < 12) return;
                if (!EnsurePixelReadback()) { (void)Fail("no backbuffer readback"); return; }
                if (++SettleFrames < 8) return;
                CpuPixels = CapturePixels();
                const auto halves = NonBackgroundHalves(CpuPixels);
                if (CullScenario)
                {
                    EXPECT_EQ(halves.Left + halves.Right, 0u) << "the cloud outside the frustum is drawn: " << Describe(CpuPixels);
                }
                else
                {
                    EXPECT_GT(halves.Left, 0u) << "the cloud is not drawn on the left: " << Describe(CpuPixels);
                    EXPECT_GT(halves.Right, 0u) << "the cloud is not drawn on the right: " << Describe(CpuPixels);
                }
                EXPECT_FALSE(Extraction->ShowsUncommittedPositions(RenderId));
                if (!PublishShifted(CullScenario ? glm::vec3{-kCulledOffset, 0.f, 0.f} : glm::vec3{0.9f, 0.f, 0.f})) return;
                SettleFrames = 0;
                ++Step;
                return;
            }
            case 1: // Before Accept the front shows: the pixels moved, the block's shadow is stale.
            {
                if (++SettleFrames < 8) return;
                const auto stats = Extraction->GetLastStats();
                EXPECT_EQ(stats.PositionPreviewsObserved, 1u);
                EXPECT_EQ(stats.PositionPreviewBlocksRejected, 0u);
                EXPECT_TRUE(Extraction->ShowsUncommittedPositions(RenderId));
                const auto block = BlockView();
                ASSERT_TRUE(block.has_value());
                EXPECT_TRUE(block->PositionShadowStale) << "the block shows the front, not its CPU shadow";
                if (MeshScenario)
                {
                    // Six split GPU vertices over four property rows: only the gather kernel
                    // can have filled this block from the front.
                    EXPECT_EQ(block->VertexCount, 6u);
                    EXPECT_EQ(Original.size(), 4u);
                }
                PreviewPixels = CapturePixels();
                const auto halves = NonBackgroundHalves(PreviewPixels);
                if (CullScenario)
                {
                    EXPECT_GT(halves.Left + halves.Right, 0u)
                        << "the preview moved into view is culled by the CPU bounds: " << Describe(PreviewPixels);
                }
                else
                {
                    EXPECT_EQ(halves.Left, 0u) << "the preview still draws on the left: " << Describe(PreviewPixels);
                    EXPECT_GT(halves.Right, 0u) << "the preview does not draw on the right: " << Describe(PreviewPixels);
                }
                EXPECT_GT(ChangedPixels(CpuPixels, PreviewPixels), 0u);
                EXPECT_EQ(std::as_const(*Scene).Raw().get<GS::Vertices>(Entity).Properties.Get<glm::vec3>("v:position").Vector(), Original)
                    << "the CPU positions are untouched before Accept";
                Residency()->Discard(Key());
                EXPECT_FALSE(Residency()->HasRing(Key()));
                SettleFrames = 0;
                ++Step;
                return;
            }
            case 2: // Discard: the block is restored from the CPU positions; the pixels return.
            {
                if (Extraction->GetLastStats().PositionPreviewRestores > 0u) SawRestore = true;
                if (++SettleFrames < 8) return;
                EXPECT_TRUE(SawRestore) << "the preview's end forced a position upload";
                EXPECT_FALSE(Extraction->ShowsUncommittedPositions(RenderId));
                const auto block = BlockView();
                ASSERT_TRUE(block.has_value());
                EXPECT_FALSE(block->PositionShadowStale);
                const auto restored = CapturePixels();
                EXPECT_EQ(ChangedPixels(CpuPixels, restored), 0u)
                    << "after Discard the frame differs from the CPU frame: " << Describe(restored) << " vs " << Describe(CpuPixels);
                EXPECT_GT(ChangedPixels(PreviewPixels, restored), 0u) << "control: the preview was visible";
                Done = true;
                Kernel().RequestExit();
                return;
            }
            default: return;
            }
        }

        void Shutdown() override
        {
            if (SpatialIndices && Residency()) Residency()->Discard(Key());
            if (Pixels.IsValid())
            {
                Kernel().GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer({});
                Kernel().GetDevice().DestroyBuffer(Pixels);
                Pixels = {};
            }
        }

        Extrinsic::ECS::Scene::Registry* Scene{};
        Runtime::WorldHandle World{};
        Runtime::SpatialIndexCache* SpatialIndices{};
        Runtime::RenderExtractionCache* Extraction{};
        entt::entity Entity{};
        std::uint32_t RenderId{};
        Runtime::GeometryPropertyRef PositionRef{};
        std::vector<glm::vec3> Original{};
        std::vector<std::uint8_t> CpuPixels{}, PreviewPixels{};
        Extrinsic::RHI::BufferHandle Pixels{};
        std::uint32_t PixelBytes{}, Width{}, Height{};
        std::chrono::steady_clock::time_point Started{};
        std::size_t OperationalFrames{}, SettleFrames{};
        int Step{};
        bool Done{}, TimedOut{}, SawRestore{};
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

    void RunScenario(const bool cullScenario, const bool meshScenario = false)
    {
        Extrinsic::Core::Config::EngineConfig config;
        if (!Prepare(config)) GTEST_SKIP() << "GLFW unavailable";
        auto app = std::make_unique<PreviewApp>();
        auto* run = app.get();
        run->CullScenario = cullScenario;
        run->MeshScenario = meshScenario;
        Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
        engine.EmplaceModule<Runtime::SpatialIndexCache>();
        engine.EmplaceModule<Runtime::CameraModule>();
        engine.Initialize();
        Shutdown shutdown{engine};
        engine.Run();
        ASSERT_TRUE(engine.GetDevice().IsOperational());
        ASSERT_FALSE(run->TimedOut);
        ASSERT_TRUE(run->Done) << "the scenario stopped at step " << run->Step;
    }
}

TEST(GRAPHICS156PositionPreview, ObservedPixelsMoveBeforeAcceptAndReturnOnDiscard) { RunScenario(false); }

TEST(GRAPHICS156PositionPreview, AMovedPreviewIsNotCulledByTheCpuBounds) { RunScenario(true); }

TEST(GRAPHICS156PositionPreview, ASeamSplitSurfaceGathersTheFrontThroughItsRemap) { RunScenario(false, true); }
