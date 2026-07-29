#pragma once

// Include this file only after importing Extrinsic.Runtime.EditorCommandHistory.
// The including translation unit must provide <memory>, <string>, and <utility>
// from its global module fragment.

namespace Extrinsic::Runtime::Internal
{
    // Runtime-internal, typed transaction shape shared by feature owners.
    //
    // `validate` must be side-effect free and return `Applied` only when the
    // stable identity and current generations match `expectedGenerations`.
    // `applyAtomically` must either publish the complete target state and return
    // `Applied`, report `NoChange`, or fail without partial mutation. `stamp`
    // runs only after a successful apply, stamps the owner's dirty generations,
    // and returns the generations expected by the next undo/redo transition.
    template <typename TIdentity,
              typename TGenerations,
              typename TState,
              typename TValidate,
              typename TApply,
              typename TStamp>
    [[nodiscard]] static EditorCommandHistoryResult
    ExecuteUndoableEntityMutation(
        EditorCommandHistory& history,
        std::string label,
        TIdentity identity,
        TGenerations expectedGenerations,
        TState before,
        TState after,
        TValidate validate,
        TApply applyAtomically,
        TStamp stamp,
        const bool dirtying = true)
    {
        struct MutationContext final
        {
            TIdentity Identity;
            TGenerations ExpectedGenerations;
            TState Before;
            TState After;
            TValidate Validate;
            TApply ApplyAtomically;
            TStamp Stamp;

            [[nodiscard]] EditorCommandHistoryStatus ApplyTarget(
                const TState& target)
            {
                const EditorCommandHistoryStatus validation =
                    Validate(Identity, ExpectedGenerations, target);
                if (validation != EditorCommandHistoryStatus::Applied)
                    return validation;

                const EditorCommandHistoryStatus applied =
                    ApplyAtomically(Identity, target);
                if (applied != EditorCommandHistoryStatus::Applied)
                    return applied;

                ExpectedGenerations =
                    Stamp(Identity, ExpectedGenerations, target);
                return EditorCommandHistoryStatus::Applied;
            }
        };

        auto context = std::make_shared<MutationContext>(
            MutationContext{
                .Identity = std::move(identity),
                .ExpectedGenerations = std::move(expectedGenerations),
                .Before = std::move(before),
                .After = std::move(after),
                .Validate = std::move(validate),
                .ApplyAtomically = std::move(applyAtomically),
                .Stamp = std::move(stamp),
            });

        return history.Execute(
            EditorCommandRecord{
                .Label = std::move(label),
                .Redo =
                    [context]()
                    {
                        return context->ApplyTarget(context->After);
                    },
                .Undo =
                    [context]()
                    {
                        return context->ApplyTarget(context->Before);
                    },
                .Dirtying = dirtying,
            });
    }
}
