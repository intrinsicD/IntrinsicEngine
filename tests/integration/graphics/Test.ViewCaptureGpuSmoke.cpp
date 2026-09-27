// GRAPHICS-109 / RUNTIME-281: on a Vulkan-capable host the runtime capture queue reads the
// presented frame back through the renderer's one-shot backbuffer capture and writes a PNG
// whose size matches the framebuffer and whose pixels show the reference triangle over the
// clear color.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>
#include <stb_image.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.ViewCapture;

namespace R = Extrinsic::Runtime;

namespace
{
    // Requests one capture once the device is operational and exits when it finishes.
    class CaptureDriver final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        R::ViewCaptureModule* Capture{nullptr};
        R::ViewCaptureRequest Request{};
        std::uint64_t Ticket{0};
        R::ViewCaptureStatus Result{};
        int Frames{0};
        int OperationalFrames{0};

    protected:
        // The reference triangle and its camera, as the Sandbox session seeds them.
        void Resolve() override
        {
            auto& worlds = Kernel().Worlds();
            const auto world = worlds.ActiveWorld();
            auto population = R::BootstrapReferenceScene(Extrinsic::Core::Config::ReferenceSceneSelector::Triangle,
                                                         *worlds.Get(world));
            if (auto* cameras = Kernel().Services().Find<R::CameraControllerRegistry>())
                (void)cameras->SetWorldSeed(world, population.Camera);
        }

        void Frame(double, double) override
        {
            ++Frames;
            // Let the reference scene upload and present for a few frames first.
            if (Kernel().GetDevice().IsOperational()) ++OperationalFrames;
            if (Ticket == 0 && OperationalFrames > 30 && !Capture->UnavailableReason())
                Ticket = Capture->Request(Request);
            if (Ticket != 0)
            {
                Result = Capture->Status(Ticket);
                if (Result.State == R::ViewCaptureState::Completed || Result.State == R::ViewCaptureState::Failed)
                    Kernel().RequestExit();
            }
            if (Frames > 600) Kernel().RequestExit();
        }
    };
}

TEST(ViewCaptureGpuSmoke, SavesTheReferenceTriangleAsPng)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW could not initialize; gpu;vulkan capture smoke is opt-in.";

    const auto directory = std::filesystem::temp_directory_path() /
                           ("intrinsic-capture-smoke-" + std::to_string(::getpid()));
    std::filesystem::remove_all(directory);
    const auto path = directory / "frame.png";

    auto config = R::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic view capture gpu;vulkan smoke";
    config.Window.Width = 320;
    config.Window.Height = 240;
    config.Window.Resizable = false;
    config.Render.EnableVSync = false;
    R::Engine engine{config};
    engine.EmplaceModule<R::CameraModule>();
    auto captureModule = std::make_unique<R::ViewCaptureModule>(directory);
    R::ViewCaptureModule* capture = captureModule.get();
    engine.AddModule(std::move(captureModule));
    auto driverModule = std::make_unique<CaptureDriver>();
    CaptureDriver* driver = driverModule.get();
    driver->Capture = capture;
    driver->Request = {.Region = R::ViewCaptureRegion::Window, .OutputPath = path.string()};
    Intrinsic::Tests::AddRuntimeTestModule(engine, std::move(driverModule));
    engine.Initialize();
    engine.Run();
    const bool operational = engine.GetDevice().IsOperational();
    const auto extent = engine.GetDevice().GetBackbufferExtent();
    engine.Shutdown();

    if (!operational && driver->Ticket == 0)
        GTEST_SKIP() << "Promoted Vulkan did not become operational on this host.";
    ASSERT_NE(driver->Ticket, 0u) << "no capture was requested";
    ASSERT_EQ(driver->Result.State, R::ViewCaptureState::Completed) << driver->Result.Diagnostic;
    EXPECT_EQ(driver->Result.Path, path.string());
    EXPECT_EQ(driver->Result.Width, static_cast<std::uint32_t>(extent.Width));
    EXPECT_EQ(driver->Result.Height, static_cast<std::uint32_t>(extent.Height));
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".partial"));

    std::ifstream file(path, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    ASSERT_NE(pixels, nullptr) << "saved file is not a decodable PNG";
    EXPECT_EQ(width, extent.Width);
    EXPECT_EQ(height, extent.Height);
    // The reference triangle covers the frame center; the corner below the menu bar is clear color.
    const auto at = [&](int x, int y) { const unsigned char* p = pixels + (std::size_t(y) * width + x) * 4; return std::uint32_t(p[0]) << 16 | p[1] << 8 | p[2]; };
    EXPECT_NE(at(width / 2, height / 2), at(4, height - 4)) << "center and corner share a color: the frame is blank";
    EXPECT_NE(at(4, height - 4), 0u) << "clear color read back as black";
    stbi_image_free(pixels);
    std::filesystem::remove_all(directory);
}
