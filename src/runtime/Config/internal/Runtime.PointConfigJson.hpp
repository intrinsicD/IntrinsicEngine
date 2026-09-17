// String-token property JSON shared by point-processing config implementations.
// Include after JSON and property-reference declarations are visible.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime::ConfigDetail
    {
        enum class PointPropertyValidation { Valid, InvalidReference, UnknownDomain };

        [[nodiscard]] const char* PointPropertyKindToken(Geometry::PropertyValueKind kind) noexcept;
        [[nodiscard]] nlohmann::json EncodePointPropertyRef(const GeometryPropertyRef& ref);
        [[nodiscard]] PointPropertyValidation ValidatePointPropertyRef(
            const nlohmann::json& ref, Geometry::PropertyValueKind kind);
        // Decodes validated bindings without changing the caller's expected value kind.
        void DecodePointPropertyRef(const nlohmann::json& value, GeometryPropertyRef& ref);
    }
}
