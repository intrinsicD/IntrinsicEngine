#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include "MinimalTriangleReadback.hpp"
#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;

namespace
{
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;
namespace Readback = Extrinsic::Tests::Support::MinimalTriangleReadback;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;

inline constexpr std::uint32_t kReadbackWidth = 256u;
inline constexpr std::uint32_t kReadbackHeight = 256u;

[[nodiscard]] constexpr Readback::ExpectedPixel ExpectedColor(const float r,
															  const float g,
															  const float b,
															  const float a = 1.0f) noexcept
{
	return Readback::ExpectedPixel{
		.R = Readback::Quantize8(r),
		.G = Readback::Quantize8(g),
		.B = Readback::Quantize8(b),
		.A = Readback::Quantize8(a),
	};
}

struct VisualizationOverlaySamplePoint
{
	std::string_view Label;
	std::uint32_t PixelX;
	std::uint32_t PixelY;
	Readback::ExpectedPixel Expected;
};

inline constexpr std::array<VisualizationOverlaySamplePoint, 3> kVisualizationReadbackSamples{{
	VisualizationOverlaySamplePoint{"vector_field_red", 72u, 64u, ExpectedColor(1.0f, 0.0f, 0.0f)},
	VisualizationOverlaySamplePoint{"isoline_green", 184u, 176u, ExpectedColor(0.0f, 1.0f, 0.0f)},
	// BUG-015/BUG-016: the default-recipe SceneColorHDR background clears to a
	// visible blue (0.10, 0.20, 0.45), not black.
	VisualizationOverlaySamplePoint{"scene_clear_blue", 16u, 16u, ExpectedColor(0.10f, 0.20f, 0.45f)},
}};

[[nodiscard]] Readback::ExpectedPixel ReorderToRgba(
	const Extrinsic::RHI::Format format,
	const std::uint8_t b0,
	const std::uint8_t b1,
	const std::uint8_t b2,
	const std::uint8_t b3) noexcept
{
	switch (format)
	{
	case Extrinsic::RHI::Format::BGRA8_UNORM:
	case Extrinsic::RHI::Format::BGRA8_SRGB:
		return Readback::ExpectedPixel{.R = b2, .G = b1, .B = b0, .A = b3};
	case Extrinsic::RHI::Format::RGBA8_UNORM:
	case Extrinsic::RHI::Format::RGBA8_SRGB:
	default:
		return Readback::ExpectedPixel{.R = b0, .G = b1, .B = b2, .A = b3};
	}
}

[[nodiscard]] constexpr bool IsSrgbFormat(const Extrinsic::RHI::Format format) noexcept
{
	return format == Extrinsic::RHI::Format::RGBA8_SRGB ||
		   format == Extrinsic::RHI::Format::BGRA8_SRGB;
}

[[nodiscard]] std::uint8_t SrgbByteToLinearByte(const std::uint8_t srgb) noexcept
{
	const float s = static_cast<float>(srgb) / 255.0f;
	const float linear = (s <= 0.04045f)
							 ? (s / 12.92f)
							 : std::pow((s + 0.055f) / 1.055f, 2.4f);
	const float clamped = linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear);
	return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
}

[[nodiscard]] Readback::ExpectedPixel SrgbToLinearPixel(
	const Extrinsic::RHI::Format format,
	const Readback::ExpectedPixel& srgbPixel) noexcept
{
	if (!IsSrgbFormat(format))
	{
		return srgbPixel;
	}
	return Readback::ExpectedPixel{
		.R = SrgbByteToLinearByte(srgbPixel.R),
		.G = SrgbByteToLinearByte(srgbPixel.G),
		.B = SrgbByteToLinearByte(srgbPixel.B),
		.A = srgbPixel.A,
	};
}

class ExitAfterFramesApp final : public IApplication
{
public:
	explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
		: m_TargetFrames(targetFrames)
	{
	}

	void OnInitialize(Engine&) override {}
	void OnSimTick(Engine&, double) override {}

	void OnVariableTick(Engine& engine, double, double) override
	{
		++m_Frames;
		if (m_Frames >= m_TargetFrames)
		{
			engine.RequestExit();
		}
	}

	void OnShutdown(Engine&) override {}

private:
	std::uint32_t m_TargetFrames{1u};
	std::uint32_t m_Frames{0u};
};

Counters::Snapshot ToCounterSnapshot(
	const Extrinsic::Backends::Vulkan::VulkanOperationalDiagnosticsSnapshot& vk) noexcept
{
	return Counters::Snapshot{
		vk.VulkanFallbackToNullCount,
		vk.VulkanInitFailureCount,
		vk.VulkanValidationErrorCount,
		vk.VulkanOperationalGateFailureCount,
	};
}

struct VisualizationOverlayBootstrap
{
	std::unique_ptr<Engine> EnginePtr;
	bool Skipped{false};
	std::string SkipReason;
};

[[nodiscard]] VisualizationOverlayBootstrap BootstrapOperationalDefaultRecipe(
	const std::uint32_t targetFrames = 4u,
	const char* const windowTitle = "Intrinsic VisualizationOverlay gpu;vulkan smoke")
{
	if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
	{
		return VisualizationOverlayBootstrap{
			.EnginePtr = nullptr,
			.Skipped = true,
			.SkipReason = "GLFW could not initialize in this environment; gpu;vulkan visualization-overlay smoke is opt-in.",
		};
	}

	auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
	config.Window.Title = windowTitle;
	config.Window.Width = kReadbackWidth;
	config.Window.Height = kReadbackHeight;
	config.Window.Resizable = false;
	config.Render.EnableValidation = false;
	config.Render.EnableVSync = false;

	auto enginePtr = std::make_unique<Engine>(
		config, std::make_unique<ExitAfterFramesApp>(targetFrames));
	enginePtr->Initialize();

	const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
	if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
	{
		enginePtr->Shutdown();
		return VisualizationOverlayBootstrap{
			.EnginePtr = nullptr,
			.Skipped = true,
			.SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
		};
	}

	enginePtr->Run();

	const auto warmupStatus = EvaluateVulkanDeviceOperationalStatus(&enginePtr->GetDevice());
	if (!enginePtr->GetDevice().IsOperational())
	{
		std::string skipReason{"Promoted Vulkan operational gate did not flip during default-recipe warmup: status="};
		skipReason += ToString(warmupStatus.Code);
		skipReason += " reason=";
		skipReason += ToString(warmupStatus.Reason);
		enginePtr->Shutdown();
		return VisualizationOverlayBootstrap{
			.EnginePtr = nullptr,
			.Skipped = true,
			.SkipReason = std::move(skipReason),
		};
	}

	return VisualizationOverlayBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
}

[[nodiscard]] const Extrinsic::Graphics::RenderGraphCommandPassStats* FindCommandPass(
	const Extrinsic::Graphics::RenderGraphFrameStats& stats,
	const std::string_view passName) noexcept
{
	const auto it = std::find_if(
		stats.CommandRecords.Passes.begin(),
		stats.CommandRecords.Passes.end(),
		[passName](const auto& pass) { return pass.Name == passName; });
	return it == stats.CommandRecords.Passes.end() ? nullptr : &*it;
}

struct VisualizationOverlayRunCapture
{
	Counters::Snapshot Before{};
	Counters::Snapshot After{};
	Extrinsic::Graphics::RenderGraphFrameStats Stats{};
	Extrinsic::Backends::Vulkan::VulkanOperationalStatus Status{};
	bool DeviceOperational{false};
};

[[nodiscard]] VisualizationOverlayRunCapture DriveVisualizationOverlayFrameAndCapture(Engine& engine)
{
	static const std::array<Extrinsic::Graphics::VectorFieldOverlayPacket, 1> kVectorFields{{
		Extrinsic::Graphics::VectorFieldOverlayPacket{
			.Name = "GpuSmoke.VectorField",
			.Domain = Extrinsic::Graphics::VisualizationAttributeDomain::Vertex,
			.ElementCount = 2u,
			.Scale = 1.0f,
			.Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
			.DepthTested = false,
		},
	}};
	static const std::array<Extrinsic::Graphics::IsolineOverlayPacket, 1> kIsolines{{
		Extrinsic::Graphics::IsolineOverlayPacket{
			.SourceScalarName = "GpuSmoke.Iso",
			.Domain = Extrinsic::Graphics::VisualizationAttributeDomain::Face,
			.IsoValueCount = 3u,
			.RangeMin = 0.0f,
			.RangeMax = 1.0f,
			.LineWidth = 1.0f,
			.Color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
			.DepthTested = false,
		},
	}};

	auto& renderer = engine.GetRenderer();
	auto& device = engine.GetDevice();

	VisualizationOverlayRunCapture capture;
	capture.Before = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
	renderer.SetDebugViewRequestedResourceName("SceneColorHDR");

	Extrinsic::RHI::FrameHandle frame{};
	if (!renderer.BeginFrame(frame))
	{
		capture.Status = EvaluateVulkanDeviceOperationalStatus(&device);
		capture.DeviceOperational = device.IsOperational();
		capture.Stats = renderer.GetLastRenderGraphStats();
		capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
		return capture;
	}

	renderer.SubmitRuntimeSnapshots(Extrinsic::Graphics::RuntimeRenderSnapshotBatch{
		.VisualizationVectorFields = std::span<const Extrinsic::Graphics::VectorFieldOverlayPacket>{
			kVectorFields.data(), kVectorFields.size()},
		.VisualizationIsolines = std::span<const Extrinsic::Graphics::IsolineOverlayPacket>{
			kIsolines.data(), kIsolines.size()},
	});

	const Extrinsic::Graphics::RenderFrameInput input{
		.Viewport = {.Width = kReadbackWidth, .Height = kReadbackHeight},
		.DebugOverlayEnabled = true,
	};
	Extrinsic::Graphics::RenderWorld world = renderer.ExtractRenderWorld(input);
	renderer.PrepareFrame(world);
	renderer.ExecuteFrame(frame, world);
	(void)renderer.EndFrame(frame);
	device.Present(frame);

	capture.Status = EvaluateVulkanDeviceOperationalStatus(&device);
	capture.DeviceOperational = device.IsOperational();
	capture.Stats = renderer.GetLastRenderGraphStats();
	capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
	return capture;
}
} // namespace

TEST(VisualizationOverlaySurfaceGpuSmoke, MixedLanesRecordOnOperationalVulkanCommandStream)
{
	auto bootstrap = BootstrapOperationalDefaultRecipe();
	if (bootstrap.Skipped)
	{
		GTEST_SKIP() << bootstrap.SkipReason;
	}
	Engine& engine = *bootstrap.EnginePtr;

	const auto run = DriveVisualizationOverlayFrameAndCapture(engine);

	if (!run.DeviceOperational)
	{
		engine.Shutdown();
		ADD_FAILURE() << "Promoted Vulkan operational gate dropped during visualization-overlay frame: status="
					  << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason);
		return;
	}

	EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
	EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

	const auto& stats = run.Stats;
	EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
	EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
	EXPECT_TRUE(stats.Execute.DeviceOperational);

	const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
	ASSERT_NE(pass, nullptr)
		<< "Default recipe omitted VisualizationOverlayPass despite overlay snapshots.";
	EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
		<< "VisualizationOverlayPass did not record on the operational Vulkan command stream.";

	EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
	EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);
	EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 1u);
	EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 1u);
	EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
	EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

	EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
		<< "Vulkan fallback counters incremented across the visualization-overlay frame: "
		<< "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
		<< ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
		<< ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
		<< ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

	engine.Shutdown();
}

TEST(VisualizationOverlaySurfaceGpuSmoke, MixedLanesReadBackExpectedSampleColors)
{
	auto bootstrap = BootstrapOperationalDefaultRecipe();
	if (bootstrap.Skipped)
	{
		GTEST_SKIP() << bootstrap.SkipReason;
	}
	Engine& engine = *bootstrap.EnginePtr;

	auto& renderer = engine.GetRenderer();
	auto& device = engine.GetDevice();
	const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
	const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
	if (bytesPerPixel < 4u)
	{
		engine.Shutdown();
		GTEST_SKIP() << "Backbuffer format has no 4-channel host-readable layout on this host; readback skipped.";
	}

	const std::uint64_t readbackSize =
		static_cast<std::uint64_t>(bytesPerPixel) *
		static_cast<std::uint64_t>(kReadbackWidth) *
		static_cast<std::uint64_t>(kReadbackHeight);
	Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
		.SizeBytes = readbackSize,
		.Usage = Extrinsic::RHI::BufferUsage::TransferDst,
		.HostVisible = true,
		.DebugName = "VisualizationOverlay.Readback",
	});
	if (!readbackBuffer.IsValid())
	{
		engine.Shutdown();
		GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
	}
	renderer.SetVisualizationOverlayBackbufferReadbackBuffer(readbackBuffer);

	const auto run = DriveVisualizationOverlayFrameAndCapture(engine);

	if (!run.DeviceOperational)
	{
		renderer.SetVisualizationOverlayBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
		device.DestroyBuffer(readbackBuffer);
		engine.Shutdown();
		ADD_FAILURE() << "Promoted Vulkan operational gate dropped during visualization-overlay readback frame: status="
					  << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason);
		return;
	}

	EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
	EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

	const auto& stats = run.Stats;
	EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
	EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
	EXPECT_TRUE(stats.Execute.DeviceOperational);

	const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
	ASSERT_NE(pass, nullptr)
		<< "Default recipe omitted VisualizationOverlayPass despite overlay snapshots.";
	EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
		<< "VisualizationOverlayPass did not record on the operational Vulkan command stream.";

	EXPECT_EQ(stats.VisualizationOverlayBackbufferReadbackCopyCount, 1u)
		<< "Visualization-overlay readback copy must record once for the armed mixed-lane frame.";
	EXPECT_EQ(stats.DefaultRecipeBackbufferReadbackCopyCount, 0u)
		<< "Visualization-overlay pixel parity must use its own counter, not the canonical surface readback counter.";
	EXPECT_EQ(stats.TransientDebugBackbufferReadbackCopyCount, 0u)
		<< "Visualization-overlay pixel parity must not reuse the transient-debug counter.";
	EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);
	EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 1u);

	EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
		<< "Vulkan fallback counters incremented across the visualization-overlay readback frame: "
		<< "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
		<< ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
		<< ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
		<< ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

	std::vector<std::uint8_t> readbackBytes(static_cast<std::size_t>(readbackSize), 0u);
	device.ReadBuffer(readbackBuffer, readbackBytes.data(), readbackSize, 0u);

	const std::uint64_t rowStride =
		static_cast<std::uint64_t>(bytesPerPixel) *
		static_cast<std::uint64_t>(kReadbackWidth);

	for (const VisualizationOverlaySamplePoint& sample : kVisualizationReadbackSamples)
	{
		const std::uint64_t pixelOffset =
			static_cast<std::uint64_t>(sample.PixelY) * rowStride +
			static_cast<std::uint64_t>(sample.PixelX) * static_cast<std::uint64_t>(bytesPerPixel);
		ASSERT_LE(pixelOffset + 4u, readbackSize)
			<< "Sample point " << sample.Label << " is outside the readback buffer.";

		const std::uint8_t b0 = readbackBytes[static_cast<std::size_t>(pixelOffset + 0u)];
		const std::uint8_t b1 = readbackBytes[static_cast<std::size_t>(pixelOffset + 1u)];
		const std::uint8_t b2 = readbackBytes[static_cast<std::size_t>(pixelOffset + 2u)];
		const std::uint8_t b3 = readbackBytes[static_cast<std::size_t>(pixelOffset + 3u)];

		const Readback::ExpectedPixel actualSrgb = ReorderToRgba(backbufferFormat, b0, b1, b2, b3);
		const Readback::ExpectedPixel actualLinear = SrgbToLinearPixel(backbufferFormat, actualSrgb);
		EXPECT_TRUE(Readback::ChannelsWithinTolerance(sample.Expected, actualLinear))
			<< "Sample " << sample.Label
			<< " (pixel " << sample.PixelX << "," << sample.PixelY << ")"
			<< " expected linear RGBA=("
			<< static_cast<int>(sample.Expected.R) << ","
			<< static_cast<int>(sample.Expected.G) << ","
			<< static_cast<int>(sample.Expected.B) << ","
			<< static_cast<int>(sample.Expected.A) << ")"
			<< " actual linear RGBA=("
			<< static_cast<int>(actualLinear.R) << ","
			<< static_cast<int>(actualLinear.G) << ","
			<< static_cast<int>(actualLinear.B) << ","
			<< static_cast<int>(actualLinear.A) << ")"
			<< " raw bytes=("
			<< static_cast<int>(b0) << ","
			<< static_cast<int>(b1) << ","
			<< static_cast<int>(b2) << ","
			<< static_cast<int>(b3) << ")"
			<< " backbuffer format=" << static_cast<int>(backbufferFormat);
	}

	renderer.SetVisualizationOverlayBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
	device.DestroyBuffer(readbackBuffer);

	engine.Shutdown();
}
