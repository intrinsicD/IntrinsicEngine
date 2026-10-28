// RUNTIME-327: the style of a vector-field layer declared once. Each row is one field of
// `GeometryVectorFieldLayerRecipe`: its spec (agent name, type, bounds, enum tokens), its scene
// key and accessors. The agent schema, agent reads and listings, scene write and read and the
// command's per-field checks all come from these rows; the panel reads the specs.
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <glm/vec4.hpp>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.GeometryPresentation;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.GeometryProperty.Types;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "GeometryIntegration/internal/Runtime.GeometryVectorFieldStyleJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        using Layer = GeometryVectorFieldLayerRecipe;
        using FT = ConfigFieldType;

        constexpr std::array<std::string_view, 2> kLengthModes{"normalized", "raw"};
        constexpr std::array<std::string_view, 2> kLengthModeLabels{"Normalized", "Raw vectors (scaled)"};
        constexpr std::array<std::string_view, 2> kLengthModeSceneTokens{"Normalized", "Raw"};
        constexpr std::array<std::string_view, 6> kColormaps{"Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
        static_assert(kColormaps.size() == Graphics::Colormap::kColormapCount);
        constexpr ConfigFieldSpec kColorComponent{.Name = "color", .Type = FT::Float, .Min = 0.0, .Max = 1.0};
        constexpr ConfigFieldSpec kRangeEnd{.Name = "color_range", .Type = FT::Float};

        struct Row
        {
            ConfigFieldSpec Spec{};
            std::string_view SceneKey{};
            bool SceneRequired{false}; // written by every scene since the layer existed
            std::span<const std::string_view> SceneTokens{}; // StringEnum: the scene's spelling of EnumValues
            Json (*Get)(const Layer&){nullptr};
            void (*Set)(const Json&, Layer&){nullptr}; // the value already passed the spec
        };

        // An out-of-range enum reads as "" so the spec check refuses it.
        template <std::size_t N>
        constexpr std::string_view Token(const std::array<std::string_view, N>& tokens, const std::size_t index)
        {
            return index < N ? tokens[index] : std::string_view{};
        }
        template <std::size_t N>
        constexpr std::size_t IndexOf(const std::array<std::string_view, N>& tokens, const Json& value)
        {
            return static_cast<std::size_t>(std::ranges::find(tokens, value.get<std::string>()) - tokens.begin());
        }

        constexpr auto kRows = std::to_array<Row>({
            {{.Name = "visible", .Type = FT::Bool},
                "enabled",
                true,
                {},
                [](const Layer& l)
                {
                    return Json(l.Enabled);
                },
                [](const Json& v, Layer& l)
                {
                    l.Enabled = v.get<bool>();
                }},
            {{.Name = "depth_tested", .Type = FT::Bool},
                "depthTested",
                true,
                {},
                [](const Layer& l)
                {
                    return Json(l.DepthTested);
                },
                [](const Json& v, Layer& l)
                {
                    l.DepthTested = v.get<bool>();
                }},
            {{.Name = "length_mode",
                 .Type = FT::StringEnum,
                 .EnumNames = kLengthModeLabels,
                 .EnumValues = kLengthModes},
                "lengthMode",
                true,
                kLengthModeSceneTokens,
                [](const Layer& l)
                {
                    return Json(Token(kLengthModes, static_cast<std::size_t>(l.LengthMode)));
                },
                [](const Json& v, Layer& l)
                {
                    l.LengthMode = static_cast<GeometryVectorFieldLengthMode>(IndexOf(kLengthModes, v));
                }},
            {{.Name = "length",
                 .Type = FT::Float,
                 .Description = "Arrow length (normalized) or scale (raw); multiplies a length property.",
                 .Min = 0.0,
                 .ExclusiveMin = true},
                "length",
                true,
                {},
                [](const Layer& l)
                {
                    return Json(l.Length);
                },
                [](const Json& v, Layer& l)
                {
                    l.Length = v.get<float>();
                }},
            // RUNTIME-331: the pass-through style floats.
#define VF_STYLE_ROW(member, fallback, agentName, sceneKey, sceneRequired, label, help, lo, hi)                        \
    {{.Name = #agentName, .Type = FT::Float, .Description = help, .Min = lo, .Max = hi},                               \
        #sceneKey,                                                                                                     \
        sceneRequired,                                                                                                 \
        {},                                                                                                            \
        [](const Layer& l)                                                                                             \
        {                                                                                                              \
            return Json(l.member);                                                                                     \
        },                                                                                                             \
        [](const Json& v, Layer& l)                                                                                    \
        {                                                                                                              \
            l.member = v.get<float>();                                                                                 \
        }},
#include "common/vector_field_style_rows.glsl"
#undef VF_STYLE_ROW
            {{.Name = "color",
                 .Type = FT::Array,
                 .Description = "Uniform RGBA, drawn while color_property is Default.",
                 .Item = &kColorComponent,
                 .MinItems = 4u,
                 .MaxItems = 4u},
                "color",
                true,
                {},
                [](const Layer& l)
                {
                    return Json::array({l.Color.x, l.Color.y, l.Color.z, l.Color.w});
                },
                [](const Json& v, Layer& l)
                {
                    l.Color = glm::vec4{v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()};
                }},
            {{.Name = "stride",
                 .Type = FT::UInt,
                 .Description = "Draw every n-th element.",
                 .Min = 1.0,
                 .Max = double(kMaxGeometryVectorFieldStride)},
                "stride",
                true,
                {},
                [](const Layer& l)
                {
                    return Json(l.Stride);
                },
                [](const Json& v, Layer& l)
                {
                    l.Stride = v.get<std::uint32_t>();
                }},
            {{.Name = "max_glyphs", .Type = FT::UInt, .Description = "At most this many arrows; 0 draws all."},
                "maxGlyphs",
                true,
                {},
                [](const Layer& l)
                {
                    return Json(l.MaxGlyphs);
                },
                [](const Json& v, Layer& l)
                {
                    l.MaxGlyphs = v.get<std::uint32_t>();
                }},
            {{.Name = "length_property",
                 .Type = FT::String,
                 .Description = "Scalar property multiplying each arrow's length; empty is Default."},
                "lengthProperty",
                false,
                {},
                [](const Layer& l)
                {
                    return Json(l.LengthProperty);
                },
                [](const Json& v, Layer& l)
                {
                    l.LengthProperty = v.get<std::string>();
                }},
            {{.Name = "color_property",
                 .Type = FT::String,
                 .Description = "Scalar or vec3/vec4 property coloring each arrow; empty is Default."},
                "colorProperty",
                false,
                {},
                [](const Layer& l)
                {
                    return Json(l.ColorProperty);
                },
                [](const Json& v, Layer& l)
                {
                    l.ColorProperty = v.get<std::string>();
                }},
            {{.Name = "colormap", .Type = FT::StringEnum, .EnumNames = kColormaps, .EnumValues = kColormaps},
                "colorMap",
                false,
                {},
                [](const Layer& l)
                {
                    return Json(Token(kColormaps, static_cast<std::size_t>(l.ColorMap)));
                },
                [](const Json& v, Layer& l)
                {
                    l.ColorMap = static_cast<Graphics::Colormap::Type>(IndexOf(kColormaps, v));
                }},
            {{.Name = "color_auto_range", .Type = FT::Bool},
                "colorAutoRange",
                false,
                {},
                [](const Layer& l)
                {
                    return Json(l.ColorAutoRange);
                },
                [](const Json& v, Layer& l)
                {
                    l.ColorAutoRange = v.get<bool>();
                }},
            {{.Name = "color_range",
                 .Type = FT::Array,
                 .Description = "Manual colormap range, min < max.",
                 .Item = &kRangeEnd,
                 .MinItems = 2u,
                 .MaxItems = 2u},
                "colorRange",
                false,
                {},
                [](const Layer& l)
                {
                    return Json::array({l.ColorRangeMin, l.ColorRangeMax});
                },
                [](const Json& v, Layer& l)
                {
                    l.ColorRangeMin = v[0].get<float>();
                    l.ColorRangeMax = v[1].get<float>();
                }},
        });

        constexpr auto kSliders = std::to_array<GeometryVectorFieldStyleSlider>({
#define VF_STYLE_ROW(member, fallback, agentName, sceneKey, sceneRequired, label, help, lo, hi)                        \
    {label, #agentName, &Layer::member},
#include "common/vector_field_style_rows.glsl"
#undef VF_STYLE_ROW
        });

        constexpr std::array<ConfigFieldSpec, kRows.size()> kSpecs = []
        {
            std::array<ConfigFieldSpec, kRows.size()> specs{};
            for (std::size_t i = 0u; i < kRows.size(); ++i)
                specs[i] = kRows[i].Spec;
            return specs;
        }();

        std::string_view Key(const Row& row, const GeometryVectorFieldStyleKeys keys)
        {
            return keys == GeometryVectorFieldStyleKeys::Scene ? row.SceneKey : row.Spec.Name;
        }

        // Scene token <-> agent token, by index. An unknown token becomes "", which no enum accepts
        // (passing it through would let a scene use the agent's spelling).
        Json Translate(const Json& value, const std::span<const std::string_view> from,
            const std::span<const std::string_view> to)
        {
            if (!value.is_string())
                return value;
            const auto it = std::ranges::find(from, value.get<std::string>());
            return it == from.end() ? Json("") : Json(to[static_cast<std::size_t>(it - from.begin())]);
        }
    }

    std::span<const ConfigFieldSpec> GeometryVectorFieldStyleFields() noexcept
    {
        return kSpecs;
    }

    std::span<const GeometryVectorFieldStyleSlider> GeometryVectorFieldStyleSliders() noexcept
    {
        return kSliders;
    }

    std::optional<GeometryVectorFieldStyleProperty> FindGeometryVectorFieldStyleProperty(
        const Geometry::PropertySet& properties, const GeometryElementDomain domain, const std::string_view name)
    {
        using K = Geometry::PropertyValueKind;
        if (IsTopologyProperty(domain, name))
            return std::nullopt;
        const auto descriptors = properties.Descriptors();
        const auto descriptor = std::ranges::find(descriptors, name, &Geometry::PropertyDescriptor::Name);
        if (descriptor == descriptors.end() || descriptor->ElementCount != properties.Size())
            return std::nullopt;
        const K kind = descriptor->ValueKind;
        const bool scalar = kind == K::Bool || kind == K::Int32 || kind == K::UInt32 || kind == K::UInt64 ||
            kind == K::Float || kind == K::Double;
        if (!scalar && kind != K::Vec3 && kind != K::Vec4)
            return std::nullopt;
        return GeometryVectorFieldStyleProperty{.Kind = kind, .Scalar = scalar};
    }

    std::optional<std::string> CheckGeometryVectorFieldStyle(const Layer& layer)
    {
        const auto errors = ConfigDetail::CollectDeclaredFieldErrors(
            GeometryVectorFieldStyleToJson(layer, GeometryVectorFieldStyleKeys::Agent), kSpecs);
        return errors.empty() ? std::nullopt : std::optional<std::string>{errors.front().Message};
    }
}

// Declared in the internal header, outside the module (no module interface exports nlohmann).
extern "C++"
{
    namespace Extrinsic::Runtime
    {
        Json GeometryVectorFieldStyleToJson(const Layer& layer, const GeometryVectorFieldStyleKeys keys)
        {
            Json object = Json::object();
            for (const Row& row : kRows)
            {
                Json value = row.Get(layer);
                if (keys == GeometryVectorFieldStyleKeys::Scene && !row.SceneTokens.empty())
                    value = Translate(value, row.Spec.EnumValues, row.SceneTokens);
                object[std::string(Key(row, keys))] = std::move(value);
            }
            return object;
        }

        std::optional<std::string> ReadGeometryVectorFieldStyleJson(
            const Json& object, Layer& layer, const GeometryVectorFieldStyleKeys keys)
        {
            if (!object.is_object())
                return std::string("the style must be an object");
            // Rename to agent names so the specs check every value, then apply all or nothing.
            Json values = Json::object();
            for (const Row& row : kRows)
            {
                const auto it = object.find(std::string(Key(row, keys)));
                if (it == object.end())
                {
                    if (keys == GeometryVectorFieldStyleKeys::Scene && row.SceneRequired)
                        return "missing " + std::string(row.SceneKey);
                    continue;
                }
                values[std::string(row.Spec.Name)] = keys == GeometryVectorFieldStyleKeys::Scene && !row.SceneTokens.empty()
                    ? Translate(*it, row.SceneTokens, row.Spec.EnumValues)
                    : *it;
            }
            if (const auto errors = ConfigDetail::CollectDeclaredFieldErrors(values, kSpecs); !errors.empty())
                return errors.front().Message;
            // The stored floats are checked too: 1e39 would become infinity, a double just above an
            // exclusive 0 would become 0.
            Layer applied = layer;
            Json stored = Json::object();
            for (const Row& row : kRows)
                if (const auto it = values.find(std::string(row.Spec.Name)); it != values.end())
                {
                    row.Set(*it, applied);
                    stored[std::string(row.Spec.Name)] = row.Get(applied);
                }
            if (const auto errors = ConfigDetail::CollectDeclaredFieldErrors(stored, kSpecs); !errors.empty())
                return errors.front().Message;
            layer = std::move(applied);
            return std::nullopt;
        }

        Json GeometryVectorFieldStyleSchemaProperties()
        {
            return Json::parse(ConfigDetail::BuildSectionSchemaJson("", "", "", kSpecs, Json::object()))["properties"];
        }
    }
}
