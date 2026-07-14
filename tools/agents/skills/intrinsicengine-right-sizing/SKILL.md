---
name: intrinsicengine-right-sizing
description: Identify over-engineering before and while it ships, plan the simplest alternative that still satisfies the repository contract, and implement that instead. Use at plan time before adding any new interface, `*Service`/`*Bridge`/`*Registry`/`*Queue`/`*Binding`/`*Submission` file, module/plugin framework, event/command/job indirection, dependency struct, or forwarding facade; at review time when a diff grows abstraction surface or pure pass-through code; when a small behavior change fans out across many files/layers; and whenever the user says "over-engineered", "too complex", "too much glue", "Kleber", "boilerplate", "ceremony", "YAGNI", "simplify", or asks whether an abstraction is worth it.
---

# IntrinsicEngine Right-Sizing

A discipline for keeping the engine's complexity proportional to the problems
it actually solves. The failure mode this skill guards against is not "too
much code" — it is **structure that does not carry its weight**: interfaces
with one implementation, frameworks with one consumer, facades that only
forward, and features fragmented across role-named files.

The counter-failure mode is equally real: stripping load-bearing seams and
calling it simplification. Both directions are covered here.

**Authority.** Cross-cutting discipline skill: this SKILL.md is authoritative
for the right-sizing procedure, but `AGENTS.md` wins on anything it covers
(layering invariants, module rules, task workflow, verification). "Simpler"
never means violating the dependency table, dropping explicit failure states,
or skipping the CPU/null correctness gate.

## When to run

Three checkpoints. Do not wait for the third.

1. **Plan time (cheapest).** Before writing a task slice or implementation
   plan that introduces new abstraction surface — an `I*` interface, a
   `*Service`/`*Bridge`/`*Registry`/`*Queue`/`*Binding`/`*Submission` unit, a
   registration/scheduling framework, an event/command/job hop, a
   `SetDependencies(BigStruct)` seam — run the audit below on the *plan* and
   write the right-sizing note into the task file.
2. **Diff time.** Before committing, if the diff adds any of the above or a
   file whose methods are ≥80% single-line delegation, audit the diff.
3. **Review time.** The pre-PR review checklist
   (`docs/agent/review-checklist.md`) includes a right-sizing item; when it
   flags, apply this skill and either fix in-slice or file a follow-up task.

## Detection heuristics

Each heuristic cites the evidencing finding from the 2026-07 repo audit so it
is falsifiable, not taste. A hit is a *flag to investigate*, not an automatic
verdict — the justified-complexity test below decides.

1. **Interface with one production implementation.** Null devices and test
   doubles that exist to serve the CPU gate count as real implementations;
   a one-off adapter struct implementing a hooks interface does not.
   *Evidence:* at audit time ~10 of 16 `export class I*` interfaces had one
   production impl; the seven `Core::I*FrameHooks` contracts expressed
   straight-line call ordering as 7 interfaces + 7 one-off structs + 5
   `Execute*Contract` free functions.
2. **Framework with one consumer.** Registration, scheduling, or plugin
   machinery whose registry contains exactly one entry.
   *Evidence:* `IRuntimeModule` + `ServiceRegistry` + `ModuleSchedule`
   (topological sort, cycle detection, two-phase boot) served exactly one
   module (`ClusteringModule`).
3. **Pure-forwarding facade / double-forwarding chain.** A class whose every
   method delegates to one member, especially when a second facade re-forwards
   to it (Engine → Service → object).
   *Evidence:* `Runtime.RenderExtractionService.cpp` (113 lines, 100%
   delegation); `VisualizationAdapterBinding` setters traversing
   Engine → RenderExtractionService → RenderExtractionCache.
4. **Feature fragmentation across role-named files.** One feature spread over
   Service → Queue → GpuQueue → Binding → Submission units that individually
   mostly forward or buffer.
   *Evidence:* object-space normal bake: 12 files, ~2,650 lines for one
   feature whose kernel lives in a single graphics module.
5. **Plumbing ratio.** Estimate `glue LOC / total feature LOC` where glue is
   registration, subscription, status enums/strings, dependency structs, and
   forwarding. Above ~70%, flag.
   *Evidence:* `ClusteringModule` — 990 lines around one call to
   `Geometry::KMeans::Cluster(...)`.
6. **Async ceremony for synchronous-feeling actions.** Command bus → handler →
   job → completion event → subscriber → second event, where a direct call
   with an explicit result would satisfy the same contract.
7. **Missing seam symptom.** A trivially-scoped behavior change (e.g. one new
   debug primitive) requires touching ≥5 files across ≥3 layers because no
   direct API exists. The fix is usually *adding one good seam*, not more
   layers.
8. **Speculative generality markers.** Config or type parameters with exactly
   one value ever passed; pool/buffering machinery dormant in the default
   configuration; "future backend" branches with no task attached.

## Quantitative probes

Run what the flagged scope needs, not all of it:

```bash
# Interface inventory vs. implementation count
grep -rn "export class I" src/ --include='*.cppm'
grep -rln "public .*I<Name>" src/            # count impls per interface

# Layer weight — composition should not outweigh the domain it wires
for d in src/*/; do echo "$d $(find $d -name '*.cpp' -o -name '*.cppm' | xargs cat | wc -l)"; done

# Role-named glue surface in a layer
ls src/runtime/ | grep -E 'Service|Bridge|Registry|Queue|Binding|Submission|Adapters'

# Fan-in/fan-out hot spots (module-import graph only; confirm with layering gate)
# knowledge-graph MCP: god_nodes, get_neighbors on the flagged module
python3 tools/repo/check_layering.py --root src --strict
```

## Justified-complexity test (the keep-list)

A flagged element **stays** if any of these holds — record which one in the
task file or review note:

- **Two real implementations exist today**, or the second is in an *active*
  task with an ID (not "someday"). Null/GLFW windows and Null/Vulkan devices
  are the canonical justified pair: they are what make the headless CPU gate
  possible.
- **It isolates a genuinely volatile boundary** — GPU backend, platform/window
  system, file-format zoo — where churn on the far side must not ripple.
- **It is load-bearing for correctness** — e.g. deferred-retire residency
  windows, snapshot/token cancellation in the job system, render-graph
  barrier/alias compilation. Real lifetime and thread-safety machinery is not
  ceremony.
- **Removing it would violate `AGENTS.md`** (layering, explicit failure
  states, deterministic testability).

If none holds, the element is ceremony. Plan the simpler alternative.

## The right-sizing plan (required before implementing)

For each ceremony finding, write a short plan — in the task file for new work,
in the review note for existing code:

1. **Element** — file/module and which heuristic flagged it.
2. **Simpler alternative** — typical moves:
   - Inline a single-impl interface at its only call site; keep a plain
     struct/free function (matches the repo preference for data-driven plain
     structs).
   - Delete framework machinery until a second consumer exists; wire the one
     consumer directly.
   - Collapse forwarding facades; let the owner hold the object directly.
   - Merge role-named fragments of one feature into one module with clear
     internal sections.
   - Replace async event chains with a direct call returning an explicit
     result, where no cross-thread or cross-frame boundary is crossed.
   - Add the missing direct seam (e.g. an immediate-mode debug-draw API)
     instead of threading one more field through the pipeline.
3. **Blast radius** — importers/consumers (knowledge graph + grep), tests
   touched, and confirmation `check_layering.py` stays green.
4. **Reintroduction trigger** — the concrete future event that would justify
   the abstraction again (e.g. "second `IRuntimeModule` implementor lands").
   This keeps the deletion reversible as a decision, not just as code.

## Implementing the alternative instead

- **New work:** implement the simple version from the start. Do not build the
  ceremonial version first "to match house patterns" — proven slice templates
  (geometry-IO format, import-visibility) are house patterns *because* they
  carry evidence; a fresh abstraction is not.
- **Existing code, small blast radius** (single layer, tests already cover
  behavior): simplify in the same slice, but in a **separate commit** from any
  feature work.
- **Existing code, large blast radius:** file a task per
  `intrinsicengine-task-workflow`; never mix a structural collapse with
  semantic changes (same rule as mechanical moves).
- **Verification:** behavior-preserving simplification still runs the
  strongest relevant test subset; the default CPU gate must stay green.
  Simplifying untested code means writing the missing test first.
- **Docs:** structure changes follow `intrinsicengine-docs-sync` as usual.

## Anti-patterns (do not do these in the name of simplicity)

- **Confusing simple with sloppy.** The sibling framework24 audit shows where
  under-structure decays after a few years: a god-state object passed to 65
  systems via a leaking shared_ptr cycle, live `_new`/`_old` duplicate forks
  compiled side by side, one menu bar copy-pasted across 58 files, zero viewer
  tests, and mesh IO whose only input validation was `assert()` — compiled out
  in release. None of that is the target state. Right-sizing removes
  *ceremony*, never tests, explicit failure handling, or input validation.
- **Stripping the vertical seams.** `RHI::IDevice`, `Platform::IWindow`, the
  render-graph compiler, and the ECS are what make the headless gate and the
  backend split work. They pass the keep-list; leave them.
- **Simplification churn.** Do not re-litigate a keep-list decision every
  session; record the verdict once (task file or code comment stating the
  constraint) and move on.
- **Big-bang rewrites.** Right-size one flagged element per slice. A 20-file
  "simplification pass" is itself over-engineering of the cleanup.
- **Pattern-matching without reading.** A `*Service` name or a single-impl
  interface is a flag, not a verdict — read the code and run the keep-list
  test before planning its removal.
