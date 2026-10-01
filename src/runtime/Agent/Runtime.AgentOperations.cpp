module;

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

module Extrinsic.Runtime.AgentOperations;

namespace Extrinsic::Runtime
{
    bool AgentOperationRegistry::Register(AgentOperationSpec spec)
    {
        if (spec.Name.empty() || !spec.Invoke || Find(spec.Name) != nullptr) return false;
        m_Entries.push_back(std::move(spec));
        return true;
    }

    const AgentOperationSpec* AgentOperationRegistry::Find(const std::string_view name) const noexcept
    {
        for (const auto& entry : m_Entries)
            if (entry.Name == name) return &entry;
        return nullptr;
    }

    AgentOperationOutcome InvokeAgentOperation(const AgentOperationRegistry& registry, const std::string_view name,
                                               const AgentOperationContext& context, const std::string_view argumentsJson,
                                               const bool readOnlySession)
    {
        const auto* spec = registry.Find(name);
        if (spec == nullptr) return {.IsError = true, .Text = "Unknown operation '" + std::string(name) + "'."};
        if (readOnlySession && !spec->ReadOnly)
            return {.IsError = true,
                    .Text = "'" + spec->Name + "' changes the scene or files, but the Sandbox agent lane is read-only (--agent-readonly)."};
        if (spec->NeedsPresentedFrame && !context.ViewportPresentable)
            return {.IsError = true,
                    .Text = "'" + spec->Name + "' needs a presented frame, but the Sandbox window is minimized; restore it and retry.",
                    .ErrorCode = "viewport_not_presentable"};
        const ScopedEditorCommandLabelPrefix prefix{context.History, "Agent: "};
        return spec->Invoke(context, argumentsJson.empty() ? std::string_view{"{}"} : argumentsJson);
    }

    std::optional<std::string> ResolveAgentPath(const AgentOperationContext& context, const std::string_view path)
    {
        namespace fs = std::filesystem;
        if (context.AllowedRoots.empty() || path.empty()) return std::nullopt;
        std::error_code error;
        fs::path candidate{std::string(path)};
        if (candidate.is_relative()) candidate = fs::path(context.AllowedRoots.front()) / candidate;
        const fs::path resolved = fs::weakly_canonical(candidate, error);
        if (error) return std::nullopt;
        // weakly_canonical resolves every symlink that has a target; one still in the path is dangling and
        // a write through it would create its target, possibly outside the roots.
        fs::path partial;
        for (const auto& component : resolved)
        {
            partial /= component;
            if (fs::is_symlink(fs::symlink_status(partial, error))) return std::nullopt;
        }
        for (const auto& root : context.AllowedRoots)
        {
            const fs::path base = fs::weakly_canonical(fs::path(root), error);
            if (error) continue;
            // Component-wise containment: "/data/a" must not admit "/data/ab" or "/data/a/../b".
            const fs::path relative = resolved.lexically_relative(base);
            const bool inside = !relative.empty() && *relative.begin() != "..";
            if (inside) return resolved.string();
        }
        return std::nullopt;
    }
}
