module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.VisualizationRecipes;

import Geometry.Properties;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;

namespace Extrinsic::Runtime
{
    namespace
    {
        struct VisualizationEncodingOptions
        {
            std::string SourceName{};
            std::string OutputName{};
            Graphics::VisualizationAttributeDomain Domain{
                Graphics::VisualizationAttributeDomain::Vertex};
            std::uint64_t BufferBDA{0u};
            std::uint64_t ColorBufferBDA{0u};
            std::uint64_t PositionBufferBDA{0u};
            std::uint64_t VectorBufferBDA{0u};
            std::string PropertyBufferSourceKey{};
            std::string PositionBufferSourceKey{};
            std::string VectorBufferSourceKey{};
            std::uint64_t DirtyStamp{0u};
            bool AutoRange{true};
            float RangeMin{0.0f};
            float RangeMax{1.0f};
            Graphics::Colormap::Type Colormap{Graphics::Colormap::Type::Viridis};
            std::uint32_t IsoValueCount{0u};
            float LineWidth{1.0f};
            glm::vec4 OverlayColor{0.0f, 0.0f, 0.0f, 1.0f};
            float VectorScale{1.0f};
            glm::vec4 VectorColor{1.0f};
            bool DepthTested{true};
        };

        [[nodiscard]] bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4& value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y) &&
                   IsFinite(value.z) && IsFinite(value.w);
        }

        [[nodiscard]] bool ValidRange(const float minValue,
                                      const float maxValue) noexcept
        {
            return IsFinite(minValue) && IsFinite(maxValue) && minValue < maxValue;
        }

        [[nodiscard]] std::optional<Graphics::VisualizationAttributeDomain>
            ToVisualizationDomain(const GeometryElementDomain domain) noexcept
        {
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
            case GeometryElementDomain::GraphNode:
            case GeometryElementDomain::PointCloudPoint:
                return Graphics::VisualizationAttributeDomain::Vertex;
            case GeometryElementDomain::MeshEdge:
            case GeometryElementDomain::GraphEdge:
                return Graphics::VisualizationAttributeDomain::Edge;
            case GeometryElementDomain::MeshFace:
                return Graphics::VisualizationAttributeDomain::Face;
            case GeometryElementDomain::MeshHalfedge:
            case GeometryElementDomain::GraphHalfedge:
            case GeometryElementDomain::Unknown:
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] VisualizationRecipeStatus ToRecipeStatus(
            const GeometryPropertyResolutionStatus status) noexcept
        {
            switch (status)
            {
            case GeometryPropertyResolutionStatus::Resolved:
                return VisualizationRecipeStatus::Encoded;
            case GeometryPropertyResolutionStatus::UnsupportedDomain:
                return VisualizationRecipeStatus::UnsupportedDomain;
            case GeometryPropertyResolutionStatus::MissingName:
            case GeometryPropertyResolutionStatus::MissingProperty:
                return VisualizationRecipeStatus::MissingSource;
            case GeometryPropertyResolutionStatus::ValueKindMismatch:
                return VisualizationRecipeStatus::UnsupportedSourceType;
            case GeometryPropertyResolutionStatus::ElementCountMismatch:
                return VisualizationRecipeStatus::ElementCountMismatch;
            case GeometryPropertyResolutionStatus::NonFiniteValues:
                return VisualizationRecipeStatus::NonFiniteValue;
            }
            return VisualizationRecipeStatus::InvalidResource;
        }

        [[nodiscard]] VisualizationRecipeStatus ToRecipeStatus(
            const VisualizationEncodingDiagnostics& stats) noexcept
        {
            if (stats.PacketAppendCount != 0u)
                return VisualizationRecipeStatus::Encoded;
            if (stats.MissingSourceCount != 0u)
                return VisualizationRecipeStatus::MissingSource;
            if (stats.UnsupportedSourceTypeCount != 0u)
                return VisualizationRecipeStatus::UnsupportedSourceType;
            if (stats.EmptySourceCount != 0u)
                return VisualizationRecipeStatus::EmptySource;
            if (stats.InvalidBufferCount != 0u)
                return VisualizationRecipeStatus::InvalidBuffer;
            if (stats.InvalidResourceCount != 0u)
                return VisualizationRecipeStatus::InvalidResource;
            if (stats.MissingTexcoordCount != 0u)
                return VisualizationRecipeStatus::MissingTexcoord;
            if (stats.InvalidRangeCount != 0u)
                return VisualizationRecipeStatus::InvalidRange;
            if (stats.NonFiniteValueCount != 0u)
                return VisualizationRecipeStatus::NonFiniteValue;
            if (stats.ElementCountOverflowCount != 0u)
                return VisualizationRecipeStatus::ElementCountOverflow;
            return VisualizationRecipeStatus::InvalidResource;
        }

        template <typename T>
        [[nodiscard]] bool ToFiniteFloat(const T value, float& out) noexcept
        {
            if (!std::isfinite(static_cast<double>(value)))
                return false;

            const float converted = static_cast<float>(value);
            if (!IsFinite(converted))
                return false;

            out = converted;
            return true;
        }

        template <typename T>
        [[nodiscard]] std::vector<std::byte> CopyBytes(
            const std::span<const T> values)
        {
            std::vector<std::byte> bytes(values.size_bytes());
            if (!values.empty())
            {
                std::memcpy(bytes.data(), values.data(), values.size_bytes());
            }
            return bytes;
        }

        void AppendPropertyBuffer(
            VisualizationEncodingBatch& out,
            const std::string& sourceKey,
            const Graphics::VisualizationAttributeDomain domain,
            const Graphics::VisualizationValueType valueType,
            const std::uint32_t elementCount,
            const std::uint32_t strideBytes,
            const std::uint64_t dirtyStamp,
            std::vector<std::byte> payload)
        {
            out.PropertyBufferPayloads.push_back(std::move(payload));
            const std::vector<std::byte>& stored =
                out.PropertyBufferPayloads.back();
            out.PropertyBuffers.push_back(
                Graphics::VisualizationPropertyBufferUploadDescriptor{
                    .SourceKey = sourceKey,
                    .Domain = domain,
                    .ValueType = valueType,
                    .ElementCount = elementCount,
                    .StrideBytes = strideBytes,
                    .DirtyStamp = dirtyStamp,
                    .Bytes = std::span<const std::byte>{stored.data(), stored.size()},
                });
        }

        template <typename T>
        [[nodiscard]] std::vector<std::byte> CopyScalarFloats(
            const std::span<const T> values)
        {
            std::vector<float> converted;
            converted.reserve(values.size());
            for (const T value : values)
            {
                converted.push_back(static_cast<float>(value));
            }
            return CopyBytes(std::span<const float>{converted.data(), converted.size()});
        }

        // BUG-059 — robust auto-range. Raw min/max auto ranges collapse on
        // heavy-tailed fields (mean/gaussian curvature spikes at slivers or
        // near-degenerate vertices): a handful of extreme values stretches the
        // range so far that every other element normalizes to the colormap's
        // darkest bin and the surface reads as uniformly black. For fields
        // with enough samples the auto range therefore clamps to the
        // [2%, 98%] quantiles; the shader already clamps t into [0, 1], so
        // outliers saturate at the colormap ends instead of owning the range.
        // Small fields keep exact min/max (quantiles degenerate to the
        // extremes there, and synthetic contract fixtures expect exactness).
        inline constexpr std::size_t kRobustAutoRangeMinSamples = 64u;
        inline constexpr double kRobustAutoRangeLowerQuantile = 0.02;
        inline constexpr double kRobustAutoRangeUpperQuantile = 0.98;

        template <typename T>
        [[nodiscard]] bool ComputeRange(std::span<const T> values,
                                        float& minOut,
                                        float& maxOut,
                                        VisualizationEncodingDiagnostics& stats)
        {
            if (values.empty())
            {
                ++stats.EmptySourceCount;
                return false;
            }

            std::vector<float> finite;
            finite.reserve(values.size());
            for (const T value : values)
            {
                float converted = 0.0f;
                ++stats.ScalarValueScanCount;
                if (!ToFiniteFloat(value, converted))
                {
                    ++stats.NonFiniteValueCount;
                    return false;
                }
                finite.push_back(converted);
            }

            const auto [minIt, maxIt] =
                std::minmax_element(finite.begin(), finite.end());
            float minValue = *minIt;
            float maxValue = *maxIt;

            if (finite.size() >= kRobustAutoRangeMinSamples)
            {
                const auto quantile = [&finite](const double q)
                {
                    const std::size_t index = static_cast<std::size_t>(
                        q * static_cast<double>(finite.size() - 1u));
                    std::nth_element(finite.begin(),
                                     finite.begin() + static_cast<std::ptrdiff_t>(index),
                                     finite.end());
                    return finite[index];
                };
                const float lower = quantile(kRobustAutoRangeLowerQuantile);
                const float upper = quantile(kRobustAutoRangeUpperQuantile);
                if (lower < upper && (lower > minValue || upper < maxValue))
                {
                    minValue = lower;
                    maxValue = upper;
                    ++stats.RobustAutoRangeClampedCount;
                }
            }

            if (minValue == maxValue)
            {
                minValue -= 0.5f;
                maxValue += 0.5f;
                ++stats.FlatAutoRangeExpandedCount;
            }

            if (!ValidRange(minValue, maxValue))
            {
                ++stats.InvalidRangeCount;
                return false;
            }

            minOut = minValue;
            maxOut = maxValue;
            return true;
        }

        template <typename T>
        bool AppendScalarPacket(const Geometry::ConstProperty<T>& property,
                                VisualizationEncodingBatch& out,
                                const VisualizationEncodingOptions& options,
                                VisualizationEncodingDiagnostics& stats)
        {
            const std::span<const T> values = property.Span();
            if (values.empty())
            {
                ++stats.EmptySourceCount;
                return false;
            }
            if (values.size() > std::numeric_limits<std::uint32_t>::max())
            {
                ++stats.ElementCountOverflowCount;
                return false;
            }

            float minValue = options.RangeMin;
            float maxValue = options.RangeMax;
            if (options.AutoRange)
            {
                if (!ComputeRange(values, minValue, maxValue, stats))
                    return false;
            }
            else
            {
                ++stats.ManualRangeCount;
                if (!ValidRange(minValue, maxValue))
                {
                    ++stats.InvalidRangeCount;
                    return false;
                }
                for (const T value : values)
                {
                    float converted = 0.0f;
                    ++stats.ScalarValueScanCount;
                    if (!ToFiniteFloat(value, converted))
                    {
                        ++stats.NonFiniteValueCount;
                        return false;
                    }
                }
            }

            const std::string sourceKey =
                options.OutputName.empty() ? options.SourceName : options.OutputName;
            const std::string bufferSourceKey =
                options.PropertyBufferSourceKey.empty()
                    ? sourceKey
                    : options.PropertyBufferSourceKey;
            if (options.BufferBDA == 0u)
            {
                AppendPropertyBuffer(out,
                                     bufferSourceKey,
                                     options.Domain,
                                     Graphics::VisualizationValueType::ScalarFloat,
                                     static_cast<std::uint32_t>(values.size()),
                                     sizeof(float),
                                     options.DirtyStamp,
                                     CopyScalarFloats(values));
            }

            out.Scalars.push_back(Graphics::ScalarAttributePacket{
                .Name = sourceKey,
                .SourceBufferKey = bufferSourceKey,
                .Domain = options.Domain,
                .ElementCount = static_cast<std::uint32_t>(values.size()),
                .RangeMin = minValue,
                .RangeMax = maxValue,
                .Colormap = options.Colormap,
                .ScalarBufferBDA = options.BufferBDA,
            });
            ++stats.PacketAppendCount;
            return true;
        }

        template <typename T>
        [[nodiscard]] bool ValidateSourceSpan(std::span<const T> values,
                                              VisualizationEncodingDiagnostics& stats) noexcept
        {
            if (values.empty())
            {
                ++stats.EmptySourceCount;
                return false;
            }
            if (values.size() > std::numeric_limits<std::uint32_t>::max())
            {
                ++stats.ElementCountOverflowCount;
                return false;
            }
            for (const T& value : values)
            {
                if (!IsFinite(value))
                {
                    ++stats.NonFiniteValueCount;
                    return false;
                }
            }
            return true;
        }

        template <typename T>
        [[nodiscard]] bool ValidateFiniteScalarSource(std::span<const T> values,
                                                      VisualizationEncodingDiagnostics& stats) noexcept
        {
            if (values.empty())
            {
                ++stats.EmptySourceCount;
                return false;
            }
            if (values.size() > std::numeric_limits<std::uint32_t>::max())
            {
                ++stats.ElementCountOverflowCount;
                return false;
            }

            for (const T value : values)
            {
                float converted = 0.0f;
                ++stats.ScalarValueScanCount;
                if (!ToFiniteFloat(value, converted))
                {
                    ++stats.NonFiniteValueCount;
                    return false;
                }
            }
            return true;
        }

        bool AppendColorPacket(const Geometry::ConstProperty<glm::vec4>& property,
                               VisualizationEncodingBatch& out,
                               const VisualizationEncodingOptions& options,
                               VisualizationEncodingDiagnostics& stats)
        {
            const std::span<const glm::vec4> values = property.Span();
            if (!ValidateSourceSpan(values, stats))
                return false;

            const std::string sourceKey =
                options.OutputName.empty() ? options.SourceName : options.OutputName;
            const std::string bufferSourceKey =
                options.PropertyBufferSourceKey.empty()
                    ? sourceKey
                    : options.PropertyBufferSourceKey;
            if (options.ColorBufferBDA == 0u)
            {
                AppendPropertyBuffer(out,
                                     bufferSourceKey,
                                     options.Domain,
                                     Graphics::VisualizationValueType::RgbaFloat4,
                                     static_cast<std::uint32_t>(values.size()),
                                     sizeof(glm::vec4),
                                     options.DirtyStamp,
                                     CopyBytes(values));
            }

            out.Colors.push_back(Graphics::ColorAttributePacket{
                .Name = sourceKey,
                .SourceBufferKey = bufferSourceKey,
                .Domain = options.Domain,
                .ElementCount = static_cast<std::uint32_t>(values.size()),
                .ColorBufferBDA = options.ColorBufferBDA,
            });
            ++stats.PacketAppendCount;
            return true;
        }

        template <typename T>
        bool AppendIsolinePacket(const Geometry::ConstProperty<T>& property,
                                 VisualizationEncodingBatch& out,
                                 const VisualizationEncodingOptions& options,
                                 VisualizationEncodingDiagnostics& stats)
        {
            const std::span<const T> values = property.Span();
            if (!ValidateFiniteScalarSource(values, stats))
                return false;

            if (options.IsoValueCount == 0u || !IsFinite(options.LineWidth) ||
                options.LineWidth <= 0.0f || !IsFinite(options.OverlayColor))
            {
                ++stats.InvalidRangeCount;
                return false;
            }

            float minValue = options.RangeMin;
            float maxValue = options.RangeMax;
            if (options.AutoRange)
            {
                if (!ComputeRange(values, minValue, maxValue, stats))
                    return false;
            }
            else
            {
                ++stats.ManualRangeCount;
                if (!ValidRange(minValue, maxValue))
                {
                    ++stats.InvalidRangeCount;
                    return false;
                }
                if (!ValidateFiniteScalarSource(values, stats))
                    return false;
            }

            const std::string sourceKey =
                options.OutputName.empty() ? options.SourceName : options.OutputName;
            const std::string bufferSourceKey =
                options.PropertyBufferSourceKey.empty()
                    ? sourceKey
                    : options.PropertyBufferSourceKey;
            if (options.BufferBDA == 0u)
            {
                AppendPropertyBuffer(out,
                                     bufferSourceKey,
                                     options.Domain,
                                     Graphics::VisualizationValueType::ScalarFloat,
                                     static_cast<std::uint32_t>(values.size()),
                                     sizeof(float),
                                     options.DirtyStamp,
                                     CopyScalarFloats(values));
            }

            out.Isolines.push_back(Graphics::IsolineOverlayPacket{
                .SourceScalarName = sourceKey,
                .ScalarBufferSourceKey = bufferSourceKey,
                .Domain = options.Domain,
                .IsoValueCount = options.IsoValueCount,
                .ScalarBufferBDA = options.BufferBDA,
                .RangeMin = minValue,
                .RangeMax = maxValue,
                .LineWidth = options.LineWidth,
                .Color = options.OverlayColor,
                .DepthTested = options.DepthTested,
            });
            ++stats.PacketAppendCount;
            return true;
        }

        bool AppendVectorFieldPacket(const Geometry::ConstProperty<glm::vec3>& property,
                                     VisualizationEncodingBatch& out,
                                     const VisualizationEncodingOptions& options,
                                     VisualizationEncodingDiagnostics& stats)
        {
            const std::span<const glm::vec3> values = property.Span();
            if (!ValidateSourceSpan(values, stats))
                return false;

            if (options.PositionBufferBDA == 0u &&
                options.PositionBufferSourceKey.empty())
            {
                ++stats.InvalidBufferCount;
                return false;
            }
            if (!IsFinite(options.VectorScale) || options.VectorScale <= 0.0f ||
                !IsFinite(options.VectorColor))
            {
                ++stats.InvalidRangeCount;
                return false;
            }

            const std::string sourceKey =
                options.OutputName.empty() ? options.SourceName : options.OutputName;
            const std::string vectorSourceKey =
                options.VectorBufferSourceKey.empty()
                    ? (options.PropertyBufferSourceKey.empty()
                           ? sourceKey
                           : options.PropertyBufferSourceKey)
                    : options.VectorBufferSourceKey;
            if (options.VectorBufferBDA == 0u)
            {
                AppendPropertyBuffer(out,
                                     vectorSourceKey,
                                     options.Domain,
                                     Graphics::VisualizationValueType::VectorFloat3,
                                     static_cast<std::uint32_t>(values.size()),
                                     sizeof(glm::vec3),
                                     options.DirtyStamp,
                                     CopyBytes(values));
            }

            out.VectorFields.push_back(Graphics::VectorFieldOverlayPacket{
                .Name = sourceKey,
                .PositionBufferSourceKey = options.PositionBufferSourceKey,
                .VectorBufferSourceKey = vectorSourceKey,
                .Domain = options.Domain,
                .ElementCount = static_cast<std::uint32_t>(values.size()),
                .PositionBufferBDA = options.PositionBufferBDA,
                .VectorBufferBDA = options.VectorBufferBDA,
                .Scale = options.VectorScale,
                .Color = options.VectorColor,
                .DepthTested = options.DepthTested,
            });
            ++stats.PacketAppendCount;
            return true;
        }

        void EncodeScalarProperty(
            const Geometry::ConstPropertySet& properties,
            VisualizationEncodingBatch& out,
            const VisualizationEncodingOptions& options,
            VisualizationEncodingDiagnostics& diagnostics)
        {
            if (options.SourceName.empty())
            {
                ++diagnostics.MissingSourceCount;
                return;
            }

            if (const auto property = properties.Get<float>(options.SourceName);
                property.IsValid())
            {
                (void)AppendScalarPacket(property, out, options, diagnostics);
                return;
            }
            if (const auto property = properties.Get<double>(options.SourceName);
                property.IsValid())
            {
                (void)AppendScalarPacket(property, out, options, diagnostics);
                return;
            }

            if (properties.Exists(options.SourceName))
                ++diagnostics.UnsupportedSourceTypeCount;
            else
                ++diagnostics.MissingSourceCount;
        }

        void EncodeColorProperty(
            const Geometry::ConstPropertySet& properties,
            VisualizationEncodingBatch& out,
            const VisualizationEncodingOptions& options,
            VisualizationEncodingDiagnostics& diagnostics)
        {
            if (options.SourceName.empty())
            {
                ++diagnostics.MissingSourceCount;
                return;
            }

            if (const auto property = properties.Get<glm::vec4>(options.SourceName);
                property.IsValid())
            {
                (void)AppendColorPacket(property, out, options, diagnostics);
                return;
            }

            if (properties.Exists(options.SourceName))
                ++diagnostics.UnsupportedSourceTypeCount;
            else
                ++diagnostics.MissingSourceCount;
        }

        void EncodeVectorProperty(
            const Geometry::ConstPropertySet& properties,
            VisualizationEncodingBatch& out,
            const VisualizationEncodingOptions& options,
            VisualizationEncodingDiagnostics& diagnostics)
        {
            if (options.SourceName.empty())
            {
                ++diagnostics.MissingSourceCount;
                return;
            }

            if (const auto property = properties.Get<glm::vec3>(options.SourceName);
                property.IsValid())
            {
                (void)AppendVectorFieldPacket(property, out, options, diagnostics);
                return;
            }

            if (properties.Exists(options.SourceName))
                ++diagnostics.UnsupportedSourceTypeCount;
            else
                ++diagnostics.MissingSourceCount;
        }

        void EncodeIsolineProperty(
            const Geometry::ConstPropertySet& properties,
            VisualizationEncodingBatch& out,
            const VisualizationEncodingOptions& options,
            VisualizationEncodingDiagnostics& diagnostics)
        {
            if (options.SourceName.empty())
            {
                ++diagnostics.MissingSourceCount;
                return;
            }

            if (const auto property = properties.Get<float>(options.SourceName);
                property.IsValid())
            {
                (void)AppendIsolinePacket(property, out, options, diagnostics);
                return;
            }
            if (const auto property = properties.Get<double>(options.SourceName);
                property.IsValid())
            {
                (void)AppendIsolinePacket(property, out, options, diagnostics);
                return;
            }

            if (properties.Exists(options.SourceName))
                ++diagnostics.UnsupportedSourceTypeCount;
            else
                ++diagnostics.MissingSourceCount;
        }

        // Scheduling receipt for an HTEX regeneration request. Encoding is
        // side-effect free; scheduling remains a separate typed operation.
        struct HtexRecreateScheduled
        {
            std::uint64_t PayloadToken{0u};
        };
    }

    std::string_view ToString(const VisualizationRecipeStatus status) noexcept
    {
        switch (status)
        {
        case VisualizationRecipeStatus::Encoded: return "Encoded";
        case VisualizationRecipeStatus::EmptyRecipe: return "EmptyRecipe";
        case VisualizationRecipeStatus::UnsupportedDomain: return "UnsupportedDomain";
        case VisualizationRecipeStatus::MissingSource: return "MissingSource";
        case VisualizationRecipeStatus::UnsupportedSourceType: return "UnsupportedSourceType";
        case VisualizationRecipeStatus::EmptySource: return "EmptySource";
        case VisualizationRecipeStatus::InvalidBuffer: return "InvalidBuffer";
        case VisualizationRecipeStatus::InvalidResource: return "InvalidResource";
        case VisualizationRecipeStatus::MissingTexcoord: return "MissingTexcoord";
        case VisualizationRecipeStatus::InvalidRange: return "InvalidRange";
        case VisualizationRecipeStatus::NonFiniteValue: return "NonFiniteValue";
        case VisualizationRecipeStatus::ElementCountMismatch: return "ElementCountMismatch";
        case VisualizationRecipeStatus::ElementCountOverflow: return "ElementCountOverflow";
        }
        return "InvalidResource";
    }

    VisualizationRecipeKind GetVisualizationRecipeKind(
        const VisualizationRecipe& recipe) noexcept
    {
        return std::visit(
            [](const auto& authored) noexcept
            {
                using Recipe = std::decay_t<decltype(authored)>;
                if constexpr (std::is_same_v<Recipe, std::monostate>)
                    return VisualizationRecipeKind::Empty;
                else if constexpr (std::is_same_v<Recipe, ScalarVisualizationRecipe>)
                    return VisualizationRecipeKind::Scalar;
                else if constexpr (std::is_same_v<Recipe, ColorVisualizationRecipe>)
                    return VisualizationRecipeKind::Color;
                else if constexpr (std::is_same_v<Recipe, LabelVisualizationRecipe>)
                    return VisualizationRecipeKind::Label;
                else if constexpr (std::is_same_v<Recipe, VectorFieldVisualizationRecipe>)
                    return VisualizationRecipeKind::VectorField;
                else if constexpr (std::is_same_v<Recipe, IsolineVisualizationRecipe>)
                    return VisualizationRecipeKind::Isoline;
                else if constexpr (std::is_same_v<Recipe, HtexPreviewVisualizationRecipe>)
                    return VisualizationRecipeKind::HtexPreview;
                else
                    return VisualizationRecipeKind::FragmentBake;
            },
            recipe.Data);
    }

    std::string_view ToString(const VisualizationRecipeKind kind) noexcept
    {
        switch (kind)
        {
        case VisualizationRecipeKind::Empty: return "Empty";
        case VisualizationRecipeKind::Scalar: return "Scalar";
        case VisualizationRecipeKind::Color: return "Color";
        case VisualizationRecipeKind::Label: return "Label";
        case VisualizationRecipeKind::VectorField: return "VectorField";
        case VisualizationRecipeKind::Isoline: return "Isoline";
        case VisualizationRecipeKind::HtexPreview: return "HtexPreview";
        case VisualizationRecipeKind::FragmentBake: return "FragmentBake";
        }
        return "Empty";
    }

    bool SameVisualizationRecipe(const VisualizationRecipe& lhs,
                                 const VisualizationRecipe& rhs) noexcept
    {
        if (lhs.Data.index() != rhs.Data.index())
            return false;

        const auto sameRef = [](const GeometryPropertyRef& a,
                                const GeometryPropertyRef& b) noexcept
        {
            return a == b;
        };
        const auto sameVec4 = [](const glm::vec4& a,
                                 const glm::vec4& b) noexcept
        {
            return a.x == b.x && a.y == b.y &&
                   a.z == b.z && a.w == b.w;
        };

        return std::visit(
            [&](const auto& a, const auto& b) noexcept
            {
                using Lhs = std::decay_t<decltype(a)>;
                using Rhs = std::decay_t<decltype(b)>;
                if constexpr (!std::is_same_v<Lhs, Rhs>)
                {
                    return false;
                }
                else if constexpr (std::is_same_v<Lhs, std::monostate>)
                {
                    return true;
                }
                else if constexpr (std::is_same_v<Lhs, ScalarVisualizationRecipe>)
                {
                    return sameRef(a.Source, b.Source) &&
                           a.OutputName == b.OutputName &&
                           a.BufferBDA == b.BufferBDA &&
                           a.BufferSourceKey == b.BufferSourceKey &&
                           a.DirtyStamp == b.DirtyStamp &&
                           a.AutoRange == b.AutoRange &&
                           a.RangeMin == b.RangeMin &&
                           a.RangeMax == b.RangeMax &&
                           a.Colormap == b.Colormap;
                }
                else if constexpr (std::is_same_v<Lhs, ColorVisualizationRecipe> ||
                                   std::is_same_v<Lhs, LabelVisualizationRecipe>)
                {
                    return sameRef(a.Source, b.Source) &&
                           a.OutputName == b.OutputName &&
                           a.BufferBDA == b.BufferBDA &&
                           a.BufferSourceKey == b.BufferSourceKey &&
                           a.DirtyStamp == b.DirtyStamp;
                }
                else if constexpr (std::is_same_v<Lhs, VectorFieldVisualizationRecipe>)
                {
                    return sameRef(a.Source, b.Source) &&
                           sameRef(a.PositionSource, b.PositionSource) &&
                           a.OutputName == b.OutputName &&
                           a.PositionBufferBDA == b.PositionBufferBDA &&
                           a.VectorBufferBDA == b.VectorBufferBDA &&
                           a.PositionBufferSourceKey == b.PositionBufferSourceKey &&
                           a.VectorBufferSourceKey == b.VectorBufferSourceKey &&
                           a.DirtyStamp == b.DirtyStamp &&
                           a.Scale == b.Scale &&
                           sameVec4(a.Color, b.Color) &&
                           a.DepthTested == b.DepthTested;
                }
                else if constexpr (std::is_same_v<Lhs, IsolineVisualizationRecipe>)
                {
                    return sameRef(a.Source, b.Source) &&
                           a.OutputName == b.OutputName &&
                           a.BufferBDA == b.BufferBDA &&
                           a.BufferSourceKey == b.BufferSourceKey &&
                           a.DirtyStamp == b.DirtyStamp &&
                           a.AutoRange == b.AutoRange &&
                           a.RangeMin == b.RangeMin &&
                           a.RangeMax == b.RangeMax &&
                           a.IsoValueCount == b.IsoValueCount &&
                           a.LineWidth == b.LineWidth &&
                           sameVec4(a.Color, b.Color) &&
                           a.DepthTested == b.DepthTested;
                }
                else if constexpr (std::is_same_v<Lhs, HtexPreviewVisualizationRecipe>)
                {
                    return a.Name == b.Name &&
                           a.PatchCount == b.PatchCount &&
                           a.AtlasWidth == b.AtlasWidth &&
                           a.AtlasHeight == b.AtlasHeight;
                }
                else
                {
                    return a.Name == b.Name &&
                           sameRef(a.Source, b.Source) &&
                           a.Mapping == b.Mapping &&
                           a.MeshHasTexcoords == b.MeshHasTexcoords &&
                           a.FaceCount == b.FaceCount &&
                           a.AtlasWidth == b.AtlasWidth &&
                           a.AtlasHeight == b.AtlasHeight &&
                           a.TexcoordBufferSourceKey == b.TexcoordBufferSourceKey &&
                           a.TexcoordBufferBDA == b.TexcoordBufferBDA &&
                           a.AtlasTextureAsset == b.AtlasTextureAsset &&
                           a.GeneratedTextureSemantic == b.GeneratedTextureSemantic &&
                           a.TexcoordDirtyStamp == b.TexcoordDirtyStamp &&
                           a.SourceAttributeDirtyStamp == b.SourceAttributeDirtyStamp;
                }
            },
            lhs.Data,
            rhs.Data);
    }

    VisualizationEncodingResult EncodeVisualizationRecipe(
        const GeometryEntityAvailability& availability,
        const VisualizationRecipe& recipe)
    {
        VisualizationEncodingResult result{};

        const auto resolveSource = [&availability, &result](
            const GeometryPropertyRef& source,
            const std::optional<std::size_t> expectedElementCount = std::nullopt,
            const GeometryPropertyValueKindFilter requiredValueKind = std::nullopt)
            -> const Geometry::PropertySet*
        {
            if (requiredValueKind.has_value() &&
                source.ValueKind != Geometry::PropertyValueKind::Unknown &&
                source.ValueKind != *requiredValueKind)
            {
                result.Status =
                    VisualizationRecipeStatus::UnsupportedSourceType;
                return nullptr;
            }

            const GeometryPropertyResolution resolution = requiredValueKind
                ? ResolveGeometryProperty(
                      availability,
                      source.Domain,
                      source.Name,
                      requiredValueKind,
                      expectedElementCount,
                      /*requireFiniteValues=*/false)
                : ResolveGeometryProperty(
                      availability,
                      source,
                      expectedElementCount,
                      /*requireFiniteValues=*/false);
            if (!resolution.Resolved())
            {
                result.Status = ToRecipeStatus(resolution.Status);
                return nullptr;
            }

            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, source.Domain);
            if (properties == nullptr)
            {
                result.Status = VisualizationRecipeStatus::UnsupportedDomain;
                return nullptr;
            }
            return properties;
        };

        const auto appendPropertyRecipe = [&result, &resolveSource](
            const GeometryPropertyRef& source,
            VisualizationEncodingOptions options,
            const auto& encodeProperty) -> bool
        {
            const std::optional<Graphics::VisualizationAttributeDomain> domain =
                ToVisualizationDomain(source.Domain);
            if (!domain.has_value())
            {
                result.Status = VisualizationRecipeStatus::UnsupportedDomain;
                return false;
            }

            const Geometry::PropertySet* properties = resolveSource(source);
            if (properties == nullptr)
                return false;

            options.SourceName = source.Name;
            options.Domain = *domain;
            if (options.BufferBDA == 0u && options.ColorBufferBDA == 0u)
            {
                options.DirtyStamp = properties->FindPropertyRevision(
                    source.Name).value_or(options.DirtyStamp);
            }
            encodeProperty(Geometry::ConstPropertySet{*properties},
                           result.Batch,
                           options,
                           result.Diagnostics);
            result.Status = ToRecipeStatus(result.Diagnostics);
            return result.Succeeded();
        };

        std::visit(
            [&](const auto& authored)
            {
                using Recipe = std::decay_t<decltype(authored)>;
                if constexpr (std::is_same_v<Recipe, std::monostate>)
                {
                    result.Status = VisualizationRecipeStatus::EmptyRecipe;
                }
                else if constexpr (std::is_same_v<Recipe, ScalarVisualizationRecipe>)
                {
                    (void)appendPropertyRecipe(
                        authored.Source,
                        VisualizationEncodingOptions{
                            .OutputName = authored.OutputName,
                            .BufferBDA = authored.BufferBDA,
                            .PropertyBufferSourceKey = authored.BufferSourceKey,
                            .DirtyStamp = authored.DirtyStamp,
                            .AutoRange = authored.AutoRange,
                            .RangeMin = authored.RangeMin,
                            .RangeMax = authored.RangeMax,
                            .Colormap = authored.Colormap,
                        },
                        EncodeScalarProperty);
                }
                else if constexpr (std::is_same_v<Recipe, ColorVisualizationRecipe> ||
                                   std::is_same_v<Recipe, LabelVisualizationRecipe>)
                {
                    (void)appendPropertyRecipe(
                        authored.Source,
                        VisualizationEncodingOptions{
                            .OutputName = authored.OutputName,
                            .ColorBufferBDA = authored.BufferBDA,
                            .PropertyBufferSourceKey = authored.BufferSourceKey,
                            .DirtyStamp = authored.DirtyStamp,
                        },
                        EncodeColorProperty);
                }
                else if constexpr (std::is_same_v<Recipe, VectorFieldVisualizationRecipe>)
                {
                    const std::optional<Graphics::VisualizationAttributeDomain> domain =
                        ToVisualizationDomain(authored.Source.Domain);
                    if (!domain.has_value())
                    {
                        result.Status = VisualizationRecipeStatus::UnsupportedDomain;
                        return;
                    }
                    if (authored.Source.ValueKind !=
                            Geometry::PropertyValueKind::Unknown &&
                        authored.Source.ValueKind !=
                            Geometry::PropertyValueKind::Vec3)
                    {
                        result.Status =
                            VisualizationRecipeStatus::UnsupportedSourceType;
                        return;
                    }

                    const GeometryPropertyResolution vectorResolution =
                        ResolveGeometryProperty(
                            availability,
                            authored.Source.Domain,
                            authored.Source.Name,
                            Geometry::PropertyValueKind::Vec3);
                    if (!vectorResolution.Resolved())
                    {
                        result.Status = ToRecipeStatus(vectorResolution.Status);
                        return;
                    }
                    if (resolveSource(
                            authored.PositionSource,
                            vectorResolution.ElementCount,
                            Geometry::PropertyValueKind::Vec3) == nullptr)
                    {
                        return;
                    }
                    const Geometry::PropertySet* properties =
                        ResolveGeometryPropertySet(
                            availability, authored.Source.Domain);
                    if (properties == nullptr)
                    {
                        result.Status = VisualizationRecipeStatus::UnsupportedDomain;
                        return;
                    }

                    EncodeVectorProperty(
                        Geometry::ConstPropertySet{*properties},
                        result.Batch,
                        VisualizationEncodingOptions{
                            .SourceName = authored.Source.Name,
                            .OutputName = authored.OutputName,
                            .Domain = *domain,
                            .PositionBufferBDA = authored.PositionBufferBDA,
                            .VectorBufferBDA = authored.VectorBufferBDA,
                            .PositionBufferSourceKey =
                                authored.PositionBufferSourceKey.empty()
                                    ? authored.PositionSource.Name
                                    : authored.PositionBufferSourceKey,
                            .VectorBufferSourceKey = authored.VectorBufferSourceKey,
                            .DirtyStamp = authored.VectorBufferBDA == 0u
                                ? properties->FindPropertyRevision(
                                      authored.Source.Name).value_or(
                                      authored.DirtyStamp)
                                : authored.DirtyStamp,
                            .VectorScale = authored.Scale,
                            .VectorColor = authored.Color,
                            .DepthTested = authored.DepthTested,
                        },
                        result.Diagnostics);
                    result.Status = ToRecipeStatus(result.Diagnostics);
                }
                else if constexpr (std::is_same_v<Recipe, IsolineVisualizationRecipe>)
                {
                    (void)appendPropertyRecipe(
                        authored.Source,
                        VisualizationEncodingOptions{
                            .OutputName = authored.OutputName,
                            .BufferBDA = authored.BufferBDA,
                            .PropertyBufferSourceKey = authored.BufferSourceKey,
                            .DirtyStamp = authored.DirtyStamp,
                            .AutoRange = authored.AutoRange,
                            .RangeMin = authored.RangeMin,
                            .RangeMax = authored.RangeMax,
                            .IsoValueCount = authored.IsoValueCount,
                            .LineWidth = authored.LineWidth,
                            .OverlayColor = authored.Color,
                            .DepthTested = authored.DepthTested,
                        },
                        EncodeIsolineProperty);
                }
                else if constexpr (std::is_same_v<Recipe, HtexPreviewVisualizationRecipe>)
                {
                    if (authored.Name.empty() || authored.PatchCount == 0u ||
                        authored.AtlasWidth == 0u || authored.AtlasHeight == 0u)
                    {
                        result.Status = VisualizationRecipeStatus::InvalidResource;
                        ++result.Diagnostics.InvalidResourceCount;
                        return;
                    }
                    result.Batch.HtexAtlases.push_back(
                        Graphics::HtexPatchPreviewAtlasPacket{
                            .Name = authored.Name,
                            .PatchCount = authored.PatchCount,
                            .AtlasWidth = authored.AtlasWidth,
                            .AtlasHeight = authored.AtlasHeight,
                        });
                    ++result.Diagnostics.PacketAppendCount;
                    result.Status = VisualizationRecipeStatus::Encoded;
                }
                else if constexpr (std::is_same_v<Recipe, FragmentBakeVisualizationRecipe>)
                {
                    if (authored.Name.empty() || authored.FaceCount == 0u ||
                        authored.AtlasWidth == 0u || authored.AtlasHeight == 0u)
                    {
                        result.Status = VisualizationRecipeStatus::InvalidResource;
                        ++result.Diagnostics.InvalidResourceCount;
                        return;
                    }
                    const Geometry::PropertySet* sourceProperties =
                        resolveSource(authored.Source);
                    if (sourceProperties == nullptr)
                        return;

                    const std::uint64_t sourceAttributeRevision =
                        sourceProperties->FindPropertyRevision(
                            authored.Source.Name).value_or(
                                authored.SourceAttributeDirtyStamp);
                    std::uint64_t texcoordRevision =
                        authored.TexcoordDirtyStamp;
                    if (authored.TexcoordBufferBDA == 0u)
                    {
                        // BUG-137 — the CPU bake reads whichever domain owns
                        // the UVs, so its dirty stamp has to follow the same
                        // corner-over-vertex resolution order. Watching only
                        // `v:texcoord` left a seam-carrying mesh — which has
                        // no `v:texcoord` at all — pinned to the authored
                        // stamp, so editing its corner UVs never re-baked.
                        const auto revisionIn =
                            [&availability](
                                const GeometryElementDomain domain,
                                const std::string_view name)
                            -> std::optional<std::uint64_t>
                        {
                            const Geometry::PropertySet* properties =
                                ResolveGeometryPropertySet(availability, domain);
                            return properties != nullptr
                                ? properties->FindPropertyRevision(name)
                                : std::nullopt;
                        };

                        const std::optional<std::uint64_t> resolved =
                            revisionIn(
                                GeometryElementDomain::MeshHalfedge,
                                "h:texcoord");
                        texcoordRevision =
                            resolved.has_value()
                                ? *resolved
                                : revisionIn(
                                      GeometryElementDomain::MeshVertex,
                                      "v:texcoord")
                                      .value_or(texcoordRevision);
                    }

                    switch (authored.Mapping)
                    {
                    case Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords:
                        if (!authored.MeshHasTexcoords ||
                            (authored.TexcoordBufferBDA == 0u &&
                             authored.TexcoordBufferSourceKey.empty()))
                        {
                            result.Status = VisualizationRecipeStatus::MissingTexcoord;
                            ++result.Diagnostics.MissingTexcoordCount;
                            return;
                        }
                        break;
                    case Graphics::VisualizationFragmentBakeMapping::ExistingHtex:
                    case Graphics::VisualizationFragmentBakeMapping::RecreateHtex:
                        break;
                    default:
                        result.Status = VisualizationRecipeStatus::InvalidResource;
                        ++result.Diagnostics.InvalidResourceCount;
                        return;
                    }

                    result.Batch.FragmentBakeAtlases.push_back(
                        Graphics::FragmentBakeAtlasPacket{
                            .Name = authored.Name,
                            .SourceAttributeName = authored.Source.Name,
                            .TexcoordBufferSourceKey =
                                authored.TexcoordBufferSourceKey,
                            .Mapping = authored.Mapping,
                            .MeshHasTexcoords = authored.MeshHasTexcoords,
                            .FaceCount = authored.FaceCount,
                            .AtlasWidth = authored.AtlasWidth,
                            .AtlasHeight = authored.AtlasHeight,
                            .TexcoordBufferBDA = authored.TexcoordBufferBDA,
                            .AtlasTextureAsset = authored.AtlasTextureAsset,
                            .GeneratedTextureSemantic =
                                authored.GeneratedTextureSemantic,
                            .TexcoordProvenance =
                                authored.Mapping == Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords
                                    ? Graphics::VisualizationTexcoordProvenance::RuntimeResolved
                                    : Graphics::VisualizationTexcoordProvenance::Unknown,
                            .TexcoordDirtyStamp = texcoordRevision,
                            .SourceAttributeDirtyStamp =
                                sourceAttributeRevision,
                        });
                    ++result.Diagnostics.PacketAppendCount;
                    result.Status = VisualizationRecipeStatus::Encoded;
                }
            },
            recipe.Data);

        return result;
    }

    VisualizationHtexRecreateResult ScheduleVisualizationHtexRecreate(
        JobService& jobs,
        const VisualizationHtexRecreateRequest& request)
    {
        VisualizationHtexRecreateResult result{};
        result.Task = jobs.Submit(JobDesc{
            .DebugName = request.DebugName.empty()
                ? std::string{"Visualization.HtexRecreate"}
                : std::string{"Visualization.HtexRecreate."} + request.DebugName,
            .Scope = request.World,
            .EstimatedCost = 1u,
            .Work = [payloadToken = request.PayloadToken](const JobCancellation&)
            {
                return JobResultEnvelope::Make<HtexRecreateScheduled>(
                    HtexRecreateScheduled{.PayloadToken = payloadToken});
            },
            .PublishCompletion = [](KernelEventBus& events,
                                    const JobResultEnvelope& envelope) -> bool
            {
                const HtexRecreateScheduled* payload =
                    envelope.TryGet<HtexRecreateScheduled>();
                if (payload == nullptr)
                    return false;
                events.Publish(*payload);
                return true;
            },
        });
        if (!result.Task.IsValid())
            result.Diagnostic = "JobService rejected visualization HTEX recreate request";
        return result;
    }

    void VisualizationEncodingBatch::Clear() noexcept
    {
        PropertyBuffers.clear();
        PropertyBufferPayloads.clear();
        AttributeBuffers.clear();
        Scalars.clear();
        Colors.clear();
        VectorFields.clear();
        Isolines.clear();
        HtexAtlases.clear();
        FragmentBakeAtlases.clear();
    }

    void VisualizationEncodingBatch::Append(VisualizationEncodingBatch&& other)
    {
        PropertyBufferPayloads.insert(
            PropertyBufferPayloads.end(),
            std::make_move_iterator(other.PropertyBufferPayloads.begin()),
            std::make_move_iterator(other.PropertyBufferPayloads.end()));
        PropertyBuffers.insert(
            PropertyBuffers.end(),
            std::make_move_iterator(other.PropertyBuffers.begin()),
            std::make_move_iterator(other.PropertyBuffers.end()));
        AttributeBuffers.insert(
            AttributeBuffers.end(),
            std::make_move_iterator(other.AttributeBuffers.begin()),
            std::make_move_iterator(other.AttributeBuffers.end()));
        Scalars.insert(Scalars.end(),
                       std::make_move_iterator(other.Scalars.begin()),
                       std::make_move_iterator(other.Scalars.end()));
        Colors.insert(Colors.end(),
                      std::make_move_iterator(other.Colors.begin()),
                      std::make_move_iterator(other.Colors.end()));
        VectorFields.insert(
            VectorFields.end(),
            std::make_move_iterator(other.VectorFields.begin()),
            std::make_move_iterator(other.VectorFields.end()));
        Isolines.insert(Isolines.end(),
                        std::make_move_iterator(other.Isolines.begin()),
                        std::make_move_iterator(other.Isolines.end()));
        HtexAtlases.insert(
            HtexAtlases.end(),
            std::make_move_iterator(other.HtexAtlases.begin()),
            std::make_move_iterator(other.HtexAtlases.end()));
        FragmentBakeAtlases.insert(
            FragmentBakeAtlases.end(),
            std::make_move_iterator(other.FragmentBakeAtlases.begin()),
            std::make_move_iterator(other.FragmentBakeAtlases.end()));
    }

    Graphics::VisualizationPacketBatch VisualizationEncodingBatch::AsPacketBatch(
        const bool enforceDomain,
        const Graphics::VisualizationAttributeDomain expectedDomain) const noexcept
    {
        return Graphics::VisualizationPacketBatch{
            .PropertyBuffers = PropertyBuffers,
            .AttributeBuffers = AttributeBuffers,
            .Scalars = Scalars,
            .Colors = Colors,
            .VectorFields = VectorFields,
            .Isolines = Isolines,
            .HtexAtlases = HtexAtlases,
            .FragmentBakeAtlases = FragmentBakeAtlases,
            .EnforceDomain = enforceDomain,
            .ExpectedDomain = expectedDomain,
        };
    }

}
