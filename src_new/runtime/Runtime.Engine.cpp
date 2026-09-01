module;

#include <memory>
#include <optional>
#include <string_view>

module Runtime.Engine;

import Core.CommandBus;
import Core.EventBus;
import ECS.World;

namespace Extrinsic::Runtime
{
    struct FrameContext
    {
        std::uint64_t FrameIndex{};
        double RawDeltaSeconds{};
        double SimulationDeltaSeconds{};
        double InterpolationAlpha{};
    };

    struct Snapshot
    {
        // Placeholder for render snapshot data
    };

    struct RenderResult
    {
        // Placeholder for render result data
    };

    [[nodiscard]]
    static constexpr std::string_view ToString(
        EngineState state) noexcept
    {
        switch (state)
        {
        case EngineState::Constructed: return "Constructed";
        case EngineState::Initialized: return "Initialized";
        case EngineState::Running: return "Running";
        case EngineState::ShuttingDown: return "ShuttingDown";
        case EngineState::Stopped: return "Stopped";
        }

        return "Unknown";
    }

    struct Engine::Impl
    {
        explicit Impl(EngineConfig config) : m_Config(std::move(config))
        {
        }

        ~Impl() = default;

        std::optional<FrameContext> PrepareNextFrame()
        {
            return {};
        }

        void RunSimulation(std::optional<FrameContext> frame)
        {
        }

        Snapshot BuildRenderSnapshot(std::optional<FrameContext> frame)
        {
            return {};
        }

        RenderResult RenderFrame(std::optional<FrameContext> frame, const Snapshot& snapshot)
        {
            return {};
        }

        void FinishFrame(std::optional<FrameContext> frame, const RenderResult& renderResult)
        {
        }

        [[nodiscard]]
        static constexpr bool IsValidTransition(EngineState from, EngineState to) noexcept
        {
            switch (from)
            {
            case EngineState::Constructed:
                return to == EngineState::Initialized || to == EngineState::ShuttingDown;

            case EngineState::Initialized:
                return to == EngineState::Running || to == EngineState::ShuttingDown;

            case EngineState::Running:
                return to == EngineState::ShuttingDown;

            case EngineState::ShuttingDown:
                return to == EngineState::Stopped;

            case EngineState::Stopped:
                return false;
            }

            return false;
        }

        void RecordInvalidTransition(EngineState from, EngineState requested)
        {
            ++m_Diagnostics.InvalidStateTransitionCount;

            m_Diagnostics.LastInvalidTransition = InvalidStateTransition{
                .From = from,
                .Requested = requested,
                .Sequence = m_Diagnostics.InvalidStateTransitionCount
            };

            Core::Log::Error(
                "[Engine] invalid state transition: {} -> {}",
                ToString(from),
                ToString(requested));
        }

        bool TransitionTo(EngineState next)
        {
            if (m_State == next)
                return true;

            const EngineState previous = m_State;

            if (!IsValidTransition(previous, next))
            {
                RecordInvalidTransition(previous, next);
                return false;
            }

            m_State = next;

            ++m_Diagnostics.StateTransitionCount;
            m_Diagnostics.State = next;
            m_Diagnostics.LastTransition = StateTransition{
                .From = previous,
                .To = next,
                .Sequence = m_Diagnostics.StateTransitionCount
            };

            if (m_Observability.LogStateTransitions)
            {
                Core::Log::Debug(
                    "[Engine] state: {} -> {}",
                    ToString(previous),
                    ToString(next));
            }

            return true;
        }

        EngineState State() const noexcept { return m_State; }

        EngineState m_State{EngineState::Constructed};
        EngineConfig m_Config;
        EngineDiagnosticsSnapshot m_Diagnostics{};
        ObservabilityConfig m_Observability{};

        ECS::World m_World;

        Core::EventBus m_EventBus;
        Core::CommandBus m_CommandBus;
    };

    Engine::Engine(EngineConfig config) : m_Impl(std::make_unique<Impl>(config))
    {
    }

    Engine::~Engine()
    {
    }

    void Engine::Initialize()
    {
        if (m_Impl->State() != EngineState::Constructed)
        {
            // Invalid-state diagnostic
            return;
        }

        m_Impl->TransitionTo(EngineState::Initialized);
    }

    void Engine::Run()
    {
        if (m_Impl->State() != EngineState::Initialized)
        {
            // Invalid-state diagnostic
            return;
        }

        m_Impl->TransitionTo(EngineState::Running);

        while (m_Impl->State() == EngineState::Running)
        {
            auto frame = m_Impl->PrepareNextFrame();
            if (!frame)
                break;

            m_Impl->RunSimulation(*frame);

            const auto snapshot = m_Impl->BuildRenderSnapshot(*frame);
            const auto renderResult = m_Impl->RenderFrame(*frame, snapshot);

            m_Impl->FinishFrame(*frame, renderResult);
        }

        RequestExit();
    }

    void Engine::RequestExit() noexcept
    {
        m_Impl->TransitionTo(EngineState::ShuttingDown);
    }

    void Engine::Shutdown()
    {
        if (m_Impl->State() == EngineState::Stopped)
            return;

        if (m_Impl->State() != EngineState::ShuttingDown)
        {
            if (!m_Impl->TransitionTo(EngineState::ShuttingDown))
                return;
        }

        // Ressourcen in umgekehrter Reihenfolge freigeben.

        m_Impl->TransitionTo(EngineState::Stopped);
    }

    EngineDiagnosticsSnapshot Engine::GetDiagnostics() const noexcept
    {
        return m_Impl->m_Diagnostics;
    }

    ObservabilityConfig Engine::GetObservabilityConfig() const noexcept
    {
        return m_Impl->m_Observability;
    }

    void Engine::ApplyObservabilityConfig(ObservabilityConfig config) const
    {
        m_Impl->m_Observability = config;
    }
}
