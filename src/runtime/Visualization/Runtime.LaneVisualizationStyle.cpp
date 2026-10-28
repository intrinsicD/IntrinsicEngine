// RUNTIME-329: each row is one field of `VisualizationConfig`: its spec (name, type, enum tokens),
// its scene key path and accessors. Scene write and read and the command's check come from the
// rows; the panel reads the specs.
module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <glm/vec4.hpp>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.LaneVisualizationStyle;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.GeometryProperty.Types;
import Geometry.Properties.Types;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Visualization/internal/Runtime.LaneVisualizationStyleJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        using Config = Graphics::Components::VisualizationConfig;
        using FT = ConfigFieldType;

        constexpr std::array<std::string_view, 6> kSources{
            "Material", "UniformColor", "ScalarField", "PerVertexBuffer", "PerEdgeBuffer", "PerFaceBuffer"};
        constexpr std::array<std::string_view, 3> kDomains{"Vertex", "Edge", "Face"};
        constexpr std::array<std::string_view, 2> kInterpretations{"components", "normal_direction"};
        constexpr std::array<std::string_view, 6> kColormaps{"Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
        static_assert(kColormaps.size() == Graphics::Colormap::kColormapCount);
        constexpr ConfigFieldSpec kComponent{.Name = "color", .Type = FT::Float};
        constexpr ConfigFieldSpec kIsovalue{.Name = "isoline_values", .Type = FT::Float};

        // Strict: an enum outside its tokens keeps its number, which the check refuses and the
        // fingerprint tells apart; the scene writes the first token instead.
        enum class Encoding : std::uint8_t { Strict, Scene };

        template <std::size_t N>
        Json Token(const std::array<std::string_view, N>& tokens, const std::size_t index, const Encoding encoding)
        {
            if (index < N)
                return Json(tokens[index]);
            return encoding == Encoding::Scene ? Json(tokens[0]) : Json(index);
        }
        template <typename E, std::size_t N>
        E FromToken(const std::array<std::string_view, N>& tokens, const Json& value)
        {
            return static_cast<E>(std::ranges::find(tokens, value.get<std::string>()) - tokens.begin());
        }
        Json Vec4(const glm::vec4& v) { return Json::array({v.x, v.y, v.z, v.w}); }
        glm::vec4 ToVec4(const Json& v)
        {
            return {v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()};
        }

        struct Row
        {
            ConfigFieldSpec Spec{};
            std::array<std::string_view, 3> ScenePath{}; // nested scene keys, unused ones empty
            Json (*Get)(const Config&, Encoding){nullptr};
            void (*Set)(const Json&, Config&){nullptr}; // the value already passed the spec
        };

        constexpr std::array<Row, 17> kRows{{
            {{.Name = "source", .Type = FT::StringEnum, .EnumNames = kSources, .EnumValues = kSources}, {"source"},
                [](const Config& c, Encoding e) { return Token(kSources, std::size_t(c.Source), e); },
                [](const Json& v, Config& c) { c.Source = FromToken<Config::ColorSource>(kSources, v); }},
            {{.Name = "color", .Type = FT::Array, .Item = &kComponent, .MinItems = 4u, .MaxItems = 4u}, {"color"},
                [](const Config& c, Encoding) { return Vec4(c.Color); },
                [](const Json& v, Config& c) { c.Color = ToVec4(v); }},
            {{.Name = "uniform_by_default", .Type = FT::Bool}, {"uniformByDefault"},
                [](const Config& c, Encoding) { return Json(c.UniformByDefault); },
                [](const Json& v, Config& c) { c.UniformByDefault = v.get<bool>(); }},
            {{.Name = "scalar_field_name", .Type = FT::String}, {"scalarFieldName"},
                [](const Config& c, Encoding) { return Json(c.ScalarFieldName); },
                [](const Json& v, Config& c) { c.ScalarFieldName = v.get<std::string>(); }},
            {{.Name = "scalar_domain", .Type = FT::StringEnum, .EnumNames = kDomains, .EnumValues = kDomains},
                {"scalarDomain"}, [](const Config& c, Encoding e) { return Token(kDomains, std::size_t(c.ScalarDomain), e); },
                [](const Json& v, Config& c) { c.ScalarDomain = FromToken<Config::Domain>(kDomains, v); }},
            {{.Name = "color_buffer_name", .Type = FT::String}, {"colorBufferName"},
                [](const Config& c, Encoding) { return Json(c.ColorBufferName); },
                [](const Json& v, Config& c) { c.ColorBufferName = v.get<std::string>(); }},
            {{.Name = "color_interpretation", .Type = FT::StringEnum, .EnumValues = kInterpretations},
                {"colorInterpretation"},
                [](const Config& c, Encoding e) { return Token(kInterpretations, std::size_t(c.Interpretation), e); },
                [](const Json& v, Config& c)
                { c.Interpretation = FromToken<Config::ColorInterpretation>(kInterpretations, v); }},
            {{.Name = "use_baked_texture", .Type = FT::Bool}, {"useBakedTexture"},
                [](const Config& c, Encoding) { return Json(c.UseBakedTexture); },
                [](const Json& v, Config& c) { c.UseBakedTexture = v.get<bool>(); }},
            {{.Name = "colormap", .Type = FT::StringEnum, .EnumNames = kColormaps, .EnumValues = kColormaps},
                {"scalar", "map"}, [](const Config& c, Encoding e) { return Token(kColormaps, std::size_t(c.Scalar.Map), e); },
                [](const Json& v, Config& c) { c.Scalar.Map = FromToken<Graphics::Colormap::Type>(kColormaps, v); }},
            {{.Name = "auto_range", .Type = FT::Bool}, {"scalar", "autoRange"},
                [](const Config& c, Encoding) { return Json(c.Scalar.AutoRange); },
                [](const Json& v, Config& c) { c.Scalar.AutoRange = v.get<bool>(); }},
            {{.Name = "range_min", .Type = FT::Float}, {"scalar", "rangeMin"},
                [](const Config& c, Encoding) { return Json(c.Scalar.RangeMin); },
                [](const Json& v, Config& c) { c.Scalar.RangeMin = v.get<float>(); }},
            {{.Name = "range_max", .Type = FT::Float}, {"scalar", "rangeMax"},
                [](const Config& c, Encoding) { return Json(c.Scalar.RangeMax); },
                [](const Json& v, Config& c) { c.Scalar.RangeMax = v.get<float>(); }},
            {{.Name = "bin_count", .Type = FT::UInt}, {"scalar", "binCount"},
                [](const Config& c, Encoding) { return Json(c.Scalar.BinCount); },
                [](const Json& v, Config& c) { c.Scalar.BinCount = v.get<std::uint32_t>(); }},
            {{.Name = "isoline_count", .Type = FT::UInt}, {"scalar", "isolines", "num"},
                [](const Config& c, Encoding) { return Json(c.Scalar.Isolines.Num); },
                [](const Json& v, Config& c) { c.Scalar.Isolines.Num = v.get<std::uint32_t>(); }},
            {{.Name = "isoline_color", .Type = FT::Array, .Item = &kComponent, .MinItems = 4u, .MaxItems = 4u},
                {"scalar", "isolines", "color"}, [](const Config& c, Encoding) { return Vec4(c.Scalar.Isolines.Color); },
                [](const Json& v, Config& c) { c.Scalar.Isolines.Color = ToVec4(v); }},
            {{.Name = "isoline_width", .Type = FT::Float}, {"scalar", "isolines", "width"},
                [](const Config& c, Encoding) { return Json(c.Scalar.Isolines.Width); },
                [](const Json& v, Config& c) { c.Scalar.Isolines.Width = v.get<float>(); }},
            {{.Name = "isoline_values", .Type = FT::Array, .Item = &kIsovalue,
                 .MaxItems = std::size_t{Graphics::Components::ScalarFieldConfig::kMaxIsolineValues}},
                {"scalar", "isolines", "values"},
                [](const Config& c, Encoding)
                {
                    const auto& iso = c.Scalar.Isolines;
                    return Json(std::vector<float>(
                        iso.Values.begin(), iso.Values.begin() + std::min<std::size_t>(iso.ValueCount, iso.Values.size())));
                },
                [](const Json& v, Config& c)
                {
                    auto& iso = c.Scalar.Isolines;
                    iso.ValueCount = static_cast<std::uint32_t>(v.size());
                    for (std::size_t i = 0u; i < v.size(); ++i)
                        iso.Values[i] = v[i].get<float>();
                }},
        }};

        constexpr std::array<ConfigFieldSpec, kRows.size()> kSpecs = []
        {
            std::array<ConfigFieldSpec, kRows.size()> specs{};
            for (std::size_t i = 0u; i < kRows.size(); ++i)
                specs[i] = kRows[i].Spec;
            return specs;
        }();

        // The flat object of every field by spec name, as the declared-field check reads it.
        Json Flat(const Config& config, const Encoding encoding)
        {
            Json object = Json::object();
            for (const Row& row : kRows)
                object[std::string(row.Spec.Name)] = row.Get(config, encoding);
            return object;
        }
    }

    std::span<const ConfigFieldSpec> LaneVisualizationStyleFields() noexcept
    {
        return kSpecs;
    }

    std::optional<std::string> CheckLaneVisualizationStyle(const Config& config)
    {
        const auto errors = ConfigDetail::CollectDeclaredFieldErrors(Flat(config, Encoding::Strict), kSpecs);
        return errors.empty() ? std::nullopt : std::optional<std::string>{"lane style " + errors.front().Message};
    }

    bool SameLaneVisualizationStyle(const Config& lhs, const Config& rhs)
    {
        return std::ranges::all_of(kRows, [&](const Row& row)
            { return row.Get(lhs, Encoding::Strict) == row.Get(rhs, Encoding::Strict); });
    }

    std::uint64_t LaneVisualizationStyleHash(const Config& config)
    {
        // FNV-1a over bytes (a whole-word XOR lets two sign flips cancel); strings carry their length.
        std::uint64_t hash = 14695981039346656037ull;
        const auto mixByte = [&](const std::uint8_t byte)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        };
        const auto mix = [&](const std::uint64_t value)
        {
            for (std::uint32_t i = 0u; i < 8u; ++i)
                mixByte(static_cast<std::uint8_t>(value >> (i * 8u)));
        };
        const auto visit = [&](const auto& self, const Json& value) -> void
        {
            mix(static_cast<std::uint64_t>(value.type()));
            if (value.is_number_float())
                mix(std::bit_cast<std::uint64_t>(value.get<double>()));
            else if (value.is_number_integer())
                mix(value.get<std::uint64_t>());
            else if (value.is_boolean())
                mix(value.get<bool>() ? 1u : 0u);
            else if (value.is_string())
            {
                const std::string& text = value.get_ref<const std::string&>();
                mix(text.size());
                for (const char c : text)
                    mixByte(static_cast<std::uint8_t>(c));
            }
            else if (value.is_array())
            {
                mix(value.size());
                for (const Json& element : value)
                    self(self, element);
            }
        };
        for (const Row& row : kRows)
            visit(visit, row.Get(config, Encoding::Strict));
        return hash;
    }
}

// Declared in the internal header, outside the module (no module interface exports nlohmann).
extern "C++"
{
    namespace Extrinsic::Runtime
    {
        Json LaneVisualizationStyleToJson(const Config& config)
        {
            Json object = Json::object();
            for (const Row& row : kRows)
            {
                Json* node = &object;
                std::size_t depth = 0u;
                while (depth + 1u < row.ScenePath.size() && !row.ScenePath[depth + 1u].empty())
                    node = &(*node)[std::string(row.ScenePath[depth++])];
                (*node)[std::string(row.ScenePath[depth])] = row.Get(config, Encoding::Scene);
            }
            return object;
        }

        bool ReadLaneVisualizationStyleJson(const Json& value, Config& config)
        {
            if (!value.is_object() || !value.contains("source"))
                return false;
            // Gather the present keys by spec name; the flat reader checks them all, then applies all or nothing.
            Json values = Json::object();
            for (const Row& row : kRows)
            {
                const Json* node = &value;
                bool present = true;
                for (const std::string_view key : row.ScenePath)
                {
                    if (key.empty())
                        break;
                    if (!node->is_object())
                        return false; // "scalar" or "isolines" that is not an object
                    const auto it = node->find(std::string(key));
                    if (it == node->end())
                    {
                        present = false;
                        break;
                    }
                    node = &*it;
                }
                if (present)
                    values[std::string(row.Spec.Name)] = *node;
            }
            return ReadLaneVisualizationStyleFields(values, config);
        }

        Json LaneVisualizationStyleFieldsJson(const Config& config)
        {
            return Flat(config, Encoding::Strict);
        }

        bool ReadLaneVisualizationStyleFields(const Json& values, Config& config)
        {
            if (!values.is_object() || !ConfigDetail::CollectDeclaredFieldErrors(values, kSpecs).empty())
                return false;
            for (const Row& row : kRows)
                if (const auto it = values.find(std::string(row.Spec.Name)); it != values.end())
                    row.Set(*it, config);
            return true;
        }
    }
}
