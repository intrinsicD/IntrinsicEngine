// Declarative config field tables (RUNTIME-276). One table per config section drives
// the mechanical part of its validation, its generated JSON Schema (agents) and the
// editor's field hints, so names, types, ranges and enum names cannot drift apart.
// Integer and string enum payloads retain the representation chosen by their owner.
module;

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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
        StringEnum,  // one of EnumValues (wire tokens), optionally named by EnumNames
        Array,       // homogeneous Item with optional length bounds
        Object,      // nested Fields; schema only, owner retains merge/fallback semantics; without
                     // Fields any object (RUNTIME-342)
        ObjectOrString, // any JSON object, or a string (an object as JSON text; its consumer parses it)
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
        bool Nullable{false};
        bool NonEmpty{false}; // String must not be empty
        std::span<const std::string_view> EnumNames{};
        // PropertyRef: accepted value kinds (any scalar kind when AnyScalar and a
        // listed kind is scalar) and, when not empty, accepted element domains.
        std::span<const Geometry::PropertyValueKind> RefKinds{};
        std::span<const GeometryElementDomain> RefDomains{};
        bool AnyScalar{false};
        std::span<const std::string_view> EnumValues{};
        const ConfigFieldSpec* Item{nullptr};
        std::optional<std::size_t> MinItems{}, MaxItems{};
        const ConfigFieldSpec* Fields{nullptr};
        std::size_t FieldCount{0u};
        bool IntegerRefKinds{false};
        // RUNTIME-339: a member of an Object's Fields that the Object's generated schema lists as
        // required. Schema only: config validation leaves nested objects to their owner, and a
        // tool's top-level arguments use AgentArgument::Required instead.
        bool Required{false};
        // RUNTIME-341: a Bool that accepts only true (a flag whose absence means false); its
        // schema is const true. The operation params' Bool reader does not read it (no table sets it).
        // Not combined with Nullable: const true would refuse the null the check accepts.
        bool OnlyTrue{false};
    };

    [[nodiscard]] const ConfigFieldSpec* FindConfigFieldSpec(
        std::span<const ConfigFieldSpec> fields, std::string_view name) noexcept;
    // Human-readable accepted values, e.g. "1 to 10000", "greater than 0, at most 1",
    // "one of: Averaging, Taubin"; empty when unconstrained.
    [[nodiscard]] std::string DescribeConfigFieldRange(const ConfigFieldSpec& field);
    // Clamps a numeric value into the declared range. An exclusive integer bound clamps one step
    // inside it; an exclusive Float bound clamps onto the bound and stays refused by
    // `AcceptsConfigFieldNumber`, so validation reports it instead of the panel inventing a value.
    [[nodiscard]] double ClampToConfigFieldRange(const ConfigFieldSpec& field, double value) noexcept;
    // True when `value` is finite and inside the declared range, exclusive bounds included. Command
    // owners and the agent's params share this check, so a range is declared once.
    [[nodiscard]] bool AcceptsConfigFieldNumber(const ConfigFieldSpec& field, double value) noexcept;
    // Exact membership in EnumValues; an empty value list accepts no token.
    [[nodiscard]] bool AcceptsConfigFieldToken(const ConfigFieldSpec& field, std::string_view value) noexcept;

    // RUNTIME-343: a command's field declared once together with the member it sets, so the
    // command's range check, the agent's params and the panel read one table. Get and Set read and
    // write the member as a double (Bool 0 or 1, an enum its enumerator value): lossless for the
    // members bound so far (32-bit integers, float, double, bool, small enums, and size_t within its
    // declared range). Set takes a value its member can hold (a caller checks the declared range
    // first). FieldNumber gives the field's wire number (an Enum adds its Min). A row without
    // accessors is a field its owner reads itself (a property reference).
    template <class Cmd> struct BoundField
    {
        ConfigFieldSpec Spec{};
        double (*Get)(const Cmd&){nullptr};
        void (*Set)(Cmd&, double){nullptr};
    };

    template <class> struct BoundMember;
    template <class C, class T> struct BoundMember<T C::*>
    {
        using Command = C;
        using Type = T;
    };

    // The row of `spec` bound to `Member` (a pointer to a numeric, bool or enum member).
    template <auto Member> [[nodiscard]] constexpr auto Bind(const ConfigFieldSpec& spec)
    {
        using Cmd = typename BoundMember<decltype(Member)>::Command;
        using T = typename BoundMember<decltype(Member)>::Type;
        return BoundField<Cmd>{.Spec = spec,
            .Get = [](const Cmd& command) -> double
            {
                if constexpr (std::is_enum_v<T>)
                    return static_cast<double>(std::to_underlying(command.*Member));
                else
                    return static_cast<double>(command.*Member);
            },
            .Set =
                [](Cmd& command, const double value)
            {
                if constexpr (std::is_same_v<T, bool>)
                    command.*Member = value != 0.0;
                else if constexpr (std::is_enum_v<T>)
                    command.*Member = static_cast<T>(static_cast<std::underlying_type_t<T>>(value));
                else
                    command.*Member = static_cast<T>(value);
            }};
    }

    // The specs of `rows`, in order: the plain table schemas, checks and hints read.
    template <class Cmd, std::size_t N>
    [[nodiscard]] constexpr std::array<ConfigFieldSpec, N> SpecsOf(const std::array<BoundField<Cmd>, N>& rows)
    {
        std::array<ConfigFieldSpec, N> specs{};
        for (std::size_t i = 0; i < N; ++i)
            specs[i] = rows[i].Spec;
        return specs;
    }

    template <class Cmd> [[nodiscard]] double FieldNumber(const BoundField<Cmd>& row, const Cmd& command)
    {
        return row.Get(command) + (row.Spec.Type == ConfigFieldType::Enum ? row.Spec.Min.value_or(0.0) : 0.0);
    }
    template <class Cmd> void SetFieldNumber(const BoundField<Cmd>& row, Cmd& command, const double wire)
    {
        row.Set(command, wire - (row.Spec.Type == ConfigFieldType::Enum ? row.Spec.Min.value_or(0.0) : 0.0));
    }

    // Every bound numeric or enum field of `command` is accepted by its spec (a Bool always is).
    template <class Cmd>
    [[nodiscard]] bool FieldsInDeclaredRange(const std::span<const BoundField<Cmd>> rows, const Cmd& command)
    {
        for (const BoundField<Cmd>& row : rows)
            if (row.Get != nullptr && row.Spec.Type != ConfigFieldType::Bool &&
                !AcceptsConfigFieldNumber(row.Spec, FieldNumber(row, command)))
                return false;
        return true;
    }
}
