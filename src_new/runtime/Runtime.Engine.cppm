module;

#include <memory>

export module Runtime.Engine;

import Core.Log;

namespace Extrinsic::Runtime
{
    enum class EngineState
    {
        Constructed,
        Initialized,
        Running,
        ShuttingDown,
        Stopped
    };

    export struct ObservabilityConfig
    {
        Core::Log::Level MinimumLogLevel{Core::Log::Level::Info};
        bool LogStateTransitions{false};
    };

    export struct EngineConfig
    {
        double FixedStepSeconds{1.0 / 60.0};
        double MaxFrameDeltaSeconds{0.25};

        PlatformBackend Platform{PlatformBackend::Auto};
        GraphicsBackend Graphics{GraphicsBackend::Null};
        ObservabilityConfig Observability{};
    };

    export struct StateTransition
    {
        EngineState From{};
        EngineState To{};
        std::uint64_t Sequence{};
    };

    export struct InvalidStateTransition
    {
        EngineState From{};
        EngineState Requested{};
        std::uint64_t Sequence{};
    };

    export struct EngineDiagnosticsSnapshot
    {
        EngineState State{EngineState::Constructed};

        std::uint64_t FrameIndex{};
        std::uint64_t StateTransitionCount{};

        double LastFrameSeconds{};
        double RunElapsedSeconds{};
        double SimulationAccumulatorSeconds{};

        std::optional<StateTransition> LastTransition;
        std::uint64_t InvalidStateTransitionCount{};
        std::optional<InvalidStateTransition> LastInvalidTransition;
    };

    export class Engine
    {
    public:
        explicit Engine(EngineConfig config);
        ~Engine();

        void Initialize();
        void Run();
        void RequestExit() noexcept;
        void Shutdown();
        [[nodiscard]]
        EngineDiagnosticsSnapshot GetDiagnostics() const;
        void ApplyObservabilityConfig(ObservabilityConfig config);

        [[nodiscard]]
        ObservabilityConfig GetObservabilityConfig() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
