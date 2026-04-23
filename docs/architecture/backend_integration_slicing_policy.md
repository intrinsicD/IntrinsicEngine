# Backend Bring-up Slicing Policy

## Goal

Keep backend integration reviewable, bisectable, and low-risk by slicing work into concern-isolated commits.

## Required slice order

1. **Bootstrap & Device Init**
   - Instance/device creation, queue family selection, swapchain setup.
2. **Descriptor/Bindless**
   - Descriptor heap model, slot lifecycle, update/free behavior.
3. **Transfer & Staging**
   - Upload path, fences/timeline signaling, completion semantics.
4. **Pass Wiring**
   - Render/compute pass execution and pipeline binding.
5. **Runtime/Sandbox Integration**
   - App wiring, build system integration, startup/runtime selection.

## Acceptance checklist per slice

- Interfaces documented (inputs, outputs, error paths).
- Invariants listed (resource lifetime, ownership, synchronization assumptions).
- Failure modes listed and test strategy identified.
- Telemetry markers added for frame and queue phase boundaries.

## Commit-size guideline

- Soft budget: `< 800` changed lines per commit.
- If exceeded, tag commit/PR with `large-change-approved` and include rationale.

## Notes

This policy applies to both Null and Vulkan backend work, and should be referenced in backend PR descriptions.
