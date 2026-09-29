// Per-iteration plots shared by the registration panels (UI-067): every series of a run on
// its own small plot, against the iteration or the elapsed seconds, with linked x axes.
// Include after <implot.h> and <imgui.h>; the panel builds the series from its snapshot trace.
#pragma once

extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    struct RegistrationTraceSeries
    {
        const char* Label{""};
        std::vector<double> Values{};
        bool Log{false}; // log10 y axis (strictly positive values)
    };

    inline void DrawRegistrationTracePlots(const char* id, const std::vector<double>& iterations,
                                           const std::vector<double>& seconds,
                                           const std::span<const RegistrationTraceSeries> series, bool& bySeconds)
    {
        if (iterations.empty() || series.empty()) return;
        ImGui::PushID(id);
        int axis = bySeconds ? 1 : 0;
        ImGui::TextUnformatted("x axis:");
        ImGui::SameLine();
        ImGui::RadioButton("iteration", &axis, 0);
        ImGui::SameLine();
        ImGui::RadioButton("seconds", &axis, 1);
        bySeconds = axis == 1 && seconds.size() == iterations.size();
        const std::vector<double>& x = bySeconds ? seconds : iterations;
        const int n = int(x.size());
        const int columns = series.size() > 1u ? 2 : 1;
        const int rows = int((series.size() + std::size_t(columns) - 1u) / std::size_t(columns));
        if (ImPlot::BeginSubplots("##trace", rows, columns, ImVec2(-1.0f, 130.0f * float(rows)), ImPlotSubplotFlags_LinkAllX))
        {
            for (const RegistrationTraceSeries& s : series)
            {
                if (!ImPlot::BeginPlot(s.Label)) continue;
                ImPlot::SetupAxes(bySeconds ? "s" : "iteration", nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                if (s.Log) ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::PlotLine(s.Label, x.data(), s.Values.data(), std::min(n, int(s.Values.size())));
                ImPlot::EndPlot();
            }
            ImPlot::EndSubplots();
        }
        ImGui::PopID();
    }
}
}
