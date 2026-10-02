// GRAPHICS-109 / RUNTIME-281: the CPU half of screenshots — viewport crop, PNG encoding,
// file writes that leave no partial file, default names — and the capture module failing
// closed on a Null render device.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Runtime.ViewCapture;
import Extrinsic.Runtime.CameraControllers;
namespace R = Extrinsic::Runtime;
namespace
{
    R::ViewCaptureImage Gradient(std::uint32_t width, std::uint32_t height)
    {
        R::ViewCaptureImage image{.Width = width, .Height = height};
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                image.Rgba8.insert(image.Rgba8.end(), {std::uint8_t(x), std::uint8_t(y), 7, 255});
        return image;
    }
    std::uint32_t BigEndian(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        return std::uint32_t(bytes[offset]) << 24 | std::uint32_t(bytes[offset + 1]) << 16 |
               std::uint32_t(bytes[offset + 2]) << 8 | bytes[offset + 3];
    }
    std::filesystem::path TempDir(const char* name)
    {
        auto dir = std::filesystem::temp_directory_path() / (std::string(name) + "-" + std::to_string(::getpid()));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        return dir;
    }
}

TEST(ViewCapture, CropKeepsTheSceneRectangleAndClipsToTheImage)
{
    const auto image = Gradient(8, 6);
    const auto cropped = R::CropViewCaptureImage(image, {.Offset = {2, 1}, .Extent = {3, 2}});
    ASSERT_EQ(cropped.Width, 3u);
    ASSERT_EQ(cropped.Height, 2u);
    EXPECT_EQ(cropped.Rgba8[0], 2u); // x of the first pixel
    EXPECT_EQ(cropped.Rgba8[1], 1u); // y of the first pixel
    EXPECT_EQ(cropped.Rgba8[(1 * 3 + 2) * 4 + 0], 4u);
    EXPECT_EQ(cropped.Rgba8[(1 * 3 + 2) * 4 + 1], 2u);

    const auto clipped = R::CropViewCaptureImage(image, {.Offset = {6, 4}, .Extent = {10, 10}});
    EXPECT_EQ(clipped.Width, 2u);
    EXPECT_EQ(clipped.Height, 2u);
    // Empty or disjoint rectangles keep the whole image (no scene rectangle published).
    EXPECT_EQ(R::CropViewCaptureImage(image, {}).Width, 8u);
    EXPECT_EQ(R::CropViewCaptureImage(image, {.Offset = {20, 20}, .Extent = {4, 4}}).Height, 6u);
}

TEST(ViewCapture, EncodesPngWithTheImageSize)
{
    const auto png = R::EncodeViewCapturePng(Gradient(5, 3));
    ASSERT_GT(png.size(), 33u);
    const std::vector<std::uint8_t> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    EXPECT_TRUE(std::equal(signature.begin(), signature.end(), png.begin()));
    EXPECT_EQ(std::string(png.begin() + 12, png.begin() + 16), "IHDR");
    EXPECT_EQ(BigEndian(png, 16), 5u);
    EXPECT_EQ(BigEndian(png, 20), 3u);

    EXPECT_TRUE(R::EncodeViewCapturePng({}).empty());
    auto inconsistent = Gradient(4, 4);
    inconsistent.Rgba8.pop_back();
    EXPECT_TRUE(R::EncodeViewCapturePng(inconsistent).empty());
}

TEST(ViewCapture, WritesCompleteFilesOrNothing)
{
    const auto dir = TempDir("intrinsic-capture-write");
    const std::vector<std::uint8_t> bytes{1, 2, 3, 4};
    const auto nested = dir / "a" / "b" / "shot.png";
    EXPECT_FALSE(R::WriteViewCaptureFile(nested, bytes).has_value());
    EXPECT_EQ(std::filesystem::file_size(nested), 4u);
    EXPECT_FALSE(std::filesystem::exists(nested.string() + ".partial"));

    // A regular file where a directory is needed: fails with a diagnostic and no file.
    std::ofstream(dir / "blocker") << "x";
    const auto blocked = dir / "blocker" / "shot.png";
    const auto error = R::WriteViewCaptureFile(blocked, bytes);
    ASSERT_TRUE(error.has_value());
    EXPECT_FALSE(error->Message.empty());
    EXPECT_EQ(error->Kind, R::ViewCaptureFailure::None);
    EXPECT_FALSE(std::filesystem::exists(blocked));
    std::filesystem::remove_all(dir);
}

TEST(ViewCapture, NoOverwriteKeepsAnExistingFile)
{
    const auto dir = TempDir("intrinsic-capture-keep");
    const auto path = dir / "shot.png";
    const std::vector<std::uint8_t> first{1, 2, 3, 4};
    const std::vector<std::uint8_t> second{9, 9};
    ASSERT_FALSE(R::WriteViewCaptureFile(path, first, false).has_value()) << "a new file is written";
    const auto refused = R::WriteViewCaptureFile(path, second, false);
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->Kind, R::ViewCaptureFailure::FileExists);
    EXPECT_EQ(std::filesystem::file_size(path), 4u) << "the existing file is untouched";
    ASSERT_FALSE(R::WriteViewCaptureFile(path, second).has_value()) << "overwrite is the default";
    EXPECT_EQ(std::filesystem::file_size(path), 2u);
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        EXPECT_EQ(entry.path().filename(), "shot.png") << "no temporary file is left behind";
    std::filesystem::remove_all(dir);
}

TEST(ViewCapture, NoOverwriteTreatsDanglingSymlinksAsOccupiedAndLeavesUserFilesAlone)
{
    const auto dir = TempDir("intrinsic-capture-symlink");
    const std::vector<std::uint8_t> bytes{1, 2, 3};
    std::error_code ec;
    std::filesystem::create_symlink(dir / "missing-target.png", dir / "link.png", ec);
    ASSERT_FALSE(ec) << ec.message();
    const auto refused = R::WriteViewCaptureFile(dir / "link.png", bytes, false);
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->Kind, R::ViewCaptureFailure::FileExists);
    EXPECT_FALSE(std::filesystem::exists(dir / "missing-target.png")) << "the link target was not created";

    // A user's file that looks like the old fixed temporary name is neither used nor removed.
    { std::ofstream(dir / "shot.png.partial") << "mine"; }
    ASSERT_FALSE(R::WriteViewCaptureFile(dir / "shot.png", bytes, false).has_value());
    EXPECT_EQ(std::filesystem::file_size(dir / "shot.png.partial"), 4u);
    EXPECT_EQ(std::filesystem::file_size(dir / "shot.png"), 3u);
    std::filesystem::remove_all(dir);
}

TEST(ViewCapture, DefaultNamesAreTimestampedPngsInTheDirectory)
{
    const auto path = R::DefaultViewCapturePath("/shots", 42);
    EXPECT_EQ(path.parent_path(), std::filesystem::path("/shots"));
    EXPECT_EQ(path.extension(), ".png");
    const auto name = path.stem().string();
    EXPECT_TRUE(name.starts_with("intrinsic-")) << name;
    EXPECT_TRUE(name.ends_with("-42")) << name;
    EXPECT_EQ(name.size(), std::string("intrinsic-20260927-210150-42").size()) << name;
}

TEST(ViewCapture, FailsClosedOnTheNullDevice)
{
    const auto dir = TempDir("intrinsic-capture-null");
    Extrinsic::Core::Config::EngineConfig config{};
    config.Simulation.WorkerThreadCount = 1u;
    config.ReferenceScene.Enabled = false;
    config.Camera.Enabled = false;
    config.Window.Backend = Extrinsic::Core::Config::WindowBackend::Null;

    struct Driver final : Intrinsic::Tests::RuntimeTestModule
    {
        R::ViewCaptureModule* Capture{nullptr};
        std::filesystem::path Path{};
        std::uint64_t Ticket{0};
        R::ViewCaptureStatus Result{};
        int Frames{0};
        void Frame(double, double) override
        {
            if (Ticket == 0) Ticket = Capture->Request({.OutputPath = Path.string()});
            Result = Capture->Status(Ticket);
            if (Result.State == R::ViewCaptureState::Failed || ++Frames > 20) Kernel().RequestExit();
        }
    };
    auto driver = std::make_unique<Driver>();
    Driver* frames = driver.get();
    frames->Path = dir / "never.png";
    Intrinsic::Tests::RuntimeTestKernel engine{config, std::move(driver)};
    frames->Capture = &engine.EmplaceModule<R::ViewCaptureModule>(dir);
    engine.Initialize();
    engine.Run();

    EXPECT_EQ(frames->Result.State, R::ViewCaptureState::Failed);
    EXPECT_NE(frames->Result.Diagnostic.find("operational render device"), std::string::npos) << frames->Result.Diagnostic;
    EXPECT_TRUE(frames->Capture->UnavailableReason().has_value());
    EXPECT_FALSE(std::filesystem::exists(frames->Path));
    ASSERT_TRUE(frames->Capture->LastFinished().has_value());
    EXPECT_EQ(frames->Capture->LastFinished()->Ticket, frames->Ticket);
    std::filesystem::remove_all(dir);
}

TEST(ViewCapture, LegendAppendsAColormapStripBelowTheImage)
{
    auto image = Gradient(40, 10);
    const auto original = image.Rgba8;
    const std::vector<std::uint8_t> lut{255, 0, 0, 0, 255, 0, 0, 0, 255}; // red, green, blue
    R::AppendViewCaptureLegend(image, lut);
    ASSERT_EQ(image.Height, 10u + R::kViewCaptureLegendHeight);
    ASSERT_EQ(image.Rgba8.size(), std::size_t{4} * 40 * image.Height);
    EXPECT_TRUE(std::equal(original.begin(), original.end(), image.Rgba8.begin())) << "the capture stays untouched";
    const auto pixel = [&](std::uint32_t x, std::uint32_t y) { return &image.Rgba8[(std::size_t(y) * 40 + x) * 4]; };
    const std::uint32_t stripRow = 10 + R::kViewCaptureLegendHeight / 2;
    EXPECT_EQ(pixel(4, stripRow)[0], 255u) << "min end";
    EXPECT_EQ(pixel(35, stripRow)[2], 255u) << "max end";
    EXPECT_EQ(pixel(20, stripRow)[1], 255u) << "middle";
    EXPECT_EQ(pixel(0, stripRow)[0], 24u) << "margin";
    EXPECT_EQ(pixel(20, 10)[0], 24u) << "top margin row";
    EXPECT_EQ(R::EncodeViewCapturePng(image).empty(), false);
}

// Screenshots restore the view through ICameraController::Clone (exact state, not a re-seed).
TEST(ViewCapture, CameraClonesKeepTheExactView)
{
    R::OrbitCameraController orbit;
    orbit.Focus({.Center = {3.0f, -2.0f, 7.0f}, .Radius = 0.5f});
    const auto before = orbit.GetView({640, 480});
    const auto copy = orbit.Clone();
    ASSERT_NE(copy, nullptr);
    orbit.Focus({.Center = {-10.0f, 0.0f, 0.0f}, .Radius = 4.0f});
    const auto restored = copy->GetView({640, 480});
    EXPECT_EQ(restored.Position, before.Position);
    EXPECT_EQ(restored.Forward, before.Forward);
    EXPECT_EQ(restored.FarPlane, before.FarPlane);
    for (const auto kind : {Extrinsic::Core::Config::CameraControllerKind::Orbit, Extrinsic::Core::Config::CameraControllerKind::Fly,
                            Extrinsic::Core::Config::CameraControllerKind::FreeLook, Extrinsic::Core::Config::CameraControllerKind::TopDown})
    {
        const auto controller = R::CreateCameraController(kind);
        ASSERT_NE(controller->Clone(), nullptr);
        EXPECT_EQ(controller->Clone()->Kind(), kind);
    }
}
