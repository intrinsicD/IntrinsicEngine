module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Backends.Null;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;

namespace Extrinsic::Backends::Null
{
    class NullProfiler final : public RHI::IProfiler
    {
    public:
        explicit NullProfiler(std::uint32_t framesInFlight)
            : m_FramesInFlight(framesInFlight)
            , m_ResolvedFrames(framesInFlight)
        {
        }

        [[nodiscard]] std::expected<RHI::ProfilerFramePlan, RHI::ProfilerError>
        BeginFrame(const RHI::ProfilerFrameKey frame,
                   const std::span<const RHI::ProfilerScopeDesc> scopes) override
        {
            if (m_ActiveFrame.has_value())
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            if (frame.FrameSlot >= m_FramesInFlight)
            {
                return std::unexpected(RHI::ProfilerError::InvalidArgument);
            }
            if (scopes.size() > RHI::kMaxTimestampScopesPerFrame)
            {
                return std::unexpected(RHI::ProfilerError::Exhausted);
            }
            for (std::size_t index = 0; index < scopes.size(); ++index)
            {
                if (scopes[index].Name.empty())
                {
                    return std::unexpected(RHI::ProfilerError::InvalidArgument);
                }
                if (const std::optional<RHI::ProfilerError> queueError =
                        ValidateQueue(scopes[index].Queue);
                    queueError.has_value())
                {
                    return std::unexpected(*queueError);
                }
                for (std::size_t prior = 0; prior < index; ++prior)
                {
                    if (scopes[prior].Ordinal == scopes[index].Ordinal)
                    {
                        return std::unexpected(
                            RHI::ProfilerError::InvalidArgument);
                    }
                }
            }

            m_ResolvedFrames[frame.FrameSlot].reset();
            const std::uint64_t planGeneration = m_NextPlanGeneration;
            ++m_NextPlanGeneration;
            if (m_NextPlanGeneration ==
                RHI::ProfilerScopeToken::InvalidGeneration)
            {
                m_NextPlanGeneration = 1;
            }
            ActiveFrameState active{
                .Frame = frame,
                .PlanGeneration = planGeneration,
            };
            active.Scopes.reserve(scopes.size());
            active.ScopeLifecycles =
                std::make_unique<std::atomic<ScopeLifecycle>[]>(scopes.size());

            RHI::ProfilerFramePlan plan{
                .Frame = frame,
            };
            plan.ScopeTokens.reserve(scopes.size());
            for (std::uint32_t index = 0;
                 index < static_cast<std::uint32_t>(scopes.size());
                 ++index)
            {
                active.Scopes.push_back(scopes[index]);
                active.ScopeLifecycles[index].store(
                    ScopeLifecycle::Planned,
                    std::memory_order_relaxed);
                plan.ScopeTokens.push_back(RHI::ProfilerScopeToken{
                    .PlanGeneration = active.PlanGeneration,
                    .ScopeIndex = index,
                });
            }
            m_ActiveFrame = std::move(active);
            return plan;
        }

        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        BeginQueue(RHI::ICommandContext&,
                   const RHI::QueueAffinity queue) override
        {
            if (const std::optional<RHI::ProfilerError> queueError =
                    ValidateQueue(queue);
                queueError.has_value())
            {
                return std::unexpected(*queueError);
            }
            ActiveFrameState* active = ActiveFrame();
            if (active == nullptr)
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            QueueLifecycle& lifecycle = active->Queues[QueueIndex(queue)];
            if (lifecycle != QueueLifecycle::Unused)
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            lifecycle = QueueLifecycle::Open;
            return {};
        }

        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        EndQueue(RHI::ICommandContext&,
                 const RHI::QueueAffinity queue) override
        {
            if (const std::optional<RHI::ProfilerError> queueError =
                    ValidateQueue(queue);
                queueError.has_value())
            {
                return std::unexpected(*queueError);
            }
            ActiveFrameState* active = ActiveFrame();
            if (active == nullptr)
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            QueueLifecycle& lifecycle = active->Queues[QueueIndex(queue)];
            if (lifecycle != QueueLifecycle::Open)
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            for (std::size_t index = 0; index < active->Scopes.size(); ++index)
            {
                if (active->Scopes[index].Queue == queue &&
                    active->ScopeLifecycles[index].load(
                        std::memory_order_acquire) == ScopeLifecycle::Begun)
                {
                    return std::unexpected(RHI::ProfilerError::InvalidState);
                }
            }
            lifecycle = QueueLifecycle::Closed;
            return {};
        }

        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        BeginScope(RHI::ICommandContext&,
                   const RHI::ProfilerScopeToken scope) override
        {
            ActiveFrameState* active = ActiveFrame();
            if (active == nullptr || !ScopeBelongsToFrame(*active, scope) ||
                active->Queues[QueueIndex(
                    active->Scopes[scope.ScopeIndex].Queue)] !=
                    QueueLifecycle::Open)
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }

            ScopeLifecycle expected = ScopeLifecycle::Planned;
            if (!active->ScopeLifecycles[scope.ScopeIndex]
                     .compare_exchange_strong(
                         expected,
                         ScopeLifecycle::Begun,
                         std::memory_order_acq_rel,
                         std::memory_order_acquire))
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            return {};
        }

        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        EndScope(RHI::ICommandContext&,
                 const RHI::ProfilerScopeToken scope) override
        {
            ActiveFrameState* active = ActiveFrame();
            if (active == nullptr || !ScopeBelongsToFrame(*active, scope))
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            if (active->Queues[QueueIndex(
                    active->Scopes[scope.ScopeIndex].Queue)] !=
                QueueLifecycle::Open)
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }

            ScopeLifecycle expected = ScopeLifecycle::Begun;
            if (!active->ScopeLifecycles[scope.ScopeIndex]
                     .compare_exchange_strong(
                         expected,
                         ScopeLifecycle::Ended,
                         std::memory_order_acq_rel,
                         std::memory_order_acquire))
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }
            return {};
        }

        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        EndFrame(const RHI::ProfilerFrameKey frame,
                 const RHI::ProfilerFrameDisposition disposition) override
        {
            ActiveFrameState* active = ActiveFrame();
            if (active == nullptr || active->Frame != frame)
            {
                return std::unexpected(RHI::ProfilerError::InvalidState);
            }

            if (disposition == RHI::ProfilerFrameDisposition::Discarded)
            {
                m_ActiveFrame.reset();
                return {};
            }

            for (std::size_t index = 0; index < active->Scopes.size(); ++index)
            {
                if (active->ScopeLifecycles[index].load(
                        std::memory_order_acquire) == ScopeLifecycle::Begun)
                {
                    return std::unexpected(RHI::ProfilerError::InvalidState);
                }
            }
            for (const QueueLifecycle queue : active->Queues)
            {
                if (queue == QueueLifecycle::Open)
                {
                    return std::unexpected(RHI::ProfilerError::InvalidState);
                }
            }

            RHI::GpuTimestampFrame resolved{
                .Frame = frame,
                .Source = RHI::GpuTimestampSource::ContractOnly,
            };
            for (std::uint32_t queueIndex = 0;
                 queueIndex < active->Queues.size();
                 ++queueIndex)
            {
                if (active->Queues[queueIndex] == QueueLifecycle::Closed)
                {
                    resolved.QueueEnvelopes.push_back(
                        RHI::GpuTimestampQueueEnvelope{
                            .Queue = QueueFromIndex(queueIndex),
                            .Source =
                                RHI::GpuTimestampSource::ContractOnly,
                            .DurationNs = std::nullopt,
                        });
                }
            }
            resolved.Scopes.reserve(active->Scopes.size());
            for (std::size_t index = 0; index < active->Scopes.size(); ++index)
            {
                if (active->ScopeLifecycles[index].load(
                        std::memory_order_acquire) == ScopeLifecycle::Ended)
                {
                    const RHI::ProfilerScopeDesc& scope =
                        active->Scopes[index];
                    resolved.Scopes.push_back(RHI::GpuTimestampScope{
                        .Ordinal = scope.Ordinal,
                        .Name = scope.Name,
                        .Queue = scope.Queue,
                        .Source = RHI::GpuTimestampSource::ContractOnly,
                        .DurationNs = std::nullopt,
                    });
                }
            }

            m_ResolvedFrames[frame.FrameSlot] = std::move(resolved);
            m_ActiveFrame.reset();
            return {};
        }

        [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
        Resolve(const RHI::ProfilerFrameKey frame) const override
        {
            if (frame.FrameSlot >= m_ResolvedFrames.size())
            {
                return std::unexpected(RHI::ProfilerError::InvalidArgument);
            }
            const std::optional<RHI::GpuTimestampFrame>& resolved =
                m_ResolvedFrames[frame.FrameSlot];
            if (!resolved.has_value() || resolved->Frame != frame)
            {
                return std::unexpected(RHI::ProfilerError::NotReady);
            }
            return *resolved;
        }

        [[nodiscard]] RHI::ProfilerStatusSnapshot GetStatus() const override
        {
            return RHI::ProfilerStatusSnapshot{
                .Status = RHI::ProfilerBackendStatus::ContractOnly,
                .Source = RHI::GpuTimestampSource::ContractOnly,
                .Diagnostic =
                    "Null profiler validates lifecycle only; native GPU "
                    "durations are unavailable.",
            };
        }

        [[nodiscard]] std::uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }

    private:
        enum class ScopeLifecycle : std::uint8_t
        {
            Planned = 0,
            Begun,
            Ended,
        };

        enum class QueueLifecycle : std::uint8_t
        {
            Unused = 0,
            Open,
            Closed,
        };

        static constexpr std::size_t kProfiledQueueCount = 2;

        struct ActiveFrameState
        {
            RHI::ProfilerFrameKey Frame{};
            std::uint64_t PlanGeneration{0};
            std::vector<RHI::ProfilerScopeDesc> Scopes{};
            std::unique_ptr<std::atomic<ScopeLifecycle>[]> ScopeLifecycles{};
            std::array<QueueLifecycle, kProfiledQueueCount> Queues{};
        };

        [[nodiscard]] static constexpr std::optional<RHI::ProfilerError>
        ValidateQueue(const RHI::QueueAffinity queue) noexcept
        {
            switch (queue)
            {
            case RHI::QueueAffinity::Graphics:
            case RHI::QueueAffinity::AsyncCompute:
                return std::nullopt;
            case RHI::QueueAffinity::Transfer:
                return RHI::ProfilerError::Unsupported;
            }
            return RHI::ProfilerError::InvalidArgument;
        }

        [[nodiscard]] static constexpr std::uint32_t
        QueueIndex(const RHI::QueueAffinity queue) noexcept
        {
            return queue == RHI::QueueAffinity::Graphics ? 0u : 1u;
        }

        [[nodiscard]] static constexpr RHI::QueueAffinity
        QueueFromIndex(const std::uint32_t queueIndex) noexcept
        {
            return queueIndex == 0u
                       ? RHI::QueueAffinity::Graphics
                       : RHI::QueueAffinity::AsyncCompute;
        }

        [[nodiscard]] ActiveFrameState* ActiveFrame() noexcept
        {
            return m_ActiveFrame.has_value() ? &*m_ActiveFrame : nullptr;
        }

        [[nodiscard]] static bool
        ScopeBelongsToFrame(const ActiveFrameState& active,
                            const RHI::ProfilerScopeToken token) noexcept
        {
            return token.IsValid() &&
                   token.PlanGeneration == active.PlanGeneration &&
                   token.ScopeIndex < active.Scopes.size();
        }

        std::uint32_t m_FramesInFlight{2};
        std::optional<ActiveFrameState> m_ActiveFrame{};
        std::vector<std::optional<RHI::GpuTimestampFrame>> m_ResolvedFrames{};
        std::uint64_t m_NextPlanGeneration{1};
    };

    std::unique_ptr<RHI::IProfiler> CreateNullProfiler(std::uint32_t framesInFlight)
    {
        return std::make_unique<NullProfiler>(framesInFlight);
    }
}
