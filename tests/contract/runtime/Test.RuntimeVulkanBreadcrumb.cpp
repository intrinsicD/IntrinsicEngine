#include <cstddef>
#include <memory>
#include <string_view>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Logging;
import Extrinsic.Runtime.Engine;

namespace
{
using Extrinsic::Core::Config::EngineConfig;
using Extrinsic::Core::Config::GraphicsBackend;
using Extrinsic::Core::Config::RenderConfig;
using Extrinsic::Core::Log::LogSnapshot;
using Extrinsic::Core::Log::Level;
using Extrinsic::Core::Log::TakeSnapshot;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;
using Extrinsic::Runtime::ShouldEmitVulkanRequestedButNotOperationalBreadcrumb;

constexpr std::string_view kBreadcrumbPrefix =
    "[Runtime] VulkanRequestedButNotOperational";

class StubApplication final : public IApplication
{
public:
    void OnInitialize(Engine& /*engine*/) override {}
    void OnSimTick(Engine& /*engine*/, double /*fixedDt*/) override {}
    void OnVariableTick(Engine& /*engine*/, double /*alpha*/, double /*dt*/) override {}
    void OnShutdown(Engine& /*engine*/) override {}
};

[[nodiscard]] std::size_t CountBreadcrumbWarnings(const LogSnapshot& snapshot,
                                                  const std::uint64_t sinceSequence) noexcept
{
    // LogSnapshot::Entries is the ring buffer in chronological order. We can't
    // align by index alone across snapshots (entries may have rolled out of the
    // ring), so the caller must take a "before" snapshot, drive Initialize,
    // then take an "after" snapshot. The sequence number on the snapshot is
    // monotonic across logs; we count entries that look like our breadcrumb
    // and fall in the new tail of the ring.
    (void)sinceSequence;
    std::size_t count = 0;
    for (const auto& entry : snapshot.Entries)
    {
        if (entry.Lvl != Level::Warning)
            continue;
        const std::string_view message{entry.Message};
        if (message.starts_with(kBreadcrumbPrefix))
            ++count;
    }
    return count;
}

} // namespace

// -----------------------------------------------------------------------------
// GRAPHICS-033B pure decision helper: the runtime startup breadcrumb fires
// exactly when the runtime requested the promoted Vulkan device (Backend ==
// Vulkan AND EnablePromotedVulkanDevice) but the resolved device is not
// operational. It is silent in every other truth-table row.
// -----------------------------------------------------------------------------
TEST(RuntimeVulkanBreadcrumb, DecisionFiresWhenRequestedVulkanAndDeviceNotOperational)
{
    RenderConfig config{};
    config.Backend = GraphicsBackend::Vulkan;
    config.EnablePromotedVulkanDevice = true;

    EXPECT_TRUE(ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(config, false));
}

TEST(RuntimeVulkanBreadcrumb, DecisionSilentWhenDeviceIsOperational)
{
    RenderConfig config{};
    config.Backend = GraphicsBackend::Vulkan;
    config.EnablePromotedVulkanDevice = true;

    EXPECT_FALSE(ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(config, true));
}

TEST(RuntimeVulkanBreadcrumb, DecisionSilentWhenPromotedVulkanNotRequested)
{
    RenderConfig config{};
    config.Backend = GraphicsBackend::Vulkan;
    config.EnablePromotedVulkanDevice = false;

    EXPECT_FALSE(ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(config, false));
    EXPECT_FALSE(ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(config, true));
}

// -----------------------------------------------------------------------------
// End-to-end: `Engine::Initialize()` emits exactly one breadcrumb when the
// runtime requested promoted Vulkan but the resolved device is not
// operational (the default Null fallback path on the CPU gate), and zero when
// promoted Vulkan was not requested. Runtime never aborts solely because of
// the fallback — `engine.Initialize()` returns normally either way.
// -----------------------------------------------------------------------------
TEST(RuntimeVulkanBreadcrumb, EngineInitializeFiresBreadcrumbOncePerStartupWhenRequested)
{
    EngineConfig config{};
    config.Render.Backend = GraphicsBackend::Vulkan;
    config.Render.EnablePromotedVulkanDevice = true;

    const LogSnapshot before = TakeSnapshot();
    const std::size_t beforeCount = CountBreadcrumbWarnings(before, 0);

    Engine engine(config, std::make_unique<StubApplication>());
    engine.Initialize();

    const LogSnapshot after = TakeSnapshot();
    const std::size_t afterCount = CountBreadcrumbWarnings(after, before.Sequence);

    EXPECT_EQ(afterCount - beforeCount, 1u)
        << "Engine::Initialize() must emit the VulkanRequestedButNotOperational "
           "warn breadcrumb exactly once per startup when promoted Vulkan is "
           "requested but the resolved device is not operational.";

    engine.Shutdown();
}

TEST(RuntimeVulkanBreadcrumb, EngineInitializeSilentWhenPromotedVulkanNotRequested)
{
    EngineConfig config{}; // Backend defaults to Vulkan, EnablePromotedVulkanDevice = false.
    ASSERT_FALSE(config.Render.EnablePromotedVulkanDevice);

    const LogSnapshot before = TakeSnapshot();
    const std::size_t beforeCount = CountBreadcrumbWarnings(before, 0);

    Engine engine(config, std::make_unique<StubApplication>());
    engine.Initialize();

    const LogSnapshot after = TakeSnapshot();
    const std::size_t afterCount = CountBreadcrumbWarnings(after, before.Sequence);

    EXPECT_EQ(afterCount, beforeCount)
        << "Engine::Initialize() must not emit the breadcrumb when promoted "
           "Vulkan was not requested (truth-table NotRequested rows).";

    engine.Shutdown();
}

TEST(RuntimeVulkanBreadcrumb, EngineInitializeSucceedsEvenWhenVulkanFallsBackToNull)
{
    // Runtime never aborts solely because the requested Vulkan device falls
    // back to Null. Driving the full Initialize/Shutdown cycle on the CPU
    // path locks in the truth-table "Runtime result = continue" column.
    EngineConfig config{};
    config.Render.Backend = GraphicsBackend::Vulkan;
    config.Render.EnablePromotedVulkanDevice = true;

    Engine engine(config, std::make_unique<StubApplication>());
    engine.Initialize();
    EXPECT_FALSE(engine.GetDevice().IsOperational())
        << "CPU contract gate falls back to the Null device which reports "
           "non-operational; the engine must still have initialized cleanly.";
    engine.Shutdown();
}
