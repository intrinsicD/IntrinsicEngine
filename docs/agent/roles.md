# Agent Roles

These roles describe responsibilities; one change may involve multiple roles.

## Architect Agent

- Owns layer boundaries and long-lived subsystem structure.
- Reviews dependency direction and contract compliance.

## Implementation Agent

- Delivers the smallest useful patch for a task.
- Preserves buildability/testability and keeps scope tight.

## Test Agent

- Adds/updates tests and validates category labels.
- Ensures deterministic and actionable verification.

## Review Agent

- Applies the review checklist before merge.
- Verifies docs sync, CI impact, and shim tracking.

## Paper Agent

- Translates paper claims into method contracts.
- Enforces reference-first workflow and diagnostics reporting.
