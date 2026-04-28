module;

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

export module Core.InplaceFunction;

// -------------------------------------------------------------------------
// Core::InplaceFunction — Small-buffer owning callable (no heap allocation)
// -------------------------------------------------------------------------
// PURPOSE: Drop-in replacement for std::function in hot paths where heap
// allocation is unacceptable (per-frame loops, FrameGraph nodes, render
// stages). Stores the callable inline in a fixed-size buffer.
//
// Key properties:
// - ZERO heap allocations — ever. Callable must fit in BufferSize.
// - Move-only (not copyable) — matches move-only callable captures.
// - const operator() — matches std::function semantics; mutable storage.
// - Compile-time size check — static_assert if callable exceeds buffer.
//
// Alternatives in the codebase:
// - Thunk + context pointer (FrameGraph pattern): zero overhead but no
//   ownership; requires arena allocation for captures.
// - std::function: heap-allocates for large captures, banned in hot paths.
// - InplaceFunction: owns captures inline, no heap, slightly larger object.
//
// Typical usage:
//   Core::InplaceFunction<void(int)> fn = [x, y](int z) { return x+y+z; };
//   fn(42);
//
// Buffer sizing:
// - Default 64 bytes covers lambdas capturing up to ~7 pointers.
// - Increase if needed: InplaceFunction<void(), 128>.
// -------------------------------------------------------------------------

export namespace Core
{
    inline constexpr size_t kDefaultInplaceFunctionSize = 64;

    // Primary template (undefined) — only the partial specialization for
    // function signatures R(Args...) is defined below.
    template <typename Signature, size_t BufferSize = kDefaultInplaceFunctionSize>
    class InplaceFunction;

    template <typename R, typename... Args, size_t BufferSize>
    class InplaceFunction<R(Args...), BufferSize>
    {
        // -----------------------------------------------------------------
        // Type-erased operations table (no RTTI, no virtual, no heap)
        // -----------------------------------------------------------------
        struct Vtable
        {
            R (*Invoke)(void*, Args&&...);
            void (*Destroy)(void*);
            void (*MoveConstruct)(void* dst, void* src);
        };

        template <typename F>
        static const Vtable* GetVtableFor()
        {
            static constexpr Vtable s_Vtable{
                // Invoke
                +[](void* storage, Args&&... args) -> R {
                    return (*static_cast<F*>(storage))(static_cast<Args&&>(args)...);
                },
                // Destroy
                +[](void* storage) {
                    std::destroy_at(static_cast<F*>(storage));
                },
                // MoveConstruct
                +[](void* dst, void* src) {
                    ::new (dst) F(static_cast<F&&>(*static_cast<F*>(src)));
                }
            };
            return &s_Vtable;
        }

        // Storage: mutable so const operator() can invoke mutable callables
        // (matches std::function semantics where the wrapper is const but
        // the stored callable may have mutable captured state).
        // Wrapped in a struct for alignment control on the storage buffer.
        struct alignas(std::max_align_t) Storage
        {
            unsigned char Data[BufferSize];
        };
        mutable Storage m_Storage{};
        const Vtable* m_Vtable = nullptr;

    public:
        // -----------------------------------------------------------------
        // Construction
        // -----------------------------------------------------------------

        // Default: empty (not callable).
        InplaceFunction() noexcept = default;

        // Construct from nullptr (empty).
        InplaceFunction(std::nullptr_t) noexcept {}

        // Construct from any callable that fits in the buffer.
        template <typename F>
            requires (!std::is_same_v<std::decay_t<F>, InplaceFunction>)
                  && std::is_invocable_r_v<R, std::decay_t<F>&, Args...>
        InplaceFunction(F&& f) // NOLINT(google-explicit-constructor)
        {
            using Stored = std::decay_t<F>;
            static_assert(sizeof(Stored) <= BufferSize,
                "Core::InplaceFunction: callable is too large for the inline buffer. "
                "Increase the BufferSize template parameter.");
            static_assert(alignof(Stored) <= alignof(std::max_align_t),
                "Core::InplaceFunction: callable alignment exceeds std::max_align_t.");
            static_assert(std::is_nothrow_move_constructible_v<Stored>,
                "Core::InplaceFunction: callable must be nothrow-move-constructible.");

            ::new (static_cast<void*>(m_Storage.Data)) Stored(static_cast<F&&>(f));
            m_Vtable = GetVtableFor<Stored>();
        }

        // -----------------------------------------------------------------
        // Not copyable
        // -----------------------------------------------------------------
        InplaceFunction(const InplaceFunction&) = delete;
        InplaceFunction& operator=(const InplaceFunction&) = delete;

        // -----------------------------------------------------------------
        // Move construction
        // -----------------------------------------------------------------
        InplaceFunction(InplaceFunction&& other) noexcept
        {
            if (other.m_Vtable)
            {
                other.m_Vtable->MoveConstruct(m_Storage.Data, other.m_Storage.Data);
                m_Vtable = other.m_Vtable;
                other.m_Vtable->Destroy(other.m_Storage.Data);
                other.m_Vtable = nullptr;
            }
        }

        // -----------------------------------------------------------------
        // Move assignment
        // -----------------------------------------------------------------
        InplaceFunction& operator=(InplaceFunction&& other) noexcept
        {
            if (this != &other)
            {
                if (m_Vtable)
                {
                    m_Vtable->Destroy(m_Storage.Data);
                }

                if (other.m_Vtable)
                {
                    other.m_Vtable->MoveConstruct(m_Storage.Data, other.m_Storage.Data);
                    m_Vtable = other.m_Vtable;
                    other.m_Vtable->Destroy(other.m_Storage.Data);
                    other.m_Vtable = nullptr;
                }
                else
                {
                    m_Vtable = nullptr;
                }
            }
            return *this;
        }

        // Assign from nullptr (reset to empty).
        InplaceFunction& operator=(std::nullptr_t) noexcept
        {
            if (m_Vtable)
            {
                m_Vtable->Destroy(m_Storage.Data);
                m_Vtable = nullptr;
            }
            return *this;
        }

        // -----------------------------------------------------------------
        // Destructor
        // -----------------------------------------------------------------
        ~InplaceFunction()
        {
            if (m_Vtable)
            {
                m_Vtable->Destroy(m_Storage.Data);
            }
        }

        // -----------------------------------------------------------------
        // Invocation (const — matches std::function semantics)
        // -----------------------------------------------------------------
        R operator()(Args... args) const
        {
            assert(m_Vtable && "Core::InplaceFunction: invoked empty function");
            return m_Vtable->Invoke(m_Storage.Data, static_cast<Args&&>(args)...);
        }

        // -----------------------------------------------------------------
        // State queries
        // -----------------------------------------------------------------
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return m_Vtable != nullptr;
        }

        // -----------------------------------------------------------------
        // Swap
        // -----------------------------------------------------------------
        friend void swap(InplaceFunction& a, InplaceFunction& b) noexcept
        {
            InplaceFunction tmp(static_cast<InplaceFunction&&>(a));
            a = static_cast<InplaceFunction&&>(b);
            b = static_cast<InplaceFunction&&>(tmp);
        }
    };
}
