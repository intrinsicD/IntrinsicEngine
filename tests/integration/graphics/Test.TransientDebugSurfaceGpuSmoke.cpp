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
import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
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
using Extrinsic::Core::Config::FrameRecipeKind;
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

struct TransientDebugBootstrap
{
	std::unique_ptr<Engine> EnginePtr;
	bool Skipped{false};
	std::string SkipReason;
};

[[nodiscard]] TransientDebugBootstrap BootstrapOperationalDefaultRecipe(
	const std::uint32_t targetFrames = 4u,
	const char* const windowTitle = "Intrinsic TransientDebug gpu;vulkan smoke")
{
	if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
	{
		return TransientDebugBootstrap{
			.EnginePtr = nullptr,
			.Skipped = true,
			.SkipReason = "GLFW could not initialize in this environment; gpu;vulkan transient-debug smoke is opt-in.",
		};
	}

	auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
	config.Window.Title = windowTitle;
	config.Window.Width = 256u;
	config.Window.Height = 256u;
	config.Window.Resizable = false;
	config.Render.EnableValidation = false;
	config.Render.EnableVSync = false;
	config.Render.FrameRecipe = FrameRecipeKind::Default;

	auto enginePtr = std::make_unique<Engine>(
		config, std::make_unique<ExitAfterFramesApp>(targetFrames));
	enginePtr->Initialize();

	const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
	if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
	{
		enginePtr->Shutdown();
		return TransientDebugBootstrap{
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
		return TransientDebugBootstrap{
			.EnginePtr = nullptr,
			.Skipped = true,
			.SkipReason = std::move(skipReason),
		};
	}

	return TransientDebugBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
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

struct TransientDebugRunCapture
{
	Counters::Snapshot Before{};
	Counters::Snapshot After{};
	Extrinsic::Graphics::RenderGraphFrameStats Stats{};
	Extrinsic::Backends::Vulkan::VulkanOperationalStatus Status{};
	bool DeviceOperational{false};
};

[[nodiscard]] TransientDebugRunCapture DriveTransientDebugFrameAndCapture(Engine& engine)
{
	static const std::array<Extrinsic::Graphics::DebugTrianglePacket, 1> kTriangles{{
		Extrinsic::Graphics::DebugTrianglePacket{
			.A = glm::vec3{-0.60f, -0.15f, 0.0f},
			.B = glm::vec3{-0.25f, -0.15f, 0.0f},
			.C = glm::vec3{-0.425f, 0.30f, 0.0f},
			.Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
			.DepthTested = true,
		},
	}};
	static const std::array<Extrinsic::Graphics::DebugLinePacket, 1> kLines{{
		Extrinsic::Graphics::DebugLinePacket{
			.Start = glm::vec3{-0.10f, 0.0f, 0.0f},
			.End = glm::vec3{0.35f, 0.0f, 0.0f},
			.Color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
			.Width = 1.0f,
			.DepthTested = true,
		},
	}};
	static const std::array<Extrinsic::Graphics::DebugPointPacket, 1> kPoints{{
		Extrinsic::Graphics::DebugPointPacket{
			.Position = glm::vec3{0.60f, 0.0f, 0.0f},
			.Color = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
			.Radius = 0.05f,
			.DepthTested = true,
		},
	}};

	auto& renderer = engine.GetRenderer();
	auto& device = engine.GetDevice();

	TransientDebugRunCapture capture;
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
		.DebugLines = std::span<const Extrinsic::Graphics::DebugLinePacket>{kLines.data(), kLines.size()},
		.DebugPoints = std::span<const Extrinsic::Graphics::DebugPointPacket>{kPoints.data(), kPoints.size()},
		.DebugTriangles = std::span<const Extrinsic::Graphics::DebugTrianglePacket>{kTriangles.data(), kTriangles.size()},
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

TEST(TransientDebugSurfaceGpuSmoke, MixedLanesRecordOnOperationalVulkanCommandStream)
{
	auto bootstrap = BootstrapOperationalDefaultRecipe();
	if (bootstrap.Skipped)
	{
		GTEST_SKIP() << bootstrap.SkipReason;
	}
	Engine& engine = *bootstrap.EnginePtr;

	const auto run = DriveTransientDebugFrameAndCapture(engine);

	if (!run.DeviceOperational)
	{
		engine.Shutdown();
		ADD_FAILURE() << "Promoted Vulkan operational gate dropped during transient-debug frame: status="
					  << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason);
		return;
	}

	EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
	EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

	const auto& stats = run.Stats;
	EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
	EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
	EXPECT_TRUE(stats.Execute.DeviceOperational);

	const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
	ASSERT_NE(pass, nullptr)
		<< "Default recipe omitted TransientDebugSurfacePass despite mixed debug primitive snapshots.";
	EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
		<< "TransientDebugSurfacePass did not record on the operational Vulkan command stream.";

	EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 1u);
	EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 1u);
	EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 1u);
	EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 1u);
	EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 1u);
	EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 1u);
	EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
	EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

	EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
		<< "Vulkan fallback counters incremented across the transient-debug frame: "
		<< "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
		<< ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
		<< ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
		<< ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

	engine.Shutdown();
}



