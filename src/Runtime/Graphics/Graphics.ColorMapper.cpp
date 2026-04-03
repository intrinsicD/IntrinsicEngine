module;

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Graphics.ColorMapper;

import Graphics.Colormap;
import Graphics.GpuColor;
import Graphics.VisualizationConfig;
import Geometry.Properties;

using namespace Graphics;

namespace
{
    template <class PropertySetT>
    [[nodiscard]] std::optional<ColorMapper::MappingResult> MapScalar(
        const PropertySetT& ps,
        ColorSource& config,
        const std::function<bool(size_t)>& skipDeleted)
    {
        auto prop = ps.template Get<float>(config.PropertyName);
        if (!prop.IsValid())
            return std::nullopt;

        const auto& data = prop.Vector();
        const size_t count = data.size();

        // Determine which elements are active.
        std::vector<size_t> activeIndices;
        activeIndices.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            if (skipDeleted && skipDeleted(i))
                continue;
            activeIndices.push_back(i);
        }

        if (activeIndices.empty())
            return ColorMapper::MappingResult{};

        // Auto-range: compute min/max from active elements.
        float dataMin = std::numeric_limits<float>::max();
        float dataMax = std::numeric_limits<float>::lowest();

        if (config.AutoRange)
        {
            for (const size_t i : activeIndices)
            {
                const float v = data[i];
                if (!std::isfinite(v)) continue;
                dataMin = std::min(dataMin, v);
                dataMax = std::max(dataMax, v);
            }

            if (dataMin > dataMax)
            {
                // All values were non-finite.
                dataMin = 0.0f;
                dataMax = 1.0f;
            }
            else if (dataMin == dataMax)
            {
                // Constant field — center it.
                dataMin -= 0.5f;
                dataMax += 0.5f;
            }

            config.RangeMin = dataMin;
            config.RangeMax = dataMax;
        }

        const float rangeMin = config.RangeMin;
        const float rangeMax = config.RangeMax;
        const float rangeSpan = (rangeMax - rangeMin);
        const float invRange = (rangeSpan > 1e-12f) ? 1.0f / rangeSpan : 0.0f;

        ColorMapper::MappingResult result;
        result.Colors.reserve(activeIndices.size());
        result.ComputedMin = config.RangeMin;
        result.ComputedMax = config.RangeMax;

        for (const size_t i : activeIndices)
        {
            const float v = data[i];
            float t = std::isfinite(v) ? (v - rangeMin) * invRange : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);

            result.Colors.push_back(
                config.Bins > 0
                    ? Colormap::SampleBinned(config.Map, t, config.Bins)
                    : Colormap::Sample(config.Map, t));
        }

        return result;
    }

    template <class PropertySetT>
    [[nodiscard]] std::optional<ColorMapper::MappingResult> MapVec2(
        const PropertySetT& ps,
        const ColorSource& config,
        const std::function<bool(size_t)>& skipDeleted)
    {
        auto prop = ps.template Get<glm::vec2>(config.PropertyName);
        if (!prop.IsValid())
            return std::nullopt;

        const auto& data = prop.Vector();
        const size_t count = data.size();

        ColorMapper::MappingResult result;
        result.Colors.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            if (skipDeleted && skipDeleted(i))
                continue;

            const auto& c = data[i];
            result.Colors.push_back(GpuColor::PackColorF(c.x, c.y, 0.0f, 1.0f));
        }

        return result;
    }

    template <class PropertySetT>
    [[nodiscard]] std::optional<ColorMapper::MappingResult> MapVec3(
        const PropertySetT& ps,
        const ColorSource& config,
        const std::function<bool(size_t)>& skipDeleted)
    {
        auto prop = ps.template Get<glm::vec3>(config.PropertyName);
        if (!prop.IsValid())
            return std::nullopt;

        const auto& data = prop.Vector();
        const size_t count = data.size();

        ColorMapper::MappingResult result;
        result.Colors.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            if (skipDeleted && skipDeleted(i))
                continue;

            const auto& c = data[i];
            result.Colors.push_back(GpuColor::PackColorF(c.r, c.g, c.b, 1.0f));
        }

        return result;
    }

    template <class PropertySetT>
    [[nodiscard]] std::optional<ColorMapper::MappingResult> MapVec4(
        const PropertySetT& ps,
        const ColorSource& config,
        const std::function<bool(size_t)>& skipDeleted)
    {
        auto prop = ps.template Get<glm::vec4>(config.PropertyName);
        if (!prop.IsValid())
            return std::nullopt;

        const auto& data = prop.Vector();
        const size_t count = data.size();

        ColorMapper::MappingResult result;
        result.Colors.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            if (skipDeleted && skipDeleted(i))
                continue;

            const auto& c = data[i];
            result.Colors.push_back(GpuColor::PackColorF(c.r, c.g, c.b, c.a));
        }

        return result;
    }
} // namespace

std::optional<ColorMapper::MappingResult> ColorMapper::MapProperty(
    const Geometry::PropertySet& ps,
    ColorSource& config,
    std::function<bool(size_t)> skipDeleted)
{
    return MapProperty(Geometry::ConstPropertySet(ps), config, std::move(skipDeleted));
}

std::optional<ColorMapper::MappingResult> ColorMapper::MapProperty(
    const Geometry::ConstPropertySet& ps,
    ColorSource& config,
    std::function<bool(size_t)> skipDeleted)
{
    if (config.PropertyName.empty())
        return std::nullopt;

    // Try each type. float first (most common for scalar fields),
    // then vec4 (RGBA color), then vec3 (RGB), then vec2 (UV → RG).
    if (auto r = MapScalar(ps, config, skipDeleted))
        return r;
    if (auto r = MapVec4(ps, config, skipDeleted))
        return r;
    if (auto r = MapVec3(ps, config, skipDeleted))
        return r;
    if (auto r = MapVec2(ps, config, skipDeleted))
        return r;

    return std::nullopt;
}
