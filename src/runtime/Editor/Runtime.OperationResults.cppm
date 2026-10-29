// Results of declared operations (ADR 0031 Decision 7, RUNTIME-348): per operation, the newest run's
// result. The workspace session owns one store; the operation runtime writes it whoever started the
// run, and panels read a frame's copy. Keyed by result type: each operation has its own result type,
// and the definitions are not visible to the readers yet (RUNTIME-349 reconciles the key).
module;
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
export module Extrinsic.Runtime.OperationResults;

export namespace Extrinsic::Runtime
{
    // The address of Tag is the key of an operation's results; it names the stored type.
    template <class Result> struct OperationResultKey
    {
        static constexpr char Tag{};
    };

    // An immutable copy of the stored results, read by one frame.
    class OperationResultSnapshot
    {
    public:
        template <class Result> [[nodiscard]] std::shared_ptr<const Result> Last() const
        {
            const Entry* entry = Find(&OperationResultKey<Result>::Tag);
            // The key was stored together with this type (Store<Result>), so the cast is exact.
            return entry ? std::static_pointer_cast<const Result>(entry->Result) : nullptr;
        }

    protected:
        struct Entry
        {
            const void* Key{};
            std::uint64_t Invocation{};
            std::shared_ptr<const void> Result{};
        };
        [[nodiscard]] const Entry* Find(const void* key) const
        {
            for (const Entry& entry : m_Entries)
                if (entry.Key == key)
                    return &entry;
            return nullptr;
        }
        std::vector<Entry> m_Entries{};
    };

    class OperationResultStore : private OperationResultSnapshot
    {
    public:
        using OperationResultSnapshot::Last;

        // Ids in submission order; never reused, not after a dismiss or a reset.
        [[nodiscard]] std::uint64_t BeginRun() noexcept
        {
            return ++m_LastInvocation;
        }

        // Refused (false) when a newer run's result is stored; an equal id replaces, so a run's
        // terminal result replaces its first answer.
        template <class Result> bool Store(const std::uint64_t invocation, Result result)
        {
            const void* key = &OperationResultKey<Result>::Tag;
            Entry* entry = nullptr;
            for (Entry& candidate : m_Entries)
                if (candidate.Key == key)
                    entry = &candidate;
            if (entry && invocation < entry->Invocation)
                return false;
            auto stored = std::make_shared<const Result>(std::move(result));
            if (entry)
                *entry = {key, invocation, std::move(stored)};
            else
                m_Entries.push_back({key, invocation, std::move(stored)});
            return true;
        }

        // Removes the result and its ordering barrier: the next delivery of any run fills the slot.
        void Dismiss(const void* key)
        {
            std::erase_if(m_Entries,
                [key](const Entry& e)
                {
                    return e.Key == key;
                });
        }
        void Clear() noexcept
        {
            m_Entries.clear();
        }

        [[nodiscard]] OperationResultSnapshot Copy() const
        {
            return *this;
        }

    private:
        std::uint64_t m_LastInvocation{};
    };

    // A frame's dismiss of one operation's result (a no-op once its session is gone).
    using OperationResultDismiss = std::function<void(const void* key)>;
    template <class Result> void DismissOperationResult(const OperationResultDismiss& dismiss)
    {
        if (dismiss)
            dismiss(&OperationResultKey<Result>::Tag);
    }

    // The runtime's write: only while the attachment that owns `store` is active (the check reads only
    // the attachment epoch, so a delivery that outlives its session stores nothing).
    template <class Result>
    void RetainOperationResult(OperationResultStore* store,
        const std::function<bool()>& attachmentActive,
        const std::uint64_t invocation,
        const Result& result)
    {
        if (store && attachmentActive && attachmentActive())
            (void)store->Store(invocation, result);
    }
}
