// Typed capture/publication of the optional vertex properties mesh-field
// methods own; private to this family, included after its Geometry.Properties
// import. An unauthored property is removed rather than left stale.
#pragma once

namespace Extrinsic::Runtime::MeshFieldDetail
{
        template <typename T>
        [[nodiscard]] bool CaptureCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const std::size_t expectedCount,
            bool& hadProperty,
            std::vector<T>& values,
            std::string& diagnostic)
        {
            hadProperty = false;
            values.clear();
            if (!properties.Exists(name))
                return true;

            auto property = properties.Get<T>(name);
            if (!property || property.Vector().size() != expectedCount)
            {
                diagnostic = "existing curvature property has an incompatible type or count: ";
                diagnostic += std::string{name};
                return false;
            }

            hadProperty = true;
            values = property.Vector();
            return true;
        }

        template <typename T>
        [[nodiscard]] bool ApplyCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const bool hasProperty,
            const std::vector<T>& values,
            const T& defaultValue)
        {
            if (!hasProperty)
            {
                auto property = properties.Get<T>(name);
                if (property)
                {
                    properties.Remove(property);
                    return true;
                }
                return !properties.Exists(name);
            }

            auto property =
                properties.GetOrAdd<T>(std::string{name}, defaultValue);
            if (!property || property.Vector().size() != values.size())
                return false;
            property.Vector() = values;
            return true;
        }
}
