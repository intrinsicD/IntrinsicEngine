#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Runtime.Engine;

namespace
{
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;

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
	config.Window.Width = 256u;
	config.Window.Height = 256u;
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
			.DepthTested = true,
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
			.DepthTested = true,
		},
	}};

	auto& renderer = engine.GetRenderer();
	auto& device = engine.GetDevice();

	VisualizationOverlayRunCapture capture;
	capture.Before = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());

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
		.Viewport = {.Width = 256u, .Height = 256u},
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
