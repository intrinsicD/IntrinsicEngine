module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

#include <glm/glm.hpp>

module Extrinsic.Runtime.VertexAttributeBinding;

import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr float kDegenerateLengthEpsilon = 1.0e-6f;

        [[nodiscard]] bool IsFinite(const glm::vec2& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
                   std::isfinite(v.w);
        }

        // Classify a whole-property failure so callers and tests see the precise
        // reason. `properties.Exists` distinguishes a missing name from a present
        // name whose stored element type does not match the requested `Get<T>`.
        [[nodiscard]] AttributeBindStatus ClassifyMissing(
            const Geometry::PropertySet& properties,
            std::string_view name) noexcept
        {
            return properties.Exists(name) ? AttributeBindStatus::TypeMismatch
                                           : AttributeBindStatus::PropertyMissing;
        }

        [[nodiscard]] std::uint32_t PackUnorm8(const glm::vec4& rgba) noexcept
        {
            const glm::vec4 c = glm::clamp(rgba, glm::vec4(0.0f), glm::vec4(1.0f));
            const auto r = static_cast<std::uint32_t>(c.x * 255.0f + 0.5f);
            const auto g = static_cast<std::uint32_t>(c.y * 255.0f + 0.5f);
            const auto b = static_cast<std::uint32_t>(c.z * 255.0f + 0.5f);
            const auto a = static_cast<std::uint32_t>(c.w * 255.0f + 0.5f);
            return r | (g << 8) | (b << 16) | (a << 24);
        }
    } // namespace

    const char* DebugNameForVertexChannel(const VertexChannel channel) noexcept
    {
        switch (channel)
        {
        case VertexChannel::Position: return "Position";
        case VertexChannel::Normal:   return "Normal";
        case VertexChannel::Texcoord: return "Texcoord";
        case VertexChannel::Color:    return "Color";
        case VertexChannel::Tangent:  return "Tangent";
        case VertexChannel::Custom:   return "Custom";
        }
        return "Unknown";
    }

    const char* DebugNameForAttributeBindStatus(const AttributeBindStatus status) noexcept
    {
        switch (status)
        {
        case AttributeBindStatus::Bound:           return "Bound";
        case AttributeBindStatus::EmptyBinding:    return "EmptyBinding";
        case AttributeBindStatus::PropertyMissing: return "PropertyMissing";
        case AttributeBindStatus::TypeMismatch:    return "TypeMismatch";
        case AttributeBindStatus::CountMismatch:   return "CountMismatch";
        }
        return "Unknown";
    }

    AttributeBindResult ResolveVec3Channel(
        const Geometry::PropertySet& properties,
        const VertexAttributeBinding& binding,
        const std::uint32_t vertexCount,
        const std::span<glm::vec3> out)
    {
        AttributeBindResult result{};
        if (out.size() < vertexCount)
        {
            result.Status = AttributeBindStatus::CountMismatch;
            return result;
        }

        const glm::vec3 fallback{binding.Fallback.x, binding.Fallback.y, binding.Fallback.z};

        const auto fillFallback = [&](const AttributeBindStatus why) {
            result.Status = why;
            if (binding.AllowFallback)
            {
                for (std::uint32_t i = 0; i < vertexCount; ++i)
                {
                    out[i] = fallback;
                }
                result.FallbackCount = vertexCount;
                result.FullyPopulated = true;
            }
            return result;
        };

        if (binding.SourceProperty.empty())
        {
            return fillFallback(AttributeBindStatus::EmptyBinding);
        }

        const auto prop = properties.Get<glm::vec3>(binding.SourceProperty);
        if (!prop)
        {
            return fillFallback(ClassifyMissing(properties, binding.SourceProperty));
        }

        const auto& values = prop.Vector();
        if (values.size() != vertexCount)
        {
            return fillFallback(AttributeBindStatus::CountMismatch);
        }

        for (std::uint32_t i = 0; i < vertexCount; ++i)
        {
            const glm::vec3 v = values[i];
            if (!IsFinite(v))
            {
                out[i] = fallback;
                ++result.FallbackCount;
                ++result.NonFiniteCount;
                continue;
            }
            if (binding.Normalize)
            {
                const float len = glm::length(v);
                if (len <= kDegenerateLengthEpsilon)
                {
                    out[i] = fallback;
                    ++result.FallbackCount;
                    continue;
                }
                out[i] = v / len;
            }
            else
            {
                out[i] = v;
            }
            ++result.SourceCount;
        }

        result.Status = AttributeBindStatus::Bound;
        result.FullyPopulated = true;
        return result;
    }

    AttributeBindResult ResolveVec2Channel(
        const Geometry::PropertySet& properties,
        const VertexAttributeBinding& binding,
        const std::uint32_t vertexCount,
        const std::span<glm::vec2> out)
    {
        AttributeBindResult result{};
        if (out.size() < vertexCount)
        {
            result.Status = AttributeBindStatus::CountMismatch;
            return result;
        }

        const glm::vec2 fallback{binding.Fallback.x, binding.Fallback.y};

        const auto fillFallback = [&](const AttributeBindStatus why) {
            result.Status = why;
            if (binding.AllowFallback)
            {
                for (std::uint32_t i = 0; i < vertexCount; ++i)
                {
                    out[i] = fallback;
                }
                result.FallbackCount = vertexCount;
                result.FullyPopulated = true;
            }
            return result;
        };

        if (binding.SourceProperty.empty())
        {
            return fillFallback(AttributeBindStatus::EmptyBinding);
        }

        const auto prop = properties.Get<glm::vec2>(binding.SourceProperty);
        if (!prop)
        {
            return fillFallback(ClassifyMissing(properties, binding.SourceProperty));
        }

        const auto& values = prop.Vector();
        if (values.size() != vertexCount)
        {
            return fillFallback(AttributeBindStatus::CountMismatch);
        }

        for (std::uint32_t i = 0; i < vertexCount; ++i)
        {
            const glm::vec2 v = values[i];
            if (!IsFinite(v))
            {
                out[i] = fallback;
                ++result.FallbackCount;
                ++result.NonFiniteCount;
                continue;
            }
            out[i] = v;
            ++result.SourceCount;
        }

        result.Status = AttributeBindStatus::Bound;
        result.FullyPopulated = true;
        return result;
    }

    AttributeBindResult ResolveColorChannelPackedUnorm8(
        const Geometry::PropertySet& properties,
        const VertexAttributeBinding& binding,
        const std::uint32_t vertexCount,
        const std::span<std::uint32_t> out)
    {
        AttributeBindResult result{};
        if (out.size() < vertexCount)
        {
            result.Status = AttributeBindStatus::CountMismatch;
            return result;
        }

        const std::uint32_t packedFallback = PackUnorm8(binding.Fallback);

        const auto fillFallback = [&](const AttributeBindStatus why) {
            result.Status = why;
            if (binding.AllowFallback)
            {
                for (std::uint32_t i = 0; i < vertexCount; ++i)
                {
                    out[i] = packedFallback;
                }
                result.FallbackCount = vertexCount;
                result.FullyPopulated = true;
            }
            return result;
        };

        if (binding.SourceProperty.empty())
        {
            return fillFallback(AttributeBindStatus::EmptyBinding);
        }

        // RGBA (vec4) source.
        if (binding.SourceType == AttributeSourceType::Vec4)
        {
            const auto prop = properties.Get<glm::vec4>(binding.SourceProperty);
            if (!prop)
            {
                return fillFallback(ClassifyMissing(properties, binding.SourceProperty));
            }
            const auto& values = prop.Vector();
            if (values.size() != vertexCount)
            {
                return fillFallback(AttributeBindStatus::CountMismatch);
            }
            for (std::uint32_t i = 0; i < vertexCount; ++i)
            {
                const glm::vec4 v = values[i];
                if (!IsFinite(v))
                {
                    out[i] = packedFallback;
                    ++result.FallbackCount;
                    ++result.NonFiniteCount;
                    continue;
                }
                out[i] = PackUnorm8(v);
                ++result.SourceCount;
            }
            result.Status = AttributeBindStatus::Bound;
            result.FullyPopulated = true;
            return result;
        }

        // RGB (vec3) source, opaque alpha.
        const auto prop = properties.Get<glm::vec3>(binding.SourceProperty);
        if (!prop)
        {
            return fillFallback(ClassifyMissing(properties, binding.SourceProperty));
        }
        const auto& values = prop.Vector();
        if (values.size() != vertexCount)
        {
            return fillFallback(AttributeBindStatus::CountMismatch);
        }
        for (std::uint32_t i = 0; i < vertexCount; ++i)
        {
            const glm::vec3 v = values[i];
            if (!IsFinite(v))
            {
                out[i] = packedFallback;
                ++result.FallbackCount;
                ++result.NonFiniteCount;
                continue;
            }
            out[i] = PackUnorm8(glm::vec4{v, 1.0f});
            ++result.SourceCount;
        }
        result.Status = AttributeBindStatus::Bound;
        result.FullyPopulated = true;
        return result;
    }
} // namespace Extrinsic::Runtime
