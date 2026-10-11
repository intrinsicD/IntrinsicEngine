**Layer A reconstruction only; no approval.** HEAD matches `96c21108f`. The manifest SHA256 matches `2d51234c6d506b829a7e11837b741f2eb206782d4206a205ca5d84f7ea90cadc`. Executable and source hashes match the [bindings](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/binary-bindings.json); restoring only the candidate’s loop guard reproduces the baseline source hash.

All cells use one synthetic fixture and seed: 100,000 points per set, a bent/similarity-transformed surface, Gaussian noise of 0.004 world units per coordinate, and 5% target clutter. Bayesian registration uses normalized inputs, rank 100, at most 20 iterations, and eight pinned threads. The [preview](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/fixture-preview.txt) agrees with the [generator](/home/alex/Documents/IntrinsicEngine/benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp).

| Cells | What they actually measure |
|---|---|
| `exact` ×1 | Auto-policy endpoint and runtime reference; potentially truncated/IFGT, not dense ground truth. |
| `floor` ×1 | 4096-landmark request rejected before approximation by the estimated-cost cap; tests fallback parity. |
| `negative` ×1 | Two-landmark approximation with effectively disabled error threshold; positivity checks still apply. |
| Baseline/candidate × two variants × two repeats | Effect of removing cached rejection suppression, including retry overhead and changed registration trajectories. Second pass reverses order. |

[Plan and commands](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/diagnose.py). The point variant uses fixed farthest-point identities with moving coordinates; “random” affects box sampling. Boxes are rebuilt and resampled each iteration. [Sampling implementation](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp)

The measurements are:

- **Geometry:** indexwise RMS/max distance to Auto and RMS to synthetic truth, in world units—not nearest-neighbor distances.
- **Diagnostics:** post-update σ² in world units²; pre-E-step σ² in normalized units²; dimensionless scale and sampled relative error; accepted-iteration counts.
- **Objective:** summed normalized-coordinate NLL in nats, evaluated before the update. Bayesian reported coherence is zero, so this is not a full variational objective.
- **Resources:** registration seconds, including setup and diagnostic logging but excluding fixture generation and output metrics; cumulative completed-child peak RSS in KiB. Kernel counts omit some rejected work.

[Metric implementation](/home/alex/Documents/IntrinsicEngine/benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp), [NLL](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.cpp), [coherence](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.cpp).

The programmed comparison requires RMS ≤0.001 and max ≤0.01 versus Auto, matching termination/iteration counts, identical selected numerical summaries across repeats, and candidate’s slower repeat taking **less than 90%** of baseline’s faster repeat in **both** variants. At least one variant must gain accepted iterations. Truth error is recorded but ungated. [Assessment](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/diagnose.py)

An ineffective retry change produces unchanged acceptance and endpoints and cannot satisfy the extra-acceptance requirement. Always falling back produces **zero accepted iterations and reported sampled error zero**; that zero does not certify Nyström accuracy. [Fallback reporting](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp)

A wrong or collapsed solution shared with Auto can satisfy geometric parity. The internal approximation check can also favor wrong-but-consistent sums: it checks 32 target denominators and 32 source masses, uses approximate denominators for the source check, and never checks `PX`. It is not a global error bound. [Error calculation](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp)

Concrete questions for Layer B:

1. Is the intended conclusion limited to retry behavior and endpoint equivalence? These gates cannot establish successful registration or exclude shared collapse.
2. What interpretation is prescribed if the two-landmark control falls back instead of accepting an inaccurate approximation?
3. How is the partial error oracle justified? Its fixed stride also samples **8/32 clutter targets**, versus 5% overall.
4. Is Auto-versus-fallback asymmetry intentional? Nyström fallback disables IFGT, while the reference permits it. [Policy selection](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp)
5. What inference rests on two timing repeats of one fixture/seed? They measure repeatability, not variability across registration problems.
6. How is the failure report preserved when the outer 1800-second timeout terminates the driver? Reporting itself consumes that same budget. [Execution](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/diagnose.py)

No Layer B, prior-result/log/review files, experiments, writes, or delegation were used; compilation provenance beyond hash bindings remains unchecked.  
Blindness caveat: the permitted [report source](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-078/diagnose.py) exposed a historical-result assertion. I excluded it from reasoning, but cannot claim strictly unexposed blindness.

Rendering note: source-line suffixes were removed from file links for the repository validator. [Verbatim answer](codex-a.txt).
