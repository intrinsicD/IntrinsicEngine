// Declared tool arguments (RUNTIME-337, RUNTIME-338): an agent tool's input schema generated from
// ConfigFieldSpec declarations. Private to the Extrinsic.Runtime.AgentOperations implementation
// units, also those without the editor helpers (Capture.cpp, Diagnostics.cpp): include it after the
// module declaration and an import of Extrinsic.Runtime.ConfigFieldSpec, with a global module
// fragment that provides <initializer_list>, <span>, <string>, <string_view>, <utility> and
// <nlohmann/json.hpp>.
#pragma once
extern "C++"
{
    namespace Extrinsic::Runtime::AgentDetail
    {
    // One tool argument declared once: its spec (name, type, bounds, enum tokens, description;
    // descriptions and tokens must outlive the schema build), whether it is required and its
    // default (null: none). The tool's input schema is generated from the list, so the agent's
    // validator checks exactly the declared rules.
        struct AgentArgument
        {
            ConfigFieldSpec Spec{};
            bool Required{false};
            nlohmann::json Default{};
        };
    // A tool's input schema text and whether it was generated from declared arguments.
        struct ToolSchema
        {
            ToolSchema(std::string text) : Text(std::move(text)) {}
            ToolSchema(const char* text) : Text(text) {}
            ToolSchema(std::string text, const bool declared) : Text(std::move(text)), Declared(declared) {}
            std::string Text;
            bool Declared{false};
        };
    // The schema of `arguments`, followed by every field of `table` as an optional argument (a
    // style table a tool takes whole). With `alternatives`, a call names exactly one of these
    // argument sets (JSON Schema oneOf of required sets; RUNTIME-341). Defined beside ParamsSchema
    // (Operations.cpp).
        [[nodiscard]] ToolSchema ArgumentsSchema(std::initializer_list<AgentArgument> arguments,
            std::span<const ConfigFieldSpec> table = {},
            std::initializer_list<std::initializer_list<std::string_view>> alternatives = {});

    // The entity argument, shared by most tools (the other shared ones in Detail.hpp).
        inline AgentArgument EntityArgument(
            const bool required = true, const std::string_view description = "Stable entity id from scene_entities.")
        {
            return {.Spec = {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = description, .Min = 1.0},
                .Required = required};
        }
    }
}
