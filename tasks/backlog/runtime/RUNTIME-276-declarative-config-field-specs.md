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

## Goal
- Give every Sandbox config section one declarative field table that both generates
  its JSON Schema (`SchemaJson`, CORE-010) and replaces the mechanical parts of its
  `Validate`, so names, types, bounds and enum cardinality cannot drift between
  validation, schema, agent tool input and UI hints.

## Non-goals
- No change to payload encoding: enums stay integer-coded (operator decision); schemas add `x-enum-names`.
- Cross-field rules (`FilterConflict`, mesh-only weights, Vulkan capability) stay hand-written in `Validate`.
- No JSON-Schema validator dependency (it uses exceptions and is unnecessary for structural agreement).

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Today each owner hand-writes `Validate`: e.g. `src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp` calls `ConfigDetail::ValidatePointConfigFields(doc, merged, objectError, unknownPrefix, {field list})`, loops finite-float lists, checks enum upper bounds such as `merged["method"].get<unsigned>() > unsigned(S::PropertyFilter::VariationalFit)`, then cross-field rules and `RejectConfigSection(subject, message)`.
- Shared helpers: `src/runtime/Config/internal/Runtime.PointConfigJson.hpp` (`ConfigDetail::{ParseConfigJson, SerializeConfigJson, RejectConfigSection, FindValidatedCanonicalPayload, ValidatePointConfigFields, ValidatePointConfigNonnegativeFloats, ValidatePointPropertyRef, Encode/DecodePointPropertyRef}`), compiled in `Runtime.FeatureConfigCodecs.Detail.cpp`.
- Field spec shape: `ConfigFieldSpec { Name; ConfigFieldType Type (Bool, UInt, Int, Float, String, Enum, PropertyRef, Object, Nullable…); Min, Max; ExclusiveMin/Max; Description; EnumNames; RefKinds (PropertyValueKind); RefDomains (GeometryElementDomain) }`. Property-ref fields emit `x-property-kinds`/`x-domains` from the same table used by `ValidatePointPropertyRef`. Sections already using string tokens (parameterization `lscm`…) declare `Enum` with string values. Feature codecs with warning/merge semantics carry `x-fallback:true` and a looser conformance rule.

## Control surfaces
- Config: every Sandbox section gains a generated schema; validation behavior unchanged except that mechanical checks come from the table.
- UI: `FindConfigFieldSpec(section, field)` runtime accessor (no JSON) consumed by UI-057 hints and range-aware inputs.
- Agent/CLI: `config_preview`/`config_apply` agent operations use the section `SchemaJson` as their per-section `inputSchema`; `config_schema` returns it.

## Required changes
- [ ] `ConfigDetail::BuildSectionSchemaJson(title, description, fields)` and `ConfigDetail::ValidateDeclaredFields(doc, merged, fields, subject)` in `Runtime.FeatureConfigCodecs.Detail.cpp`; `FindConfigFieldSpec` public accessor.
- [ ] Slice A — mesh-field family (smoothing, harmonic field, Laplacian eigenbasis, scalar gradient, geodesics, mesh curvature): tables, generated `SchemaJson`, `Validate` reduced to table check + cross-field rules.
- [ ] Slice B — point families (normal estimation, outlier analysis, kernel density, point spacing, bilateral filter, keypoints, descriptors, density weight, point construction, registration).
- [ ] Slice C — remaining sections (clustering, curvature segmentation, progressive Poisson, parameterization, point-cloud consolidation, physics module, selection).
- [ ] Registrations in `src/app/Sandbox/Sandbox.ConfigSections.cpp` set `SchemaJson` from the owners.

## Tests
- [ ] `tests/contract/runtime/Test.EngineConfigSectionSchemas.cpp`: for every registration in `CreateSandboxConfigSectionRegistry()`: default payload keys == schema `properties` keys and `additionalProperties:false`; default payload validates; each enum field rejects max+1 and accepts every listed value except for cross-field rejections (never "unknown field"/type rejections); each numeric field accepts `Min`/`Max` and rejects out-of-range values; `x-enum-names` length equals enum count.
- [ ] Existing per-family validation tests stay green unchanged except for intentional message wording.

## Docs
- [ ] `docs/architecture/engine-config.md` points to the generated schema instead of hand-maintained field tables (or states the schema is authoritative); `docs/architecture/runtime-config-control.md` documents per-section `inputSchema`.

## Acceptance criteria
- [ ] All 24 sections expose a generated schema and pass the conformance test.
- [ ] Unknown-field lists, enum upper bounds and finite/non-negative float lists no longer appear hand-written in migrated `Validate` functions.
- [ ] Agent config operations advertise the generated schema as tool input.

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
- Migrating enum payloads to string tokens or changing section names/schema IDs.
- A parallel schema description that duplicates rather than replaces validator field lists.
- JSON types in `.cppm` interfaces.
