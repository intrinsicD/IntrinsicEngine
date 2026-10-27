// RUNTIME-326: the advertised input schema of an agent tool is the one declaration of its
// argument shape. This unit checks arguments against the JSON Schema subset the registry uses;
// a schema with any other keyword is unsupported, so no declared constraint is silently skipped.
module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <compare>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.AgentOperations;

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::array<std::string_view, 4> kAnnotations{"description", "default", "title", "examples"};
        // No `pattern`: std::regex recurses per character and throws on a bad pattern (fatal without exceptions).
        constexpr std::array<std::string_view, 19> kKeywords{"type", "enum", "const", "minimum", "maximum",
            "exclusiveMinimum", "exclusiveMaximum", "minLength", "minItems", "maxItems", "items",
            "properties", "required", "additionalProperties", "oneOf", "anyOf", "allOf", "if", "then"};
        constexpr std::array<std::string_view, 7> kTypes{"object", "array", "string", "integer", "number", "boolean", "null"};

        bool Contains(const auto& list, const std::string_view value)
        {
            return std::ranges::find(list, value) != list.end();
        }

        bool SupportedTypes(const Json& type)
        {
            if (type.is_string()) return Contains(kTypes, type.get_ref<const std::string&>());
            return type.is_array() && !type.empty() &&
                std::ranges::all_of(type, [](const Json& t) { return t.is_string() && Contains(kTypes, t.get_ref<const std::string&>()); });
        }

        bool Supported(const Json& schema)
        {
            if (!schema.is_object()) return false;
            for (const auto& [key, value] : schema.items())
            {
                if (Contains(kAnnotations, key) || key.starts_with("x-")) continue;
                if (!Contains(kKeywords, key)) return false;
                if (key == "type" && !SupportedTypes(value)) return false;
                if ((key == "required" || key == "enum") && !value.is_array()) return false;
                if (key == "required" && !std::ranges::all_of(value, [](const Json& k) { return k.is_string(); })) return false;
                if (key.starts_with("minimum") || key.starts_with("maximum") || key.starts_with("exclusive"))
                    if (!value.is_number()) return false;
                if ((key == "minLength" || key == "minItems" || key == "maxItems") && !value.is_number_unsigned()) return false;
                if (key == "additionalProperties" && !value.is_boolean()) return false;
                if ((key == "items" || key == "if" || key == "then") && !Supported(value)) return false;
                if (key == "properties" &&
                    (!value.is_object() || !std::ranges::all_of(value.items(), [](const auto& p) { return Supported(p.value()); })))
                    return false;
                if ((key == "oneOf" || key == "anyOf" || key == "allOf") &&
                    (!value.is_array() || !std::ranges::all_of(value, [](const Json& s) { return Supported(s); })))
                    return false;
            }
            return true;
        }

        // Exact numeric order across int64, uint64 and double (nlohmann compares mixed signed/unsigned
        // through a cast, and a double cannot hold every 64-bit integer).
        static_assert(std::numeric_limits<long double>::digits >= 64, "long double must hold every 64-bit integer exactly");
        long double Exact(const Json& number)
        {
            if (number.is_number_unsigned()) return static_cast<long double>(number.get<std::uint64_t>());
            if (number.is_number_integer()) return static_cast<long double>(number.get<std::int64_t>());
            return static_cast<long double>(number.get<double>());
        }

        bool Equal(const Json& a, const Json& b)
        {
            if (a.is_number() && b.is_number()) return Exact(a) == Exact(b);
            if (a.is_array() && b.is_array())
                return a.size() == b.size() && std::ranges::equal(a, b, [](const Json& x, const Json& y) { return Equal(x, y); });
            if (a.is_object() && b.is_object())
                return a.size() == b.size() &&
                    std::ranges::all_of(a.items(), [&](const auto& item) { const auto it = b.find(item.key()); return it != b.end() && Equal(item.value(), *it); });
            return a == b;
        }

        bool HasType(const Json& value, const std::string_view type)
        {
            if (type == "object") return value.is_object();
            if (type == "array") return value.is_array();
            if (type == "string") return value.is_string();
            if (type == "integer") return value.is_number_integer();
            if (type == "number") return value.is_number() && std::isfinite(value.get<double>());
            if (type == "boolean") return value.is_boolean();
            return value.is_null();
        }

        std::size_t CodePoints(const std::string& text)
        {
            return static_cast<std::size_t>(std::ranges::count_if(text, [](const char c) { return (c & 0xC0) != 0x80; }));
        }

        std::string Name(const std::string& path)
        {
            return path.empty() ? std::string("arguments") : "\"" + path + "\"";
        }

        std::optional<std::string> Check(const Json& schema, const Json& value, const std::string& path)
        {
            if (const auto type = schema.find("type"); type != schema.end())
            {
                const bool ok = type->is_string() ? HasType(value, type->get_ref<const std::string&>())
                    : std::ranges::any_of(*type, [&](const Json& t) { return HasType(value, t.get_ref<const std::string&>()); });
                if (!ok) return Name(path) + " must be " + (type->is_string() ? type->get<std::string>() : type->dump());
            }
            if (const auto e = schema.find("enum");
                e != schema.end() && std::ranges::none_of(*e, [&](const Json& option) { return Equal(option, value); }))
                return Name(path) + " must be one of " + e->dump();
            if (const auto c = schema.find("const"); c != schema.end() && !Equal(*c, value))
                return Name(path) + " must be " + c->dump();
            if (value.is_number())
            {
                const long double x = Exact(value);
                const auto bound = [&](const char* key, const auto& refuses, const char* text) -> std::optional<std::string> {
                    const auto it = schema.find(key);
                    if (it != schema.end() && refuses(x, Exact(*it)))
                        return Name(path) + " must be " + text + " " + it->dump();
                    return std::nullopt;
                };
                if (auto error = bound("minimum", [](long double v, long double b) { return v < b; }, "at least")) return error;
                if (auto error = bound("maximum", [](long double v, long double b) { return v > b; }, "at most")) return error;
                if (auto error = bound("exclusiveMinimum", [](long double v, long double b) { return v <= b; }, "greater than")) return error;
                if (auto error = bound("exclusiveMaximum", [](long double v, long double b) { return v >= b; }, "less than")) return error;
            }
            if (value.is_string())
            {
                const auto& text = value.get_ref<const std::string&>();
                if (const auto it = schema.find("minLength"); it != schema.end() && CodePoints(text) < it->get<std::size_t>())
                    return Name(path) + " must have at least " + it->dump() + " characters";
            }
            if (value.is_array())
            {
                if (const auto it = schema.find("minItems"); it != schema.end() && value.size() < it->get<std::size_t>())
                    return Name(path) + " must have at least " + it->dump() + " items";
                if (const auto it = schema.find("maxItems"); it != schema.end() && value.size() > it->get<std::size_t>())
                    return Name(path) + " must have at most " + it->dump() + " items";
                if (const auto items = schema.find("items"); items != schema.end())
                    for (std::size_t i = 0; i < value.size(); ++i)
                        if (auto error = Check(*items, value[i], path + "/" + std::to_string(i))) return error;
            }
            if (value.is_object())
            {
                const auto properties = schema.find("properties");
                if (schema.value("additionalProperties", true) == false)
                    for (const auto& [key, unused] : value.items())
                        if (properties == schema.end() || !properties->contains(key))
                            return "unknown argument \"" + (path.empty() ? key : path + "/" + key) + "\"";
                if (const auto required = schema.find("required"); required != schema.end())
                    for (const auto& key : *required)
                        if (!value.contains(key.get<std::string>()))
                            return "missing argument \"" + (path.empty() ? "" : path + "/") + key.get<std::string>() + "\"";
                if (properties != schema.end())
                    for (const auto& [key, sub] : properties->items())
                        if (const auto it = value.find(key); it != value.end())
                            if (auto error = Check(sub, *it, path + (path.empty() ? "" : "/") + key)) return error;
            }
            if (const auto all = schema.find("allOf"); all != schema.end())
                for (const auto& sub : *all)
                    if (auto error = Check(sub, value, path)) return error;
            if (const auto one = schema.find("oneOf"); one != schema.end())
            {
                const auto matches = std::ranges::count_if(*one, [&](const Json& sub) { return !Check(sub, value, path); });
                if (matches != 1)
                    return Name(path) + " must match exactly one of " + one->dump() + " (matches " + std::to_string(matches) + ")";
            }
            if (const auto any = schema.find("anyOf");
                any != schema.end() && std::ranges::none_of(*any, [&](const Json& sub) { return !Check(sub, value, path); }))
                return Name(path) + " must match one of " + any->dump();
            if (const auto condition = schema.find("if"); condition != schema.end() && !Check(*condition, value, path))
                if (const auto then = schema.find("then"); then != schema.end())
                    if (auto error = Check(*then, value, path)) return error;
            return std::nullopt;
        }
    }

    bool IsSupportedAgentSchemaJson(const std::string_view schemaJson)
    {
        const Json schema = Json::parse(schemaJson, nullptr, false);
        return !schema.is_discarded() && Supported(schema);
    }

    std::optional<std::string> ValidateAgentArgumentsJson(const std::string_view schemaJson, const std::string_view argumentsJson)
    {
        const Json schema = Json::parse(schemaJson, nullptr, false);
        if (schema.is_discarded() || !Supported(schema)) return std::string("the tool's schema is not supported");
        const Json arguments = Json::parse(argumentsJson, nullptr, false);
        if (arguments.is_discarded()) return std::string("arguments must be JSON");
        return Check(schema, arguments, "");
    }
}
