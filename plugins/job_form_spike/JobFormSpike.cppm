// ARCH-022 S3b: the job form spike. Coherent point drift (the CPU reference solver, Geometry.Registration.
// CoherentPointDrift) as a step run: it moves a source cloud onto a target cloud one EM iteration at a time, shows the
// moving source in the viewport, and runs, pauses, steps, stops, accepts or discards from its window or over MCP
// (job_form_spike_start, then run_control and run_observe). Accept writes the source's positions with one undo entry.
// Kept or deleted per the S3 spike report.
module;

#include <cstdint>
#include <string>

export module Intrinsic.Plugin.JobFormSpike;

export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.PluginContract;

export namespace Intrinsic::Plugins::JobFormSpike
{
    namespace R = Extrinsic::Runtime;

    // shortcut: both clouds are registered in their own object space (their entity transforms are ignored); fine for
    // the spike's question (the job form), wrong for clouds placed apart in the world. Use the editor's world-space
    // capture if the spike becomes the CPD plugin.
    struct Request
    {
        std::uint32_t Source{}, Target{}; // stable entity ids; the source moves
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        std::uint32_t MaxIterations{150u};
        double Tolerance{1.0e-5};
        bool AcceptOnConvergence{false};
        bool Preview{true}; // the moving source in the viewport (needs an operational device)
        std::uint32_t HoldStepMs{0u}; // test hook: every step waits this long first (a step in flight on demand)
    };

    // Main thread, in the plugin's context: captures both clouds and starts a ready run (rigid CPD, reference E-step).
    // Its id, or 0 with the reason.
    [[nodiscard]] std::uint64_t StartRun(const Request& request, std::string& refusal);
    // The run started last (from the window or over MCP); the window controls it. 0 before the first.
    [[nodiscard]] std::uint64_t LastRun() noexcept;

    void AddTools(R::PluginRegistrations& staged);
    void AddWindow(R::PluginRegistrations& staged);
    void ShutdownWindow() noexcept;

    bool Register(R::PluginRegistrations& staged, std::string& error) noexcept;
    void Shutdown() noexcept;
}
