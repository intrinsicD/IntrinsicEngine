module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    void ServiceRegistry::BeginRegistration()
    {
        Reset();
        m_Phase = ServiceRegistryPhase::Registration;
    }

    void ServiceRegistry::BeginResolution()
    {
        m_Phase = ServiceRegistryPhase::Resolution;
    }

    void ServiceRegistry::Lock() noexcept
    {
        m_Phase = ServiceRegistryPhase::Locked;
    }

    void ServiceRegistry::Reset()
    {
        m_Services.clear();
        m_BootErrors.clear();
        m_Stats = ServiceRegistryStats{};
        m_Phase = ServiceRegistryPhase::Registration;
    }

    Core::Result ServiceRegistry::ProvideErased(
        const ServiceTypeKey type,
        const std::string_view typeName,
        void* const instance,
        const std::string_view provider)
    {
        if (m_Phase != ServiceRegistryPhase::Registration)
        {
            RecordBootError("ServiceRegistry Provide<" +
                            std::string(typeName) +
                            "> called outside registration phase by " +
                            std::string(provider.empty() ? "<unnamed>" : provider));
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        if (instance == nullptr)
        {
            RecordBootError("ServiceRegistry Provide<" +
                            std::string(typeName) +
                            "> received a null service from " +
                            std::string(provider.empty() ? "<unnamed>" : provider));
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        if (const auto existing = m_Services.find(type);
            existing != m_Services.end())
        {
            RecordBootError("ServiceRegistry duplicate Provide<" +
                            std::string(typeName) +
                            "> from " +
                            std::string(provider.empty() ? "<unnamed>" : provider) +
                            "; already provided by " +
                            existing->second.Provider);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Services.emplace(
            type,
            ServiceRecord{
                .Instance = instance,
                .TypeName = std::string(typeName),
                .Provider = std::string(provider.empty() ? "<unnamed>" : provider),
            });
        m_Stats.ProvidedServices =
            static_cast<std::uint32_t>(m_Services.size());
        return Core::Ok();
    }

    const ServiceRegistry::ServiceRecord* ServiceRegistry::FindErased(
        const ServiceTypeKey type) const noexcept
    {
        const auto it = m_Services.find(type);
        if (it == m_Services.end())
            return nullptr;
        return &it->second;
    }

    void ServiceRegistry::RecordMissingRequirement(
        const std::string_view requester,
        const std::string_view typeName)
    {
        m_Stats.MissingRequirements += 1u;
        RecordBootError("ServiceRegistry missing Require<" +
                        std::string(typeName) +
                        "> requested by " +
                        std::string(requester.empty() ? "<unnamed>" : requester));
    }

    void ServiceRegistry::RecordBootError(std::string message)
    {
        m_BootErrors.push_back(std::move(message));
        m_Stats.BootErrors = static_cast<std::uint32_t>(m_BootErrors.size());
    }

    Core::Result ServiceRegistry::ValidateBoot() const noexcept
    {
        if (m_BootErrors.empty())
            return Core::Ok();
        return Core::Err(Core::ErrorCode::InvalidState);
    }

    bool ServiceRegistry::HasBootErrors() const noexcept
    {
        return !m_BootErrors.empty();
    }

    std::string_view ServiceRegistry::LastBootError() const noexcept
    {
        if (m_BootErrors.empty())
            return {};
        return m_BootErrors.back();
    }

    std::span<const std::string> ServiceRegistry::BootErrors() const noexcept
    {
        return m_BootErrors;
    }

    ServiceRegistryStats ServiceRegistry::Stats() const noexcept
    {
        ServiceRegistryStats stats = m_Stats;
        stats.ProvidedServices =
            static_cast<std::uint32_t>(m_Services.size());
        stats.BootErrors = static_cast<std::uint32_t>(m_BootErrors.size());
        return stats;
    }

    ServiceRegistryPhase ServiceRegistry::Phase() const noexcept
    {
        return m_Phase;
    }
}
