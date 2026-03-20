module;

#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Graphics.PropertyEnumerator;

import Geometry;

using namespace Graphics;

namespace
{
    // Internal properties that should not appear in the color selector UI.
    [[nodiscard]] bool IsInternalProperty(std::string_view name) noexcept
    {
        // Vertex-domain internals
        if (name == "v:position" || name == "v:normal" || name == "v:tex") return true;
        // Point-cloud-domain internals
        if (name == "p:position" || name == "p:normal") return true;
        // Halfedge-domain internals
        if (name == "h:next" || name == "h:prev" || name == "h:vertex" ||
            name == "h:face" || name == "h:edge") return true;
        // Edge-domain connectivity
        if (name == "e:halfedge") return true;
        // Face-domain connectivity
        if (name == "f:halfedge" || name == "f:normal") return true;
        return false;
    }
}

std::vector<PropertyInfo> Graphics::EnumerateColorableProperties(
    const Geometry::PropertySet& ps)
{
    std::vector<PropertyInfo> result;
    const auto names = ps.Properties();

    for (const auto& name : names)
    {
        if (IsInternalProperty(name))
            continue;

        // Try each supported type. First match wins.
        if (ps.Get<float>(name).IsValid())
        {
            result.push_back({name, PropertyDataType::Scalar});
        }
        else if (ps.Get<glm::vec3>(name).IsValid())
        {
            result.push_back({name, PropertyDataType::Vec3});
        }
        else if (ps.Get<glm::vec4>(name).IsValid())
        {
            result.push_back({name, PropertyDataType::Vec4});
        }
    }

    return result;
}

std::vector<PropertyInfo> Graphics::EnumerateScalarProperties(
    const Geometry::PropertySet& ps)
{
    std::vector<PropertyInfo> result;
    const auto names = ps.Properties();

    for (const auto& name : names)
    {
        if (IsInternalProperty(name))
            continue;

        if (ps.Get<float>(name).IsValid())
            result.push_back({name, PropertyDataType::Scalar});
    }

    return result;
}

std::vector<PropertyInfo> Graphics::EnumerateVectorProperties(
    const Geometry::PropertySet& ps)
{
    std::vector<PropertyInfo> result;
    const auto names = ps.Properties();

    for (const auto& name : names)
    {
        if (IsInternalProperty(name))
            continue;

        if (ps.Get<glm::vec3>(name).IsValid())
            result.push_back({name, PropertyDataType::Vec3});
    }

    return result;
}
