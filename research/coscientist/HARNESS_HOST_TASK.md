# Task brief: implement the Co-Scientist harness host in IntrinsicEngine

Hand this to Claude Code **inside the IntrinsicEngine repo**, where it can read the actual
sampler registry, compute-dispatch path, shader-compilation entry, and CMake setup. This is
the one component that must be written against the engine's internals; everything upstream
(orchestrator, tournament, trusted analyzer) is already implemented and engine-agnostic.

## What you are building

A standalone command-line program — the **harness host** — that takes one *untrusted*
candidate algorithm, builds and runs it **headless** against fixed inputs in isolation, dumps
the resulting point set, and emits a harness-result JSON. The trusted Python analyzer scores
the point set separately; you do not implement scoring here.

Read `contract.md` (in the co-scientist package) first — §1 (candidate manifest), §2 (harness
result) and the "Rules the harness host MUST enforce" list are the spec you implement against.

## CLI (must match exactly — the orchestrator calls this)

```
poisson_harness --manifest <m.json> --points <out.txt> --out <result.json> \
                --timeout-ms <N> --radius <r> --target <count> --seed <s>
```

- `--manifest` : candidate manifest JSON (contract §1). `kind` is `"shader"` or `"cpp_module"`;
  `source.path` points to the candidate source on disk; `params` holds tunables.
- `--points`   : write the produced point set here as plain text, one `x y` per line
  (the analyzer reads whitespace-separated text — no .npy needed from C++).
- `--out`      : write the harness-result JSON here (contract §2: `build`, `run`, `artifact`,
  `performance`). Set `artifact` to the `--points` path on success.
- `--timeout-ms`, `--radius`, `--target`, `--seed` : fixed experiment inputs; honor them
  identically for every candidate so metrics are comparable.

## Two candidate paths (both required — targets are "both shaders and C++")

1. **`shader`** — swap the candidate compute shader into a *fixed, trusted host pipeline* that
   you own (buffers, bindings, dispatch, readback are engine code; only the shader body is
   the candidate). Compile via the engine's existing shader-compilation path. A bad shader can
   hang the GPU but cannot corrupt host memory — keep it this way; never let candidate code
   run on the host side for this path.
2. **`cpp_module`** — compile the candidate as a module implementing the engine's
   sampler-registry interface, link it into (or `dlopen`/`LoadLibrary` it from) the harness,
   invoke through the registry, read back the point buffer.

## Hard requirements (these are the safety + validity guarantees)

- **Headless**: initialize Vulkan with no swapchain/window; compute + readback only.
- **Process isolation**: run each candidate in a child process the parent can kill. A crash,
  hang, device-lost, or OOM must be *reported* (`run.ok=false`), never fatal to the caller.
- **Hard timeout**: enforce `--timeout-ms`; kill the child on expiry; set `run.timed_out=true`.
- **GPU reset / device-lost detection**: catch `VK_ERROR_DEVICE_LOST` and validation-layer
  fatal errors; set `run.gpu_reset=true` and capture the log.
- **Warmup + repeats**: discard a warmup run, time several runs, report the **median** in
  `performance.time_ms`; also report `samples`, `samples_per_sec`, `vram_mb`.
- **Logs are feedback**: on any build or run failure, put the compiler/validation log into
  `build.log` / `run.log`. The Evolution agent consumes these to fix the next candidate.
- **Never trust the candidate.** Assume it will hang, allocate absurd memory, write OOB in a
  shader, or never converge. Bound everything.

## Integration points to locate in the repo

- the sampler / algorithm **registry interface** (for `cpp_module` candidates),
- the **compute dispatch + buffer readback** path (for the fixed shader host pipeline),
- the **shader compilation** entry (GLSL/HLSL -> SPIR-V),
- **headless Vulkan** device/instance init (reuse the engine's if present),
- the **CMake** target and the build command that produces `poisson_harness`.

## Acceptance tests

1. **Reference agreement**: run the engine's existing, proven Poisson sampler through the
   harness; the analyzer (`python analyzer.py --points out.txt --radius <r> --domain 0 0 1 1`)
   must report `gate.passed = true` and a clean blue-noise spectrum. This confirms the harness
   faithfully reflects a known-good algorithm.
2. **Bad-candidate containment**: feed a deliberately broken shader (e.g. writes points
   outside the domain, or violates the radius); harness returns `run.ok=true` with an artifact,
   analyzer reports `gate.passed=false`, and the host does **not** crash.
3. **Timeout**: feed a busy-loop / non-terminating candidate; harness kills it within the
   timeout and reports `run.timed_out=true` without hanging the caller.
4. **Device-lost**: (if reproducible) a shader that triggers a TDR is caught and reported as
   `run.gpu_reset=true`, and the next invocation still succeeds.

## Suggested step order

1. Headless Vulkan init + a trivial compute dispatch that writes a known buffer and reads it
   back to `--points`; wire the CLI and emit a valid harness-result JSON. Pass acceptance #1
   with a trivial built-in sampler first.
2. Shader-swap path: load candidate shader from `source.path`, compile, run in the fixed host
   pipeline, read back. Pass #1 with the real reference sampler.
3. Child-process isolation + timeout + device-lost handling. Pass #2, #3, #4.
4. `cpp_module` path against the registry interface.
5. CMake target `poisson_harness`; document the build command.

## Wiring back to the orchestrator

Once `poisson_harness` builds and passes acceptance #1–#3, run the loop live:

```bash
python -m coscientist.run --goal examples/poisson_progressive.json \
    --live --harness ./build/poisson_harness
```

(`--harness` sets `harness_cmd` and flips `offline` off. The orchestrator writes candidate
source to the workdir and passes its path in the manifest exactly as the CLI above expects.)
