// App-private selection and copied query results for the Property Inspector window.
// Include after runtime inspection imports and Sandbox.PanelSupport.hpp.
#pragma once
extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    struct PropertyInspectorUiState
    {
        std::uint32_t Entity{};
        std::optional<std::vector<std::uint32_t>> PreviousSelection{};
        Runtime::GeometryPropertyRef Property{}, CompareWith{};
        int Bins{32}, HistogramComponent{};
        std::size_t Offset{};
        bool Compare{}, ShowValues{};
        std::string DisplayDiagnostic{};
        // Invalidated by scene epoch, source/property generations or query parameters.
        std::uint64_t Epoch{};
        Runtime::GeometryPropertyCatalogSnapshot Catalog{};
        Runtime::EditorPropertyStatisticsResult Statistics{};
        Runtime::EditorPropertyComparisonResult Comparison{};
        Runtime::EditorPropertyValuesResult Values{};
        Runtime::GeometryPropertyRef QueriedProperty{}, QueriedComparison{};
        int QueriedBins{-1};
        std::size_t QueriedOffset{};
        bool QueriedCompare{}, QueriedValues{}, Valid{};
    };
    void DrawPropertyInspectorContents(const SandboxEditorContext&, PropertyInspectorUiState&);
    void DrawPropertyInspectorWindow(bool& open, const SandboxEditorContext&, PropertyInspectorUiState&);
}
}
