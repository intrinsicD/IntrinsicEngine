module;

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.EditorPropertyWidgets;

import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    struct EditorScalarPropertyOption
    {
        std::string Name{};
        Geometry::PropertyValueKind ValueKind{Geometry::PropertyValueKind::Unknown};
        std::size_t ElementCount{0u};
    };

    struct EditorScalarPropertyPlotModel
    {
        std::vector<EditorScalarPropertyOption> Options{};
        std::string SelectedProperty{};
        Geometry::PropertyValueKind SelectedValueKind{
            Geometry::PropertyValueKind::Unknown};
        std::vector<double> FiniteSamples{};
        std::size_t SourceSampleCount{0u};
        std::size_t FilteredNonFiniteSampleCount{0u};
        bool HasFiniteRange{false};
        double Minimum{0.0};
        double Maximum{0.0};
    };

    struct EditorPropertyPlotWidgetState
    {
        std::string SelectedProperty{};
        int HistogramBins{32};
    };

    [[nodiscard]] bool IsEditorScalarPropertyKind(
        Geometry::PropertyValueKind kind) noexcept;

    [[nodiscard]] EditorScalarPropertyPlotModel
    BuildEditorScalarPropertyPlotModel(
        const Geometry::ConstPropertySet& properties,
        std::string_view selectedProperty = {});

    // Draws a scalar-property selector plus an ImPlot histogram. ImGui and
    // ImPlot types remain private to the implementation unit.
    [[nodiscard]] bool DrawEditorScalarPropertyPlotWidget(
        std::string_view widgetId,
        const Geometry::ConstPropertySet& properties,
        EditorPropertyPlotWidgetState& state);
}
