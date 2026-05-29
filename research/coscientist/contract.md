# Co-Scientist ⇄ Engine Contract (Poisson-disk bootstrap)

Everything in the system talks through these two JSON shapes. Get this boundary stable
and the agents, the harness, and the judge can all evolve independently.

## Data flow

```
 orchestrator                harness host (trusted)            analyzer (trusted)
 ------------                ----------------------            ------------------
 Generation/Evolution  -->   builds + runs the UNTRUSTED   -->  reads point-set
 emit a CANDIDATE            candidate in isolation,            artifact, runs the
 manifest (below)           dumps a point-set artifact          correctness gate +
                            + perf, emits HARNESS RESULT        quality, emits SCORE
        ^                                                             |
        |____________________ final RESULT record ___________________|
                        (fitness signal -> Elo / ranking)
```

Trust boundary: the candidate is untrusted code. The harness host and the analyzer are
fixed, reviewed, trusted. The candidate only ever *produces a point set*; it never touches
scoring, ranking, or the loop. This is what makes a perf win believable.

## 1. Candidate manifest  (orchestrator -> harness)

```json
{
  "id": "cand_0007",
  "goal_id": "poisson_progressive_v1",
  "kind": "shader",                  // "shader" | "cpp_module"
  "entry": "main",                   // compute entry point / registered algo name
  "params": { "workgroup": 64, "phase_groups": 27, "k_candidates": 16 },
  "source": {
    "lang": "glsl-comp",             // "glsl-comp" | "spirv" | "cpp"
    "path": "candidates/cand_0007.comp"   // or inline "text": "..."
  },
  "provenance": { "parent": "cand_0003", "agent": "evolution", "round": 4 }
}
```

`shader` candidates are swapped into a fixed, trusted host pipeline (a bad shader can hang
the GPU but cannot corrupt host memory or your tree). `cpp_module` candidates implement
your engine's algorithm-registry interface and are compiled into the benchmark executable.
Same manifest, two build paths.

## 2. Harness result  (harness host -> analyzer/orchestrator)

```json
{
  "id": "cand_0007",
  "build":  { "ok": true,  "log": "..." },
  "run":    { "ok": true,  "timed_out": false, "gpu_reset": false, "log": "..." },
  "artifact": "out/cand_0007_points.npy",      // Nx2 point set the analyzer scores
  "performance": {
    "samples": 65536,
    "time_ms": 4.21,                            // median of repeated, warmed-up runs
    "samples_per_sec": 1.557e7,
    "vram_mb": 38.0
  }
}
```

Rules the harness host MUST enforce (these are the safety/validity guarantees, not options):
- **hard timeout** per run; kill the child process on expiry, report `timed_out`.
- **process isolation**: each candidate runs in a child process so a crash/hang/GPU reset
  is recoverable and reported (`run.ok=false`), never fatal to the orchestrator.
- **fixed inputs**: identical domain, radius, target count, RNG seed, and test scene across
  all candidates for a goal — otherwise metrics aren't comparable.
- **warmup + repeats** for timing; report the median, not a single sample.
- on any build/run failure, return the log: it becomes feedback to the Evolution agent.

## 3. Final result record  (what the orchestrator stores per candidate)

`harness result` ⊕ `analyzer output` (gate + quality). The analyzer already emits
`gate` and `quality`; merge the harness `build`/`run`/`performance` in. Ranking reads it:

- `gate.passed == false`  -> candidate is dead. Never ranked on perf. Log routed to Evolution.
- `gate.passed == true`   -> eligible. Fitness = weighted(quality components, performance),
  weights chosen per research goal (e.g. progressive blue noise weights spectrum heavily;
  a real-time pass weights `samples_per_sec` heavily). Elo updates from pairwise outcomes
  on this fitness; debate-only Elo is the fallback for candidates that didn't run.

## Why correctness is gated before perf (restate, because it's the whole game)
An LLM will cheerfully hand you a sampler that's 3x faster and subtly wrong. If perf ever
counts before the gate passes, the system optimizes toward fast invalid output and reports
success. The order in §3 is not negotiable.
