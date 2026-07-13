module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <implot.h>

module Extrinsic.Runtime.EditorPropertyWidgets;

namespace Extrinsic::Runtime
{
    namespace
    {
        template <typename T>
        void AppendSamples(
            const Geometry::ConstPropertySet& properties,
            const std::string_view name,
            EditorScalarPropertyPlotModel& model)
        {
            const Geometry::ConstProperty<T> property = properties.Get<T>(name);
            if (!property)
                return;

            model.SourceSampleCount = property.Vector().size();
            model.FiniteSamples.reserve(model.SourceSampleCount);
            for (const auto value : property.Vector())
            {
                const double sample = static_cast<double>(value);
                if (std::isfinite(sample))
                    model.FiniteSamples.push_back(sample);
                else
                    ++model.FilteredNonFiniteSampleCount;
            }
        }

        void PopulateSelectedSamples(
            const Geometry::ConstPropertySet& properties,
            EditorScalarPropertyPlotModel& model)
        {
            using Kind = Geometry::PropertyValueKind;
            switch (model.SelectedValueKind)
            {
            case Kind::Bool:
                AppendSamples<bool>(properties, model.SelectedProperty, model);
                break;
            case Kind::Int32:
                AppendSamples<std::int32_t>(properties, model.SelectedProperty, model);
                break;
            case Kind::UInt32:
                AppendSamples<std::uint32_t>(properties, model.SelectedProperty, model);
                break;
            case Kind::UInt64:
                AppendSamples<std::uint64_t>(properties, model.SelectedProperty, model);
                break;
            case Kind::Float:
                AppendSamples<float>(properties, model.SelectedProperty, model);
                break;
            case Kind::Double:
                AppendSamples<double>(properties, model.SelectedProperty, model);
                break;
            case Kind::Unknown:
            case Kind::Vec2:
            case Kind::Vec3:
            case Kind::Vec4:
                return;
            }

            if (model.FiniteSamples.empty())
                return;

            const auto [minimum, maximum] = std::minmax_element(
                model.FiniteSamples.begin(), model.FiniteSamples.end());
            model.HasFiniteRange = true;
            model.Minimum = *minimum;
            model.Maximum = *maximum;
        }
    }

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
        const std::string_view selectedProperty)
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
        PopulateSelectedSamples(properties, model);
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

        EditorScalarPropertyPlotModel model =
            BuildEditorScalarPropertyPlotModel(properties, state.SelectedProperty);
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

        if (selectionChanged)
        {
            model = BuildEditorScalarPropertyPlotModel(
                properties,
                state.SelectedProperty);
        }

        state.HistogramBins = std::clamp(state.HistogramBins, 1, 256);
        ImGui::SliderInt("Bins", &state.HistogramBins, 1, 256);
        ImGui::Text("Samples: %zu", model.FiniteSamples.size());
        if (model.FilteredNonFiniteSampleCount > 0u)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "(%zu non-finite filtered)",
                model.FilteredNonFiniteSampleCount);
        }

        if (!model.FiniteSamples.empty() &&
            ImPlot::BeginPlot("##PropertyHistogram", ImVec2(-1.0f, 240.0f)))
        {
            const std::size_t boundedCount = std::min(
                model.FiniteSamples.size(),
                static_cast<std::size_t>(std::numeric_limits<int>::max()));
            ImPlot::PlotHistogram(
                model.SelectedProperty.c_str(),
                model.FiniteSamples.data(),
                static_cast<int>(boundedCount),
                state.HistogramBins);
            ImPlot::EndPlot();
        }

        ImGui::PopID();
        return selectionChanged;
    }
}
