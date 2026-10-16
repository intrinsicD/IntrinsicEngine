---
id: RUNTIME-276
theme: F
depends_on: [CORE-010, RUNTIME-287]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, repo.task-contract-discovery, geometry.element-domain-sources]
---
# RUNTIME-276 — Declarative `ConfigFieldSpec` tables, schema generation and conformance test

## Current status — 2026-10-16

The operator selected preservation of existing string-enum payloads on 2026-10-16.
Slices B and C, the 26-section registry schema coverage, and backend display-label
reuse are integrated under [HARDEN-090](../../active/HARDEN-090-src-audit-remediation.md).
Existing integer enums retain integer codes; existing string enums retain stable
tokens. The shared schema supports both, with display labels in `x-enum-names`.
Strict owners preserve diagnostic order; fallback owners retain their warning,
merge and cross-field behavior. Schemas reach agents through `config_schema` and
`config_get`; the generic tools' input schemas remain static.
The independent final Config review accepts the criteria below; its initially
pending schema build now passes. Verification and formal retirement/commit
association are coordinated in HARDEN-090.

## Historical progress — 2026-09-28 (slice A, `claude/config-schemas`)
Done: `Extrinsic.Runtime.ConfigFieldSpec` (Bool, UInt, Int, Float, Enum with a first
value, String, UIntArray, PropertyRef with kinds/domains/any-scalar/nullable) and the
internal `Runtime.ConfigFieldJson.hpp` (`ValidateDeclaredFields`,
`BuildSectionSchemaJson`); tables, generated `SchemaJson` and table-driven `Validate`
for smoothing, harmonic field, Laplacian eigenbasis, scalar gradient, geodesics and
mesh curvature (per-owner accessors such as `PropertySmoothingConfigFieldSpecs()`);
`Test.EngineConfigSectionSchemas.cpp` in `tests/integration/runtime` (the registry
is app-owned) checks keys, defaults, every enum value, every bound and the
rejection past it for every section that has a schema.
Deviations: the accessor is `FindConfigFieldSpec(fields, name)` over an owner's
table rather than `(section, field)`, so no central section map is needed; the
agent config tools stay generic over sections, so the schema reaches agents
through `config_schema` and `config_get` instead of per-section `inputSchema`
(tools/list is static and one inputSchema cannot follow the `section` argument).
Schema defaults are the omitted-field values of each validator's merge (for the
harmonic field this is `pin_boundary:false`, while its registered default section
pins the boundary). Known limit: a table bound that is looser than an owner's
backstop check (e.g. `ValidatePropertyFilterParams`) is not detected when the
backstop rejects with a different message.
At that date, slices B and C remained; see the current status above.

## Goal
- Give every Sandbox config section one declarative field table that both generates
  its JSON Schema (`SchemaJson`, CORE-010) and replaces the mechanical parts of its
  `Validate`, so names, types, bounds and enum cardinality cannot drift between
  validation, schema, agent tool input and UI hints.

## Non-goals
- No change to existing payload encoding: preserve integer codes and existing string tokens (operator decision 2026-10-16); schemas add `x-enum-names` where display labels are defined.
- Cross-field rules (`FilterConflict`, mesh-only weights, Vulkan capability) stay hand-written in `Validate`.
- No JSON-Schema validator dependency (it uses exceptions and is unnecessary for structural agreement).

## Context
- Historical operator direction 2026-09-27 (encoding clarified on 2026-10-16): agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Historical planning context (2026-09-27, superseded by the integrated tables): each owner hand-wrote `Validate`: e.g. `src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp` calls `ConfigDetail::ValidatePointConfigFields(doc, merged, objectError, unknownPrefix, {field list})`, loops finite-float lists, checks enum upper bounds such as `merged["method"].get<unsigned>() > unsigned(S::PropertyFilter::VariationalFit)`, then cross-field rules and `RejectConfigSection(subject, message)`.
- Historical helper inventory (2026-09-27): `src/runtime/Config/internal/Runtime.PointConfigJson.hpp` (`ConfigDetail::{ParseConfigJson, SerializeConfigJson, RejectConfigSection, FindValidatedCanonicalPayload, ValidatePointConfigFields, ValidatePointConfigNonnegativeFloats, ValidatePointPropertyRef, Encode/DecodePointPropertyRef}`), compiled in `Runtime.FeatureConfigCodecs.Detail.cpp`.
- Field spec shape: `ConfigFieldSpec { Name; ConfigFieldType Type (Bool, UInt, Int, Float, String, Enum, PropertyRef, Object, Nullable…); Min, Max; ExclusiveMin/Max; Description; EnumNames; RefKinds (PropertyValueKind); RefDomains (GeometryElementDomain) }`. Property-ref fields emit `x-property-kinds`/`x-domains` from the same table used by `ValidatePointPropertyRef`. Sections already using string tokens (parameterization `lscm`…) declare `Enum` with string values. Feature codecs with warning/merge semantics carry `x-fallback:true` and a looser conformance rule.

## Control surfaces
- Config: every Sandbox section gains a generated schema; validation behavior unchanged except that mechanical checks come from the table.
- UI: `FindConfigFieldSpec(fields, name)` over the runtime owner’s table (no JSON) consumed by UI-057 hints and range-aware inputs.
- Agent/CLI: `config_preview`/`config_apply` retain their generic static tool input; `config_schema` and `config_get` expose each registered section’s `SchemaJson`.

## Required changes
- [x] `ConfigDetail::BuildSectionSchemaJson(title, description, fields)` and `ConfigDetail::ValidateDeclaredFields(doc, merged, fields, subject)` in `Runtime.FeatureConfigCodecs.Detail.cpp`; `FindConfigFieldSpec` public accessor.
- [x] Slice A — mesh-field family (smoothing, harmonic field, Laplacian eigenbasis, scalar gradient, geodesics, mesh curvature): tables, generated `SchemaJson`, `Validate` reduced to table check + cross-field rules.
- [x] Slice B — point families (normal estimation, outlier analysis, kernel density, point spacing, bilateral filter, keypoints, descriptors, density weight, point construction, registration), including the backend display labels so the Sandbox panels' remaining hard-coded backend combos read them from the owner (UI-072 slice 4). The accepted B2 scope retains Consolidation `initial_backend` as its existing local two-choice UI; its config field remains table-driven.
- [x] Slice C — remaining sections (clustering, curvature segmentation, progressive Poisson, parameterization, point-cloud consolidation, physics module, selection).
- [x] Registrations in `src/app/Sandbox/Sandbox.ConfigSections.cpp` set `SchemaJson` from the owners.

## Tests
- [x] `tests/integration/runtime/Test.EngineConfigSectionSchemas.cpp`: for every registration in `CreateSandboxConfigSectionRegistry()`: default payload keys == schema `properties` keys and `additionalProperties:false`; default payload validates; each enum field rejects max+1 and accepts every listed value except for cross-field rejections (never "unknown field"/type rejections); each numeric field accepts `Min`/`Max` and rejects out-of-range values; `x-enum-names` length equals enum count.
- [x] Existing per-family validation tests stay green unchanged except for intentional message wording.

## Docs
- [x] `docs/architecture/engine-config.md` points to the generated schema instead of hand-maintained field tables (or states the schema is authoritative); `docs/architecture/runtime-config-control.md` documents schema access through `config_schema` and `config_get`.

## Acceptance criteria
- [x] All 26 sections expose a generated schema and pass the conformance test.
- [x] Unknown-field lists, enum upper bounds and finite/non-negative float lists no longer appear hand-written in migrated `Validate` functions.
- [x] Agent config operations expose the generated section schema through `config_schema` and `config_get`.

Acceptance source: independent Claude session `08d458ce-2c29-4271-96df-eb4bb52d0a57`,
final diff `89fe465cbbc97529179b637577b2754c3017c5d87c5c3b36c5110baaf092986f`,
approves B1/C, B2 and Physics and accepts all prior review criteria. The final
13/13 schema cases, existing backend controls and actual socket schema export
subsequently passed. Cross-field rules and fixed diagnostic text remain the
reviewed exceptions to table-driven field lists. Original nine omitted-field
schema defaults and seventeen new registered defaults retain their distinct
contracts. HARDEN-090 owns counts, limits and the composite CPU verification.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'EngineConfigSectionSchemas|Config|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing an existing integer or string enum payload representation, or changing section names/schema IDs.
- A parallel schema description that duplicates rather than replaces validator field lists.
- JSON types in `.cppm` interfaces.
