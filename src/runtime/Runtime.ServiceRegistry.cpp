module;

#include <cstddef>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

module Extrinsic.Runtime.ServiceRegistry;

import Extrinsic.Core.Logging;

namespace Extrinsic::Runtime
{
    void ServiceRegistry::ProvideErased(std::size_t      type,
                                        std::string_view typeName,
                                        void*            service)
    {
        if (service == nullptr)
        {
            Core::Log::Error(
                "[ServiceRegistry] Rejected Provide of a null '{}' service.",
                typeName);
            return;
        }

        // Re-Provide replaces: two modules providing the same service type is
        // a composition decision the app owns (last provider wins), logged so
        // an accidental double-provide is never silent.
        const auto [it, inserted] =
            m_Services.try_emplace(type, Entry{service, typeName});
        if (!inserted)
        {
            Core::Log::Warn(
                "[ServiceRegistry] Service '{}' re-provided; replacing the "
                "previously provided reference.",
                typeName);
            it->second = Entry{service, typeName};
        }
    }

    void* ServiceRegistry::RequireErased(std::size_t type, std::string_view typeName)
    {
        const auto it = m_Services.find(type);
        if (it == m_Services.end())
        {
            // Fail-closed (ADR-0024 D3): a missing required service is a
            // composition-root defect surfaced loudly at boot — naming the
            // requesting module and the missing type — never a null-deref at
            // frame 400. The build is -fno-exceptions, so there is no throw
            // path to unwind; the process aborts by design.
            const std::string_view requester =
                m_ActiveRequester.empty() ? std::string_view{"<unknown>"}
                                          : m_ActiveRequester;
            Core::Log::Error(
                "[ServiceRegistry] Module '{}' requires service '{}', which no "
                "module provided; aborting boot.",
                requester,
                typeName);
            std::abort();
        }
        return it->second.Service;
    }

    void* ServiceRegistry::FindErased(std::size_t type) noexcept
    {
        const auto it = m_Services.find(type);
        return it == m_Services.end() ? nullptr : it->second.Service;
    }

    void ServiceRegistry::SetActiveRequester(std::string_view moduleName) noexcept
    {
        m_ActiveRequester = moduleName;
    }

    std::size_t ServiceRegistry::ServiceCount() const noexcept
    {
        return m_Services.size();
    }

    void ServiceRegistry::Clear() noexcept
    {
        m_Services.clear();
        m_ActiveRequester = {};
    }
}
