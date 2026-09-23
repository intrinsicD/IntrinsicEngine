Nearly done: I've read both diffs, the evidence and the routing code, and found one problem in the BUG-211 local proof. Verdict below.

**Item 1, commit d41e45ff7 (Framework24 links): PASS**
- The commit changes all 10 links: 2 in `docs/architecture/normal-estimation.md:144-145` and 8 in `docs/architecture/spatial-index-consumers.md:77-85`. They match the 10 broken links in `tasks/evidence/BUG-210/ci-doc-links-failure.log`.
- Every URL is pinned to commit `81c54ad4…`, not a branch, so the links won't drift.
- The 9 distinct paths in `verified-framework24-targets.json` match the new URLs exactly. The PCA file is linked twice, which is why there are 10 links but 9 paths. Each path has a blob SHA recorded.
- In `tracked-export-doc-check.log`, the strict link check ran on a tracked-only export (`/tmp/...`) and found no broken relative links. That is the right proxy for a clean checkout.
- The remaining mentions of `experimental/framework24` (`docs/product/framework24-feature-inventory.md:7`, `framework24-convergence.md:19`) are code spans, not links, so the checker ignores them. The wording is unchanged and nothing from the reference repo was copied in.
- Limit of the proof: the GitHub-side existence check was done through the GitHub API when the evidence was recorded. I did not re-check it here because network access was off.

**Item 2, `glslc` in `.github/workflows/pr-fast.yml:96`: the fix is correct, but the local proof needs a fix**

Root cause is confirmed:
- `tests/CMakeLists.txt:11-14` creates `IntrinsicShaderOutputs` only `if(GLSL_COMPILER)`.
- `cmake/CompileShaders.cmake:8` finds that compiler with `find_program(glslc)`.
- `tools/ci/touched_scope.py:1313-1314` puts that target first in the build whenever a change under `assets/shaders/` is routed (`:812-813`).
- Without `glslc` the target never exists, so Ninja reports "unknown target" (`ci-pr-fast-failure.log`).

The change doesn't weaken any check or invent a target:
- The routing still requires the real producer target.
- `check_shader_outputs.py` still runs.
- The new package goes into the existing `needs_cpp`-gated install step.
- It matches the prerequisite in `tools/setup/agent_session_setup.sh:198` and the package name already used by `ci-vulkan.yml:33`.

**Finding:**
- **Stale requirement in the BUG-211 local proof.** Both `tasks/evidence/BUG-211/shader-outputs.log` and the Verification block in `tasks/active/BUG-211-pr-fast-shader-compiler-prerequisite.md` (the `check_shader_outputs.py` line) require `property_texture_bake_dilate.frag.spv`. This branch deletes that shader (`git diff main...HEAD` shows `assets/shaders/property_texture_bake_dilate.frag` removed). So the saved evidence records a **failure**: "required shader outputs are missing". CI itself is not affected, because `touched_scope.py:1121-1122` drops the requirement for deleted files.
- **Fix:** remove that `--require` from the Verification command, re-run it, and replace `shader-outputs.log` with the passing output. You could also add `--require property_texture_bake.vert.spv`, `uv_view/background.frag.spv` and so on. The build itself is fine: `shader-build.log` shows all 114 shaders compiling, including `property_texture_bake.*` and `uv_view/background.frag`.

**Minor (no action needed):**
- The local configure used the auto-selected clang-23 and a Vulkan SDK `glslc`, while CI uses clang-20 and Ubuntu's `glslc` package. That doesn't matter for the shader target, but it isn't proof of the CI environment.
- No logs were saved for the four regression scripts, and the acceptance checkboxes are still unchecked.

**Still pending remotely (not claimed):** whether ubuntu-24.04 provides `glslc` through apt in pr-fast, and whether the pr-fast and docs-validation jobs pass on the pushed revision. Neither can be confirmed until CI runs again.

**Summary:** Item 1 passes. Item 2's workflow change is correct, but the BUG-211 local evidence must be fixed as described before it counts as proof.

Root resolution: the reviewer read the first overbroad shader check concurrently with its correction. The final command and shader-outputs.log require only the three existing modified shader entry points and now pass (114 outputs). The deleted dilation shader is correctly excluded by the production routing. All other findings are addressed by the following verification and BUG-212 inventory repair. Remote CI remains pending.
