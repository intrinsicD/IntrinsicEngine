# Source Documentation Policy

Code is the primary explanation of the system. Names, types, file boundaries,
and control flow should make the normal case readable without a parallel prose
version of the implementation. Comments document only context that the code
cannot express clearly.

This policy applies repository-wide to project-owned C++ sources and to
`README.md` files. Generated, vendored, dependency, and build-output trees are
outside its scope.

## File synopses

Every project-owned C++23 module interface (`.cppm`) and header (`.h`, `.hh`,
`.hpp`, or `.hxx`) begins with a short comment that says both:

- what the file contains; and
- why that surface exists or which boundary it serves.

The synopsis normally occupies one to three comment lines. It appears before
the first preprocessor directive, global module fragment, module declaration,
or other code. A required copyright, license, or SPDX banner may come first;
the synopsis follows it as a separate comment paragraph.

Do not require a `Purpose:` tag, repeat the filename, list every declaration,
or paste the same template into many files. A weak synopsis is still a review
finding even when a comment is physically present.

```cpp
// Exposes immutable geometry snapshots so render preparation can cross the
// runtime-to-graphics boundary without giving graphics live ECS ownership.
export module Extrinsic.Runtime.GeometrySnapshot;
```

```cpp
// GeometrySnapshot.hpp
// Contains GeometrySnapshot.
#pragma once
```

The second example is not compliant: it restates names and does not explain
why the header exists.

## Declaration comments

Do not routinely document structs, classes, enums, fields, methods, or free
functions. A declaration comment is justified only when it is mandatory to
understand a contract that cannot be made clear through names, types,
organization, or a smaller API. Typical reasons are:

- a correctness invariant or coupled-field rule;
- ownership, lifetime, thread-affinity, or synchronization requirements;
- ordering or transactional behavior that callers must preserve;
- units, coordinate conventions, numerical limitations, or undefined input
  domains that the type system cannot encode; or
- a deliberately surprising constraint whose removal would break behavior.

Comments that merely restate a symbol, parameter, return type, or obvious line
of code must be removed. If a declaration needs a long explanation to be
understood, first improve its name, types, helpers, or placement.

Write comments in present tense as current invariants. A task, bug, paper, or
ADR may be an optional trailing reference after the invariant; it must not be
the explanation itself.

## Implementation explanations

Explanations of how an implementation works belong in its `.cpp` implementation
unit. Put a short file-level note near the top when one model or invariant
governs the whole implementation; otherwise put the comment immediately beside
the smallest function, branch, or data structure that needs it.

Explain why the non-obvious choice is necessary, not what each statement does.
Do not retain chronological narratives such as what an older slice did, which
task introduced a branch, or what code used to exist. That history belongs in
Git, task records, ADRs, migration documents, or the retirement log.

Templates and inline implementations that must remain in a `.cppm` or header
are the technical exception: put necessary implementation rationale beside
that implementation, not as decorative documentation on its declaration.

## README files

A `README.md` is a concise current-state entry point for its directory. It may
contain only information that helps a reader navigate or use the directory as
it exists now, such as:

- the directory's role and ownership boundary;
- what it explicitly does not own;
- a small directory map or a few stable entry points;
- the current configuration or extension path;
- links to canonical architecture/API documentation; and
- focused build, test, or validation commands.

Keep the shortest structure that makes those facts discoverable. Prefer links
to canonical or generated material over copied prose and manually maintained
exhaustive module/API tables.

Do not put changelogs, completed-task or slice narratives, retired behavior,
migration journals, dates-as-progress, roadmaps, planned features, or other
future state in a README. A README may link briefly to a purpose-built history
record, but it does not reproduce that record. Use:

- `docs/adr/` for durable decisions and their rationale;
- `docs/migration/` for bounded transitions;
- `tasks/` and `tasks/done/RETIREMENT-LOG.md` for work state and completion
  history;
- method reports or the ARA evidence tree for research evidence; and
- Git history for line-by-line chronology.

## Organization before comments

When code is hard to read, prefer a structural improvement over explanatory
prose: narrow the file's responsibility, use domain names, extract a named
helper, make ownership explicit, group related declarations, and keep control
flow linear. A comment is not a substitute for those changes.

There is no universal maximum file length. The audit may report unusually
large source files and READMEs as review hotspots because size impedes
discovery, but size alone is not a contract violation.

## Adoption and enforcement

New `.cppm` and header files must comply immediately. Add the required synopsis
when materially changing an existing interface or header. A materially changed
comment block or README section must comply with the current-state rules; remove
directly adjacent debt when that remains within the task's scope. Untouched
existing violations are migration debt, so do not bulk-generate comments or mix
unrelated cleanup into a semantic task.

Use the `intrinsicengine-source-documentation` skill for repository-wide or
path-scoped audits. Its scanner separates:

- **errors**: objective rules such as a missing leading synopsis or a README
  section explicitly dedicated to history/future plans; and
- **review findings**: possible boilerplate, declaration comments, historical
  wording, manual inventories, or oversized files that require human judgment.

Until a named cleanup task closes or explicitly baselines existing debt, run
the whole-tree audit in report-only mode. CI validates the audit tool itself;
it does not treat the current debt inventory as a blocking gate.
