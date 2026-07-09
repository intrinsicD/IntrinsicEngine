module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module Extrinsic.Runtime.ServiceRegistry;

import Extrinsic.Core.Error;
import Extrinsic.Core.FrameGraph;

namespace Extrinsic::Runtime
{
    export using ServiceTypeKey = std::size_t;

    export template <typename TService>
    [[nodiscard]] consteval std::string_view ServiceTypeNameOf() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return "ServiceTypeNameOf<unknown>";
#endif
    }

    export enum class ServiceRegistryPhase : std::uint8_t
    {
        Registration,
        Resolution,
        Locked,
    };

    export struct ServiceRegistryStats
    {
        std::uint32_t ProvidedServices{0};
        std::uint32_t MissingRequirements{0};
        std::uint32_t BootErrors{0};
    };

    export class ServiceRegistry
    {
    public:
        ServiceRegistry() = default;
        ServiceRegistry(const ServiceRegistry&) = delete;
        ServiceRegistry& operator=(const ServiceRegistry&) = delete;

        void BeginRegistration();
        void BeginResolution();
        void Lock() noexcept;
        void Reset();

        template <typename TService>
        [[nodiscard]] Core::Result Provide(
            TService& service,
            std::string_view provider = {})
        {
            return ProvideErased(Core::TypeToken<TService>(),
                                 ServiceTypeNameOf<TService>(),
                                 &service,
                                 provider);
        }

        template <typename TService>
        [[nodiscard]] TService* Find() const noexcept
        {
            const ServiceRecord* record =
                FindErased(Core::TypeToken<TService>());
            if (record == nullptr)
                return nullptr;
            return static_cast<TService*>(record->Instance);
        }

        template <typename TService>
        [[nodiscard]] Core::Expected<std::reference_wrapper<TService>> Require(
            std::string_view requester)
        {
            if (TService* service = Find<TService>(); service != nullptr)
                return std::ref(*service);

            RecordMissingRequirement(requester, ServiceTypeNameOf<TService>());
            return Core::Err<std::reference_wrapper<TService>>(
                Core::ErrorCode::ResourceNotFound);
        }

        [[nodiscard]] Core::Result ValidateBoot() const noexcept;
        [[nodiscard]] bool HasBootErrors() const noexcept;
        [[nodiscard]] std::string_view LastBootError() const noexcept;
        [[nodiscard]] std::span<const std::string> BootErrors() const noexcept;
        [[nodiscard]] ServiceRegistryStats Stats() const noexcept;
        [[nodiscard]] ServiceRegistryPhase Phase() const noexcept;

    private:
        struct ServiceRecord
        {
            void* Instance{};
            std::string TypeName{};
            std::string Provider{};
        };

        [[nodiscard]] Core::Result ProvideErased(ServiceTypeKey type,
                                                 std::string_view typeName,
                                                 void* instance,
                                                 std::string_view provider);
        [[nodiscard]] const ServiceRecord* FindErased(
            ServiceTypeKey type) const noexcept;
        void RecordMissingRequirement(std::string_view requester,
                                      std::string_view typeName);
        void RecordBootError(std::string message);

        std::unordered_map<ServiceTypeKey, ServiceRecord> m_Services{};
        std::vector<std::string> m_BootErrors{};
        ServiceRegistryPhase m_Phase{ServiceRegistryPhase::Registration};
        ServiceRegistryStats m_Stats{};
    };
}
