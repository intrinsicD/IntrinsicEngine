module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.VisualizationAdapters;

import Geometry.Properties;
import Extrinsic.Graphics.VisualizationPackets;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value);
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
            }

            out.Scalars.push_back(Graphics::ScalarAttributePacket{
                .Name = options.OutputName.empty() ? options.SourceName : options.OutputName,
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
    }

    void VisualizationAdapterBatch::Clear() noexcept
    {
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
        if (options.BufferBDA == 0u)
        {
            ++stats.InvalidBufferCount;
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
