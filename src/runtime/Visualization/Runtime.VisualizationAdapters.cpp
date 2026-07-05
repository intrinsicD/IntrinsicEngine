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
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.VisualizationAdapters;

import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.StreamingExecutor;

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

        template <typename T>
        [[nodiscard]] bool ComputeRange(std::span<const T> values,
                                        float& minOut,
                                        float& maxOut,
                                        VisualizationAdapterStats& stats) noexcept
        {
            if (values.empty())
            {
                ++stats.EmptySourceCount;
                return false;
            }

            float first = 0.0f;
            ++stats.ScalarValueScanCount;
            if (!ToFiniteFloat(values.front(), first))
            {
                ++stats.NonFiniteValueCount;
                return false;
            }

            float minValue = first;
            float maxValue = first;
            for (const T value : values.subspan(1u))
            {
                float converted = 0.0f;
                ++stats.ScalarValueScanCount;
                if (!ToFiniteFloat(value, converted))
                {
                    ++stats.NonFiniteValueCount;
                    return false;
                }

                minValue = std::min(minValue, converted);
                maxValue = std::max(maxValue, converted);
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

        [[nodiscard]] bool ScheduleHtexRecreate(
            StreamingExecutor* executor,
            const std::string& packetName,
            const VisualizationAdapterOptions& options,
            VisualizationAdapterStats& stats)
        {
            if (executor == nullptr)
            {
                ++stats.InvalidResourceCount;
                return false;
            }

            const StreamingTaskHandle handle = executor->Submit(StreamingTaskDesc{
                .Name = packetName.empty()
                    ? std::string{"Visualization.HtexRecreate"}
                    : std::string{"Visualization.HtexRecreate."} + packetName,
                .EstimatedCost = 1u,
                .Execute = [payloadToken = options.HtexRecreatePayloadToken]()
                {
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = payloadToken}};
                },
            });

            if (!handle.IsValid())
            {
                ++stats.InvalidResourceCount;
                return false;
            }

            ++stats.HtexRecreateScheduledCount;
            stats.LastHtexRecreateTask = handle;
            return true;
        }
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

    HtexMetadataAdapter::HtexMetadataAdapter(StreamingExecutor* executor) noexcept
        : m_Executor(executor)
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
                if (!ScheduleHtexRecreate(m_Executor, name, options, stats))
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
