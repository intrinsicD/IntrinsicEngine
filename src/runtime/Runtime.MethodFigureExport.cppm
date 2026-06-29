module;

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <glm/vec3.hpp>

export module Extrinsic.Runtime.MethodFigureExport;

import Extrinsic.Core.Error;

export namespace Extrinsic::Runtime
{
    enum class FigureExportStatus : std::uint8_t
    {
        Success = 0,
        InvalidArgument,
        InvalidPath,
        FileWriteError,
        NonFiniteValue,
        SizeMismatch,
    };

    using FigureExportValue = std::variant<std::int64_t, std::uint64_t, double, bool, std::string>;

    struct NamedFigureExportValue
    {
        std::string Key{};
        FigureExportValue Value{};
        std::string Unit{};
    };

    struct FigureMetricSeries
    {
        std::string Name{};
        std::string XLabel{"x"};
        std::string YLabel{"value"};
        std::string XUnit{};
        std::string YUnit{};
        std::vector<double> X{};
        std::vector<double> Y{};
        std::vector<NamedFigureExportValue> Summaries{};
    };

    struct FigureMetricBundle
    {
        std::string DatasetId{};
        std::string MethodId{};
        std::string BackendId{};
        std::string RunId{};
        std::vector<FigureMetricSeries> Series{};
        std::vector<NamedFigureExportValue> Summaries{};
    };

    struct FigurePointRecord
    {
        glm::dvec3 Position{0.0};
        std::uint32_t Level{0u};
        std::uint32_t Phase{0u};
        double SplatRadius{0.0};
    };

    struct FigureRunManifest
    {
        std::string DatasetId{};
        std::string MethodId{};
        std::string BackendId{};
        std::string RunId{};
        std::string EngineVersionStamp{};
        std::uint64_t PointCount{0u};
        std::vector<NamedFigureExportValue> SamplerConfig{};
        std::vector<NamedFigureExportValue> Seeds{};
        std::vector<NamedFigureExportValue> Artifacts{};
    };

    struct FigureExportTextResult
    {
        FigureExportStatus Status{FigureExportStatus::Success};
        std::string Text{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == FigureExportStatus::Success;
        }
    };

    struct FigureExportResult
    {
        FigureExportStatus Status{FigureExportStatus::Success};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Path{};
        std::string Diagnostic{};
        std::uint64_t BytesWritten{0u};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == FigureExportStatus::Success &&
                   Error == Core::ErrorCode::Success;
        }
    };

    [[nodiscard]] std::string_view ToString(FigureExportStatus status) noexcept;

    [[nodiscard]] FigureExportTextResult SerializeMetricBundleCsv(
        const FigureMetricBundle& bundle);
    [[nodiscard]] FigureExportTextResult SerializeMetricBundleJson(
        const FigureMetricBundle& bundle);
    [[nodiscard]] FigureExportTextResult SerializeRunManifestJson(
        const FigureRunManifest& manifest);
    [[nodiscard]] FigureExportTextResult SerializePointSetCsv(
        const std::vector<FigurePointRecord>& points);
    [[nodiscard]] FigureExportTextResult SerializePointSetPly(
        const std::vector<FigurePointRecord>& points);

    [[nodiscard]] FigureExportResult WriteMetricBundleCsv(
        std::string_view path,
        const FigureMetricBundle& bundle);
    [[nodiscard]] FigureExportResult WriteMetricBundleJson(
        std::string_view path,
        const FigureMetricBundle& bundle);
    [[nodiscard]] FigureExportResult WriteRunManifestJson(
        std::string_view path,
        const FigureRunManifest& manifest);
    [[nodiscard]] FigureExportResult WritePointSetCsv(
        std::string_view path,
        const std::vector<FigurePointRecord>& points);
    [[nodiscard]] FigureExportResult WritePointSetPly(
        std::string_view path,
        const std::vector<FigurePointRecord>& points);
}
