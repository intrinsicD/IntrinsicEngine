module;
#include <span>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <implot.h>

module Extrinsic.Runtime.EditorPropertyWidgets;

namespace Extrinsic::Runtime
{
    bool IsEditorScalarPropertyKind(
        const Geometry::PropertyValueKind kind) noexcept
    {
        using Kind = Geometry::PropertyValueKind;
        switch (kind)
        {
        case Kind::Bool:
        case Kind::Int32:
        case Kind::UInt32:
        case Kind::UInt64:
        case Kind::Float:
        case Kind::Double:
            return true;
        case Kind::Unknown:
        case Kind::Vec2:
        case Kind::Vec3:
        case Kind::Vec4:
            return false;
        }
        return false;
    }

    EditorScalarPropertyPlotModel BuildEditorScalarPropertyPlotModel(
        const Geometry::ConstPropertySet& properties,
        const std::string_view selectedProperty, const std::size_t bins)
    {
        EditorScalarPropertyPlotModel model{};
        for (const Geometry::PropertyDescriptor& descriptor :
             properties.Descriptors())
        {
            if (!IsEditorScalarPropertyKind(descriptor.ValueKind))
                continue;
            model.Options.push_back(EditorScalarPropertyOption{
                .Name = descriptor.Name,
                .ValueKind = descriptor.ValueKind,
                .ElementCount = descriptor.ElementCount,
            });
        }

        if (model.Options.empty())
            return model;

        auto selected = std::find_if(
            model.Options.begin(),
            model.Options.end(),
            [selectedProperty](const EditorScalarPropertyOption& option)
            {
                return option.Name == selectedProperty;
            });
        if (selected == model.Options.end())
            selected = model.Options.begin();

        model.SelectedProperty = selected->Name;
        model.SelectedValueKind = selected->ValueKind;
        const auto statistics = Geometry::ComputePropertyStatistics(
            properties, model.SelectedProperty, {.Bins = bins});
        model.SourceSampleCount = statistics.Count;
        model.FilteredNonFiniteSampleCount = statistics.NaNCount + statistics.InfCount;
        if (!statistics.Components.empty())
        {
            model.Statistics = statistics.Components.front();
            model.HasFiniteRange = model.Statistics.FiniteCount != 0;
            model.Minimum = model.Statistics.Min;
            model.Maximum = model.Statistics.Max;
        }
        return model;
    }

    bool DrawEditorScalarPropertyPlotWidget(
        const std::string_view widgetId,
        const Geometry::ConstPropertySet& properties,
        EditorPropertyPlotWidgetState& state)
    {
        if (widgetId.empty())
            ImGui::PushID("EditorScalarPropertyPlot");
        else
            ImGui::PushID(widgetId.data(), widgetId.data() + widgetId.size());

        state.HistogramBins = std::clamp(state.HistogramBins, 1, 256);
        EditorScalarPropertyPlotModel model = BuildEditorScalarPropertyPlotModel(
            properties, state.SelectedProperty, static_cast<std::size_t>(state.HistogramBins));
        bool selectionChanged = false;
        if (state.SelectedProperty != model.SelectedProperty)
        {
            state.SelectedProperty = model.SelectedProperty;
            selectionChanged = true;
        }

        if (model.Options.empty())
        {
            ImGui::TextDisabled("No scalar properties");
            ImGui::PopID();
            return selectionChanged;
        }

        if (ImGui::BeginCombo("Property", model.SelectedProperty.c_str()))
        {
            for (const EditorScalarPropertyOption& option : model.Options)
            {
                const bool selected = option.Name == model.SelectedProperty;
                if (ImGui::Selectable(option.Name.c_str(), selected))
                {
                    state.SelectedProperty = option.Name;
                    selectionChanged = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const bool binsChanged = ImGui::SliderInt("Bins", &state.HistogramBins, 1, 256);
        if (selectionChanged || binsChanged)
        {
            model = BuildEditorScalarPropertyPlotModel(
                properties,
                state.SelectedProperty, static_cast<std::size_t>(state.HistogramBins));
        }

        ImGui::Text("Samples: %zu", model.Statistics.FiniteCount);
        if (model.FilteredNonFiniteSampleCount > 0u)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "(%zu non-finite filtered)",
                model.FilteredNonFiniteSampleCount);
        }

        DrawEditorPropertyHistogramWidget("PropertyHistogram", model.Statistics);

        ImGui::PopID();
        return selectionChanged;
    }

    void DrawEditorPropertyHistogramWidget(
        const std::string_view widgetId, const Geometry::PropertyNumericStatistics& statistics)
    {
        const auto& histogram = statistics.Histogram;
        if (histogram.Counts.empty() || histogram.Edges.size() != histogram.Counts.size() + 1)
            return;
        ImGui::PushID(widgetId.data(), widgetId.data() + widgetId.size());
        if (ImPlot::BeginPlot("##Histogram", ImVec2(-1.0f, 240.0f)))
        {
            ImPlot::SetupAxes("value", "count", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            std::vector<double> centers(histogram.Counts.size()), counts(histogram.Counts.size());
            for (std::size_t i = 0; i < counts.size(); ++i)
            {
                centers[i] = std::midpoint(histogram.Edges[i], histogram.Edges[i + 1]);
                counts[i] = static_cast<double>(histogram.Counts[i]);
            }
            // Long-double subtraction avoids overflow for finite opposite endpoints.
            const auto span = static_cast<long double>(histogram.Edges.back()) - histogram.Edges.front();
            const double width = span > 0 ? static_cast<double>(std::min(
                span / counts.size(), static_cast<long double>(std::numeric_limits<double>::max()))) : 1.0;
            ImPlot::PlotBars("samples", centers.data(), counts.data(), static_cast<int>(counts.size()), width);
            ImPlot::EndPlot();
        }
        ImGui::PopID();
    }

    bool DrawEditorSpectrumBarWidget(std::string_view widgetId, std::span<const double> values, int& selected)
    {
        if (values.empty()) return false;
        ImGui::PushID(widgetId.data(), widgetId.data() + widgetId.size());
        const int count = static_cast<int>(std::min(values.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
        if (ImPlot::BeginPlot("##Spectrum", ImVec2(-1.0f, 200.0f)))
        {
            ImPlot::SetupAxes("index", "eigenvalue", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBars("eigenvalue", values.data(), count, 0.67);
            ImPlot::EndPlot();
        }
        const int previous = selected;
        selected = std::clamp(selected, 0, count - 1);
        ImGui::SliderInt("Eigenvector", &selected, 0, count - 1);
        ImGui::Text("lambda_%d = %.6g", selected, values[static_cast<std::size_t>(selected)]);
        ImGui::PopID();
        return selected != previous;
    }
}

