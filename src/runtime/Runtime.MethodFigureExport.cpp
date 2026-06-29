module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <glm/vec3.hpp>

module Extrinsic.Runtime.MethodFigureExport;

import Extrinsic.Core.Error;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::string_view kMetricBundleCsvSchema = "intrinsic.method_figure_export.metric_bundle.csv.v1";
        constexpr std::string_view kMetricBundleJsonSchema = "intrinsic.method_figure_export.metric_bundle.json.v1";
        constexpr std::string_view kRunManifestJsonSchema = "intrinsic.method_figure_export.run_manifest.json.v1";
        constexpr std::string_view kPointSetCsvSchema = "intrinsic.method_figure_export.point_set.csv.v1";
        constexpr std::string_view kPointSetPlySchema = "intrinsic.method_figure_export.point_set.ply.v1";

        [[nodiscard]] FigureExportTextResult TextError(const FigureExportStatus status,
                                                       std::string diagnostic)
        {
            return FigureExportTextResult{status, {}, std::move(diagnostic)};
        }

        [[nodiscard]] FigureExportTextResult TextOk(std::string text)
        {
            return FigureExportTextResult{FigureExportStatus::Success, std::move(text), {}};
        }

        [[nodiscard]] FigureExportResult WriteError(const FigureExportStatus status,
                                                    const Core::ErrorCode error,
                                                    const std::string_view path,
                                                    std::string diagnostic)
        {
            return FigureExportResult{status, error, std::string(path), std::move(diagnostic), 0u};
        }

        [[nodiscard]] bool IsFinite(const double value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec3& value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
        }

        [[nodiscard]] std::string FormatDouble(const double value)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::scientific << std::setprecision(17) << value;
            return out.str();
        }

        [[nodiscard]] std::string JsonEscape(const std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 2u);
            out.push_back('"');
            for (const unsigned char c : text)
            {
                switch (c)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20u)
                    {
                        constexpr char kHex[] = "0123456789abcdef";
                        out += "\\u00";
                        out.push_back(kHex[(c >> 4u) & 0x0Fu]);
                        out.push_back(kHex[c & 0x0Fu]);
                    }
                    else
                    {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
                }
            }
            out.push_back('"');
            return out;
        }

        [[nodiscard]] std::string CsvEscape(const std::string_view text)
        {
            const bool needsQuotes = text.find_first_of(",\"\n\r") != std::string_view::npos;
            if (!needsQuotes)
                return std::string(text);

            std::string out;
            out.reserve(text.size() + 2u);
            out.push_back('"');
            for (const char c : text)
            {
                if (c == '"')
                    out += "\"\"";
                else
                    out.push_back(c);
            }
            out.push_back('"');
            return out;
        }

        [[nodiscard]] bool ValidateNonEmptyId(const std::string& value,
                                              const std::string_view name,
                                              std::string& diagnostic)
        {
            if (!value.empty())
                return true;
            diagnostic = std::string(name) + " is required";
            return false;
        }

        [[nodiscard]] bool ValueIsFinite(const FigureExportValue& value) noexcept
        {
            if (const auto* numeric = std::get_if<double>(&value))
                return IsFinite(*numeric);
            return true;
        }

        [[nodiscard]] std::string ValueToJson(const FigureExportValue& value)
        {
            return std::visit(
                [](const auto& input) -> std::string
                {
                    using T = std::decay_t<decltype(input)>;
                    if constexpr (std::is_same_v<T, std::int64_t> ||
                                  std::is_same_v<T, std::uint64_t>)
                    {
                        return std::to_string(input);
                    }
                    else if constexpr (std::is_same_v<T, double>)
                    {
                        return FormatDouble(input);
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                    {
                        return input ? "true" : "false";
                    }
                    else
                    {
                        return JsonEscape(input);
                    }
                },
                value);
        }

        [[nodiscard]] std::string ValueToCsv(const FigureExportValue& value)
        {
            return std::visit(
                [](const auto& input) -> std::string
                {
                    using T = std::decay_t<decltype(input)>;
                    if constexpr (std::is_same_v<T, std::int64_t> ||
                                  std::is_same_v<T, std::uint64_t>)
                    {
                        return std::to_string(input);
                    }
                    else if constexpr (std::is_same_v<T, double>)
                    {
                        return FormatDouble(input);
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                    {
                        return input ? "true" : "false";
                    }
                    else
                    {
                        return CsvEscape(input);
                    }
                },
                value);
        }

        [[nodiscard]] std::optional<std::vector<NamedFigureExportValue>> SortedFields(
            std::vector<NamedFigureExportValue> fields,
            const std::string_view groupName,
            std::string& diagnostic,
            FigureExportStatus& status)
        {
            for (const NamedFigureExportValue& field : fields)
            {
                if (field.Key.empty())
                {
                    diagnostic = std::string(groupName) + " contains an empty key";
                    status = FigureExportStatus::InvalidArgument;
                    return std::nullopt;
                }
                if (!ValueIsFinite(field.Value))
                {
                    diagnostic = std::string(groupName) + " contains a non-finite value for key '" + field.Key + "'";
                    status = FigureExportStatus::NonFiniteValue;
                    return std::nullopt;
                }
            }

            std::sort(fields.begin(),
                      fields.end(),
                      [](const NamedFigureExportValue& lhs,
                         const NamedFigureExportValue& rhs)
                      {
                          return lhs.Key < rhs.Key;
                      });

            for (std::size_t i = 1u; i < fields.size(); ++i)
            {
                if (fields[i - 1u].Key == fields[i].Key)
                {
                    diagnostic = std::string(groupName) + " contains duplicate key '" + fields[i].Key + "'";
                    status = FigureExportStatus::InvalidArgument;
                    return std::nullopt;
                }
            }

            return fields;
        }

        [[nodiscard]] bool ValidateMetricBundle(const FigureMetricBundle& bundle,
                                                std::string& diagnostic,
                                                FigureExportStatus& status)
        {
            status = FigureExportStatus::Success;
            if (!ValidateNonEmptyId(bundle.DatasetId, "dataset_id", diagnostic) ||
                !ValidateNonEmptyId(bundle.MethodId, "method_id", diagnostic) ||
                !ValidateNonEmptyId(bundle.BackendId, "backend_id", diagnostic) ||
                !ValidateNonEmptyId(bundle.RunId, "run_id", diagnostic))
            {
                status = FigureExportStatus::InvalidArgument;
                return false;
            }
            if (bundle.Series.empty() && bundle.Summaries.empty())
            {
                diagnostic = "metric bundle must contain at least one series or summary";
                status = FigureExportStatus::InvalidArgument;
                return false;
            }

            for (const FigureMetricSeries& series : bundle.Series)
            {
                if (series.Name.empty())
                {
                    diagnostic = "metric series name is required";
                    status = FigureExportStatus::InvalidArgument;
                    return false;
                }
                if (series.X.size() != series.Y.size())
                {
                    diagnostic = "metric series '" + series.Name + "' has mismatched x/y array sizes";
                    status = FigureExportStatus::SizeMismatch;
                    return false;
                }
                if (series.X.empty())
                {
                    diagnostic = "metric series '" + series.Name + "' is empty";
                    status = FigureExportStatus::InvalidArgument;
                    return false;
                }
                for (const double value : series.X)
                {
                    if (!IsFinite(value))
                    {
                        diagnostic = "metric series '" + series.Name + "' contains a non-finite x value";
                        status = FigureExportStatus::NonFiniteValue;
                        return false;
                    }
                }
                for (const double value : series.Y)
                {
                    if (!IsFinite(value))
                    {
                        diagnostic = "metric series '" + series.Name + "' contains a non-finite y value";
                        status = FigureExportStatus::NonFiniteValue;
                        return false;
                    }
                }
                FigureExportStatus fieldStatus = FigureExportStatus::Success;
                std::string fieldDiagnostic;
                if (!SortedFields(series.Summaries, "series summaries", fieldDiagnostic, fieldStatus))
                {
                    diagnostic = std::move(fieldDiagnostic);
                    status = fieldStatus;
                    return false;
                }
            }

            FigureExportStatus fieldStatus = FigureExportStatus::Success;
            std::string fieldDiagnostic;
            if (!SortedFields(bundle.Summaries, "bundle summaries", fieldDiagnostic, fieldStatus))
            {
                diagnostic = std::move(fieldDiagnostic);
                status = fieldStatus;
                return false;
            }

            return true;
        }

        [[nodiscard]] bool ValidateManifest(const FigureRunManifest& manifest,
                                            std::string& diagnostic,
                                            FigureExportStatus& status)
        {
            status = FigureExportStatus::Success;
            if (!ValidateNonEmptyId(manifest.DatasetId, "dataset_id", diagnostic) ||
                !ValidateNonEmptyId(manifest.MethodId, "method_id", diagnostic) ||
                !ValidateNonEmptyId(manifest.BackendId, "backend_id", diagnostic) ||
                !ValidateNonEmptyId(manifest.RunId, "run_id", diagnostic) ||
                !ValidateNonEmptyId(manifest.EngineVersionStamp, "engine_version", diagnostic))
            {
                status = FigureExportStatus::InvalidArgument;
                return false;
            }

            for (const auto& [fields, groupName] : {
                     std::pair{manifest.SamplerConfig, std::string_view{"sampler_config"}},
                     std::pair{manifest.Seeds, std::string_view{"seeds"}},
                     std::pair{manifest.Artifacts, std::string_view{"artifacts"}},
                 })
            {
                FigureExportStatus fieldStatus = FigureExportStatus::Success;
                std::string fieldDiagnostic;
                if (!SortedFields(fields, groupName, fieldDiagnostic, fieldStatus))
                {
                    diagnostic = std::move(fieldDiagnostic);
                    status = fieldStatus;
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool ValidatePoints(const std::vector<FigurePointRecord>& points,
                                          std::string& diagnostic,
                                          FigureExportStatus& status)
        {
            status = FigureExportStatus::Success;
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                const FigurePointRecord& point = points[i];
                if (!IsFinite(point.Position) || !IsFinite(point.SplatRadius))
                {
                    diagnostic = "point record " + std::to_string(i) + " contains a non-finite value";
                    status = FigureExportStatus::NonFiniteValue;
                    return false;
                }
                if (point.SplatRadius < 0.0)
                {
                    diagnostic = "point record " + std::to_string(i) + " has a negative splat radius";
                    status = FigureExportStatus::InvalidArgument;
                    return false;
                }
            }
            return true;
        }

        void AppendJsonObjectFields(std::ostringstream& out,
                                    const std::vector<NamedFigureExportValue>& fields,
                                    const int indent)
        {
            const std::string pad(static_cast<std::size_t>(indent), ' ');
            for (std::size_t i = 0u; i < fields.size(); ++i)
            {
                const NamedFigureExportValue& field = fields[i];
                out << pad << JsonEscape(field.Key) << ": " << ValueToJson(field.Value);
                if (i + 1u < fields.size())
                    out << ',';
                out << '\n';
            }
        }

        void AppendJsonNamedValueArray(std::ostringstream& out,
                                       const std::vector<NamedFigureExportValue>& fields,
                                       const int indent)
        {
            const std::string pad(static_cast<std::size_t>(indent), ' ');
            for (std::size_t i = 0u; i < fields.size(); ++i)
            {
                const NamedFigureExportValue& field = fields[i];
                out << pad << "{"
                    << "\"key\": " << JsonEscape(field.Key)
                    << ", \"value\": " << ValueToJson(field.Value)
                    << ", \"unit\": " << JsonEscape(field.Unit)
                    << "}";
                if (i + 1u < fields.size())
                    out << ',';
                out << '\n';
            }
        }

        [[nodiscard]] FigureExportResult WriteTextAtomic(const std::string_view path,
                                                         const std::string& text)
        {
            namespace fs = std::filesystem;

            if (path.empty())
                return WriteError(FigureExportStatus::InvalidPath,
                                  Core::ErrorCode::InvalidPath,
                                  path,
                                  "target path is empty");

            const fs::path target{std::string(path)};
            if (!target.has_filename())
                return WriteError(FigureExportStatus::InvalidPath,
                                  Core::ErrorCode::InvalidPath,
                                  path,
                                  "target path has no filename");

            std::error_code ec;
            if (fs::exists(target, ec) && fs::is_directory(target, ec))
                return WriteError(FigureExportStatus::InvalidPath,
                                  Core::ErrorCode::InvalidPath,
                                  path,
                                  "target path is a directory");
            if (ec)
                return WriteError(FigureExportStatus::InvalidPath,
                                  Core::ErrorCode::InvalidPath,
                                  path,
                                  "target path could not be inspected");

            const fs::path parent = target.parent_path();
            if (!parent.empty())
            {
                if (fs::exists(parent, ec) && !fs::is_directory(parent, ec))
                    return WriteError(FigureExportStatus::InvalidPath,
                                      Core::ErrorCode::InvalidPath,
                                      path,
                                      "target parent path is not a directory");
                if (ec)
                    return WriteError(FigureExportStatus::InvalidPath,
                                      Core::ErrorCode::InvalidPath,
                                      path,
                                      "target parent path could not be inspected");

                fs::create_directories(parent, ec);
                if (ec)
                    return WriteError(FigureExportStatus::FileWriteError,
                                      Core::ErrorCode::FileWriteError,
                                      path,
                                      "target parent directory could not be created");
            }

            fs::path temp = target;
            temp += ".tmp";
            fs::remove(temp, ec);
            ec.clear();

            {
                std::ofstream file(temp, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                    return WriteError(FigureExportStatus::FileWriteError,
                                      Core::ErrorCode::FileWriteError,
                                      path,
                                      "temporary export file could not be opened");
                if (!text.empty())
                    file.write(text.data(), static_cast<std::streamsize>(text.size()));
                file.flush();
                if (!file)
                {
                    file.close();
                    fs::remove(temp, ec);
                    return WriteError(FigureExportStatus::FileWriteError,
                                      Core::ErrorCode::FileWriteError,
                                      path,
                                      "temporary export file write failed");
                }
            }

            fs::rename(temp, target, ec);
            if (ec)
            {
                fs::remove(temp, ec);
                return WriteError(FigureExportStatus::FileWriteError,
                                  Core::ErrorCode::FileWriteError,
                                  path,
                                  "temporary export file could not be committed");
            }

            return FigureExportResult{
                FigureExportStatus::Success,
                Core::ErrorCode::Success,
                std::string(path),
                {},
                static_cast<std::uint64_t>(text.size()),
            };
        }

        [[nodiscard]] FigureExportResult WriteSerializedText(
            const std::string_view path,
            const FigureExportTextResult& text)
        {
            if (!text.Succeeded())
                return WriteError(text.Status,
                                  text.Status == FigureExportStatus::InvalidPath
                                      ? Core::ErrorCode::InvalidPath
                                      : Core::ErrorCode::InvalidArgument,
                                  path,
                                  text.Diagnostic);
            return WriteTextAtomic(path, text.Text);
        }
    }

    std::string_view ToString(const FigureExportStatus status) noexcept
    {
        switch (status)
        {
        case FigureExportStatus::Success: return "Success";
        case FigureExportStatus::InvalidArgument: return "InvalidArgument";
        case FigureExportStatus::InvalidPath: return "InvalidPath";
        case FigureExportStatus::FileWriteError: return "FileWriteError";
        case FigureExportStatus::NonFiniteValue: return "NonFiniteValue";
        case FigureExportStatus::SizeMismatch: return "SizeMismatch";
        default: return "Unknown";
        }
    }

    FigureExportTextResult SerializeMetricBundleCsv(const FigureMetricBundle& bundle)
    {
        std::string diagnostic;
        FigureExportStatus status = FigureExportStatus::Success;
        if (!ValidateMetricBundle(bundle, diagnostic, status))
            return TextError(status, std::move(diagnostic));

        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "schema,kind,series,key,x,y,value,unit\n";
        for (const FigureMetricSeries& series : bundle.Series)
        {
            for (std::size_t i = 0u; i < series.X.size(); ++i)
            {
                out << CsvEscape(kMetricBundleCsvSchema) << ",series,"
                    << CsvEscape(series.Name) << ",,"
                    << FormatDouble(series.X[i]) << ','
                    << FormatDouble(series.Y[i]) << ",,\n";
            }

            FigureExportStatus fieldStatus = FigureExportStatus::Success;
            std::string fieldDiagnostic;
            const auto fields = SortedFields(series.Summaries, "series summaries", fieldDiagnostic, fieldStatus);
            if (!fields)
                return TextError(fieldStatus, std::move(fieldDiagnostic));
            for (const NamedFigureExportValue& summary : *fields)
            {
                out << CsvEscape(kMetricBundleCsvSchema) << ",summary,"
                    << CsvEscape(series.Name) << ','
                    << CsvEscape(summary.Key) << ",,,"
                    << ValueToCsv(summary.Value) << ','
                    << CsvEscape(summary.Unit) << '\n';
            }
        }

        FigureExportStatus fieldStatus = FigureExportStatus::Success;
        std::string fieldDiagnostic;
        const auto fields = SortedFields(bundle.Summaries, "bundle summaries", fieldDiagnostic, fieldStatus);
        if (!fields)
            return TextError(fieldStatus, std::move(fieldDiagnostic));
        for (const NamedFigureExportValue& summary : *fields)
        {
            out << CsvEscape(kMetricBundleCsvSchema) << ",summary,bundle,"
                << CsvEscape(summary.Key) << ",,,"
                << ValueToCsv(summary.Value) << ','
                << CsvEscape(summary.Unit) << '\n';
        }

        return TextOk(out.str());
    }

    FigureExportTextResult SerializeMetricBundleJson(const FigureMetricBundle& bundle)
    {
        std::string diagnostic;
        FigureExportStatus status = FigureExportStatus::Success;
        if (!ValidateMetricBundle(bundle, diagnostic, status))
            return TextError(status, std::move(diagnostic));

        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "{\n"
            << "  \"schema\": " << JsonEscape(kMetricBundleJsonSchema) << ",\n"
            << "  \"dataset_id\": " << JsonEscape(bundle.DatasetId) << ",\n"
            << "  \"method_id\": " << JsonEscape(bundle.MethodId) << ",\n"
            << "  \"backend_id\": " << JsonEscape(bundle.BackendId) << ",\n"
            << "  \"run_id\": " << JsonEscape(bundle.RunId) << ",\n"
            << "  \"series\": [\n";

        for (std::size_t i = 0u; i < bundle.Series.size(); ++i)
        {
            const FigureMetricSeries& series = bundle.Series[i];
            FigureExportStatus fieldStatus = FigureExportStatus::Success;
            std::string fieldDiagnostic;
            const auto fields = SortedFields(series.Summaries, "series summaries", fieldDiagnostic, fieldStatus);
            if (!fields)
                return TextError(fieldStatus, std::move(fieldDiagnostic));

            out << "    {\n"
                << "      \"name\": " << JsonEscape(series.Name) << ",\n"
                << "      \"x_label\": " << JsonEscape(series.XLabel) << ",\n"
                << "      \"y_label\": " << JsonEscape(series.YLabel) << ",\n"
                << "      \"x_unit\": " << JsonEscape(series.XUnit) << ",\n"
                << "      \"y_unit\": " << JsonEscape(series.YUnit) << ",\n"
                << "      \"samples\": [\n";
            for (std::size_t j = 0u; j < series.X.size(); ++j)
            {
                out << "        {\"x\": " << FormatDouble(series.X[j])
                    << ", \"y\": " << FormatDouble(series.Y[j]) << "}";
                if (j + 1u < series.X.size())
                    out << ',';
                out << '\n';
            }
            out << "      ],\n"
                << "      \"summaries\": [\n";
            AppendJsonNamedValueArray(out, *fields, 8);
            out << "      ]\n"
                << "    }";
            if (i + 1u < bundle.Series.size())
                out << ',';
            out << '\n';
        }
        out << "  ],\n"
            << "  \"summaries\": [\n";

        FigureExportStatus fieldStatus = FigureExportStatus::Success;
        std::string fieldDiagnostic;
        const auto fields = SortedFields(bundle.Summaries, "bundle summaries", fieldDiagnostic, fieldStatus);
        if (!fields)
            return TextError(fieldStatus, std::move(fieldDiagnostic));
        AppendJsonNamedValueArray(out, *fields, 4);
        out << "  ]\n"
            << "}\n";

        return TextOk(out.str());
    }

    FigureExportTextResult SerializeRunManifestJson(const FigureRunManifest& manifest)
    {
        std::string diagnostic;
        FigureExportStatus status = FigureExportStatus::Success;
        if (!ValidateManifest(manifest, diagnostic, status))
            return TextError(status, std::move(diagnostic));

        FigureExportStatus fieldStatus = FigureExportStatus::Success;
        std::string fieldDiagnostic;
        const auto sampler = SortedFields(manifest.SamplerConfig, "sampler_config", fieldDiagnostic, fieldStatus);
        if (!sampler)
            return TextError(fieldStatus, std::move(fieldDiagnostic));
        const auto seeds = SortedFields(manifest.Seeds, "seeds", fieldDiagnostic, fieldStatus);
        if (!seeds)
            return TextError(fieldStatus, std::move(fieldDiagnostic));
        const auto artifacts = SortedFields(manifest.Artifacts, "artifacts", fieldDiagnostic, fieldStatus);
        if (!artifacts)
            return TextError(fieldStatus, std::move(fieldDiagnostic));

        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "{\n"
            << "  \"schema\": " << JsonEscape(kRunManifestJsonSchema) << ",\n"
            << "  \"dataset_id\": " << JsonEscape(manifest.DatasetId) << ",\n"
            << "  \"method_id\": " << JsonEscape(manifest.MethodId) << ",\n"
            << "  \"backend_id\": " << JsonEscape(manifest.BackendId) << ",\n"
            << "  \"run_id\": " << JsonEscape(manifest.RunId) << ",\n"
            << "  \"engine_version\": " << JsonEscape(manifest.EngineVersionStamp) << ",\n"
            << "  \"point_count\": " << manifest.PointCount << ",\n"
            << "  \"sampler_config\": {\n";
        AppendJsonObjectFields(out, *sampler, 4);
        out << "  },\n"
            << "  \"seeds\": {\n";
        AppendJsonObjectFields(out, *seeds, 4);
        out << "  },\n"
            << "  \"artifacts\": [\n";
        AppendJsonNamedValueArray(out, *artifacts, 4);
        out << "  ]\n"
            << "}\n";

        return TextOk(out.str());
    }

    FigureExportTextResult SerializePointSetCsv(const std::vector<FigurePointRecord>& points)
    {
        std::string diagnostic;
        FigureExportStatus status = FigureExportStatus::Success;
        if (!ValidatePoints(points, diagnostic, status))
            return TextError(status, std::move(diagnostic));

        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "schema,x,y,z,level,phase,splat_radius\n";
        for (const FigurePointRecord& point : points)
        {
            out << CsvEscape(kPointSetCsvSchema) << ','
                << FormatDouble(point.Position.x) << ','
                << FormatDouble(point.Position.y) << ','
                << FormatDouble(point.Position.z) << ','
                << point.Level << ','
                << point.Phase << ','
                << FormatDouble(point.SplatRadius) << '\n';
        }
        return TextOk(out.str());
    }

    FigureExportTextResult SerializePointSetPly(const std::vector<FigurePointRecord>& points)
    {
        std::string diagnostic;
        FigureExportStatus status = FigureExportStatus::Success;
        if (!ValidatePoints(points, diagnostic, status))
            return TextError(status, std::move(diagnostic));

        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "ply\n"
            << "format ascii 1.0\n"
            << "comment schema " << kPointSetPlySchema << '\n'
            << "element vertex " << points.size() << '\n'
            << "property double x\n"
            << "property double y\n"
            << "property double z\n"
            << "property uint level\n"
            << "property uint phase\n"
            << "property double splat_radius\n"
            << "end_header\n";
        for (const FigurePointRecord& point : points)
        {
            out << FormatDouble(point.Position.x) << ' '
                << FormatDouble(point.Position.y) << ' '
                << FormatDouble(point.Position.z) << ' '
                << point.Level << ' '
                << point.Phase << ' '
                << FormatDouble(point.SplatRadius) << '\n';
        }
        return TextOk(out.str());
    }

    FigureExportResult WriteMetricBundleCsv(const std::string_view path,
                                            const FigureMetricBundle& bundle)
    {
        return WriteSerializedText(path, SerializeMetricBundleCsv(bundle));
    }

    FigureExportResult WriteMetricBundleJson(const std::string_view path,
                                             const FigureMetricBundle& bundle)
    {
        return WriteSerializedText(path, SerializeMetricBundleJson(bundle));
    }

    FigureExportResult WriteRunManifestJson(const std::string_view path,
                                            const FigureRunManifest& manifest)
    {
        return WriteSerializedText(path, SerializeRunManifestJson(manifest));
    }

    FigureExportResult WritePointSetCsv(const std::string_view path,
                                        const std::vector<FigurePointRecord>& points)
    {
        return WriteSerializedText(path, SerializePointSetCsv(points));
    }

    FigureExportResult WritePointSetPly(const std::string_view path,
                                        const std::vector<FigurePointRecord>& points)
    {
        return WriteSerializedText(path, SerializePointSetPly(points));
    }
}
