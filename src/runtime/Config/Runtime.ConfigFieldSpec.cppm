// Declarative config field tables (RUNTIME-276). One table per config section drives
// the mechanical part of its validation, its generated JSON Schema (agents) and the
// editor's field hints, so names, types, ranges and enum names cannot drift apart.
// Enums stay integer-coded in payloads; their names are metadata.
module;

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ConfigFieldSpec;

import Geometry.Properties.Types;
import Extrinsic.Runtime.GeometryProperty.Types;

export namespace Extrinsic::Runtime
{
    enum class ConfigFieldType : std::uint8_t
    {
        Bool,
        UInt,        // unsigned 32-bit integer
        Int,         // signed integer (bound it with Min/Max, e.g. to 32 bits)
        Float,       // finite number
        Enum,        // integer Min .. Min + EnumNames.size() - 1
        String,
        UIntArray,   // array of unsigned 32-bit integers
        PropertyRef, // {"domain","name","kind"} property binding
    };

    struct ConfigFieldSpec
    {
        std::string_view Name{};
        ConfigFieldType Type{ConfigFieldType::Bool};
        std::string_view Description{};
        std::optional<double> Min{}; // Enum: value of the first name (default 0)
        std::optional<double> Max{};
        bool ExclusiveMin{false};
        bool ExclusiveMax{false};
        bool Nullable{false}; // String and PropertyRef may be null
        bool NonEmpty{false}; // String must not be empty
        std::span<const std::string_view> EnumNames{};
        // PropertyRef: accepted value kinds (any scalar kind when AnyScalar and a
        // listed kind is scalar) and, when not empty, accepted element domains.
        std::span<const Geometry::PropertyValueKind> RefKinds{};
        std::span<const GeometryElementDomain> RefDomains{};
        bool AnyScalar{false};
    };

    [[nodiscard]] const ConfigFieldSpec* FindConfigFieldSpec(
        std::span<const ConfigFieldSpec> fields, std::string_view name) noexcept;
    // Human-readable accepted values, e.g. "1 to 10000", "greater than 0, at most 1",
    // "one of: Averaging, Taubin"; empty when unconstrained.
    [[nodiscard]] std::string DescribeConfigFieldRange(const ConfigFieldSpec& field);
    // Clamps a numeric value into the declared closed range (exclusive bounds are the
    // validator's job; hints only keep inputs near the valid interval).
    [[nodiscard]] double ClampToConfigFieldRange(const ConfigFieldSpec& field, double value) noexcept;
    // True when `value` is finite and inside the declared range, exclusive bounds included. Command
    // owners and the agent's params share this check, so a range is declared once.
    [[nodiscard]] bool AcceptsConfigFieldNumber(const ConfigFieldSpec& field, double value) noexcept;
}
