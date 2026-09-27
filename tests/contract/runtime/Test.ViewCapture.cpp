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
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Runtime.ViewCapture;
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
    EXPECT_FALSE(error->empty());
    EXPECT_FALSE(std::filesystem::exists(blocked));
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
