// ARCH-022 slice 2, S1: the spike plugin. Built against the host's module BMIs, linked to nothing of the
// engine: every engine, ImGui and ImPlot symbol binds to the running Sandbox at dlopen.
#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <imgui.h>
#include <implot.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Sandbox.PluginSpike;

namespace
{
    namespace Spike = Extrinsic::Sandbox::Spike;
    namespace Config = Extrinsic::Core::Config;
    using Extrinsic::Runtime::AgentOperationContext;
    using Extrinsic::Runtime::AgentOperationOutcome;

    struct Lifecycle
    {
        Lifecycle() noexcept { spike_lifecycle_event("static_construct"); }
        ~Lifecycle() { spike_lifecycle_event("static_destruct"); }
    };
    const Lifecycle g_Lifecycle{};

    int g_MarkerSize = 8;
    int g_ChangeCount = 0;
    bool g_FirstDrawSeen = false;

    std::string Address(const void* pointer) { return std::format("\"{}\"", pointer); }

    template <typename Fn>
    std::string FunctionAddress(Fn function)
    {
        return Address(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(function)));
    }

    // Reads {"marker_size": N} with N in [1, 64]; the only field of the section.
    bool ParseMarkerSize(std::string_view payload, int& size)
    {
        const auto key = payload.find("\"marker_size\"");
        if (key == std::string_view::npos) return false;
        auto start = payload.find_first_of("0123456789", key + 13);
        if (start == std::string_view::npos) return false;
        const auto [end, error] = std::from_chars(payload.data() + start, payload.data() + payload.size(), size);
        return error == std::errc{} && size >= 1 && size <= 64;
    }

    Config::EngineConfigSectionValidationResult Validate(std::string_view document, std::string_view, std::string_view)
    {
        int size = 0;
        if (!ParseMarkerSize(document, size)) return {.State = Config::EngineConfigState::Invalid};
        return {.State = Config::EngineConfigState::Valid,
                .CanonicalPayloadJson = std::format(R"({{"marker_size":{}}})", size),
                .ParsedFieldCount = 1u};
    }

    void OnChanged(const Config::EngineConfigSection&, const Config::EngineConfigSection& current)
    {
        int size = 0;
        if (ParseMarkerSize(current.PayloadJson, size)) g_MarkerSize = size;
        ++g_ChangeCount;
        spike_lifecycle_event("config_changed");
    }

    // Mutates the host's probes through imported code and reports what the plugin sees.
    AgentOperationOutcome Identity(const AgentOperationContext&, std::string_view)
    {
        ++Spike::HostCounter();
        ++Spike::HostInlineStatic();
        ++Spike::HostThreadLocal();
        ImGuiMemAllocFunc alloc = nullptr;
        ImGuiMemFreeFunc free = nullptr;
        void* userData = nullptr;
        ImGui::GetAllocatorFunctions(&alloc, &free, &userData);
        return {.Text = std::format(
            R"({{"counter":{{"address":{},"value":{}}},"inline_static":{{"address":{},"value":{}}},)"
            R"("thread_local":{{"address":{},"value":{}}},"imgui_context":{},"imgui_alloc":{},"imgui_free":{},)"
            R"("imgui_alloc_user_data":{},"implot_context":{},"marker_size":{},"config_changes":{}}})",
            Address(&Spike::HostCounter()), Spike::HostCounter(), Address(&Spike::HostInlineStatic()),
            Spike::HostInlineStatic(), Address(&Spike::HostThreadLocal()), Spike::HostThreadLocal(),
            Address(ImGui::GetCurrentContext()), FunctionAddress(alloc), FunctionAddress(free), Address(userData),
            Address(ImPlot::GetCurrentContext()), g_MarkerSize, g_ChangeCount)};
    }

    void DrawWindow(bool& open)
    {
        if (ImGui::Begin("Spike plugin###view.spike", &open))
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

extern "C" bool intrinsic_plugin_register_v0(const Spike::PluginHostContext& host, Spike::StagedRegistrations& staged,
                                             std::string& error) noexcept
{
    spike_lifecycle_event("register");
    if (host.ImGuiContext != ImGui::GetCurrentContext())
    {
        error = "the plugin sees another ImGui context than the host";
        return false;
    }
    staged.ConfigSections.push_back({
        .DefaultSection = {.Name = "spike", .SchemaId = "intrinsic.spike", .SchemaVersion = 1u,
                           .PayloadJson = R"({"marker_size":8})"},
        .Validate = Validate,
        .OnChanged = OnChanged,
        .SchemaJson = R"({"type":"object","properties":{"marker_size":{"type":"integer","minimum":1,"maximum":64}},"additionalProperties":false})",
    });
    staged.Operations.push_back({
        .Name = "spike_echo",
        .Title = "Echo (spike plugin)",
        .Description = "Returns its arguments; proves a plugin tool is callable over MCP.",
        .InputSchemaJson = R"({"type":"object","properties":{"text":{"type":"string"}},"additionalProperties":false})",
        .Invoke = [](const AgentOperationContext&, std::string_view arguments)
        { return AgentOperationOutcome{.Text = std::format(R"({{"echo":{}}})", arguments)}; },
    });
    staged.Operations.push_back({
        .Name = "spike_identity",
        .Title = "Identity probes (spike plugin)",
        .Description = "Mutates the host's probes through imported code and reports what the plugin sees.",
        .Invoke = Identity,
    });
    staged.Windows.push_back({
        .Id = "view.spike",
        .MenuPath = {"View"},
        .Title = "Spike plugin",
        .OpenByDefault = true,
        .Draw = DrawWindow,
    });
    return true;
}

extern "C" void intrinsic_plugin_shutdown_v0() noexcept { spike_lifecycle_event("shutdown"); }
