# Unit Tests

This directory hosts unit tests for isolated subsystems.

- Tests here should focus on deterministic, local behavior.
- Unit tests should avoid cross-subsystem runtime wiring.
- Prefer shared fixtures from `tests/support/` when needed.

Subdirectories are organized by owning subsystem.
