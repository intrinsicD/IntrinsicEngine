module;

#include <concepts>
#include <utility>

export module Extrinsic.Core.HandleLease;

namespace Extrinsic::Core
{
    // -------------------------------------------------------------------------
    // LeasableManager concept
    // -------------------------------------------------------------------------
    // Any manager passed as ManagerType must satisfy this concept so that
    // the Lease template fails loudly at instantiation rather than at link time.
    //
    // Required manager API:
    //   void    Retain(HandleType)          — increment refcount / mark in-use
    //   void    Release(HandleType)         — decrement refcount / free if zero
    //   Lease   AcquireLease(HandleType)    — produce a new ref-counted lease
    //
    // NOTE: AcquireLease cannot be required here because Lease<H,M> is not yet
    // complete when the concept is evaluated for M.  It is documented as a
    // convention and enforced at the call site inside Share().
    // -------------------------------------------------------------------------
    export template <typename M, typename H>
    concept LeasableManager = requires(M& mgr, H handle)
    {
        { mgr.Retain(handle)  } -> std::same_as<void>;
        { mgr.Release(handle) } -> std::same_as<void>;
    };

    // -------------------------------------------------------------------------
    // Lease<HandleType, ManagerType>
    // -------------------------------------------------------------------------
    // RAII ownership wrapper for a ref-counted opaque handle.
    //
    // ManagerType is unconstrained in the template parameter list so that
    // `using Lease<H, M>` may appear inside M's own class body (while M is
    // still incomplete).  The LeasableManager concept is verified via a
    // static_assert inside Reset() — which is only instantiated when M is
    // complete.
    //
    // Design decisions:
    //   - Opaque: exposes only HandleType, never T*.  Data access goes through
    //     the manager (manager.Get(lease.GetHandle())).  This keeps resource
    //     layout changes and thread-safety decisions inside the manager.
    //   - Non-copyable, movable: exactly one Lease owns the ref at a time.
    //     Use Share() to produce an independent second ref-counted lease.
    //   - Two static factories replace the private constructor so managers do
    //     not need to friend this template:
    //       Lease::Adopt(mgr, h)  — take ownership without incrementing refcount
    //                               (use when the manager already counted for you)
    //       Lease::Retain(mgr, h) — increment refcount then take ownership
    //                               (use when producing a secondary ref)
    // -------------------------------------------------------------------------
    export template <typename HandleType, typename ManagerType>
    class Lease
    {
    public:
        // -- Factories --------------------------------------------------------

        /// Take ownership without an extra Retain (the manager already counted).
        [[nodiscard]] static Lease Adopt(ManagerType& manager, HandleType handle) noexcept
        {
            return Lease{&manager, handle, /*retain=*/false};
        }

        /// Increment the refcount and take ownership.
        [[nodiscard]] static Lease RetainNew(ManagerType& manager, HandleType handle)
        {
            return Lease{&manager, handle, /*retain=*/true};
        }

        // -- Lifecycle --------------------------------------------------------
        Lease() = default;

        ~Lease() { Reset(); }

        Lease(const Lease&)            = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : m_Manager(std::exchange(other.m_Manager, nullptr))
            , m_Handle (std::exchange(other.m_Handle,  HandleType{}))
        {}

        Lease& operator=(Lease&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_Manager = std::exchange(other.m_Manager, nullptr);
                m_Handle  = std::exchange(other.m_Handle,  HandleType{});
            }
            return *this;
        }

        // -- Observers --------------------------------------------------------
        [[nodiscard]] bool       IsValid()   const noexcept { return m_Manager && m_Handle.IsValid(); }
        [[nodiscard]] HandleType GetHandle() const noexcept { return m_Handle; }

        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }

        // -- Operations -------------------------------------------------------

        /// Produce an independent ref-counted lease for the same handle.
        /// The manager must implement AcquireLease(HandleType) -> Lease.
        [[nodiscard]] Lease Share() const
        {
            if (!IsValid()) return {};
            // Delegates to the manager so it can choose how to bump the refcount
            // atomically — we cannot do it safely from outside.
            return m_Manager->AcquireLease(m_Handle);
        }

        void Reset()
        {
            // Deferred concept check: ManagerType must satisfy LeasableManager<HandleType>
            // when Reset() is instantiated (at which point ManagerType is complete).
            static_assert(LeasableManager<ManagerType, HandleType>,
                "ManagerType must implement Retain(HandleType) and Release(HandleType)");
            if (m_Manager && m_Handle.IsValid())
                m_Manager->Release(m_Handle);
            m_Handle  = {};
            m_Manager = nullptr;
        }

    private:
        Lease(ManagerType* manager, HandleType handle, bool retain)
            : m_Manager(manager)
            , m_Handle (handle)
        {
            if (m_Manager && m_Handle.IsValid() && retain)
                m_Manager->Retain(m_Handle);
        }

        ManagerType* m_Manager = nullptr;
        HandleType   m_Handle{};
    };
}
