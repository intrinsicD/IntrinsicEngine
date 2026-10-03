// Scalar property selection and copied statistics plots shared by editor windows.
module;
#include <span>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.EditorPropertyWidgets;

import Geometry.Properties;
import Geometry.Properties.Statistics;

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
        Geometry::PropertyNumericStatistics Statistics{};
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
        std::string_view selectedProperty = {}, std::size_t bins = 32);

    // Draws a scalar-property selector plus an ImPlot histogram. ImGui and
    // ImPlot types remain private to the implementation unit.
    [[nodiscard]] bool DrawEditorScalarPropertyPlotWidget(
        std::string_view widgetId,
        const Geometry::ConstPropertySet& properties,
        EditorPropertyPlotWidgetState& state);

    void DrawEditorPropertyHistogramWidget(
        std::string_view widgetId, const Geometry::PropertyNumericStatistics& statistics);

    // Draws an ImPlot bar chart of a spectrum (e.g. Laplacian eigenvalues) and a slider that
    // selects one index; returns true when the selection changed.
    [[nodiscard]] bool DrawEditorSpectrumBarWidget(
        std::string_view widgetId, std::span<const double> values, int& selected);
}
