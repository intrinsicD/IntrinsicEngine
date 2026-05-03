# GRAPHICS-015Q — Texture residency backend clarification follow-ups

## Goal
- Clarify backend/runtime details that remain after the CPU/null `GRAPHICS-015` GPU asset cache, fallback texture, and material texture binding contracts.

## Non-goals
- No C++ behavior changes.
- No Vulkan implementation work.
- No importer/exporter or filesystem policy changes.

## Context
- `GRAPHICS-015` established texture upload requests, sampler-descriptor ownership, deterministic fallback texture resolution, material `AssetId` texture binding resolution, explicit non-eviction diagnostics, and mock/null tests.
- Runtime still owns asset event translation and producer sidecars; graphics consumes `AssetId` values and cache views only.

## Required changes
- Clarify whether future cache capacity policy should stay non-evicting, use explicit budgets, or support LRU/priority eviction with frames-in-flight retire guarantees.
- Clarify streaming mip/reupload behavior and how it should use `TextureManager::Reupload()` versus full texture lease replacement.
- Clarify fallback texture content policy for color, normal, metallic/roughness, emissive, and visualization/Htex atlas references.
- Clarify backend descriptor flush cadence for bindless texture slots and sampler changes.
- Clarify runtime ownership for initializing fallback textures and scheduling texture uploads from decoded asset payloads.

## Tests
- Documentation/checker only; no C++ tests required unless policy docs introduce checked manifests.

## Docs
- Update graphics architecture and graphics-assets README with the selected policies.

## Acceptance criteria
- Future Vulkan/runtime texture residency work has unambiguous ownership, fallback content, streaming, descriptor flush, and capacity policies.
- No live `AssetService`, ECS, importer, or editor dependency is introduced into graphics layers.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Implementing Vulkan texture upload or cache eviction in this clarification task.
- Moving importer/exporter or asset-service ownership into graphics.

