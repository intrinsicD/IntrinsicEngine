module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdint>
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

module Extrinsic.Runtime.VisualizationAdapters;

import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;

namespace Extrinsic::Runtime
{
    namespace
    {
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
            const VisualizationAdapterStats& stats) noexcept
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
            VisualizationAdapterBatch& out,
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
                                        VisualizationAdapterStats& stats)
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
                                VisualizationAdapterBatch& out,
                                const VisualizationAdapterOptions& options,
                                VisualizationAdapterStats& stats)
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
                                              VisualizationAdapterStats& stats) noexcept
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
                                                      VisualizationAdapterStats& stats) noexcept
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
                               VisualizationAdapterBatch& out,
                               const VisualizationAdapterOptions& options,
                               VisualizationAdapterStats& stats)
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
                                 VisualizationAdapterBatch& out,
                                 const VisualizationAdapterOptions& options,
                                 VisualizationAdapterStats& stats)
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
                                     VisualizationAdapterBatch& out,
                                     const VisualizationAdapterOptions& options,
                                     VisualizationAdapterStats& stats)
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

        [[nodiscard]] std::string PacketName(
            const VisualizationAdapterOptions& options)
        {
            return options.OutputName.empty() ? options.SourceName : options.OutputName;
        }

        [[nodiscard]] std::string CurvatureDirectionName(
            const std::string& configured,
            const std::string_view fallback)
        {
            return configured.empty() ? std::string{fallback} : configured;
        }

        [[nodiscard]] std::string CurvatureDirectionPacketName(
            const VisualizationAdapterOptions& options,
            const std::string_view suffix)
        {
            const std::string base = PacketName(options);
            if (base.empty())
                return std::string{suffix};
            std::string name = base;
            name += ".";
            name += suffix;
            return name;
        }

        [[nodiscard]] std::string FragmentSourceAttributeName(
            const VisualizationAdapterOptions& options)
        {
            return options.SourceAttributeName.empty()
                ? options.SourceName
                : options.SourceAttributeName;
        }

        [[nodiscard]] bool ValidAtlasDimensions(
            const VisualizationAdapterOptions& options) noexcept
        {
            return options.AtlasWidth > 0u && options.AtlasHeight > 0u;
        }

        // Scheduling receipt for an Htex regeneration request. The adapter is
        // CPU-contracted: it proves the request was accepted and scheduled, and
        // deliberately does not run a regeneration algorithm, so the job body
        // only carries the caller's payload token forward.
        struct HtexRecreateScheduled
        {
            std::uint64_t PayloadToken{0u};
        };

        [[nodiscard]] bool ScheduleHtexRecreate(
            JobService* jobs,
            const WorldHandle world,
            const std::string& packetName,
            const VisualizationAdapterOptions& options,
            VisualizationAdapterStats& stats)
        {
            if (jobs == nullptr)
            {
                ++stats.InvalidResourceCount;
                return false;
            }

            const VisualizationHtexRecreateResult scheduled =
                ScheduleVisualizationHtexRecreate(
                    *jobs,
                    VisualizationHtexRecreateRequest{
                        .DebugName = packetName,
                        .World = world,
                        .PayloadToken = options.HtexRecreatePayloadToken,
                    });
            if (!scheduled.Scheduled())
            {
                ++stats.InvalidResourceCount;
                return false;
            }

            ++stats.HtexRecreateScheduledCount;
            stats.LastHtexRecreateTask = scheduled.Task;
            return true;
        }
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
            VisualizationAdapterOptions options,
            const auto& adapterFactory) -> bool
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
            adapterFactory(Geometry::ConstPropertySet{*properties})
                .Append(result.Batch, options, result.Diagnostics);
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
                        VisualizationAdapterOptions{
                            .OutputName = authored.OutputName,
                            .BufferBDA = authored.BufferBDA,
                            .PropertyBufferSourceKey = authored.BufferSourceKey,
                            .DirtyStamp = authored.DirtyStamp,
                            .AutoRange = authored.AutoRange,
                            .RangeMin = authored.RangeMin,
                            .RangeMax = authored.RangeMax,
                            .Colormap = authored.Colormap,
                        },
                        [](Geometry::ConstPropertySet properties)
                        {
                            return PropertyScalarAdapter{std::move(properties)};
                        });
                }
                else if constexpr (std::is_same_v<Recipe, ColorVisualizationRecipe> ||
                                   std::is_same_v<Recipe, LabelVisualizationRecipe>)
                {
                    (void)appendPropertyRecipe(
                        authored.Source,
                        VisualizationAdapterOptions{
                            .OutputName = authored.OutputName,
                            .ColorBufferBDA = authored.BufferBDA,
                            .PropertyBufferSourceKey = authored.BufferSourceKey,
                            .DirtyStamp = authored.DirtyStamp,
                        },
                        [](Geometry::ConstPropertySet properties)
                        {
                            return KMeansLabelAdapter{std::move(properties)};
                        });
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

                    VectorFieldAdapter{Geometry::ConstPropertySet{*properties}}.Append(
                        result.Batch,
                        VisualizationAdapterOptions{
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
                            .DirtyStamp = authored.DirtyStamp,
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
                        VisualizationAdapterOptions{
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
                        [](Geometry::ConstPropertySet properties)
                        {
                            return IsolineAdapter{std::move(properties)};
                        });
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
                    if (resolveSource(authored.Source) == nullptr)
                        return;

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
                            .TexcoordDirtyStamp = authored.TexcoordDirtyStamp,
                            .SourceAttributeDirtyStamp =
                                authored.SourceAttributeDirtyStamp,
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

    void VisualizationAdapterBatch::Clear() noexcept
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

    Graphics::VisualizationPacketBatch VisualizationAdapterBatch::AsPacketBatch(
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

    PropertyScalarAdapter::PropertyScalarAdapter(
        Geometry::ConstPropertySet properties) noexcept
        : m_Properties(std::move(properties))
    {
    }

    void PropertyScalarAdapter::Append(VisualizationAdapterBatch& out,
                                       const VisualizationAdapterOptions& options,
                                       VisualizationAdapterStats& stats) const
    {
        ++stats.AdapterInvocationCount;

        if (options.SourceName.empty())
        {
            ++stats.MissingSourceCount;
            return;
        }

        if (const auto floatProperty = m_Properties.Get<float>(options.SourceName);
            floatProperty.IsValid())
        {
            (void)AppendScalarPacket(floatProperty, out, options, stats);
            return;
        }

        if (const auto doubleProperty = m_Properties.Get<double>(options.SourceName);
            doubleProperty.IsValid())
        {
            (void)AppendScalarPacket(doubleProperty, out, options, stats);
            return;
        }

        if (m_Properties.Exists(options.SourceName))
        {
            ++stats.UnsupportedSourceTypeCount;
        }
        else
        {
            ++stats.MissingSourceCount;
        }
    }

    KMeansLabelAdapter::KMeansLabelAdapter(
        Geometry::ConstPropertySet properties) noexcept
        : m_Properties(std::move(properties))
    {
    }

    void KMeansLabelAdapter::Append(VisualizationAdapterBatch& out,
                                    const VisualizationAdapterOptions& options,
                                    VisualizationAdapterStats& stats) const
    {
        ++stats.AdapterInvocationCount;

        if (options.SourceName.empty())
        {
            ++stats.MissingSourceCount;
            return;
        }

        if (const auto colors = m_Properties.Get<glm::vec4>(options.SourceName);
            colors.IsValid())
        {
            (void)AppendColorPacket(colors, out, options, stats);
            return;
        }

        if (m_Properties.Exists(options.SourceName))
        {
            ++stats.UnsupportedSourceTypeCount;
        }
        else
        {
            ++stats.MissingSourceCount;
        }
    }

    VectorFieldAdapter::VectorFieldAdapter(
        Geometry::ConstPropertySet properties) noexcept
        : m_Properties(std::move(properties))
    {
    }

    void VectorFieldAdapter::Append(VisualizationAdapterBatch& out,
                                    const VisualizationAdapterOptions& options,
                                    VisualizationAdapterStats& stats) const
    {
        ++stats.AdapterInvocationCount;

        if (options.SourceName.empty())
        {
            ++stats.MissingSourceCount;
            return;
        }

        if (const auto vectors = m_Properties.Get<glm::vec3>(options.SourceName);
            vectors.IsValid())
        {
            (void)AppendVectorFieldPacket(vectors, out, options, stats);
            return;
        }

        if (m_Properties.Exists(options.SourceName))
        {
            ++stats.UnsupportedSourceTypeCount;
        }
        else
        {
            ++stats.MissingSourceCount;
        }
    }

    CurvatureVisualizationAdapter::CurvatureVisualizationAdapter(
        Geometry::ConstPropertySet properties) noexcept
        : m_Properties(std::move(properties))
    {
    }

    void CurvatureVisualizationAdapter::Append(
        VisualizationAdapterBatch& out,
        const VisualizationAdapterOptions& options,
        VisualizationAdapterStats& stats) const
    {
        namespace GS = Extrinsic::ECS::Components::GeometrySources;

        ++stats.AdapterInvocationCount;

        if (options.SourceName.empty())
        {
            ++stats.MissingSourceCount;
            return;
        }

        std::optional<std::size_t> scalarCount{};
        bool scalarAppended = false;
        if (const auto floatProperty = m_Properties.Get<float>(options.SourceName);
            floatProperty.IsValid())
        {
            scalarCount = floatProperty.Span().size();
            scalarAppended = AppendScalarPacket(floatProperty, out, options, stats);
        }
        else if (const auto doubleProperty =
                     m_Properties.Get<double>(options.SourceName);
                 doubleProperty.IsValid())
        {
            scalarCount = doubleProperty.Span().size();
            scalarAppended = AppendScalarPacket(doubleProperty, out, options, stats);
        }
        else
        {
            if (m_Properties.Exists(options.SourceName))
                ++stats.UnsupportedSourceTypeCount;
            else
                ++stats.MissingSourceCount;
            return;
        }

        if (!scalarAppended || !options.EmitPrincipalDirections)
            return;

        struct DirectionRequest
        {
            std::string SourceName{};
            std::string Suffix{};
            Geometry::ConstProperty<glm::vec3> Property{};
        };

        std::vector<DirectionRequest> directions{};
        directions.reserve(2u);
        const auto prepareDirection =
            [&](std::string sourceName,
                std::string suffix) -> bool
            {
                if (sourceName.empty())
                {
                    ++stats.MissingSourceCount;
                    return false;
                }

                const auto vectors = m_Properties.Get<glm::vec3>(sourceName);
                if (!vectors.IsValid())
                {
                    if (m_Properties.Exists(sourceName))
                        ++stats.UnsupportedSourceTypeCount;
                    else
                        ++stats.MissingSourceCount;
                    return false;
                }
                if (scalarCount.has_value() &&
                    vectors.Span().size() != *scalarCount)
                {
                    ++stats.InvalidResourceCount;
                    return false;
                }

                directions.push_back(DirectionRequest{
                    .SourceName = std::move(sourceName),
                    .Suffix = std::move(suffix),
                    .Property = vectors,
                });
                return true;
            };

        bool directionsValid = true;
        if (options.EmitPrincipalDirection1)
        {
            directionsValid = prepareDirection(
                CurvatureDirectionName(
                    options.PrincipalDirection1SourceName,
                    GS::PropertyNames::kPrincipalDir1),
                "principal_dir1") &&
                directionsValid;
        }
        if (options.EmitPrincipalDirection2)
        {
            directionsValid = prepareDirection(
                CurvatureDirectionName(
                    options.PrincipalDirection2SourceName,
                    GS::PropertyNames::kPrincipalDir2),
                "principal_dir2") &&
                directionsValid;
        }
        if (!directionsValid)
            return;

        for (const DirectionRequest& direction : directions)
        {
                VisualizationAdapterOptions directionOptions = options;
                directionOptions.SourceName = direction.SourceName;
                directionOptions.OutputName =
                    CurvatureDirectionPacketName(options, direction.Suffix);
                directionOptions.PropertyBufferSourceKey.clear();
                directionOptions.VectorBufferSourceKey =
                    directionOptions.OutputName;
                directionOptions.VectorBufferBDA = 0u;
                (void)AppendVectorFieldPacket(
                    direction.Property,
                    out,
                    directionOptions,
                    stats);
        }
    }

    IsolineAdapter::IsolineAdapter(
        Geometry::ConstPropertySet properties) noexcept
        : m_Properties(std::move(properties))
    {
    }

    void IsolineAdapter::Append(VisualizationAdapterBatch& out,
                                const VisualizationAdapterOptions& options,
                                VisualizationAdapterStats& stats) const
    {
        ++stats.AdapterInvocationCount;

        if (options.SourceName.empty())
        {
            ++stats.MissingSourceCount;
            return;
        }

        if (const auto floatProperty = m_Properties.Get<float>(options.SourceName);
            floatProperty.IsValid())
        {
            (void)AppendIsolinePacket(floatProperty, out, options, stats);
            return;
        }

        if (const auto doubleProperty = m_Properties.Get<double>(options.SourceName);
            doubleProperty.IsValid())
        {
            (void)AppendIsolinePacket(doubleProperty, out, options, stats);
            return;
        }

        if (m_Properties.Exists(options.SourceName))
        {
            ++stats.UnsupportedSourceTypeCount;
        }
        else
        {
            ++stats.MissingSourceCount;
        }
    }

    HtexMetadataAdapter::HtexMetadataAdapter(
        JobService* jobs,
        const WorldHandle world) noexcept
        : m_Jobs(jobs)
        , m_World(world)
    {
    }

    void HtexMetadataAdapter::Append(VisualizationAdapterBatch& out,
                                     const VisualizationAdapterOptions& options,
                                     VisualizationAdapterStats& stats) const
    {
        ++stats.AdapterInvocationCount;

        if (!options.EmitHtexPreview && !options.EmitFragmentBake)
        {
            ++stats.MissingSourceCount;
            return;
        }

        const std::string name = PacketName(options);
        Graphics::HtexPatchPreviewAtlasPacket htexPacket{};
        bool hasHtexPacket = false;
        if (options.EmitHtexPreview)
        {
            if (name.empty() || options.PatchCount == 0u ||
                !ValidAtlasDimensions(options))
            {
                ++stats.InvalidResourceCount;
                return;
            }

            htexPacket = Graphics::HtexPatchPreviewAtlasPacket{
                .Name = name,
                .PatchCount = options.PatchCount,
                .AtlasWidth = options.AtlasWidth,
                .AtlasHeight = options.AtlasHeight,
            };
            hasHtexPacket = true;
        }

        Graphics::FragmentBakeAtlasPacket bakePacket{};
        bool hasBakePacket = false;
        if (options.EmitFragmentBake)
        {
            const std::string sourceAttribute =
                FragmentSourceAttributeName(options);
            if (name.empty() || sourceAttribute.empty() ||
                options.FaceCount == 0u || !ValidAtlasDimensions(options))
            {
                ++stats.InvalidResourceCount;
                return;
            }

            switch (options.FragmentBakeMapping)
            {
            case Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords:
                if (!options.MeshHasTexcoords ||
                    options.TexcoordBufferBDA == 0u)
                {
                    ++stats.MissingTexcoordCount;
                    return;
                }
                break;
            case Graphics::VisualizationFragmentBakeMapping::ExistingHtex:
                break;
            case Graphics::VisualizationFragmentBakeMapping::RecreateHtex:
                if (!ScheduleHtexRecreate(
                        m_Jobs, m_World, name, options, stats))
                    return;
                break;
            default:
                ++stats.InvalidResourceCount;
                return;
            }

            bakePacket = Graphics::FragmentBakeAtlasPacket{
                .Name = name,
                .SourceAttributeName = sourceAttribute,
                .Mapping = options.FragmentBakeMapping,
                .MeshHasTexcoords = options.MeshHasTexcoords,
                .FaceCount = options.FaceCount,
                .AtlasWidth = options.AtlasWidth,
                .AtlasHeight = options.AtlasHeight,
                .TexcoordBufferBDA = options.TexcoordBufferBDA,
                .AtlasTextureAsset = options.AtlasTextureAsset,
                .GeneratedTextureSemantic =
                    options.GeneratedTextureSemantic,
                .TexcoordProvenance =
                    options.FragmentBakeMapping ==
                            Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords
                        ? Graphics::VisualizationTexcoordProvenance::RuntimeResolved
                        : Graphics::VisualizationTexcoordProvenance::Unknown,
                .TexcoordDirtyStamp = options.DirtyStamp,
                .SourceAttributeDirtyStamp =
                    options.SourceAttributeDirtyStamp,
            };
            hasBakePacket = true;
        }

        if (hasHtexPacket)
        {
            out.HtexAtlases.push_back(std::move(htexPacket));
            ++stats.PacketAppendCount;
        }
        if (hasBakePacket)
        {
            out.FragmentBakeAtlases.push_back(std::move(bakePacket));
            ++stats.PacketAppendCount;
        }
    }

    void VisualizationAdapterRegistry::Register(
        const Key key,
        const IVisualizationAdapter& adapter)
    {
        m_Adapters.insert_or_assign(key, &adapter);
    }

    bool VisualizationAdapterRegistry::Unregister(const Key key) noexcept
    {
        return m_Adapters.erase(key) != 0u;
    }

    const IVisualizationAdapter* VisualizationAdapterRegistry::Find(
        const Key key) const noexcept
    {
        const auto it = m_Adapters.find(key);
        return it != m_Adapters.end() ? it->second : nullptr;
    }

    bool VisualizationAdapterRegistry::Contains(const Key key) const noexcept
    {
        return Find(key) != nullptr;
    }

    std::size_t VisualizationAdapterRegistry::Size() const noexcept
    {
        return m_Adapters.size();
    }

    bool VisualizationAdapterRegistry::Empty() const noexcept
    {
        return m_Adapters.empty();
    }

    void VisualizationAdapterRegistry::Clear() noexcept
    {
        m_Adapters.clear();
    }
}
