module;

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <vector>
#include <expected>

export module Extrinsic.Core.CallbackRegistry;

import Extrinsic.Core.StrongHandle;

namespace Extrinsic::Core::detail
{
    template <typename T>
    struct IsExpected : std::false_type
    {
    };

    template <typename U, typename E>
    struct IsExpected<std::expected<U, E>> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool is_expected_v = IsExpected<T>::value;
}

export namespace Extrinsic::Core
{
    // -------------------------------------------------------------------------
    // CallbackRegistry - type-safe, generational, slot-based callback storage.
    //
    // Stores type-erased callables keyed by a StrongHandle<Tag> (the Token).
    // Tokens are stable across Register/Unregister churn and are invalidated
    // generationally - a stale token cannot match a newly-assigned slot, so
    // re-using a freed slot for a different callable cannot silently invoke
    // the wrong target.
    //
    // Thread-safety:
    //   - Register / Unregister take a unique lock.
    //   - TryLookup / Invoke / Size take a shared lock only long enough to
    //     COPY the std::function, then release it before invocation. This
    //     means the user callable does NOT run under the registry mutex;
    //     callers may take any other locks (AssetRegistry, etc.) without
    //     risking inversion with the registry lock.
    //
    // Tag usage (mirrors Core::StrongHandle's Tag pattern):
    //   struct LoaderTag {};
    //   using LoaderRegistry = Core::CallbackRegistry<Core::Result(std::string_view), LoaderTag>;
    //   using LoaderToken    = LoaderRegistry::Token;
    //
    // Tokens from two registries with different Tags are not interchangeable
    // at compile time. Tokens with the same Signature but different Tag are
    // distinct types (e.g. LoaderToken vs EventListenerToken).
    // -------------------------------------------------------------------------

    template <typename Signature, typename Tag>
    class CallbackRegistry;

    template <typename R, typename... Args, typename Tag>
    class CallbackRegistry<R(Args...), Tag>
    {
    public:
        using Token = StrongHandle<Tag>;
        using Function = std::function<R(Args...)>;

        CallbackRegistry() = default;
        CallbackRegistry(const CallbackRegistry&) = delete;
        CallbackRegistry& operator=(const CallbackRegistry&) = delete;
        CallbackRegistry(CallbackRegistry&&) = delete;
        CallbackRegistry& operator=(CallbackRegistry&&) = delete;

        [[nodiscard]] Token Register(Function fn)
        {
            std::unique_lock lock(m_Mutex);
            if (!m_FreeList.empty())
            {
                const uint32_t index = m_FreeList.back();
                m_FreeList.pop_back();
                auto& slot = m_Slots[index];
                slot.occupied = true;
                slot.fn = std::move(fn);
                // Generation was bumped at Unregister time; do not bump again.
                return Token{index, slot.generation};
            }

            const uint32_t index = static_cast<uint32_t>(m_Slots.size());
            m_Slots.push_back(Slot{.generation = 1u, .occupied = true, .fn = std::move(fn)});
            return Token{index, 1u};
        }

        // Returns true when the token referred to a live slot that we just
        // freed; false when the token was already stale. Bumping the
        // generation on release ensures stale tokens never match if the slot
        // is later re-used.
        bool Unregister(Token token) noexcept
        {
            std::unique_lock lock(m_Mutex);
            if (!IsLiveUnlocked(token))
            {
                return false;
            }
            auto& slot = m_Slots[token.Index];
            slot.occupied = false;
            slot.fn = Function{};
            ++slot.generation;
            m_FreeList.push_back(token.Index);
            return true;
        }

        [[nodiscard]] bool Contains(Token token) const noexcept
        {
            std::shared_lock lock(m_Mutex);
            return IsLiveUnlocked(token);
        }

        // Snapshot the stored callable and invoke it outside the registry
        // mutex. For non-void R: returns std::nullopt on stale token, the
        // result wrapped in std::optional otherwise. For void R: returns
        // bool (true = invoked, false = stale token). Callers that need an
        // error-typed flatten for Expected-returning callables should use
        // InvokeOr below.
        template <typename... A>
        [[nodiscard]] auto Invoke(Token token, A&&... args) const
            -> std::conditional_t<std::is_void_v<R>, bool, std::optional<R>>
        {
            Function fn;
            {
                std::shared_lock lock(m_Mutex);
                if (!IsLiveUnlocked(token))
                {
                    if constexpr (std::is_void_v<R>)
                    {
                        return false;
                    }
                    else
                    {
                        return std::nullopt;
                    }
                }
                fn = m_Slots[token.Index].fn;
            }
            if constexpr (std::is_void_v<R>)
            {
                fn(std::forward<A>(args)...);
                return true;
            }
            else
            {
                return std::optional<R>{fn(std::forward<A>(args)...)};
            }
        }

        // Convenience for Expected<U, E>-returning callables: flatten stale
        // token into a caller-chosen domain error code. Only available when
        // R is a std::expected<...>. Avoids making the registry aware of any
        // particular error taxonomy.
        template <typename E, typename... A>
            requires detail::is_expected_v<R> && std::is_same_v<E, typename R::error_type>
        [[nodiscard]] R InvokeOr(Token token, E missingError, A&&... args) const
        {
            Function fn;
            {
                std::shared_lock lock(m_Mutex);
                if (!IsLiveUnlocked(token))
                {
                    return R{std::unexpected(std::move(missingError))};
                }
                fn = m_Slots[token.Index].fn;
            }
            return fn(std::forward<A>(args)...);
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            std::shared_lock lock(m_Mutex);
            return m_Slots.size() - m_FreeList.size();
        }

    private:
        struct Slot
        {
            uint32_t generation = 0;
            bool occupied = false;
            Function fn;
        };

        [[nodiscard]] bool IsLiveUnlocked(Token token) const noexcept
        {
            if (!token.IsValid()) return false;
            if (token.Index >= m_Slots.size()) return false;
            const auto& slot = m_Slots[token.Index];
            return slot.occupied && slot.generation == token.Generation;
        }

        mutable std::shared_mutex m_Mutex{};
        std::vector<Slot> m_Slots{};
        std::vector<uint32_t> m_FreeList{};
    };
}