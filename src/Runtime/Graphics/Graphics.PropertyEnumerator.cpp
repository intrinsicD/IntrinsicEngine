module;

#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Graphics.PropertyEnumerator;

import Geometry.Properties;

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

    // Connectivity/position internals that should never appear in any UI.
    // Normals are excluded here because they are valid vector field sources.
    [[nodiscard]] bool IsConnectivityProperty(std::string_view name) noexcept
    {
        if (name == "v:position" || name == "v:tex") return true;
        if (name == "p:position") return true;
        if (name == "h:next" || name == "h:prev" || name == "h:vertex" ||
            name == "h:face" || name == "h:edge") return true;
        if (name == "e:halfedge") return true;
        if (name == "f:halfedge") return true;
        return false;
    }
}

template <class PropertySetT>
static std::vector<PropertyInfo> EnumerateColorablePropertiesImpl(const PropertySetT& ps)
{
    std::vector<PropertyInfo> result;
    const auto names = ps.Properties();

    for (const auto& name : names)
    {
        if (IsInternalProperty(name))
            continue;

        if (ps.template Get<float>(name).IsValid())
            result.push_back({name, PropertyDataType::Scalar});
        else if (ps.template Get<glm::vec3>(name).IsValid())
            result.push_back({name, PropertyDataType::Vec3});
        else if (ps.template Get<glm::vec4>(name).IsValid())
            result.push_back({name, PropertyDataType::Vec4});
    }

    return result;
}

template <class PropertySetT>
static std::vector<PropertyInfo> EnumerateScalarPropertiesImpl(const PropertySetT& ps)
{
    std::vector<PropertyInfo> result;
    const auto names = ps.Properties();

    for (const auto& name : names)
    {
        if (IsInternalProperty(name))
            continue;

        if (ps.template Get<float>(name).IsValid())
            result.push_back({name, PropertyDataType::Scalar});
    }

    return result;
}

template <class PropertySetT>
static std::vector<PropertyInfo> EnumerateVectorPropertiesImpl(const PropertySetT& ps)
{
    std::vector<PropertyInfo> result;
    const auto names = ps.Properties();

    for (const auto& name : names)
    {
        // Use the less restrictive filter: normals (v:normal, f:normal,
        // p:normal) are valid vector field sources even though they are
        // filtered from color-mapping enumeration.
        if (IsConnectivityProperty(name))
            continue;

        if (ps.template Get<glm::vec3>(name).IsValid())
            result.push_back({name, PropertyDataType::Vec3});
    }

    return result;
}

std::vector<PropertyInfo> Graphics::EnumerateColorableProperties(
    const Geometry::PropertySet& ps)
{
    return EnumerateColorablePropertiesImpl(ps);
}

std::vector<PropertyInfo> Graphics::EnumerateColorableProperties(
    const Geometry::ConstPropertySet& ps)
{
    return EnumerateColorablePropertiesImpl(ps);
}

std::vector<PropertyInfo> Graphics::EnumerateScalarProperties(
    const Geometry::PropertySet& ps)
{
    return EnumerateScalarPropertiesImpl(ps);
}

std::vector<PropertyInfo> Graphics::EnumerateScalarProperties(
    const Geometry::ConstPropertySet& ps)
{
    return EnumerateScalarPropertiesImpl(ps);
}

std::vector<PropertyInfo> Graphics::EnumerateVectorProperties(
    const Geometry::PropertySet& ps)
{
    return EnumerateVectorPropertiesImpl(ps);
}

std::vector<PropertyInfo> Graphics::EnumerateVectorProperties(
    const Geometry::ConstPropertySet& ps)
{
    return EnumerateVectorPropertiesImpl(ps);
}

