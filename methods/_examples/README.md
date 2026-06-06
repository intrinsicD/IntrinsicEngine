# Method package examples

Non-real, instructional method packages that demonstrate the method-package
structure. **Nothing here is a real paper intake** — these packages exist only
to show how a domain package under `methods/<domain>/` is laid out.

- `vector_heat/` — structure example modelled on a vector-heat-style geometry
  method. No real paper metadata, no executable implementation. Its `method.yaml`
  keeps a valid `geometry.`-prefixed `id` (the `--strict` manifest validator
  requires a `geometry.`/`rendering.`/`physics.` prefix) but its name, paper
  block, and `known_limitations` all state plainly that it is a structure
  example.

To start a real method, **move** an example (or the `_template/` package) into
its domain directory and complete the `AGENTS.md` §6 intake — do not leave a
real intake under `methods/_examples/`.

These manifests are still discovered by `tools/agents/validate_method_manifests.py`
(it `rglob`s `methods/**/method.yaml`), so they must remain schema-valid.
