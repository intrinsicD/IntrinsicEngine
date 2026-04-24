module;

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <expected>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Telemetry;
import Extrinsic.RHI.Profiler;

namespace Extrinsic::Backends::Null
{
    class NullProfiler final : public RHI::IProfiler
    {
    public:
        explicit NullProfiler(std::uint32_t framesInFlight)
            : m_FramesInFlight(framesInFlight)
        {
        }

        void BeginFrame(std::uint32_t frameIndex, std::uint32_t maxScopesHint) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullProfiler::BeginFrame", Extrinsic::Core::Telemetry::HashString("NullProfiler::BeginFrame")};
            std::scoped_lock lock{m_Mutex};
            m_ActiveFrame.FrameIndex = frameIndex;
            m_ActiveFrame.FrameStart = Clock::now();
            m_ActiveFrame.Scopes.clear();
            m_ActiveFrame.Scopes.reserve(maxScopesHint);
            m_FrameOpen = true;
        }

        void EndFrame() override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullProfiler::EndFrame", Extrinsic::Core::Telemetry::HashString("NullProfiler::EndFrame")};
            std::scoped_lock lock{m_Mutex};
            if (!m_FrameOpen)
                return;

            const auto now = Clock::now();
            RHI::GpuTimestampFrame resolved{};
            resolved.FrameNumber = m_ActiveFrame.FrameIndex;
            resolved.CpuSubmitTimeNs = ToNs(now.time_since_epoch());
            resolved.GpuFrameTimeNs = ToNs(now - m_ActiveFrame.FrameStart);

            resolved.Scopes.reserve(m_ActiveFrame.Scopes.size());
            for (const ScopeState& scope : m_ActiveFrame.Scopes)
            {
                RHI::GpuTimestampScope out{};
                out.Name = scope.Name;
                out.DurationNs = scope.Ended ? ToNs(scope.EndTime - scope.StartTime) : 0;
                resolved.Scopes.push_back(std::move(out));
            }

            m_ResolvedFrames[resolved.FrameNumber] = std::move(resolved);
            m_FrameOpen = false;
        }

        [[nodiscard]] std::uint32_t BeginScope(std::string_view name) override
        {
            std::scoped_lock lock{m_Mutex};
            if (!m_FrameOpen)
                return 0;

            ScopeState scope{};
            scope.Name = std::string(name);
            scope.StartTime = Clock::now();
            m_ActiveFrame.Scopes.push_back(std::move(scope));
            return static_cast<std::uint32_t>(m_ActiveFrame.Scopes.size() - 1);
        }

        void EndScope(std::uint32_t scopeHandle) override
        {
            std::scoped_lock lock{m_Mutex};
            if (!m_FrameOpen || scopeHandle >= m_ActiveFrame.Scopes.size())
                return;
            ScopeState& scope = m_ActiveFrame.Scopes[scopeHandle];
            scope.EndTime = Clock::now();
            scope.Ended = true;
        }

        [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
        Resolve(std::uint32_t frameIndex) const override
        {
            std::scoped_lock lock{m_Mutex};
            const auto it = m_ResolvedFrames.find(frameIndex);
            if (it == m_ResolvedFrames.end())
                return std::unexpected(RHI::ProfilerError::NotReady);
            return it->second;
        }

        [[nodiscard]] std::uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }

    private:
        using Clock = std::chrono::steady_clock;

        static std::uint64_t ToNs(Clock::duration duration)
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
        }

        struct ScopeState
        {
            std::string Name{};
            Clock::time_point StartTime{};
            Clock::time_point EndTime{};
            bool Ended = false;
        };

        struct ActiveFrameState
        {
            std::uint32_t FrameIndex = 0;
            Clock::time_point FrameStart{};
            std::vector<ScopeState> Scopes{};
        };

        mutable std::mutex m_Mutex;
        ActiveFrameState m_ActiveFrame{};
        std::unordered_map<std::uint32_t, RHI::GpuTimestampFrame> m_ResolvedFrames;
        bool m_FrameOpen = false;
        std::uint32_t m_FramesInFlight = 2;
    };

    std::unique_ptr<RHI::IProfiler> CreateNullProfiler(std::uint32_t framesInFlight)
    {
        return std::make_unique<NullProfiler>(framesInFlight);
    }
}
