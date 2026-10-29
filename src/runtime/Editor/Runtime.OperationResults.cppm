// Results of declared operations (ADR 0031 Decision 7, RUNTIME-348): per operation, the newest run's
// result. The workspace session owns one store; the operation runtime writes it whoever started the
// run, and panels read a frame's copy. Keyed by the operation's definition tag (`Operation::Result`
// is the payload), so key and payload cannot be chosen apart (RUNTIME-349).
module;
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
export module Extrinsic.Runtime.OperationResults;

export namespace Extrinsic::Runtime
{
    // The address of Tag is the key of an operation's results; the operation names the stored type.
    template <class Operation> struct OperationResultKey
    {
        static constexpr char Tag{};
    };

    // An immutable copy of the stored results, read by one frame.
    class OperationResultSnapshot
    {
    public:
        template <class Operation> [[nodiscard]] std::shared_ptr<const typename Operation::Result> Last() const
        {
            const Entry* entry = Find(&OperationResultKey<Operation>::Tag);
            // The key was stored with Operation::Result (Store<Operation>), so the cast is exact.
            return entry ? std::static_pointer_cast<const typename Operation::Result>(entry->Result) : nullptr;
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
        template <class Operation> bool Store(const std::uint64_t invocation, typename Operation::Result result)
        {
            using Result = typename Operation::Result;
            const void* key = &OperationResultKey<Operation>::Tag;
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
    template <class Operation> void DismissOperationResult(const OperationResultDismiss& dismiss)
    {
        if (dismiss)
            dismiss(&OperationResultKey<Operation>::Tag);
    }

    // The runtime's write: only while the attachment that owns `store` is active (the check reads only
    // the attachment epoch, so a delivery that outlives its session stores nothing).
    template <class Operation>
    void RetainOperationResult(OperationResultStore* store,
        const std::function<bool()>& attachmentActive,
        const std::uint64_t invocation,
        const typename Operation::Result& result)
    {
        if (store && attachmentActive && attachmentActive())
            (void)store->template Store<Operation>(invocation, result);
    }
}
