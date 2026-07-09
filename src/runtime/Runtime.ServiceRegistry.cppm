module;

#include <cstddef>
#include <string_view>
#include <unordered_map>

export module Extrinsic.Runtime.ServiceRegistry;

import Extrinsic.Core.FrameGraph;

// ============================================================
// ARCH-011 — two-phase kernel service registry (ADR-0024 D3).
//
// The escape hatch for always-present synchronous infrastructure
// one module needs from another. Providers publish a reference
// during the register phase (`Provide` in `OnRegister`); consumers
// bind it during the resolve phase (`Require`/`Find` in
// `OnResolve`). A `Require` of an unprovided service is a
// fail-closed boot error naming the requesting module and the
// missing service type — never a null-deref at frame 400
// (ADR-0024 D3). Direct module-to-module pointers stay forbidden;
// this registry is the sanctioned, ordered channel.
//
// The registry stores borrowed references only: the providing
// module owns the lifetime, the registry is a lookup table, not an
// owner. Type identity uses the same compile-time FNV-1a
// `Core::TypeToken` the FrameGraph and CommandBus use (no RTTI —
// the build is -fno-rtti). A missing `Require` aborts the process
// (-fno-exceptions — there is no throw path to unwind), which is
// the intended fail-closed boot behavior.
//
// Layering: kernel substrate per ADR-0024 D9 — no domain nouns.
// ============================================================

namespace Extrinsic::Runtime
{
    // Compile-time, allocation-free diagnostics name for a service type.
    // The returned view points at the compiler's function-signature literal
    // (static storage duration) and contains the type name; precise
    // formatting is compiler-specific and only used for boot diagnostics.
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

    export class ServiceRegistry
    {
    public:
        ServiceRegistry() = default;

        ServiceRegistry(const ServiceRegistry&)            = delete;
        ServiceRegistry& operator=(const ServiceRegistry&) = delete;

        // Register phase (`OnRegister`). Publishes a borrowed reference the
        // provider keeps alive for the engine's lifetime. Re-providing a type
        // replaces the previous entry (a logged composition decision).
        template <typename TService>
        void Provide(TService& service)
        {
            ProvideErased(Core::TypeToken<TService>(),
                          ServiceTypeNameOf<TService>(),
                          static_cast<void*>(&service));
        }

        // Resolve phase (`OnResolve`). Fail-closed: aborts boot with a
        // diagnostic naming the active requester and the missing type when
        // no module provided `TService`.
        template <typename TService>
        [[nodiscard]] TService& Require()
        {
            return *static_cast<TService*>(
                RequireErased(Core::TypeToken<TService>(),
                              ServiceTypeNameOf<TService>()));
        }

        // Optional dependency (contribute-if-present). Returns nullptr when
        // no module provided `TService`; callers branch on it, never deref
        // blindly.
        template <typename TService>
        [[nodiscard]] TService* Find() noexcept
        {
            return static_cast<TService*>(FindErased(Core::TypeToken<TService>()));
        }

        // The Engine sets the module currently in its resolve phase so a
        // failed `Require` names the requester. Set around each module's
        // `OnResolve` and cleared afterward; tests set it directly.
        void SetActiveRequester(std::string_view moduleName) noexcept;

        [[nodiscard]] std::size_t ServiceCount() const noexcept;

        // Drop every provided reference (the providers still own the
        // objects). The Engine clears before a re-`Initialize()` so a reused
        // engine re-runs two-phase startup from an empty registry.
        void Clear() noexcept;

    private:
        void  ProvideErased(std::size_t type, std::string_view typeName, void* service);
        [[nodiscard]] void* RequireErased(std::size_t type, std::string_view typeName);
        [[nodiscard]] void* FindErased(std::size_t type) noexcept;

        struct Entry
        {
            void*            Service{nullptr};
            std::string_view TypeName{};
        };

        std::unordered_map<std::size_t, Entry> m_Services{};
        std::string_view                       m_ActiveRequester{};
    };
}
