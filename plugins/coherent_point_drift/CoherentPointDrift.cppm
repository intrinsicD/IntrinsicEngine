// ARCH-022: coherent point drift as a step run (the job form). The CPU reference solver (Geometry.Registration.
// CoherentPointDrift, rigid) moves a source cloud onto a target cloud in world space, one EM iteration at a time,
// shows the moving source in the viewport, and runs, pauses, steps, stops, restarts, accepts or discards from its window
// (the host's shared step-run row) or over MCP (coherent_point_drift_start, then run_control and run_observe). Its
// execution options are the shared StepRunExecution (max_iterations, preview_every, preview_interval_ms, auto_accept,
// start; default start ready). Accept writes the source's positions, mapped back
// into its local frame, with one undo entry. It runs beside the editor's built-in CPD until that feature migrates.
module;

#include <cstdint>
#include <string>

export module Intrinsic.Plugin.CoherentPointDrift;

export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.PluginContract;

export namespace Intrinsic::Plugins::CoherentPointDrift
{
    namespace R = Extrinsic::Runtime;

    inline constexpr std::uint32_t kMaxIterations = 100'000u; // the run's iteration cap (one EM iteration a step)

    // Both clouds unparented (each mapped by its local Transform, as the editor's CPD does); the source's model matrix
    // invertible.
    struct Request
    {
        std::uint32_t Source{}, Target{}; // stable entity ids; the source moves
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::Unknown,
            .Name = "v:position",
            .ValueKind = Geometry::PropertyValueKind::Vec3};
        double Tolerance{1.0e-5};
        // The shared execution options; live preview shows the moving source in the viewport (needs an operational
        // device). A run starts ready by default.
        R::StepRunExecution Execution{.IterationCap = 150u,
            .PreviewEvery = 1u,
            .PreviewIntervalMs = 33u,
            .AutoAccept = R::StepRunAutoAccept::Never,
            .Start = R::StepRunStart::Ready};
        // shortcut: a test hook in the public tool schema (a step in flight on demand); gate it on --plugin-test-hooks
        // once a plugin can see that flag.
        std::uint32_t HoldStepMs{0u}; // every step waits this long first
    };

    // Main thread, in the plugin's context: captures both clouds and starts a run (rigid CPD, reference E-step) as its
    // execution options say. Its id, or 0 with the reason.
    [[nodiscard]] std::uint64_t StartRun(const Request& request, std::string& refusal);
    // The run started last (from the window or over MCP); the window controls it. 0 before the first.
    [[nodiscard]] std::uint64_t LastRun() noexcept;

    void AddTools(R::PluginRegistrations& staged);
    void AddWindow(R::PluginRegistrations& staged);
    void ShutdownWindow() noexcept;

    bool Register(R::PluginRegistrations& staged, std::string& error) noexcept;
    void Shutdown() noexcept;
}
