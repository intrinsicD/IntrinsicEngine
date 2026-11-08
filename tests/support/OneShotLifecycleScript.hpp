// ARCH-023: the common lifecycle every one-shot step run shares (a GPU transaction of scalar analysis, normals or
// outliers, or the core alone), as a frame-driven script over its run controls, so each real owner's test runs the
// same assertions: started ready it records nothing (no step, Accept refused, cap 1, no live preview); Step 1 computes
// exactly one step and ends converged; a one-shot then refuses Run and a Stop keeps its result; Restart after that
// result begins generation 1, ready again with nothing computed; Run computes the same one step; Accept commits once.
// No preview copy is made in either generation. The owner's own evidence (no device work while ready, equal outputs
// of Step 1 and Run, one undo entry) goes in the callbacks. Advance once per frame or job drain until it answers true.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <gtest/gtest.h>

import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Tests
{
    struct OneShotLifecycleScript
    {
        enum class Stage : std::uint8_t
        {
            HoldReady,
            Stepping,
            Restarting,
            Running,
            Accepting,
            Done
        };

        std::function<void()> OnReadyHeld{};       // nothing recorded yet: the owner's no-work evidence
        std::function<void()> OnFirstResult{};     // generation 0 rests with its one step (Step 1)
        std::function<void()> OnRestartedReady{};  // generation 1 is ready: the old result is gone
        std::function<void()> OnRestartedResult{}; // generation 1 rests with its one step (Run)
        unsigned HoldFrames{5u};
        Stage Current{Stage::HoldReady};
        unsigned Held{0u};

        // One frame: true once the script ended (committed, or a failed check stopped it).
        bool Advance(Extrinsic::Runtime::StepRunsService& runs, const std::uint64_t run)
        {
            namespace R = Extrinsic::Runtime;
            if (Current == Stage::Done)
                return true;
            const auto o = runs.Observe(run);
            const auto stop = [&](const std::string& why)
            {
                ADD_FAILURE() << why;
                Current = Stage::Done;
                return true;
            };
            if (!o)
                return stop("no run " + std::to_string(run));
            if (R::IsTerminal(o->Phase) && Current != Stage::Accepting)
                return stop("the run ended early: " + std::string{R::ToString(o->Phase)} + " " + o->Diagnostic);
            const bool resting = o->Phase == R::StepRunPhase::Paused && !o->SegmentInFlight;
            std::string refusal;
            switch (Current)
            {
            case Stage::HoldReady:
                EXPECT_EQ(o->Phase, R::StepRunPhase::Ready);
                EXPECT_EQ(o->Steps, 0u) << "a ready one-shot records nothing";
                EXPECT_FALSE(o->SegmentInFlight);
                EXPECT_EQ(o->Execution.IterationCap, 1u) << "a one-shot is one logical step";
                EXPECT_EQ(o->Execution.PreviewEvery, 0u) << "a one-shot makes no live preview copy";
                if (++Held < HoldFrames)
                    return false;
                EXPECT_FALSE(runs.Control(run, R::StepRunAction::Accept, 1u, refusal)) << "nothing to accept yet";
                if (OnReadyHeld)
                    OnReadyHeld();
                if (!runs.Control(run, R::StepRunAction::Step, 1u, refusal))
                    return stop("step 1 refused: " + refusal);
                Current = Stage::Stepping;
                return false;
            case Stage::Stepping:
                if (!resting)
                    return false;
                EXPECT_EQ(o->Steps, 1u) << "Step 1 computes exactly one step";
                EXPECT_EQ(o->End, R::StepRunEnd::Converged);
                EXPECT_EQ(o->Generation, 0u);
                EXPECT_EQ(o->PreviewCopies, 0u);
                EXPECT_FALSE(runs.Control(run, R::StepRunAction::Run, 1u, refusal)) << "a one-shot ends after its step";
                if (OnFirstResult)
                    OnFirstResult();
                EXPECT_TRUE(runs.Control(run, R::StepRunAction::Stop, 1u, refusal)) << refusal;
                EXPECT_EQ(runs.Observe(run)->End, R::StepRunEnd::Converged) << "a Stop after the result keeps it";
                EXPECT_TRUE(o->CanRestart);
                if (!runs.Control(run, R::StepRunAction::Restart, 1u, refusal))
                    return stop("restart refused: " + refusal);
                Current = Stage::Restarting;
                return false;
            case Stage::Restarting:
                if (o->Generation != 1u || o->Phase != R::StepRunPhase::Ready)
                    return false;
                EXPECT_EQ(o->Steps, 0u) << "a restart starts from step 0";
                if (OnRestartedReady)
                    OnRestartedReady();
                if (!runs.Control(run, R::StepRunAction::Run, 1u, refusal))
                    return stop("run refused: " + refusal);
                Current = Stage::Running;
                return false;
            case Stage::Running:
                if (!resting)
                    return false;
                EXPECT_EQ(o->Steps, 1u) << "Run computes the same one step";
                EXPECT_EQ(o->End, R::StepRunEnd::Converged);
                EXPECT_EQ(o->Generation, 1u);
                EXPECT_EQ(o->PreviewCopies, 0u);
                if (OnRestartedResult)
                    OnRestartedResult();
                if (!runs.Control(run, R::StepRunAction::Accept, 1u, refusal))
                    return stop("accept refused: " + refusal);
                Current = Stage::Accepting;
                return false;
            case Stage::Accepting:
                if (!R::IsTerminal(o->Phase))
                    return false;
                EXPECT_EQ(o->Phase, R::StepRunPhase::Committed) << o->Diagnostic;
                Current = Stage::Done;
                return true;
            case Stage::Done: break;
            }
            return true;
        }
    };
}
