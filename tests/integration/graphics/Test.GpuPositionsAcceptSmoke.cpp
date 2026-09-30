// RUNTIME-293 (ADR 0030 decision 6, positions) on a real device: a position ring front shown
// by the point cloud is accepted through the undoable publication; extraction acknowledges
// the new revision without a position upload (the block keeps the copied front), the render
// positions equal the CPU positions (the accepted frame is pixel-identical to the preview
// frame and to a frame uploaded from the CPU), undo and redo upload once each, and the
// next residency acquire of `v:position` uploads zero bytes. There is no GPU method writing
// positions yet (RUNTIME-294), so the test drives the ring itself.
#include "GeometryResidencyFingerprint.hpp"
#include "RuntimeTestModule.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.Graphics.SceneHandles;
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

    [[nodiscard]] std::uint64_t Fingerprint(const std::vector<glm::vec3>& points)
    {
        std::uint64_t fingerprint = Extrinsic::Tests::kGeometryFingerprintOffset;
        for (const auto& p : points)
            for (const float value : {p.x, p.y, p.z})
            {
                std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
                if (bits == 0x8000'0000u) bits = 0u;
                Extrinsic::Tests::AppendGeometryFingerprintWord(fingerprint, bits);
            }
        return fingerprint == 0u ? 1u : fingerprint;
    }

    class AcceptApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        // Bounds scenario (review P2): the cloud sits far outside the frustum with authored
        // culling bounds around it (culled), and the accepted positions move it into view.
        // Without bounds that follow the publication the accepted cloud vanishes as soon as
        // the preview's conservative culling ends.
        bool BoundsScenario{};
        static constexpr float kCulledOffset = -50.0f;
        glm::vec3 Shift() const { return BoundsScenario ? glm::vec3{-kCulledOffset, 0.f, 0.f} : glm::vec3{0.9f, 0.f, 0.f}; }

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
            auto& raw = Context.Scene->Raw();
            Entity = Context.Scene->Create();
            raw.emplace<ECSC::MetaData>(Entity, std::string{"AcceptCloud"});
            const glm::vec3 translation{BoundsScenario ? kCulledOffset : 0.0f, 0.0f, 0.0f};
            auto& transform = raw.emplace<ECSC::Transform::Component>(Entity);
            transform.Position = translation;
            transform.Scale = glm::vec3{1.0f};
            raw.emplace<ECSC::Transform::WorldMatrix>(Entity).Matrix = glm::translate(glm::mat4{1.f}, translation);
            raw.emplace<ECSC::Selection::SelectableTag>(Entity);
            raw.emplace<ECSC::StableId>(Entity, ECSC::StableId{293u, 1u});
            raw.emplace<GC::RenderPoints>(Entity).SizeSource = 6.0f;
            if (auto* cameras = Kernel().Services().Find<Runtime::CameraControllerRegistry>())
            {
                Graphics::CameraViewInput seed{};
                seed.Position = {0.f, 0.f, 3.f};
                seed.Forward = {0.f, 0.f, -1.f};
                seed.Up = {0.f, 1.f, 0.f};
                seed.NearPlane = 0.1f;
                seed.FarPlane = 100.f;
                seed.Valid = true;
                (void)cameras->SetWorldSeed(Context.World, seed);
            }
            auto& vertices = raw.emplace<GS::Vertices>(Entity).Properties;
            constexpr std::size_t kSide = 10;
            vertices.Resize(kSide * kSide);
            auto positions = vertices.GetOrAdd<glm::vec3>("v:position", glm::vec3{0.f});
            for (std::size_t y = 0; y < kSide; ++y)
                for (std::size_t x = 0; x < kSide; ++x)
                    positions[y * kSide + x] = {float(x) / float(kSide - 1) * 1.2f - 0.6f,
                                                float(y) / float(kSide - 1) * 1.2f - 0.6f, 0.f};
            Original = positions.Vector();
            Shifted = Original;
            for (auto& p : Shifted) p += Shift();
            if (BoundsScenario)
            {
                // Authored bounds around the local cloud (as an import authors them).
                ECSC::Culling::Local::Bounds local{};
                local.LocalBoundingAABB = {.Min = {-0.6f, -0.6f, 0.f}, .Max = {0.6f, 0.6f, 0.f}};
                local.LocalBoundingSphere = {.Center = {0.f, 0.f, 0.f}, .Radius = 0.85f};
                ECSC::Culling::World::Bounds world{};
                world.WorldBoundingSphere = {.Center = translation, .Radius = 0.85f};
                world.WorldBoundingOBB.Center = translation;
                world.WorldBoundingOBB.Extents = {0.6f, 0.6f, 0.f};
                raw.emplace<ECSC::Culling::Local::Bounds>(Entity, local);
                raw.emplace<ECSC::Culling::World::Bounds>(Entity, world);
            }
            PositionRef = {Domain::PointCloudPoint, "v:position", K::Vec3};
            RenderId = Runtime::StableEntityLookup::ToRenderId(Entity);
            Id = Runtime::SelectionController::ToStableEntityId(Entity);
        }

        auto Commands() { return Runtime::BindEditorProcessingCommands(Context); }
        Graphics::GpuPropertyResidency* Residency() { return Context.SpatialIndices->PropertyResidency(); }
        Graphics::GpuPropertyKey Key() { return Runtime::MakeGpuPropertyKey(Context.World, Entity, PositionRef); }
        std::vector<glm::vec3> Rows()
        {
            return std::as_const(Context.Scene->Raw().get<GS::Vertices>(Entity).Properties).Get<glm::vec3>("v:position").Vector();
        }

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
                                          .HostVisible = true, .DebugName = "RUNTIME293.Pixels"});
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
        [[nodiscard]] std::size_t NonBackground(const std::vector<std::uint8_t>& a) const
        {
            std::size_t count = 0;
            for (std::size_t i = PixelBytes; i + 3 < a.size(); i += PixelBytes)
                if (!IsBackground(a, i)) ++count;
            return count;
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
        [[nodiscard]] std::optional<Graphics::GpuGeometryResidencyView> BlockView()
        {
            const auto sidecar = Extraction->FindRenderableSidecarForTest(RenderId);
            if (!sidecar || !sidecar->PointCloudGeometry.IsValid()) return std::nullopt;
            Graphics::GpuGeometryResidencyView view{};
            if (!Kernel().GetRenderer().GetGpuWorld().TryGetGeometryResidencyView(sidecar->PointCloudGeometry, view))
                return std::nullopt;
            return view;
        }

        // Publishes the shifted positions as the run's ring front.
        bool PublishShifted()
        {
            auto* residency = Residency();
            if (!residency) return Fail("no property residency"), false;
            // The run owns the ring: its first write slot came with Begin.
            const auto back = Runtime::EditorGpuPositionRunFirstBack(Run);
            if (!back) return Fail("the run has no first write slot"), false;
            const auto upload = Graphics::SubmitBufferUpload(Kernel().GetDevice(), back->Buffer, Shifted.data(),
                                                             Shifted.size() * sizeof(glm::vec3));
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

        // Position uploads and block rewrites seen since the last reset (one extraction per
        // frame; `Reuploads` counts partial and full reuploads alike, `PartialUploads` is its
        // subset; a block's content revision changes once per upload or commit).
        void TrackUploads()
        {
            const auto& stats = Extraction->GetLastStats();
            Uploads += stats.PointCloudGeometryReuploads;
            Restores += stats.PositionPreviewRestores;
            AcknowledgedEver |= stats.PositionCommitsAcknowledged > 0u;
            if (const auto block = BlockView(); block && block->ContentRevision != LastContentRevision)
            {
                if (LastContentRevision != 0u) ++BlockRewrites;
                LastContentRevision = block->ContentRevision;
            }
        }
        void ResetTracking()
        {
            Uploads = Restores = BlockRewrites = 0u;
            if (const auto block = BlockView()) LastContentRevision = block->ContentRevision;
            SettleFrames = 0;
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(120))
            {
                TimedOut = true;
                (void)Fail("RUNTIME-293 smoke timeout at step " + std::to_string(Step));
                return;
            }
            if (!Kernel().GetDevice().IsOperational() || !Context.SpatialIndices || !Extraction) return;
            ++OperationalFrames;
            if (Step >= 2) TrackUploads();
            switch (Step)
            {
            case 0: // The CPU positions show; begin the run and publish the shifted front.
            {
                if (OperationalFrames < 12) return;
                if (!EnsurePixelReadback()) { (void)Fail("no backbuffer readback"); return; }
                if (++SettleFrames < 8) return;
                CpuPixels = CapturePixels();
                if (BoundsScenario) EXPECT_EQ(NonBackground(CpuPixels), 0u) << "the cloud outside the frustum is drawn";
                else EXPECT_GT(NonBackground(CpuPixels), 0u) << "the cloud is not drawn";
                std::string why;
                Run = Runtime::BeginEditorGpuPositionRun(Commands(), Id, PositionRef, *Residency(), why);
                if (!Run) { (void)Fail("BeginEditorGpuPositionRun: " + why); return; }
                EXPECT_EQ(Runtime::EditorGpuPositionRunRowCount(Run), Original.size());
                if (!PublishShifted()) return;
                SettleFrames = 0;
                ++Step;
                return;
            }
            case 1: // The front shows (pixels moved, shadow stale); Accept it.
            {
                if (++SettleFrames < 8) return;
                EXPECT_TRUE(Extraction->ShowsUncommittedPositions(RenderId));
                const auto block = BlockView();
                ASSERT_TRUE(block.has_value());
                EXPECT_TRUE(block->PositionShadowStale);
                PreviewPixels = CapturePixels();
                EXPECT_GT(NonBackground(PreviewPixels), 0u) << "the preview is not drawn";
                EXPECT_GT(ChangedPixels(CpuPixels, PreviewPixels), 0u) << "control: the preview is visible";
                EXPECT_EQ(Rows(), Original) << "the CPU positions are untouched before Accept";
                StatsBefore = Residency()->Stats();
                const auto accepted = Runtime::AcceptEditorGpuPositionRun(Commands(), Run, *Residency(), "Move points (GPU)",
                    [this](Runtime::EditorGpuPositionAcceptResult result) { Accepted = std::move(result); });
                if (accepted.Status != Runtime::EditorCommandStatus::Pending) { (void)Fail("Accept refused: " + accepted.Message); return; }
                ResetTracking();
                ++Step;
                return;
            }
            case 2: // The publication landed: CPU == front, the render side acknowledged it.
            {
                if (!Accepted) { EXPECT_EQ(Rows(), Original) << "nothing is published before the readback landed"; return; }
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                EXPECT_TRUE(Accepted->RenderAcknowledged) << Accepted->Message;
                EXPECT_EQ(Rows(), Shifted) << "the CPU rows are the accepted front";
                EXPECT_FALSE(Residency()->HasRing(Key()));
                ResetTracking();
                ++Step;
                return;
            }
            case 3: // Steady frames after Accept: no position upload, no restore; render == CPU.
            {
                if (++SettleFrames < 8) return;
                EXPECT_EQ(Uploads, 0u) << "extraction uploaded positions after Accept";
                EXPECT_EQ(Restores, 0u) << "the preview's end forced a restore upload";
                EXPECT_EQ(BlockRewrites, 0u) << "the block was rewritten after Accept";
                EXPECT_FALSE(Extraction->ShowsUncommittedPositions(RenderId));
                const auto block = BlockView();
                ASSERT_TRUE(block.has_value());
                EXPECT_FALSE(block->PositionShadowStale);
                EXPECT_EQ(block->PositionFingerprint, Fingerprint(Shifted)) << "the shadow holds the accepted rows";
                AcceptedPixels = CapturePixels();
                EXPECT_GT(NonBackground(AcceptedPixels), 0u) << "the accepted cloud vanished (culled by stale bounds)";
                if (BoundsScenario)
                {
                    const auto& bounds = Context.Scene->Raw().get<ECSC::Culling::World::Bounds>(Entity);
                    EXPECT_LT(glm::length(bounds.WorldBoundingSphere.Center), 0.1f) << "the authored bounds follow the accepted rows";
                }
                EXPECT_EQ(ChangedPixels(PreviewPixels, AcceptedPixels), 0u) << "the accepted frame differs from the preview frame";
                EXPECT_GT(ChangedPixels(CpuPixels, AcceptedPixels), 0u);
                // The front is the canonical slot of the new revision: acquiring `v:position`
                // for a GPU method uploads nothing.
                const auto revision = Context.Scene->Raw().get<GS::Vertices>(Entity).Properties.FindPropertyRevision("v:position");
                ASSERT_TRUE(revision.has_value());
                const auto canonical = Residency()->Front(Key());
                ASSERT_TRUE(canonical.has_value());
                EXPECT_EQ(canonical->Revision, *revision);
                const auto input = Runtime::ResolveGpuPropertyInput(*Residency(), *Context.Scene, Context.World, Entity, PositionRef);
                ASSERT_TRUE(input.has_value());
                EXPECT_EQ(input->Buffer, canonical->Buffer);
                EXPECT_EQ(Residency()->Stats().UploadBytes, StatsBefore.UploadBytes) << "the next acquire uploads zero bytes";
                EXPECT_EQ(Residency()->Stats().Uploads, StatsBefore.Uploads);
                ASSERT_TRUE(History.Undo().Succeeded());
                EXPECT_EQ(Rows(), Original);
                ResetTracking();
                ++Step;
                return;
            }
            case 4: // Undo uploaded the CPU rows once; the CPU frame is back.
            {
                if (++SettleFrames < 8) return;
                EXPECT_EQ(Uploads, 1u) << "undo uploads the positions once";
                EXPECT_EQ(BlockRewrites, 1u);
                const auto undone = CapturePixels();
                if (BoundsScenario)
                {
                    EXPECT_EQ(NonBackground(undone), 0u) << "undo restored the rows but not their bounds";
                    EXPECT_LT(glm::distance(Context.Scene->Raw().get<ECSC::Culling::World::Bounds>(Entity).WorldBoundingSphere.Center,
                                            glm::vec3{kCulledOffset, 0.f, 0.f}), 0.1f);
                }
                EXPECT_EQ(ChangedPixels(CpuPixels, undone), 0u) << "after undo the frame differs from the CPU frame";
                EXPECT_EQ(BlockView()->PositionFingerprint, Fingerprint(Original));
                ASSERT_TRUE(History.Redo().Succeeded());
                EXPECT_EQ(Rows(), Shifted);
                ResetTracking();
                ++Step;
                return;
            }
            case 5: // Redo uploaded once; the accepted frame is back, uploaded from the CPU this time.
            {
                if (++SettleFrames < 8) return;
                EXPECT_EQ(Uploads, 1u) << "redo uploads the positions once";
                EXPECT_EQ(BlockRewrites, 1u);
                const auto redone = CapturePixels();
                EXPECT_EQ(ChangedPixels(AcceptedPixels, redone), 0u)
                    << "the CPU upload of the accepted rows renders differently from the accepted front";
                EXPECT_EQ(BlockView()->PositionFingerprint, Fingerprint(Shifted));
                Done = true;
                Kernel().RequestExit();
                return;
            }
            default: return;
            }
        }

        void Shutdown() override
        {
            if (Context.SpatialIndices && Residency() && Run) Runtime::DiscardEditorGpuPositionRun(Commands(), Run, *Residency());
            if (Pixels.IsValid())
            {
                Kernel().GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer({});
                Kernel().GetDevice().DestroyBuffer(Pixels);
                Pixels = {};
            }
        }

        Runtime::EditorProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        Runtime::RenderExtractionCache* Extraction{};
        entt::entity Entity{};
        std::uint32_t RenderId{}, Id{};
        Runtime::GeometryPropertyRef PositionRef{};
        Runtime::EditorGpuPositionRunHandle Run{};
        std::optional<Runtime::EditorGpuPositionAcceptResult> Accepted{};
        Graphics::GpuPropertyResidencyStats StatsBefore{};
        std::vector<glm::vec3> Original{}, Shifted{};
        std::vector<std::uint8_t> CpuPixels{}, PreviewPixels{}, AcceptedPixels{};
        Extrinsic::RHI::BufferHandle Pixels{};
        std::uint32_t PixelBytes{}, Width{}, Height{};
        std::chrono::steady_clock::time_point Started{};
        std::size_t OperationalFrames{}, SettleFrames{};
        std::uint32_t Uploads{}, Restores{}, BlockRewrites{};
        bool AcknowledgedEver{};
        std::uint64_t LastContentRevision{};
        int Step{};
        bool Done{}, TimedOut{};
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
}

namespace
{
void RunScenario(const bool boundsScenario)
{
    Extrinsic::Core::Config::EngineConfig config;
    if (!Prepare(config)) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<AcceptApp>();
    auto* run = app.get();
    run->BoundsScenario = boundsScenario;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.EmplaceModule<Runtime::CameraModule>();
    engine.Initialize();
    Shutdown shutdown{engine};
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done) << "the scenario stopped at step " << run->Step;
    EXPECT_TRUE(run->AcknowledgedEver) << "extraction never reported the acknowledged commit";
}
}

TEST(RUNTIME293PositionsAccept, AcceptKeepsTheFrontInTheBlockAndUndoRedoUploadOnceEach) { RunScenario(false); }

TEST(RUNTIME293PositionsAccept, AcceptedPositionsOutsideTheAuthoredBoundsAreNotCulled) { RunScenario(true); }
