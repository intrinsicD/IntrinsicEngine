module;

#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.RHI.Profiler;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.QueueAffinity;

// ============================================================
// RHI.Profiler — backend-neutral timestamp profiling contract.
//
// Scope tokens are planned before command-recording workers fan
// out. Adapters record through the borrowed command context and
// resolve only a previously submitted frame key. A frame key
// deliberately separates monotonically increasing submission
// identity from the cyclic frames-in-flight slot.
// ============================================================

export namespace Extrinsic::RHI
{
    inline constexpr std::uint32_t kMaxTimestampScopesPerFrame = 256u;

    enum class ProfilerError : std::uint8_t
    {
        NotReady = 0,
        DeviceLost,
        InvalidState,
        InvalidArgument,
        Unsupported,
        Exhausted,
        Overflow,
    };

    [[nodiscard]] std::string_view ProfilerErrorName(ProfilerError error) noexcept;

    enum class GpuTimestampSource : std::uint8_t
    {
        Unavailable = 0,
        ContractOnly,
        NativeGpu,
    };

    enum class ProfilerBackendStatus : std::uint8_t
    {
        ContractOnly = 0,
        Ready,
        Unsupported,
        InitializationFailed,
        DeviceLost,
    };

    struct ProfilerStatusSnapshot
    {
        ProfilerBackendStatus Status{ProfilerBackendStatus::Unsupported};
        GpuTimestampSource Source{GpuTimestampSource::Unavailable};
        std::string Diagnostic{};

        [[nodiscard]] bool NativeTimestampsAvailable() const noexcept
        {
            return Status == ProfilerBackendStatus::Ready &&
                   Source == GpuTimestampSource::NativeGpu;
        }
    };

    struct ProfilerFrameKey
    {
        std::uint64_t FrameNumber{0};
        std::uint32_t FrameSlot{0};

        [[nodiscard]] friend constexpr bool operator==(
            const ProfilerFrameKey&,
            const ProfilerFrameKey&) noexcept = default;
    };

    struct ProfilerScopeDesc
    {
        std::uint32_t Ordinal{0};
        std::string Name{};
        QueueAffinity Queue{QueueAffinity::Graphics};
    };

    struct ProfilerScopeToken
    {
        static constexpr std::uint64_t InvalidGeneration = 0;
        static constexpr std::uint32_t InvalidIndex =
            std::numeric_limits<std::uint32_t>::max();

        std::uint64_t PlanGeneration{InvalidGeneration};
        std::uint32_t ScopeIndex{InvalidIndex};

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return PlanGeneration != InvalidGeneration &&
                   ScopeIndex != InvalidIndex;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const ProfilerScopeToken&,
            const ProfilerScopeToken&) noexcept = default;
    };

    struct ProfilerFramePlan
    {
        ProfilerFrameKey Frame{};
        std::vector<ProfilerScopeToken> ScopeTokens{};
    };

    enum class ProfilerFrameDisposition : std::uint8_t
    {
        Submitted = 0,
        Discarded,
    };

    struct GpuTimestampScope
    {
        std::uint32_t Ordinal{0};
        std::string Name{};
        QueueAffinity Queue{QueueAffinity::Graphics};
        GpuTimestampSource Source{GpuTimestampSource::Unavailable};
        std::optional<std::uint64_t> DurationNs{};
    };

    struct GpuTimestampQueueEnvelope
    {
        QueueAffinity Queue{QueueAffinity::Graphics};
        GpuTimestampSource Source{GpuTimestampSource::Unavailable};
        std::optional<std::uint64_t> DurationNs{};
    };

    struct GpuTimestampFrame
    {
        ProfilerFrameKey Frame{};
        GpuTimestampSource Source{GpuTimestampSource::Unavailable};
        std::vector<GpuTimestampQueueEnvelope> QueueEnvelopes{};
        std::vector<GpuTimestampScope> Scopes{};

        [[nodiscard]] std::optional<std::uint64_t>
        FindScopeDurationNs(std::string_view name) const;
    };

    struct TimestampQueryRange
    {
        std::uint32_t Base{0};
        std::uint32_t Count{0};

        [[nodiscard]] bool Contains(std::uint32_t absoluteIndex) const noexcept;
        [[nodiscard]] std::expected<std::uint32_t, ProfilerError>
        LocalIndex(std::uint32_t absoluteIndex) const noexcept;
    };

    struct TimestampQueryValue
    {
        std::uint64_t Ticks{0};
        bool Available{false};
    };

    [[nodiscard]] std::expected<std::uint64_t, ProfilerError>
    ComputeTimestampDeltaTicks(std::uint64_t beginTicks,
                               std::uint64_t endTicks,
                               std::uint32_t validBits) noexcept;

    [[nodiscard]] std::expected<std::uint64_t, ProfilerError>
    ResolveTimestampDurationNs(TimestampQueryValue begin,
                               TimestampQueryValue end,
                               std::uint32_t validBits,
                               double timestampPeriodNs) noexcept;

    class IProfiler
    {
    public:
        virtual ~IProfiler() = default;

        // Called once after the renderer has resolved accepted queues and
        // before any parallel command-recording worker starts.
        [[nodiscard]] virtual std::expected<ProfilerFramePlan, ProfilerError>
        BeginFrame(ProfilerFrameKey frame,
                   std::span<const ProfilerScopeDesc> scopes) = 0;

        // Queue brackets are recorded once per accepted queue with work in the
        // frame. The context is the primary submit context for that queue.
        [[nodiscard]] virtual std::expected<void, ProfilerError>
        BeginQueue(ICommandContext& context, QueueAffinity queue) = 0;
        [[nodiscard]] virtual std::expected<void, ProfilerError>
        EndQueue(ICommandContext& context, QueueAffinity queue) = 0;

        // Scope tokens are immutable and may be consumed concurrently when
        // each worker owns a distinct token and command context.
        [[nodiscard]] virtual std::expected<void, ProfilerError>
        BeginScope(ICommandContext& context, ProfilerScopeToken scope) = 0;
        [[nodiscard]] virtual std::expected<void, ProfilerError>
        EndScope(ICommandContext& context, ProfilerScopeToken scope) = 0;

        // Submitted is legal only after the device has proved queue submission
        // (the renderer observes its global frame number advance). Discarded
        // recording/submit attempts never publish and release their metadata.
        [[nodiscard]] virtual std::expected<void, ProfilerError>
        EndFrame(ProfilerFrameKey frame,
                 ProfilerFrameDisposition disposition) = 0;

        // Nonblocking. NotReady means no exact submitted result is available.
        [[nodiscard]] virtual std::expected<GpuTimestampFrame, ProfilerError>
        Resolve(ProfilerFrameKey frame) const = 0;

        [[nodiscard]] virtual ProfilerStatusSnapshot GetStatus() const = 0;
        [[nodiscard]] virtual std::uint32_t GetFramesInFlight() const = 0;
    };
}
