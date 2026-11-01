// ARCH-022 slice 2, S1 and S2: the spike plugin. Built against the host's module BMIs, linked to nothing of the
// engine: every engine, ImGui and ImPlot symbol binds to the running Sandbox at dlopen.
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <string>
#include <string_view>

#include <imgui.h>
#include <implot.h>
#include <nlohmann/json.hpp>

// Generated in the build directory (CMake writes the default; the S2 driver rewrites it to make a new build):
// SPIKE_GENERATION, the build's version; SPIKE_FAIL_REGISTER, a build whose Register stages and then fails;
// SPIKE_MISSING_SYMBOL (optional), a build that needs a symbol the host does not export, so dlopen fails;
// SPIKE_CLAMP_MARKER (optional), a build whose validator clamps marker_size above 10 with a fallback;
// SPIKE_EMPTY_HISTORY_COMMAND (optional), a build that stages a second history command without a handler;
// SPIKE_PAD (optional), a build 16 MiB larger, so it cannot be mapped into a smaller build's freed range.
#include "SpikeVersion.inc"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.UiHostTypes;
import Extrinsic.Sandbox.PluginSpike;

namespace
{
    namespace Spike = Extrinsic::Sandbox::Spike;
    namespace Config = Extrinsic::Core::Config;
    using Extrinsic::Runtime::AgentOperationContext;
    using Extrinsic::Runtime::AgentOperationOutcome;
    using Json = nlohmann::json;

#if defined(SPIKE_PAD) && SPIKE_PAD
    [[gnu::used]] const char g_Pad[16u << 20u] = {1};
#endif

    struct Lifecycle
    {
        Lifecycle() noexcept { spike_lifecycle_event("static_construct"); }
        ~Lifecycle() { spike_lifecycle_event("static_destruct"); }
    };
    const Lifecycle g_Lifecycle{};

    // Captured by every callable handed to the host: reports each copy and destruction, and each call, so the
    // host can refuse to close this image while any of them is alive.
    class Tracked
    {
    public:
        explicit Tracked(const char* kind) noexcept : m_Kind(kind) { spike_callable(SPIKE_GENERATION, m_Kind, +1); }
        Tracked(const Tracked& other) noexcept : m_Kind(other.m_Kind) { spike_callable(SPIKE_GENERATION, m_Kind, +1); }
        Tracked& operator=(const Tracked&) = delete;
        ~Tracked() { spike_callable(SPIKE_GENERATION, m_Kind, -1); }
        void Invoked() const noexcept { spike_invoked(SPIKE_GENERATION, m_Kind); }

    private:
        const char* m_Kind;
    };

    int g_MarkerSize = 8;
    int g_ChangeCount = 0;
    bool g_FirstDrawSeen = false;

    template <typename T>
    std::uintptr_t Address(T* pointer) { return reinterpret_cast<std::uintptr_t>(pointer); }

    // The section's only field: {"marker_size": N}, N an integer in [1, 64]; anything else is invalid.
    std::optional<int> MarkerSize(std::string_view payload)
    {
        const Json value = Json::parse(payload, nullptr, false);
        if (!value.is_object() || value.size() != 1 || !value.contains("marker_size")) return std::nullopt;
        const Json& size = value["marker_size"];
        if (!size.is_number_integer()) return std::nullopt;
        const auto n = size.get<std::int64_t>();
        if (n < 1 || n > 64) return std::nullopt;
        return static_cast<int>(n);
    }

    Config::EngineConfigSectionValidationResult Validate(std::string_view document)
    {
        const auto size = MarkerSize(document);
        if (!size) return {.State = Config::EngineConfigState::Invalid};
#if defined(SPIKE_CLAMP_MARKER) && SPIKE_CLAMP_MARKER
        if (*size > 10)
            return {.State = Config::EngineConfigState::FallbackApplied,
                    .CanonicalPayloadJson = Json{{"marker_size", 10}}.dump(),
                    .ParsedFieldCount = 1u};
#endif
        return {.State = Config::EngineConfigState::Valid,
                .CanonicalPayloadJson = Json{{"marker_size", *size}}.dump(),
                .ParsedFieldCount = 1u};
    }

    void OnChanged(const Config::EngineConfigSection& current)
    {
        if (const auto size = MarkerSize(current.PayloadJson)) g_MarkerSize = *size;
        ++g_ChangeCount;
        spike_lifecycle_event("config_changed");
    }

    // Mutates the host's probes through imported code and reports what the plugin sees.
    AgentOperationOutcome Identity()
    {
        ++Spike::HostCounter();
        ++Spike::HostInlineStatic();
        ++Spike::HostThreadLocal();
        ImGuiMemAllocFunc alloc = nullptr;
        ImGuiMemFreeFunc free = nullptr;
        void* userData = nullptr;
        ImGui::GetAllocatorFunctions(&alloc, &free, &userData);
        const Json value = {
            {"counter", {{"address", Address(&Spike::HostCounter())}, {"value", Spike::HostCounter()}}},
            {"inline_static", {{"address", Address(&Spike::HostInlineStatic())}, {"value", Spike::HostInlineStatic()}}},
            {"thread_local", {{"address", Address(&Spike::HostThreadLocal())}, {"value", Spike::HostThreadLocal()}}},
            {"imgui_context", Address(ImGui::GetCurrentContext())},
            {"imgui_alloc", reinterpret_cast<std::uintptr_t>(alloc)},
            {"imgui_free", reinterpret_cast<std::uintptr_t>(free)},
            {"imgui_alloc_user_data", Address(userData)},
            {"implot_context", Address(ImPlot::GetCurrentContext())},
            {"marker_size", g_MarkerSize},
            {"config_changes", g_ChangeCount},
        };
        return {.Text = value.dump()};
    }

    // spike_counter_add {delta, form}: an undoable edit of host state. The command form records data for the
    // registered history command; the closure form records this image's closures, as today's entries do.
    // A delta within [-kMaxUndoDelta, kMaxUndoDelta]; anything else (including integers too large to read) is not.
    std::optional<std::int64_t> Delta(const Json& value)
    {
        if (!value.is_number_integer()) return std::nullopt;
        if (value.is_number_unsigned() && value.get<std::uint64_t>() > static_cast<std::uint64_t>(Spike::kMaxUndoDelta))
            return std::nullopt;
        const auto delta = value.get<std::int64_t>();
        if (delta < -Spike::kMaxUndoDelta || delta > Spike::kMaxUndoDelta) return std::nullopt;
        return delta;
    }

    AgentOperationOutcome CounterAdd(const AgentOperationContext& context, std::string_view argumentsJson)
    {
        const Json arguments = Json::parse(argumentsJson, nullptr, false);
        const auto parsed = arguments.is_object() && arguments.contains("delta") ? Delta(arguments["delta"]) : std::nullopt;
        if (!parsed)
            return {.IsError = true, .Text = "delta must be an integer within +-1000000", .ErrorCode = "invalid_params"};
        const std::int64_t delta = *parsed;
        const bool closure = arguments.value("form", std::string{"command"}) == "closure";
        const std::string label = "Spike add " + std::to_string(delta) + (closure ? " (closure)" : "");
        const bool recorded = closure
            ? Spike::RecordClosureCommand(context, label,
                  [delta] { Spike::HostUndoValue() += delta; return true; },
                  [delta] { Spike::HostUndoValue() -= delta; return true; })
            : Spike::RecordCommand(context, label, "spike.add", std::to_string(delta));
        if (!recorded) return {.IsError = true, .Text = "the edit was not recorded", .ErrorCode = "not_recorded"};
        return {.Text = Json{{"value", Spike::HostUndoValue()}}.dump()};
    }

    // spike_start_job {seconds, ignore_cancel}: a CPU job of the plugin that runs for `seconds` (or until cancelled,
    // unless it ignores cancellation); its completion logs whether it was published.
    AgentOperationOutcome StartJob(const AgentOperationContext& context, std::string_view argumentsJson)
    {
        const Json arguments = Json::parse(argumentsJson, nullptr, false);
        const double seconds = arguments.is_object() ? arguments.value("seconds", 1.0) : 1.0;
        const bool ignoreCancel = arguments.is_object() && arguments.value("ignore_cancel", false);
        const bool submitted = Spike::SubmitPluginJob(context, {
            .Work = [t = Tracked{"job_work"}, seconds, ignoreCancel](const std::function<bool()>& cancelled)
            {
                t.Invoked();
                const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
                while (std::chrono::steady_clock::now() < end && (ignoreCancel || !cancelled()))
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            },
            .Completion = [t = Tracked{"job_completion"}](bool published)
            {
                t.Invoked();
                spike_lifecycle_event(published ? "job_published" : "job_finalized");
            },
        });
        if (!submitted) return {.IsError = true, .Text = "the job was not submitted", .ErrorCode = "not_submitted"};
        return {.Text = R"({"submitted":true})"};
    }

    // spike_gpu_work {frames}: fills a slice of the plugin's own buffer in each of the next `frames` recorded frames,
    // one slice per frame in flight, so no two unretired frames write the same bytes. Its shutdown logs the last
    // recorded frame, the current frame and the frames in flight, so the driver can check the retirement itself.
    AgentOperationOutcome GpuWork(const AgentOperationContext& context, std::string_view argumentsJson)
    {
        constexpr std::uint64_t kSlice = 64u << 10u;
        const Json arguments = Json::parse(argumentsJson, nullptr, false);
        const auto frames = std::make_shared<std::int64_t>(
            arguments.is_object() ? arguments.value("frames", std::int64_t{1} << 40) : std::int64_t{1} << 40);
        auto* device = Spike::Services().Find<Extrinsic::RHI::IDevice>();
        if (device == nullptr)
            return {.IsError = true, .Text = "no RHI device", .ErrorCode = "no_device"};
        const auto slots = device->GetFramesInFlight();
        const auto buffer = device->CreateBuffer({.SizeBytes = kSlice * slots,
                                                  .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
                                                  .DebugName = "spike plugin buffer"});
        auto last = std::make_shared<std::optional<std::uint64_t>>(); // the last recorded frame, if any
        const bool started = Spike::StartPluginGpuWork(context, {
            .DebugName = "spike plugin GPU work",
            .RecordFrameCommands = [t = Tracked{"gpu_record"}, device, buffer, slots, last, frames](
                                       Extrinsic::RHI::ICommandContext& commands)
            {
                if (*frames <= 0) return;
                --*frames;
                t.Invoked();
                const std::uint64_t frame = device->GetGlobalFrameNumber();
                commands.FillBuffer(buffer, (frame % slots) * kSlice, kSlice, 0x5eedu);
                *last = frame;
            },
            .HasInFlightWork = [t = Tracked{"gpu_in_flight"}, device, last]
            {
                t.Invoked();
                // The cache's rule: a frame has retired once FramesInFlight + 1 newer frames began.
                return last->has_value() && device->GetGlobalFrameNumber() < **last + device->GetFramesInFlight() + 1u;
            },
            .ShutdownAfterDeviceIdle = [t = Tracked{"gpu_shutdown"}, device, buffer, last]
            {
                t.Invoked();
                const std::string frames = "gpu_frames:" + std::to_string(last->value_or(0)) + ":" +
                    std::to_string(device->GetGlobalFrameNumber()) + ":" + std::to_string(device->GetFramesInFlight());
                spike_lifecycle_event(frames.c_str());
                device->DestroyBuffer(buffer);
                spike_lifecycle_event("gpu_shutdown");
            },
        });
        if (!started)
        {
            device->DestroyBuffer(buffer);
            return {.IsError = true, .Text = "the GPU work was not started", .ErrorCode = "not_started"};
        }
        return {.Text = R"({"started":true})"};
    }

    void DrawWindow(bool& open)
    {
        // Auto-resize: the window follows marker_size changes instead of clipping the marker.
        if (ImGui::Begin("Spike plugin###view.spike", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!g_FirstDrawSeen)
            {
                g_FirstDrawSeen = true;
                spike_lifecycle_event("first_draw");
            }
            // The marker the driver looks for in a window capture: pure magenta, marker_size * 8 pixels square.
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float side = static_cast<float>(g_MarkerSize * 8);
            ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side),
                                                      IM_COL32(255, 0, 255, 255));
            ImGui::Dummy(ImVec2(side, side));
        }
        ImGui::End();
    }
}

#if defined(SPIKE_MISSING_SYMBOL) && SPIKE_MISSING_SYMBOL
extern "C" void spike_symbol_the_host_does_not_export() noexcept;
#endif

extern "C" bool intrinsic_plugin_register_v0(Spike::StagedRegistrations& staged, std::string& error) noexcept
{
#if defined(SPIKE_MISSING_SYMBOL) && SPIKE_MISSING_SYMBOL
    spike_symbol_the_host_does_not_export(); // never reached: RTLD_NOW refuses the image first
#endif
    spike_lifecycle_event("register");
    staged.Generation = SPIKE_GENERATION;
    staged.ConfigSections.push_back({
        .DefaultSection = {.Name = "spike", .SchemaId = "intrinsic.spike", .SchemaVersion = 1u,
                           .PayloadJson = R"({"marker_size":8})"},
        .Validate = [t = Tracked{"validator"}](std::string_view document, std::string_view, std::string_view)
        {
            t.Invoked();
            return Validate(document);
        },
        .OnChanged = [t = Tracked{"change_callback"}](const Config::EngineConfigSection&,
                                                       const Config::EngineConfigSection& current)
        {
            t.Invoked();
            OnChanged(current);
        },
        .SchemaJson = R"({"type":"object","properties":{"marker_size":{"type":"integer","minimum":1,"maximum":64}},"additionalProperties":false})",
    });
    staged.Operations.push_back({
        .Name = "spike_echo",
        .Title = "Echo (spike plugin)",
        .Description = "Returns its arguments; proves a plugin tool is callable over MCP.",
        .InputSchemaJson = R"({"type":"object","properties":{"text":{"type":"string"}},"additionalProperties":false})",
        .Invoke = [t = Tracked{"operation"}](const AgentOperationContext&, std::string_view arguments)
        {
            t.Invoked();
            return AgentOperationOutcome{.Text = Json{{"echo", Json::parse(arguments, nullptr, false)}}.dump()};
        },
    });
    staged.Operations.push_back({
        .Name = "spike_identity",
        .Title = "Identity probes (spike plugin)",
        .Description = "Mutates the host's probes through imported code and reports what the plugin sees.",
        .Invoke = [t = Tracked{"operation"}](const AgentOperationContext&, std::string_view)
        {
            t.Invoked();
            return Identity();
        },
    });
    staged.Operations.push_back({
        .Name = "spike_version",
        .Title = "Version (spike plugin)",
        .Description = "The plugin build's generation (SpikeVersion.inc).",
        .Invoke = [t = Tracked{"operation"}](const AgentOperationContext&, std::string_view)
        {
            t.Invoked();
            return AgentOperationOutcome{.Text = Json{{"generation", SPIKE_GENERATION}}.dump()};
        },
    });
    staged.Operations.push_back({
        .Name = "spike_counter_add",
        .Title = "Undoable add (spike plugin)",
        .Description = "Adds delta to the host's undo value as an undoable edit; form command (default) or closure.",
        .InputSchemaJson = R"({"type":"object","properties":{"delta":{"type":"integer"},"form":{"type":"string","enum":["command","closure"]}},"required":["delta"],"additionalProperties":false})",
        .ReadOnly = false,
        .Invoke = [t = Tracked{"operation"}](const AgentOperationContext& context, std::string_view arguments)
        {
            t.Invoked();
            return CounterAdd(context, arguments);
        },
    });
    staged.Operations.push_back({
        .Name = "spike_start_job",
        .Title = "CPU job (spike plugin)",
        .Description = "Submits a plugin job that runs for seconds, or until cancelled unless ignore_cancel.",
        .InputSchemaJson = R"({"type":"object","properties":{"seconds":{"type":"number"},"ignore_cancel":{"type":"boolean"}},"additionalProperties":false})",
        .ReadOnly = false,
        .Invoke = [t = Tracked{"operation"}](const AgentOperationContext& context, std::string_view arguments)
        {
            t.Invoked();
            return StartJob(context, arguments);
        },
    });
    staged.Operations.push_back({
        .Name = "spike_gpu_work",
        .Title = "GPU work (spike plugin)",
        .Description = "Fills the plugin's own buffer in each of the next frames recorded frames (default: until it "
                       "unloads).",
        .InputSchemaJson = R"({"type":"object","properties":{"frames":{"type":"integer"}},"additionalProperties":false})",
        .ReadOnly = false,
        .Invoke = [t = Tracked{"operation"}](const AgentOperationContext& context, std::string_view arguments)
        {
            t.Invoked();
            return GpuWork(context, arguments);
        },
    });
    staged.HistoryCommands.push_back({
        .Name = "spike.add",
        .Apply = [t = Tracked{"history_handler"}](std::string_view payload, bool undo)
        {
            t.Invoked();
            const Json delta = Json::parse(payload, nullptr, false);
            const auto bounded = Delta(delta);
            if (!bounded) return false;
            Spike::HostUndoValue() += undo ? -*bounded : *bounded;
            return true;
        },
    });
#if defined(SPIKE_EMPTY_HISTORY_COMMAND) && SPIKE_EMPTY_HISTORY_COMMAND
    staged.HistoryCommands.push_back({.Name = "spike.empty"});
#endif
    staged.Windows.push_back({
        .Id = "view.spike",
        .MenuPath = {"View"},
        .Title = "Spike plugin",
        .OpenByDefault = true,
        .Draw = [t = Tracked{"window_draw"}](bool& open)
        {
            t.Invoked();
            DrawWindow(open);
        },
        .OpenStateChanged = [t = Tracked{"window_state"}](bool) { t.Invoked(); },
    });
#if SPIKE_FAIL_REGISTER
    error = "this build's Register stages everything and then fails";
    return false;
#else
    (void)error;
    return true;
#endif
}

extern "C" void intrinsic_plugin_shutdown_v0() noexcept { spike_lifecycle_event("shutdown"); }
