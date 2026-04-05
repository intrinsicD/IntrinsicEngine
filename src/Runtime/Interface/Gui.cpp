module;

// 1. Force ImGui to expose Dynamic Rendering fields
#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING

#include <cassert>
#include "RHI.Vulkan.hpp"

#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h> // Required for glfwGetInstanceProcAddress
#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <filesystem>

// Add this line to manually declare the missing function if it's not in the header but present in the object file (unlikely after 2025 change)
// OR, more likely, we need to use ImGui_ImplVulkan_AddTexture manually for fonts if the automatic system fails.
// But the stack trace shows automatic system is TRYING to work (ImGui_ImplVulkan_UpdateTexture).

module Interface:GUI.Impl;
import :GUI;
import Core.Logging;
import Core.Filesystem;
import Core.Telemetry;
import Core.Window;
import RHI.Device;
import RHI.Swapchain;

namespace Interface::GUI
{
    struct RegisteredPanel
    {
        std::string Name;
        UIPanelCallback Callback;
        bool IsClosable = true;
        bool IsOpen = true; // Track state
        int Flags = 0;
    };

    struct RegisteredMenu
    {
        std::string Name;
        UIMenuCallback Callback;
    };

    struct RegisteredOverlay
    {
        std::string Name;
        UIOverlayCallback Callback;
    };

    static std::vector<RegisteredPanel> s_Panels;
    static std::vector<RegisteredMenu> s_Menus;
    static std::vector<RegisteredOverlay> s_Overlays;
    static VkDescriptorPool s_DescriptorPool = VK_NULL_HANDLE;
    static RHI::VulkanDevice* s_Device = nullptr;
    static bool s_ShowTelemetryPanel = false;
    static bool s_BackendInitialized = false;
    static bool s_FrameActive = false;
    static std::unordered_set<void*> s_RegisteredTextures;
    static Theme s_ActiveTheme = Theme::Dark;
    static bool s_DockLayoutBuilt = false;
    static float s_DpiScale = 1.0f;

    // Forward declarations for theme functions (defined below).
    static void ApplyDarkTheme();
    static void ApplyLightTheme();
    static void ApplyHighContrastTheme();

    template <typename T>
    static T* FindNamedItem(std::vector<T>& items, std::string_view name)
    {
        const auto it = std::ranges::find(items, name, &T::Name);
        return it != items.end() ? &*it : nullptr;
    }

    template <typename T>
    static void RemoveNamedItem(std::vector<T>& items, std::string_view name)
    {
        std::erase_if(items, [&](const T& item) { return item.Name == name; });
    }

    static float ToMs(uint64_t ns) { return static_cast<float>(static_cast<double>(ns) / 1'000'000.0); }

    struct RollingSloStats
    {
        float P95 = 0.0f;
        float P99 = 0.0f;
        size_t SampleCount = 0;
    };

    static RollingSloStats ComputeRollingNsPercentiles(const Core::Telemetry::TelemetrySystem& telemetry,
                                                       size_t window,
                                                       uint64_t Core::Telemetry::FrameStats::* field)
    {
        std::array<uint64_t, Core::Telemetry::TelemetrySystem::MAX_FRAME_HISTORY> samples{};
        const size_t clampedWindow = std::min(window, Core::Telemetry::TelemetrySystem::MAX_FRAME_HISTORY);
        size_t sampleCount = 0;
        for (size_t i = 0; i < clampedWindow; ++i)
        {
            const auto& frame = telemetry.GetFrameStats(i);
            const uint64_t value = frame.*field;
            if (value == 0)
            {
                continue;
            }
            samples[sampleCount++] = value;
        }

        if (sampleCount == 0)
        {
            return {};
        }

        auto percentileNs = [&](float q)
        {
            const size_t rank = std::min(sampleCount - 1,
                                         static_cast<size_t>(q * static_cast<float>(sampleCount - 1)));
            std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(rank), samples.begin() + static_cast<std::ptrdiff_t>(sampleCount));
            return samples[rank];
        };

        return {
            .P95 = ToMs(percentileNs(0.95f)),
            .P99 = ToMs(percentileNs(0.99f)),
            .SampleCount = sampleCount,
        };
    }

    static void DrawSloStatusRow(const char* label, float value, float budget, bool inBand = true)
    {
        const bool pass = inBand && (value <= budget);
        const ImVec4 color = pass ? ImVec4(0.20f, 0.80f, 0.25f, 1.0f) : ImVec4(0.95f, 0.25f, 0.20f, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
        ImGui::TableNextColumn(); ImGui::Text("%.3f", value);
        ImGui::TableNextColumn(); ImGui::Text("%.3f", budget);
        ImGui::TableNextColumn(); ImGui::TextColored(color, pass ? "PASS" : "ALERT");
    }

    // Simple horizontal bar for time in ms (clamped to a target window).
    static void DrawTimeBar(const char* label, float ms, float targetMs)
    {
        const float clamped = std::min(ms, targetMs);
        const float frac = (targetMs > 0.0f) ? (clamped / targetMs) : 0.0f;

        ImGui::TextUnformatted(label);
        ImGui::SameLine();

        // Reserve a nice wide bar.
        const float barWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x - 80.0f);
        const ImVec2 size(barWidth, 0.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
        ImGui::ProgressBar(frac, size, nullptr);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::Text("%.2f ms", ms);
    }

    // Helper to draw performance/telemetry panel
    static void DrawTelemetryPanel()
    {
        ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Performance", &s_ShowTelemetryPanel))
        {
            auto& telemetry = Core::Telemetry::TelemetrySystem::Get();
            const auto& stats = telemetry.GetFrameStats(0);

            const double avgMs = telemetry.GetAverageFrameTimeMs(60);
            const double avgFps = telemetry.GetAverageFPS(60);

            const float cpuMs = ToMs(stats.CpuTimeNs);
            const float gpuMs = ToMs(stats.GpuTimeNs);
            const float frameMs = ToMs(stats.FrameTimeNs);

            // -----------------------------------------------------------------
            // Header (big FPS + key counters)
            // -----------------------------------------------------------------
            {
                ImGui::PushFont(ImGui::GetFont());
                ImGui::Text("%.1f FPS", avgFps);
                ImGui::PopFont();

                ImGui::SameLine();
                ImGui::TextDisabled("(avg %.2f ms)", avgMs);

                ImGui::Separator();

                ImGui::TextDisabled("Frame #");
                ImGui::SameLine();
                ImGui::Text("%lu", stats.FrameNumber);

                ImGui::SameLine();
                ImGui::TextDisabled("  Draw");
                ImGui::SameLine();
                ImGui::Text("%u", stats.DrawCalls);

                ImGui::SameLine();
                ImGui::TextDisabled("  Tris");
                ImGui::SameLine();
                ImGui::Text("%u", stats.TriangleCount);
            }

            ImGui::Spacing();

            // -----------------------------------------------------------------
            // Frame budget bars (CPU/GPU)
            // -----------------------------------------------------------------
            {
                // Common budgets
                constexpr float kBudget60 = 16.6667f;
                constexpr float kBudget30 = 33.3333f;

                ImGui::TextDisabled("Frame Budget");

                // Use 16.6 ms as primary target, but clamp to 33ms for visibility.
                const float targetMs = kBudget60;
                const float clampMs = kBudget30;

                DrawTimeBar("CPU", std::min(cpuMs, clampMs), targetMs);
                DrawTimeBar("GPU", std::min(gpuMs, clampMs), targetMs);
                DrawTimeBar("Total", std::min(frameMs, clampMs), targetMs);
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // Graphs: CPU and GPU history
            // -----------------------------------------------------------------
            {
                static float cpuHistoryMs[120] = {};
                static float gpuHistoryMs[120] = {};
                static int historyIdx = 0;

                cpuHistoryMs[historyIdx] = cpuMs;
                gpuHistoryMs[historyIdx] = gpuMs;
                historyIdx = (historyIdx + 1) % 120;

                float maxMs = 33.3f;
                for (int i = 0; i < 120; ++i)
                {
                    maxMs = std::max(maxMs, cpuHistoryMs[i]);
                    maxMs = std::max(maxMs, gpuHistoryMs[i]);
                }

                ImGui::TextDisabled("CPU/GPU Frame Time (ms)");

                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.25f, 0.65f, 1.0f, 1.0f));
                ImGui::PlotLines("CPU", cpuHistoryMs, 120, historyIdx, nullptr, 0.0f, maxMs * 1.1f, ImVec2(0, 70));
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.90f, 0.55f, 0.15f, 1.0f));
                ImGui::PlotLines("GPU", gpuHistoryMs, 120, historyIdx, nullptr, 0.0f, maxMs * 1.1f, ImVec2(0, 70));
                ImGui::PopStyleColor();
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // Task scheduler parking telemetry
            // -----------------------------------------------------------------
            if (ImGui::TreeNodeEx("Task Scheduler Wait Metrics", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const float parkP50Us = static_cast<float>(stats.TaskParkP50Ns) / 1000.0f;
                const float parkP95Us = static_cast<float>(stats.TaskParkP95Ns) / 1000.0f;
                const float parkP99Us = static_cast<float>(stats.TaskParkP99Ns) / 1000.0f;
                const float unparkP50Us = static_cast<float>(stats.TaskUnparkP50Ns) / 1000.0f;
                const float unparkP95Us = static_cast<float>(stats.TaskUnparkP95Ns) / 1000.0f;
                const float unparkP99Us = static_cast<float>(stats.TaskUnparkP99Ns) / 1000.0f;
                const float idleWaitMs = static_cast<float>(stats.TaskIdleWaitTotalNs) / 1'000'000.0f;
                const float unparkTailSpreadUs = static_cast<float>(stats.TaskUnparkTailSpreadNs) / 1000.0f;

                ImGui::Text("Parks: %llu   Unparks: %llu",
                            static_cast<unsigned long long>(stats.TaskParkCount),
                            static_cast<unsigned long long>(stats.TaskUnparkCount));
                ImGui::Text("Steal Success Ratio: %.3f", static_cast<float>(stats.TaskStealSuccessRatio));
                ImGui::Text("Idle waits: %llu (%.3f ms total)",
                            static_cast<unsigned long long>(stats.TaskIdleWaitCount), idleWaitMs);
                ImGui::Text("Unpark tail spread (p99 - p50): %.2f us", unparkTailSpreadUs);
                ImGui::Text("Queue contention: %llu lock misses",
                            static_cast<unsigned long long>(stats.TaskQueueContentionCount));

                if (ImGui::BeginTable("TaskWaitLatencyTable", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                {
                    ImGui::TableSetupColumn("Operation");
                    ImGui::TableSetupColumn("p50 (us)");
                    ImGui::TableSetupColumn("p95 (us)");
                    ImGui::TableSetupColumn("p99 (us)");
                    ImGui::TableHeadersRow();

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted("park");
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", parkP50Us);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", parkP95Us);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", parkP99Us);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted("unpark");
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", unparkP50Us);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", unparkP95Us);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", unparkP99Us);

                    ImGui::EndTable();
                }

                ImGui::TreePop();
            }

            ImGui::Separator();

            if (ImGui::TreeNodeEx("FrameGraph Telemetry", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const float compileMs = ToMs(stats.FrameGraphCompileTimeNs);
                const float executeMs = ToMs(stats.FrameGraphExecuteTimeNs);
                const float criticalPathMs = ToMs(stats.FrameGraphCriticalPathTimeNs);
                ImGui::Text("Compile: %.3f ms", compileMs);
                ImGui::Text("Execute: %.3f ms", executeMs);
                ImGui::Text("Critical Path (sum of DAG chain): %.3f ms", criticalPathMs);
                ImGui::TreePop();
            }

            ImGui::Separator();

            if (ImGui::TreeNodeEx("Fixed-Step Simulation", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const float simulationCpuMs = ToMs(stats.SimulationCpuTimeNs);
                ImGui::Text("Ticks: %u", stats.SimulationTickCount);
                ImGui::Text("Accumulator clamp hits: %u", stats.SimulationClampHitCount);
                ImGui::Text("Simulation CPU: %.3f ms", simulationCpuMs);
                ImGui::TreePop();
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // GPU Memory Budget
            // -----------------------------------------------------------------
            if (ImGui::TreeNodeEx("GPU Memory", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto& mem = telemetry.GetGpuMemorySnapshot();
                if (mem.HeapCount == 0)
                {
                    ImGui::TextDisabled("No GPU memory data available.");
                }
                else
                {
                    if (!mem.HasBudgetExtension)
                        ImGui::TextDisabled("VK_EXT_memory_budget unavailable; showing VMA-tracked usage vs. heap size.");

                    // Helper: color-code a usage fraction (green < 70%, yellow < 85%, red >= 85%).
                    auto UsageColor = [](float pct) -> ImVec4 {
                        if (pct < 70.0f) return ImVec4(0.20f, 0.75f, 0.30f, 1.0f);
                        if (pct < 85.0f) return ImVec4(0.90f, 0.75f, 0.15f, 1.0f);
                        return ImVec4(0.95f, 0.25f, 0.20f, 1.0f);
                    };

                    // Compute device-local totals for the summary line.
                    uint64_t totalDeviceUsed = 0;
                    uint64_t totalDeviceBudget = 0;
                    for (uint32_t i = 0; i < mem.HeapCount; ++i)
                    {
                        if (mem.Heaps[i].Flags & Core::Telemetry::kHeapFlagDeviceLocal)
                        {
                            totalDeviceUsed += mem.Heaps[i].UsageBytes;
                            totalDeviceBudget += mem.Heaps[i].BudgetBytes;
                        }
                    }

                    if (totalDeviceBudget == 0)
                    {
                        ImGui::TextDisabled("No device-local heaps detected.");
                    }
                    else
                    {
                        const float usedMB = static_cast<float>(totalDeviceUsed) / (1024.0f * 1024.0f);
                        const float budgetMB = static_cast<float>(totalDeviceBudget) / (1024.0f * 1024.0f);
                        const float frac = static_cast<float>(static_cast<double>(totalDeviceUsed) / static_cast<double>(totalDeviceBudget));
                        const float pct = frac * 100.0f;

                        char overlay[64];
                        std::snprintf(overlay, sizeof(overlay), "%.0f / %.0f MB (%.1f%%)", usedMB, budgetMB, pct);
                        ImGui::TextUnformatted("Device-Local");
                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, UsageColor(pct));
                        ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), overlay);
                        ImGui::PopStyleColor();
                    }

                    // Per-heap table.
                    if (ImGui::BeginTable("GpuHeapTable", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Heap");
                        ImGui::TableSetupColumn("Type");
                        ImGui::TableSetupColumn("Used (MB)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                        ImGui::TableSetupColumn("Budget (MB)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                        ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableHeadersRow();

                        for (uint32_t i = 0; i < mem.HeapCount; ++i)
                        {
                            const auto& heap = mem.Heaps[i];
                            const float hUsedMB = static_cast<float>(heap.UsageBytes) / (1024.0f * 1024.0f);
                            const float hBudgetMB = static_cast<float>(heap.BudgetBytes) / (1024.0f * 1024.0f);
                            const float hFrac = (heap.BudgetBytes > 0)
                                ? static_cast<float>(static_cast<double>(heap.UsageBytes) / static_cast<double>(heap.BudgetBytes))
                                : 0.0f;
                            const float hPct = hFrac * 100.0f;

                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%u", i);

                            ImGui::TableNextColumn();
                            const bool isDeviceLocal = (heap.Flags & Core::Telemetry::kHeapFlagDeviceLocal) != 0;
                            ImGui::TextUnformatted(isDeviceLocal ? "Device" : "Host");

                            ImGui::TableNextColumn();
                            ImGui::Text("%.1f", hUsedMB);

                            ImGui::TableNextColumn();
                            ImGui::Text("%.1f", hBudgetMB);

                            ImGui::TableNextColumn();
                            char hOverlay[32];
                            std::snprintf(hOverlay, sizeof(hOverlay), "%.0f%%", hPct);
                            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, UsageColor(hPct));
                            ImGui::ProgressBar(hFrac, ImVec2(-1.0f, 0.0f), hOverlay);
                            ImGui::PopStyleColor();
                        }

                        ImGui::EndTable();
                    }
                }
                ImGui::TreePop();
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // Per-Pass GPU + CPU Timing Timeline
            // -----------------------------------------------------------------
            if (ImGui::TreeNodeEx("Render Pass Timings", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto& passTimings = telemetry.GetPassTimings();
                if (passTimings.empty())
                {
                    ImGui::TextDisabled("No per-pass timing data (GPU profiler may be unavailable).");
                }
                else
                {
                    if (ImGui::BeginTable("PassTimingTable", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 180.0f)))
                    {
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("GPU Bar", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableHeadersRow();

                        // Find max GPU time for bar scaling.
                        uint64_t maxGpuNs = 1;
                        for (const auto& pt : passTimings)
                            if (pt.GpuTimeNs > maxGpuNs) maxGpuNs = pt.GpuTimeNs;

                        for (const auto& pt : passTimings)
                        {
                            const float passGpuMs = static_cast<float>(pt.GpuTimeNs) / 1'000'000.0f;
                            const float passCpuMs = static_cast<float>(pt.CpuTimeNs) / 1'000'000.0f;
                            const float barFrac = static_cast<float>(pt.GpuTimeNs) / static_cast<float>(maxGpuNs);

                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(pt.Name.c_str());

                            ImGui::TableNextColumn();
                            ImGui::Text("%.3f", passGpuMs);

                            ImGui::TableNextColumn();
                            ImGui::Text("%.3f", passCpuMs);

                            ImGui::TableNextColumn();
                            ImGui::ProgressBar(barFrac, ImVec2(-1.0f, 0.0f), "");
                        }

                        ImGui::EndTable();
                    }
                }
                ImGui::TreePop();
            }

            ImGui::Separator();

            if (ImGui::TreeNodeEx("SLO Alerts (rolling p95/p99)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                constexpr size_t kWindowFrames = 120;
                constexpr float kFrameGraphCompileP99BudgetMs = 0.35f;
                constexpr float kFrameGraphExecuteP95BudgetMs = 1.50f;
                constexpr float kFrameGraphCriticalPathP95BudgetMs = 0.90f;
                constexpr float kTaskIdleWaitP95BudgetMs = 0.70f;
                constexpr float kTaskUnparkP99BudgetMs = 0.08f;

                constexpr float kStealRatioMin = 0.20f;
                constexpr float kStealRatioMax = 0.65f;

                const auto compileRolling = ComputeRollingNsPercentiles(
                    telemetry, kWindowFrames, &Core::Telemetry::FrameStats::FrameGraphCompileTimeNs);
                const auto executeRolling = ComputeRollingNsPercentiles(
                    telemetry, kWindowFrames, &Core::Telemetry::FrameStats::FrameGraphExecuteTimeNs);
                const auto criticalRolling = ComputeRollingNsPercentiles(
                    telemetry, kWindowFrames, &Core::Telemetry::FrameStats::FrameGraphCriticalPathTimeNs);
                const auto idleRolling = ComputeRollingNsPercentiles(
                    telemetry, kWindowFrames, &Core::Telemetry::FrameStats::TaskIdleWaitTotalNs);
                const auto unparkRolling = ComputeRollingNsPercentiles(
                    telemetry, kWindowFrames, &Core::Telemetry::FrameStats::TaskUnparkP99Ns);

                float stealRatioSum = 0.0f;
                size_t stealRatioSamples = 0;
                for (size_t i = 0; i < kWindowFrames; ++i)
                {
                    const double ratio = telemetry.GetFrameStats(i).TaskStealSuccessRatio;
                    if (ratio <= 0.0)
                    {
                        continue;
                    }
                    stealRatioSum += static_cast<float>(ratio);
                    ++stealRatioSamples;
                }
                const float stealRatioAvg = (stealRatioSamples > 0) ? (stealRatioSum / static_cast<float>(stealRatioSamples)) : 0.0f;

                ImGui::TextDisabled("Window: %zu frames", kWindowFrames);

                if (ImGui::BeginTable("SloTable", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                {
                    ImGui::TableSetupColumn("Metric");
                    ImGui::TableSetupColumn("Rolling Value (ms)");
                    ImGui::TableSetupColumn("Budget (ms)");
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableHeadersRow();

                    DrawSloStatusRow("FrameGraph compile p99", compileRolling.P99, kFrameGraphCompileP99BudgetMs);
                    DrawSloStatusRow("FrameGraph execute p95", executeRolling.P95, kFrameGraphExecuteP95BudgetMs);
                    DrawSloStatusRow("FrameGraph critical path p95", criticalRolling.P95, kFrameGraphCriticalPathP95BudgetMs);
                    DrawSloStatusRow("Task idle wait p95", idleRolling.P95, kTaskIdleWaitP95BudgetMs);
                    DrawSloStatusRow("Task unpark p99", unparkRolling.P99, kTaskUnparkP99BudgetMs);

                    const bool stealRatioInBand = (stealRatioAvg >= kStealRatioMin) && (stealRatioAvg <= kStealRatioMax);
                    const ImVec4 stealColor = stealRatioInBand ? ImVec4(0.20f, 0.80f, 0.25f, 1.0f) : ImVec4(0.95f, 0.25f, 0.20f, 1.0f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted("Task steal success ratio avg");
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", stealRatioAvg);
                    ImGui::TableNextColumn(); ImGui::Text("[%.2f, %.2f]", kStealRatioMin, kStealRatioMax);
                    ImGui::TableNextColumn(); ImGui::TextColored(stealColor, stealRatioInBand ? "PASS" : "ALERT");

                    ImGui::EndTable();
                }

                ImGui::TextDisabled("Samples used (compile/execute/critical/idle/unpark): %zu / %zu / %zu / %zu / %zu",
                                    compileRolling.SampleCount,
                                    executeRolling.SampleCount,
                                    criticalRolling.SampleCount,
                                    idleRolling.SampleCount,
                                    unparkRolling.SampleCount);

                ImGui::TreePop();
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // CPU timing categories
            // -----------------------------------------------------------------
            if (ImGui::TreeNodeEx("CPU Timing Breakdown", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static char filterBuf[64] = {};
                ImGui::TextDisabled("Filter");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputText("##TimingFilter", filterBuf, sizeof(filterBuf));

                auto categories = telemetry.GetCategoriesSortedByTime();

                // Show only the top N by default (avoids overwhelming the panel).
                static int topN = 20;
                ImGui::SameLine();
                ImGui::TextDisabled("Top");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::SliderInt("##TopN", &topN, 5, 50, "%d", ImGuiSliderFlags_AlwaysClamp);

                const bool hasFilter = filterBuf[0] != '\0';

                if (ImGui::BeginTable("TimingTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX,
                    ImVec2(0.0f, 160.0f)))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Total (ms)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                    ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("%Frame", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableHeadersRow();

                    int shown = 0;
                    for (const auto* cat : categories)
                    {
                        if (!cat || cat->CallCount == 0) continue;

                        const char* name = cat->Name ? cat->Name : "<unnamed>";
                        if (hasFilter)
                        {
                            if (strstr(name, filterBuf) == nullptr)
                                continue;
                        }

                        if (!hasFilter && shown >= topN)
                            break;

                        const float totalMs = static_cast<float>(cat->TotalMs());
                        const float avgCatMs = static_cast<float>(cat->AverageMs());
                        const float pctFrame = (cpuMs > 0.0f) ? (totalMs / cpuMs) * 100.0f : 0.0f;

                        ImGui::TableNextRow();

                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(name);

                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", totalMs);

                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", avgCatMs);

                        ImGui::TableNextColumn();
                        ImGui::Text("%u", cat->CallCount);

                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f", pctFrame);

                        ++shown;
                    }

                    ImGui::EndTable();
                }

                ImGui::TreePop();
            }
        }
        ImGui::End();
    }

    void Init(Core::Windowing::Window& window,
              RHI::VulkanDevice& device,
              RHI::VulkanSwapchain& swapchain,
              VkInstance instance,
              VkQueue graphicsQueue)
    {
        s_Device = &device;
        s_BackendInitialized = false;
        s_RegisteredTextures.clear();

        // 1. Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ApplyDarkTheme();

        // --- HIGH DPI SCALING START ---

        // 1. Detect Monitor Scale from GLFW
        float x_scale = 1.0f, y_scale = 1.0f;
        glfwGetWindowContentScale((GLFWwindow*)window.GetNativeHandle(), &x_scale, &y_scale);

        // On some Linux configs (Wayland), GLFW might return 1.0 even on HiDPI.
        // You can optionally override this if x_scale == 1.0f but you know it's wrong.
        s_DpiScale = x_scale;

        if (x_scale > 1.0f)
        {
            Core::Log::Info("High DPI Detected: Scale Factor {}", x_scale);

            // 2. Scale UI Elements (Padding, Rounding, Spacing)
            ImGui::GetStyle().ScaleAllSizes(x_scale);
        }

        // 3. Load Scaled Font
        // IMPORTANT: Use a TTF file. The default bitmap font looks terrible when scaled.
        float baseFontSize = 16.0f;
        float scaledFontSize = baseFontSize * x_scale;

        // Use your filesystem helper or a relative path
        std::string fontPath = Core::Filesystem::GetAssetPath("fonts/Roboto-Medium.ttf");

        if (std::filesystem::exists(fontPath))
        {
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), scaledFontSize);
            Core::Log::Info("Loaded custom font at size {}", scaledFontSize);
        }
        else
        {
            // Fallback: Scale the default ugly font (better than tiny text)
            Core::Log::Warn("Custom font not found at '{}'. UI text may look blurry.", fontPath);
            ImFontConfig fontConfig;
            fontConfig.SizePixels = scaledFontSize;
            io.Fonts->AddFontDefault(&fontConfig);
        }
        // --- HIGH DPI SCALING END ---

        // 2. Create Descriptor Pool
        VkDescriptorPoolSize pool_sizes[] =
        {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(device.GetLogicalDevice(), &pool_info, nullptr, &s_DescriptorPool) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create ImGui Descriptor Pool");
            return;
        }

        // 3. Init GLFW Backend
        ImGui_ImplGlfw_InitForVulkan((GLFWwindow*)window.GetNativeHandle(), false);

        // 4. Init Vulkan Backend
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.ApiVersion = VK_API_VERSION_1_3;
        init_info.Instance = instance;
        init_info.PhysicalDevice = device.GetPhysicalDevice();
        init_info.Device = device.GetLogicalDevice();
        assert(device.GetQueueIndices().GraphicsFamily.has_value() && "GraphicsFamily required for ImGui init");
        init_info.QueueFamily = device.GetQueueIndices().GraphicsFamily.value();
        init_info.Queue = graphicsQueue;
        init_info.DescriptorPool = s_DescriptorPool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = (uint32_t)swapchain.GetImages().size();

        // API Fix for v1.91+
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        // Dynamic Rendering Setup
        init_info.UseDynamicRendering = true;

        VkFormat color_format = swapchain.GetImageFormat();
        VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {};
        pipeline_rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipeline_rendering_create_info.colorAttachmentCount = 1;
        pipeline_rendering_create_info.pColorAttachmentFormats = &color_format;

        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;

        ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* function_name, void* vulkan_instance)
        {
            return vkGetInstanceProcAddr(reinterpret_cast<VkInstance>(vulkan_instance), function_name);
        }, instance);

        // Init
        ImGui_ImplVulkan_Init(&init_info);
        s_BackendInitialized = true;

        // Upload Fonts
        // This is necessary because we are using dynamic rendering and managing our own headers.
        // It creates the font texture and uploads it to the GPU.
        Core::Log::Info("ImGui Initialized.");
    }

    void Shutdown()
    {
        s_BackendInitialized = false;
        s_RegisteredTextures.clear();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        if (s_DescriptorPool && s_Device)
        {
            vkDestroyDescriptorPool(s_Device->GetLogicalDevice(), s_DescriptorPool, nullptr);
            s_DescriptorPool = VK_NULL_HANDLE;
        }
        s_Device = nullptr;
    }

    void BeginFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        s_FrameActive = true;
    }

    void EndFrame()
    {
        ImGui::EndFrame();
        s_FrameActive = false;
    }

    void DrawGUI()
    {
        // 0. DOCKSPACE over the entire viewport
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);

            ImGuiWindowFlags dockFlags =
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("##IntrinsicDockSpaceWindow", nullptr, dockFlags);
            ImGui::PopStyleVar(3);

            ImGuiID dockspaceId = ImGui::GetID("IntrinsicDockSpace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

            BuildDefaultDockLayout();

            ImGui::End();
        }

        // 1. MAIN MENU BAR
        if (ImGui::BeginMainMenuBar())
        {
            // A. Execute User Registered Menus (File, Edit, etc.)
            for (const auto& menu : s_Menus)
            {
                if (menu.Callback) menu.Callback();
            }

            // B. View menu (Theme, panel toggles)
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::BeginMenu("Theme"))
                {
                    if (ImGui::MenuItem("Dark", nullptr, s_ActiveTheme == Theme::Dark))
                        ApplyTheme(Theme::Dark);
                    if (ImGui::MenuItem("Light", nullptr, s_ActiveTheme == Theme::Light))
                        ApplyTheme(Theme::Light);
                    if (ImGui::MenuItem("High Contrast", nullptr, s_ActiveTheme == Theme::HighContrast))
                        ApplyTheme(Theme::HighContrast);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                for (auto& panel : s_Panels)
                {
                    ImGui::MenuItem(panel.Name.c_str(), nullptr, &panel.IsOpen);
                }
                ImGui::Separator();
                ImGui::MenuItem("Performance", nullptr, &s_ShowTelemetryPanel);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // 2. BUILT-IN TELEMETRY PANEL
        if (s_ShowTelemetryPanel)
        {
            DrawTelemetryPanel();
        }

        // 3. USER PANELS (WINDOWS)
        for (auto it = s_Panels.begin(); it != s_Panels.end(); )
        {
            if (!it->IsOpen && it->IsClosable)
            {
                // If closed, we skip drawing, but we KEEP it in the vector
                // so the "Panels" menu above can re-enable it.
                ++it;
                continue;
            }

            bool* pOpen = it->IsClosable ? &it->IsOpen : nullptr;

            if (ImGui::Begin(it->Name.c_str(), pOpen, it->Flags))
            {
                if (it->Callback) it->Callback();
            }
            ImGui::End();

            ++it;
        }

        // 4. FRAME OVERLAYS (no window wrapper)
        for (const auto& overlay : s_Overlays)
        {
            if (overlay.Callback)
                overlay.Callback();
        }
    }

    void Render(VkCommandBuffer cmd)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        s_FrameActive = false;
    }

    void RegisterPanel(std::string name, UIPanelCallback callback, bool isClosable, int flags, bool defaultOpen)
    {
        // Update existing registration but preserve the user's current open state.
        if (auto* panel = FindNamedItem(s_Panels, name))
        {
            panel->Callback = std::move(callback);
            panel->IsClosable = isClosable;
            panel->Flags = flags;
            return;
        }

        // Add new. Non-closable panels always draw, so IsOpen only matters for closable ones.
        s_Panels.push_back({std::move(name), std::move(callback), isClosable, !isClosable || defaultOpen, flags});
    }

    void OpenPanel(const std::string& name)
    {
        if (auto* panel = FindNamedItem(s_Panels, name))
            panel->IsOpen = true;
    }

    void RemovePanel(const std::string& name)
    {
        RemoveNamedItem(s_Panels, name);
    }

    void RegisterMainMenuBar(std::string name, UIMenuCallback callback)
    {
        // Simple append - ImGui menus merge automatically if they have the same name (e.g. "File")
        s_Menus.push_back({std::move(name), std::move(callback)});
    }

    void RegisterOverlay(std::string name, UIOverlayCallback callback)
    {
        if (auto* overlay = FindNamedItem(s_Overlays, name))
        {
            overlay->Callback = std::move(callback);
            return;
        }

        s_Overlays.push_back({std::move(name), std::move(callback)});
    }

    void RemoveOverlay(const std::string& name)
    {
        RemoveNamedItem(s_Overlays, name);
    }

    bool IsFrameActive()
    {
        return s_FrameActive;
    }

    bool WantCaptureMouse()
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool WantCaptureKeyboard()
    {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    void ItemTooltip(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth)
    {
        bool changed = false;
        ImGui::PushID(label.c_str());

        // Simple columns layout
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);

        ImGui::Text("%s", label.c_str());
        ImGui::NextColumn();

        // 3 floats
        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        // X
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        if (ImGui::Button("X")) { values.x = resetValue; changed = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Y
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
        if (ImGui::Button("Y")) { values.y = resetValue; changed = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Z
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
        if (ImGui::Button("Z")) { values.z = resetValue; changed = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::PopItemWidth();

        ImGui::Columns(1);
        ImGui::PopID();

        return changed;
    }

    // =========================================================================
    // Theme system
    // =========================================================================

    static void ApplyDarkTheme()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();

        // Refine the default dark theme for a modern, professional look.
        style.WindowRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.ScrollbarRounding = 6.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 12.0f;
        style.SeparatorTextBorderSize = 2.0f;

        auto* colors = style.Colors;

        // Window backgrounds
        colors[ImGuiCol_WindowBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_ChildBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg]          = ImVec4(0.12f, 0.12f, 0.14f, 0.96f);

        // Borders
        colors[ImGuiCol_Border]           = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
        colors[ImGuiCol_BorderShadow]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Frames (input fields, checkboxes, sliders)
        colors[ImGuiCol_FrameBg]          = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
        colors[ImGuiCol_FrameBgActive]    = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);

        // Title bar
        colors[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive]    = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.10f, 0.75f);

        // Menu bar
        colors[ImGuiCol_MenuBarBg]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.08f, 0.08f, 0.10f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab]    = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);

        // Accent color (blue)
        const ImVec4 accent       = ImVec4(0.26f, 0.52f, 0.96f, 1.00f);
        const ImVec4 accentHover  = ImVec4(0.36f, 0.60f, 1.00f, 1.00f);
        const ImVec4 accentActive = ImVec4(0.20f, 0.44f, 0.86f, 1.00f);

        // Buttons
        colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
        colors[ImGuiCol_ButtonHovered]    = accent;
        colors[ImGuiCol_ButtonActive]     = accentActive;

        // Headers (collapsing headers, tree nodes, selectable)
        colors[ImGuiCol_Header]           = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_HeaderHovered]    = ImVec4(0.26f, 0.26f, 0.32f, 1.00f);
        colors[ImGuiCol_HeaderActive]     = accent;

        // Separator
        colors[ImGuiCol_Separator]        = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = accentHover;
        colors[ImGuiCol_SeparatorActive]  = accent;

        // Tabs
        colors[ImGuiCol_Tab]                = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_TabHovered]         = accent;
        colors[ImGuiCol_TabSelected]        = ImVec4(0.18f, 0.36f, 0.68f, 1.00f);
        colors[ImGuiCol_TabDimmed]          = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected]  = ImVec4(0.14f, 0.28f, 0.52f, 1.00f);

        // Docking
        colors[ImGuiCol_DockingPreview]   = ImVec4(0.26f, 0.52f, 0.96f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]   = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

        // Checkmark, slider grab
        colors[ImGuiCol_CheckMark]        = accent;
        colors[ImGuiCol_SliderGrab]       = accent;
        colors[ImGuiCol_SliderGrabActive] = accentHover;

        // Resize grip
        colors[ImGuiCol_ResizeGrip]          = ImVec4(0.26f, 0.52f, 0.96f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]   = ImVec4(0.26f, 0.52f, 0.96f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]    = ImVec4(0.26f, 0.52f, 0.96f, 0.95f);

        // Text
        colors[ImGuiCol_Text]             = ImVec4(0.90f, 0.90f, 0.93f, 1.00f);
        colors[ImGuiCol_TextDisabled]     = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]   = ImVec4(0.26f, 0.52f, 0.96f, 0.35f);
    }

    static void ApplyLightTheme()
    {
        ImGui::StyleColorsLight();
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.ScrollbarRounding = 6.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.SeparatorTextBorderSize = 2.0f;

        auto* colors = style.Colors;

        colors[ImGuiCol_WindowBg]         = ImVec4(0.95f, 0.95f, 0.96f, 1.00f);
        colors[ImGuiCol_PopupBg]          = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
        colors[ImGuiCol_Border]           = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);

        colors[ImGuiCol_FrameBg]          = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
        colors[ImGuiCol_FrameBgActive]    = ImVec4(0.82f, 0.86f, 0.94f, 1.00f);

        colors[ImGuiCol_TitleBg]          = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        colors[ImGuiCol_TitleBgActive]    = ImVec4(0.82f, 0.84f, 0.88f, 1.00f);
        colors[ImGuiCol_MenuBarBg]        = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);

        const ImVec4 accent       = ImVec4(0.16f, 0.44f, 0.86f, 1.00f);
        const ImVec4 accentHover  = ImVec4(0.22f, 0.52f, 0.94f, 1.00f);

        colors[ImGuiCol_Button]           = ImVec4(0.86f, 0.86f, 0.90f, 1.00f);
        colors[ImGuiCol_ButtonHovered]    = accent;
        colors[ImGuiCol_ButtonActive]     = ImVec4(0.12f, 0.38f, 0.78f, 1.00f);

        colors[ImGuiCol_Header]           = ImVec4(0.88f, 0.90f, 0.94f, 1.00f);
        colors[ImGuiCol_HeaderHovered]    = ImVec4(0.80f, 0.84f, 0.92f, 1.00f);
        colors[ImGuiCol_HeaderActive]     = accent;

        colors[ImGuiCol_Tab]              = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        colors[ImGuiCol_TabHovered]       = accent;
        colors[ImGuiCol_TabSelected]      = ImVec4(0.72f, 0.80f, 0.94f, 1.00f);

        colors[ImGuiCol_CheckMark]        = accent;
        colors[ImGuiCol_SliderGrab]       = accent;
        colors[ImGuiCol_SliderGrabActive] = accentHover;

        colors[ImGuiCol_DockingPreview]   = ImVec4(0.16f, 0.44f, 0.86f, 0.70f);

        colors[ImGuiCol_Text]             = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_TextDisabled]     = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]   = ImVec4(0.16f, 0.44f, 0.86f, 0.35f);
    }

    static void ApplyHighContrastTheme()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.WindowBorderSize = 2.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 2.0f;
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.SeparatorTextBorderSize = 2.0f;

        auto* colors = style.Colors;

        colors[ImGuiCol_WindowBg]         = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_PopupBg]          = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_Border]           = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

        colors[ImGuiCol_FrameBg]          = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgActive]    = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

        colors[ImGuiCol_TitleBg]          = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_TitleBgActive]    = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_MenuBarBg]        = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);

        const ImVec4 accent       = ImVec4(0.00f, 0.60f, 1.00f, 1.00f);
        const ImVec4 accentBright = ImVec4(0.20f, 0.80f, 1.00f, 1.00f);

        colors[ImGuiCol_Button]           = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_ButtonHovered]    = accent;
        colors[ImGuiCol_ButtonActive]     = accentBright;

        colors[ImGuiCol_Header]           = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_HeaderHovered]    = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_HeaderActive]     = accent;

        colors[ImGuiCol_Tab]              = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_TabHovered]       = accent;
        colors[ImGuiCol_TabSelected]      = ImVec4(0.00f, 0.40f, 0.70f, 1.00f);

        colors[ImGuiCol_CheckMark]        = ImVec4(1.00f, 1.00f, 0.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]       = accent;
        colors[ImGuiCol_SliderGrabActive] = accentBright;

        colors[ImGuiCol_DockingPreview]   = ImVec4(0.00f, 0.60f, 1.00f, 0.70f);

        colors[ImGuiCol_Separator]        = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);

        colors[ImGuiCol_Text]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled]     = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]   = ImVec4(0.00f, 0.60f, 1.00f, 0.50f);
    }

    void ApplyTheme(Theme theme)
    {
        s_ActiveTheme = theme;
        switch (theme)
        {
        case Theme::Dark:         ApplyDarkTheme(); break;
        case Theme::Light:        ApplyLightTheme(); break;
        case Theme::HighContrast: ApplyHighContrastTheme(); break;
        }

        // Re-apply HiDPI scaling after theme reset (themes set unscaled values).
        if (s_DpiScale > 1.0f)
            ImGui::GetStyle().ScaleAllSizes(s_DpiScale);
    }

    Theme GetActiveTheme()
    {
        return s_ActiveTheme;
    }

    // =========================================================================
    // Default dock layout
    // =========================================================================

    void BuildDefaultDockLayout()
    {
        if (s_DockLayoutBuilt)
            return;

        ImGuiID dockspaceId = ImGui::GetID("IntrinsicDockSpace");

        // Only build the layout if no prior layout exists (first launch / no imgui.ini).
        if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
        {
            s_DockLayoutBuilt = true;
            return;
        }

        s_DockLayoutBuilt = true;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        // Split: left panel (18%) | center+right remainder
        ImGuiID dockLeft, dockRemaining;
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.18f, &dockLeft, &dockRemaining);

        // Split: center viewport | right panel (22%)
        ImGuiID dockCenter, dockRight;
        ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);

        // Split right panel: inspector top (65%) | view settings bottom (35%)
        ImGuiID dockRightTop, dockRightBottom;
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.35f, &dockRightBottom, &dockRightTop);

        // Split bottom of center: assets/stats area
        ImGuiID dockCenterMain, dockBottom;
        ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, &dockBottom, &dockCenterMain);

        // Dock panels into their designated locations.
        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRightTop);
        ImGui::DockBuilderDockWindow("View Settings", dockRightBottom);
        ImGui::DockBuilderDockWindow("Stats", dockBottom);
        ImGui::DockBuilderDockWindow("Assets", dockBottom);
        ImGui::DockBuilderDockWindow("Performance", dockBottom);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Features", dockBottom);
        ImGui::DockBuilderDockWindow("Frame Graph", dockBottom);
        ImGui::DockBuilderDockWindow("Selection", dockBottom);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    void* AddTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
    {
        if (!s_BackendInitialized || imageView == VK_NULL_HANDLE)
            return nullptr;

        // ImGui's Vulkan backend returns an opaque ImTextureID.
        void* textureId = ImGui_ImplVulkan_AddTexture(sampler, imageView, imageLayout);
        if (textureId)
            s_RegisteredTextures.insert(textureId);
        return textureId;
    }

    void RemoveTexture(void* textureId)
    {
        if (!textureId) return;
        if (!s_BackendInitialized)
        {
            s_RegisteredTextures.erase(textureId);
            return;
        }

        if (!s_RegisteredTextures.erase(textureId))
            return;

        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(textureId));
    }
}
