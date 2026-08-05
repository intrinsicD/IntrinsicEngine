module;

#include <cstddef>
#include <cstdint>
#include <functional>
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

        // Owner-only lifetime operation. Withdrawal is phase-independent so a
        // provider can roll back partial registration or remove a borrowed
        // entry during locked shutdown. Missing/mismatched entries return an
        // error without adding a boot diagnostic; exact instance identity is
        // required before the record is erased.
        template <typename TService>
        [[nodiscard]] Core::Result Withdraw(TService& expected)
        {
            return WithdrawErased(
                Core::TypeToken<TService>(), &expected);
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
        [[nodiscard]] Core::Result WithdrawErased(
            ServiceTypeKey type,
            void* expected);
        [[nodiscard]] const ServiceRecord* FindErased(
            ServiceTypeKey type) const noexcept;
        void RecordMissingRequirement(std::string_view requester,
                                      std::string_view typeName);
        void RecordBootError(std::string message);

        std::unordered_map<ServiceTypeKey, ServiceRecord> m_Services{};
        std::vector<std::string> m_BootErrors{};
        ServiceRegistryPhase m_Phase{ServiceRegistryPhase::Registration};
    };
}
