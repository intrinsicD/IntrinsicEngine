// ARCH-022 slice 2, S1: the spike plugin. Built against the host's module BMIs, linked to nothing of the
// engine: every engine, ImGui and ImPlot symbol binds to the running Sandbox at dlopen.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>
#include <implot.h>
#include <nlohmann/json.hpp>

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
    using Json = nlohmann::json;

    struct Lifecycle
    {
        Lifecycle() noexcept { spike_lifecycle_event("static_construct"); }
        ~Lifecycle() { spike_lifecycle_event("static_destruct"); }
    };
    const Lifecycle g_Lifecycle{};

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

    Config::EngineConfigSectionValidationResult Validate(std::string_view document, std::string_view, std::string_view)
    {
        const auto size = MarkerSize(document);
        if (!size) return {.State = Config::EngineConfigState::Invalid};
        return {.State = Config::EngineConfigState::Valid,
                .CanonicalPayloadJson = Json{{"marker_size", *size}}.dump(),
                .ParsedFieldCount = 1u};
    }

    void OnChanged(const Config::EngineConfigSection&, const Config::EngineConfigSection& current)
    {
        if (const auto size = MarkerSize(current.PayloadJson)) g_MarkerSize = *size;
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

extern "C" bool intrinsic_plugin_register_v0(Spike::StagedRegistrations& staged, std::string&) noexcept
{
    spike_lifecycle_event("register");
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
        { return AgentOperationOutcome{.Text = Json{{"echo", Json::parse(arguments, nullptr, false)}}.dump()}; },
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
